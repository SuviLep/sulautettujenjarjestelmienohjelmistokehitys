#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <ctype.h>

 /* valosekvenssin vastaanotto sarjaportin kautta
    sarjaportti -> UART -> FIFO -> dispatcher  -> (condvar) -> valotaskit */

// Ledit
static const struct gpio_dt_spec red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

// UART
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

//FIFO-alkion tyyppi, pitää olla ennen K_FIFO..
struct seq_item {
    void *fifo_reserved;     
    char value;              /* 'R' / 'Y' / 'G' */
};
//UART taski tuottaa ja dispatcher lukee täältä
K_FIFO_DEFINE(seq_fifo);

//release-sema-> taskeille ilmotus dispacher valmis
K_SEM_DEFINE(release_sem, 0, 1);

//Toimivat kimpassa
K_MUTEX_DEFINE(red_mutex); //suojaa trig-lipun päivitystä
K_CONDVAR_DEFINE(red_cv);   // herättää
static volatile bool red_trig = false;  //onko töitä

//Sama setti keltaiselle
K_MUTEX_DEFINE(yellow_mutex);
K_CONDVAR_DEFINE(yellow_cv);
static volatile bool yel_trig = false;

// Ja vihreälle
K_MUTEX_DEFINE(green_mutex);
K_CONDVAR_DEFINE(green_cv);
static volatile bool grn_trig = false;

//valon kesto
#define LIGHT_MS 1000

//Protot
static int init_led(void);
static int init_uart(void);
void uart_task(void *, void *, void *);
void dispatcher_task(void *, void *, void *);
void red_led_task(void *, void *, void *);
void yellow_led_task(void *, void *, void *);
void green_led_task(void *, void *, void *);

