#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <ctype.h>
#include <zephyr/sys/util.h>

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

// Toimivat kimpassa (trig + mutex + condvar) jokaiselle värille
static volatile bool red_trig = false;
static volatile bool yel_trig = false;
static volatile bool grn_trig = false;

K_MUTEX_DEFINE(red_mutex);    K_CONDVAR_DEFINE(red_cv);
K_MUTEX_DEFINE(yellow_mutex); K_CONDVAR_DEFINE(yellow_cv);
K_MUTEX_DEFINE(green_mutex);  K_CONDVAR_DEFINE(green_cv);

//valon kesto
#define LIGHT_MS 1000

//Protot
static int init_led(void);
static int init_uart(void);
static int init_buttons(void);

void uart_task(void *, void *, void *);
void dispatcher_task(void *, void *, void *);
void red_led_task(void *, void *, void *);
void yellow_led_task(void *, void *, void *);
void green_led_task(void *, void *, void *);

//Napit takaisin
#define BUTTON_RED  DT_ALIAS(sw1)
#define BUTTON_YEL  DT_ALIAS(sw2)
#define BUTTON_GRN  DT_ALIAS(sw3)

static const struct gpio_dt_spec btn_red = GPIO_DT_SPEC_GET(BUTTON_RED, gpios);
static const struct gpio_dt_spec btn_yel = GPIO_DT_SPEC_GET(BUTTON_YEL, gpios);
static const struct gpio_dt_spec btn_grn = GPIO_DT_SPEC_GET(BUTTON_GRN, gpios);

static struct gpio_callback btn_red_cb;
static struct gpio_callback btn_yel_cb;
static struct gpio_callback btn_grn_cb;

//keskeytysten pikakäsittely -> työn ohjaus eteenpäin workille
static void btn_red_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void btn_yel_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void btn_grn_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

//napeille isr:n  kautta logiikka mitä tehdään k_malloc + k_fifo_put
static void red_work_fn(struct k_work *work);
static void yel_work_fn(struct k_work *work);
static void grn_work_fn(struct k_work *work);

K_WORK_DEFINE(red_work, red_work_fn);
K_WORK_DEFINE(yel_work, yel_work_fn);
K_WORK_DEFINE(grn_work, grn_work_fn);

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
    init_uart();
    init_led();
    init_buttons();

    printk("Ready to go. Write using Y,G, R..).\n");
    return 0; 
}

//Initit uartilla ja ledeille ja myös buttoneille
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
static int init_buttons(void) {
    int ret;

    if (!gpio_is_ready_dt(&btn_red) || !gpio_is_ready_dt(&btn_yel) || !gpio_is_ready_dt(&btn_grn)) {
        printk("Button ports not ready\n");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&btn_red, GPIO_INPUT);  if (ret) return ret;
    ret = gpio_pin_configure_dt(&btn_yel, GPIO_INPUT);  if (ret) return ret;
    ret = gpio_pin_configure_dt(&btn_grn, GPIO_INPUT);  if (ret) return ret;

    ret = gpio_pin_interrupt_configure_dt(&btn_red, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;
    ret = gpio_pin_interrupt_configure_dt(&btn_yel, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;
    ret = gpio_pin_interrupt_configure_dt(&btn_grn, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;

    gpio_init_callback(&btn_red_cb, btn_red_isr, BIT(btn_red.pin));
    gpio_add_callback(btn_red.port, &btn_red_cb);

    gpio_init_callback(&btn_yel_cb, btn_yel_isr, BIT(btn_yel.pin));
    gpio_add_callback(btn_yel.port, &btn_yel_cb);

    gpio_init_callback(&btn_grn_cb, btn_grn_isr, BIT(btn_grn.pin));
    gpio_add_callback(btn_grn.port, &btn_grn_cb);

    printk("Buttons configured (R/Y/G)\n");
    return 0;
}
// ISR:ssä ei allokointia tms, vain laukaise workki
static void btn_red_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    k_work_submit(&red_work);
}
static void btn_yel_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    k_work_submit(&yel_work);
}
static void btn_grn_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(dev); ARG_UNUSED(cb); ARG_UNUSED(pins);
    k_work_submit(&grn_work);
}
//Workki-funktiot: itse logiikka, allokoi FIFO-alkio ja kirjoita kirjaimena jonoon
static void red_work_fn(struct k_work *work) {
    struct seq_item *item = k_malloc(sizeof(*item));
    if (item) { item->value = 'R'; k_fifo_put(&seq_fifo, item); printk("BTN -> R\n"); }
}
static void yel_work_fn(struct k_work *work) {
    struct seq_item *item = k_malloc(sizeof(*item));
    if (item) { item->value = 'Y'; k_fifo_put(&seq_fifo, item); printk("BTN -> Y\n"); }
}
static void grn_work_fn(struct k_work *work) {
    struct seq_item *item = k_malloc(sizeof(*item));
    if (item) { item->value = 'G'; k_fifo_put(&seq_fifo, item); printk("BTN -> G\n"); }
}
//UART: lukee merkkejä. Huom luetaan vain r,y g, muut ingnoorataan nyt. 
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
                continue; // varalta jos merkki ei ole y,g, tai r ei odoteta releasea vaan jatketaan seuraavaan.  ei pakollinen
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
        red_trig = false; //työtä tarjolla
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