/// Threadit
#define STACKSIZE 1024
#define PRIORITY  5
K_THREAD_DEFINE(uart_thread,       STACKSIZE, uart_task,       NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(dispatcher_thread, STACKSIZE, dispatcher_task, NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(red_thread,        STACKSIZE, red_led_task,    NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(yellow_thread,     STACKSIZE, yellow_led_task, NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(green_thread,      STACKSIZE, green_led_task,  NULL,NULL,NULL, PRIORITY, 0, 0);

int main(void)
{
    if (init_uart()) return -1;
    if (init_led())  return -1;

    printk("VALMISTA! Kirjoita Y, G, R tai jotain niillä).\n");
    return 0; 
}

//Initit uartilla ja ledeille
static int init_uart(void) {
    if (!device_is_ready(uart_dev)) {
        printk("UART device not ready\n");
        return -ENODEV; // error ei laitettta..
    }
    return 0;
}

static int init_led(void) {
    int ret;

    if (!gpio_is_ready_dt(&red) || !gpio_is_ready_dt(&green)) {
        printk("LED ports not ready\n");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);   if (ret) return ret;
    ret = gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE); if (ret) return ret;

    gpio_pin_set_dt(&red,   0);
    gpio_pin_set_dt(&green, 0);

    printk("LEDs configured\n");
    return 0;
}

//UART: lukee merkkejä. Huom luetaan vain r,y g, muut ingnoorataan. 
void uart_task(void *a, void *b, void *c) {
    char rc;
    while (1) {
        if (uart_poll_in(uart_dev, &rc) == 0) {
            rc = (char)toupper((unsigned char)rc); // pienet kirjaimet ok
            if (rc == 'R' || rc == 'Y' || rc == 'G') {
                struct seq_item *item = k_malloc(sizeof(*item));
                if (item) {
                    item->value = rc;
                    k_fifo_put(&seq_fifo, item);
                    printk("Enqueued: %c\n", rc);
                } else {
                    printk("malloc failed, dropping %c\n", rc);
                }
            }
        }
        k_msleep(5);
    }
}
//Dispatcher. FIFO:sta merkki kerrallaan. lukitsee vastaavan mutexin, asettaa trig-lipun, 
//signaloi condvarin -> herättää valotaskin. odottaa release-semaa, joka taskilta kun valmista
void dispatcher_task(void *a, void *b, void *c) {
    printk("Dispatcher started\n");
    while (1) {
        struct seq_item *it = k_fifo_get(&seq_fifo, K_FOREVER); // merkki kerrallaan
        char ch = it->value; // poimitaan FIFO-alkioista kirjaimet
        k_free(it); //vapautetaan heap-muisti

        switch (ch) {
            case 'R':
                k_mutex_lock(&red_mutex, K_FOREVER); //lukitaan kirjainta vastaava mutex
                red_trig = true;                    // ja asetaan siitä lippu valmiina hommille
                k_condvar_signal(&red_cv);          // herätetään oikea valotaskille
                k_mutex_unlock(&red_mutex);         // mutexin  vapautus
                printk("Dispatch -> RED\n");
                break;

            case 'Y':
                k_mutex_lock(&yellow_mutex, K_FOREVER);
                yel_trig = true;
                k_condvar_signal(&yellow_cv);
                k_mutex_unlock(&yellow_mutex);
                printk("Dispatch -> YELLOW\n");
                break;

            case 'G':
                k_mutex_lock(&green_mutex, K_FOREVER);
                grn_trig = true;
                k_condvar_signal(&green_cv);
                k_mutex_unlock(&green_mutex);
                printk("Dispatch -> GREEN\n");
                break;

            default:
                continue; // varatla jos merkki ei ole y,g, tai r ei odoteta releasea vaan jatketaan seuraavaan.  ei pakollinen
        }

        //Odota että single-shot valmistuu ennen seuraavaa merkkiä
        k_sem_take(&release_sem, K_FOREVER);
    }
}

//Taskit valoille
// Odottaa condvaria while-silmukassa, kun red_trig on tosi, nollaa sen, sytyttää valon LIGHT_MS ajaksi
// Lopuksi release-sema -> dispatcher saa jatkaa seuraavaan merkkiin
void red_led_task(void *, void *, void*) {
    printk("Red task started\n");
    while (1) {
        k_mutex_lock(&red_mutex, K_FOREVER);
        while (!red_trig) {
            k_condvar_wait(&red_cv, &red_mutex, K_FOREVER);// Wait vapauttaa mutexin nukkuessa ja lukitsee sen uudelleen herätessä
        }
        red_trig = false; //työtä!
        k_mutex_unlock(&red_mutex);

        gpio_pin_set_dt(&red, 1);  printk("RED ON\n");
        k_msleep(LIGHT_MS);
        gpio_pin_set_dt(&red, 0);  printk("RED OFF\n");

        k_sem_give(&release_sem); //dispatcherille tieto valmista on
    }
}

void yellow_led_task(void *, void *, void*) {
    printk("Yellow task started\n");
    while (1) {
        k_mutex_lock(&yellow_mutex, K_FOREVER);
        while (!yel_trig) {
            k_condvar_wait(&yellow_cv, &yellow_mutex, K_FOREVER);
        }
        yel_trig = false;
        k_mutex_unlock(&yellow_mutex);

        // keltainen= punainen + vihreä yhtä aikaa
        gpio_pin_set_dt(&red,   1);
        gpio_pin_set_dt(&green, 1);
        printk("YELLOW ON\n");
        k_msleep(LIGHT_MS);
        gpio_pin_set_dt(&red,   0);
        gpio_pin_set_dt(&green, 0);
        printk("YELLOW OFF\n");

        k_sem_give(&release_sem);
    }
}

void green_led_task(void *, void *, void*) {
    printk("Green task started\n");
    while (1) {
        k_mutex_lock(&green_mutex, K_FOREVER);
        while (!grn_trig) {
            k_condvar_wait(&green_cv, &green_mutex, K_FOREVER);
        }
        grn_trig = false;
        k_mutex_unlock(&green_mutex);

        gpio_pin_set_dt(&green, 1);  printk("GREEN ON\n");
        k_msleep(LIGHT_MS);
        gpio_pin_set_dt(&green, 0);  printk("GREEN OFF\n");

        k_sem_give(&release_sem);
    }
}
