#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/util.h>
#include <zephyr/timing/timing.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
//Vk 5 Liikennevalojen yksikkötestaus


//Virhekoodit
#define TIME_LEN_ERROR      -1
#define TIME_ARRAY_ERROR    -2
#define TIME_VALUE_ERROR    -3

// Parseri
static int time_parse(char *time) {
    if (time == NULL) return TIME_ARRAY_ERROR;
    if (strlen(time) != 6) return TIME_LEN_ERROR;

    for (int i = 0; i < 6; ++i) {
        unsigned char uc = (unsigned char)time[i];
        if (uc < '0' || uc > '9') return TIME_LEN_ERROR;
    }

    char ss[3] = { time[4], time[5], '\0' };
    char mm[3] = { time[2], time[3], '\0' };
    char hh[3] = { time[0], time[1], '\0' };

    int second = atoi(ss);
    int minute = atoi(mm);
    int hour   = atoi(hh);

    if (hour > 23 || minute > 59 || second > 59) return TIME_VALUE_ERROR;

    return hour*3600 + minute*60 + second;
}
//Debugit
static volatile bool dbg_on = true;
#define PRINTK(...) do { if (dbg_on) printk(__VA_ARGS__); } while (0)
//Mittaus Fifo
struct meas_item {
    void *fifo_reserved;
    char value;        /* 'R','Y','G' */
    uint64_t usec;     /* kesto mikrosekunteina */
};
K_FIFO_DEFINE(meas_fifo);
//Ledit
static const struct gpio_dt_spec red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
//Uart
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
//Dispatcher FIFO
struct seq_item {
    void *fifo_reserved;
    char value;        /* 'R' / 'Y' / 'G' */
};
K_FIFO_DEFINE(seq_fifo);
//synkkaus
K_SEM_DEFINE(release_sem, 0, 1);

static volatile bool red_trig = false;
static volatile bool yel_trig = false;
static volatile bool grn_trig = false;

K_MUTEX_DEFINE(red_mutex);    K_CONDVAR_DEFINE(red_cv);
K_MUTEX_DEFINE(yellow_mutex); K_CONDVAR_DEFINE(yellow_cv);
K_MUTEX_DEFINE(green_mutex);  K_CONDVAR_DEFINE(green_cv);

//valon kesto
#define LIGHT_MS 1000

//protot
static int init_led(void);
static int init_uart(void);
static int init_buttons(void);

static void uart_task(void *, void *, void *);
static void dispatcher_task(void *, void *, void *);
static void red_led_task(void *, void *, void *);
static void yellow_led_task(void *, void *, void *);
static void green_led_task(void *, void *, void *);

static void btn_red_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void btn_yel_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);
static void btn_grn_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins);

static void red_work_fn(struct k_work *work);
static void yel_work_fn(struct k_work *work);
static void grn_work_fn(struct k_work *work);

K_WORK_DEFINE(red_work, red_work_fn);
K_WORK_DEFINE(yel_work, yel_work_fn);
K_WORK_DEFINE(grn_work, grn_work_fn);

//AJASTIN
/* Ajastin laukaisee valitun värin FIFOon timer_workin kautta. */
static struct k_timer timer;
static char timer_color = 'R';   // minkä värin ajastin laukaisee
static int  timer_delay_s = 0;   //viimeisin asetettu viive

static void timer_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    struct seq_item *it = k_malloc(sizeof(*it));
    if (!it) return;
    it->value = timer_color;  //väri mikä laitetaan
    k_fifo_put(&seq_fifo, it);
    PRINTK("TIMER -> %c (Wait %d s)\n", timer_color, timer_delay_s);
}
K_WORK_DEFINE(timer_work, timer_work_fn);

static void timer_handler(struct k_timer *t) {
    ARG_UNUSED(t);
    k_work_submit(&timer_work);
}

//nappien määrittely
#define BUTTON_RED  DT_ALIAS(sw1)
#define BUTTON_YEL  DT_ALIAS(sw2)
#define BUTTON_GRN  DT_ALIAS(sw3)

static const struct gpio_dt_spec btn_red = GPIO_DT_SPEC_GET(BUTTON_RED, gpios);
static const struct gpio_dt_spec btn_yel = GPIO_DT_SPEC_GET(BUTTON_YEL, gpios);
static const struct gpio_dt_spec btn_grn = GPIO_DT_SPEC_GET(BUTTON_GRN, gpios);

static struct gpio_callback btn_red_cb;
static struct gpio_callback btn_yel_cb;
static struct gpio_callback btn_grn_cb;

// Debuggaus taski
static void debug_task(void *, void *, void *);
#define DEBUG_PRIORITY  (5 + 2)
K_THREAD_DEFINE(debug_thread, 1024, debug_task, NULL, NULL, NULL, DEBUG_PRIORITY, 0, 0);
K_FIFO_DEFINE(taskdbg_fifo);

struct taskdbg_msg {
    void *fifo_reserved;
    char ev;   // 'S' start, '1' LED ON, '0' LED OFF, 'D' debug toggle
    char col;  // 'R','Y','G','1','0'
};

static inline void taskdbg_push(char ev, char col) {
    if (!dbg_on) return;
    struct taskdbg_msg *m = k_malloc(sizeof(*m));
    if (!m) return;
    m->ev = ev; m->col = col;
    k_fifo_put(&taskdbg_fifo, m);
}

#define STACKSIZE 1024
#define PRIORITY  5
K_THREAD_DEFINE(uart_thread,       STACKSIZE, uart_task,       NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(dispatcher_thread, STACKSIZE, dispatcher_task, NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(red_thread,        STACKSIZE, red_led_task,    NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(yellow_thread,     STACKSIZE, yellow_led_task, NULL,NULL,NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(green_thread,      STACKSIZE, green_led_task,  NULL,NULL,NULL, PRIORITY, 0, 0);


int main(void)
{
    k_timer_init(&timer, timer_handler, NULL);

    timing_init();
    timing_start();
    init_uart();
    init_led();
    init_buttons();
    return 0;
}

//Initit
static int init_uart(void) {
    if (!device_is_ready(uart_dev)) {
        PRINTK("UART device not ready\n");
        return -ENODEV;
    }
    return 0;
}
static int init_led(void) {
    int ret;

    if (!gpio_is_ready_dt(&red) || !gpio_is_ready_dt(&green)) {
        PRINTK("LED ports not ready\n");
        return -ENODEV;
    }
    ret = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);   if (ret) return ret;
    ret = gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE); if (ret) return ret;

    gpio_pin_set_dt(&red,   0);
    gpio_pin_set_dt(&green, 0);

    PRINTK("LEDs configured\n");
    return 0;
}
static int init_buttons(void) {
    int ret;

    if (!gpio_is_ready_dt(&btn_red) || !gpio_is_ready_dt(&btn_yel) || !gpio_is_ready_dt(&btn_grn)) {
        PRINTK("Button ports not ready\n");
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

//Button ISR + work-funktiot (R/Y/G)

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

static void red_work_fn(struct k_work *work) {
    struct seq_item *item = k_malloc(sizeof(*item));
    if (item) { item->value = 'R'; k_fifo_put(&seq_fifo, item); PRINTK("BTN -> R\n"); }
}
static void yel_work_fn(struct k_work *work) {
    struct seq_item *item = k_malloc(sizeof(*item));
    if (item) { item->value = 'Y'; k_fifo_put(&seq_fifo, item); PRINTK("BTN -> Y\n"); }
}
static void grn_work_fn(struct k_work *work) {
    struct seq_item *item = k_malloc(sizeof(*item));
    if (item) { item->value = 'G'; k_fifo_put(&seq_fifo, item); PRINTK("BTN -> G\n"); }
}

//UART taski
// - R/Y/G: syttyy heti ja timer_color päivittyy viimeisimmän värin mukaan
// - D: debug toggle
// - ISO A + HHMMSS: timemode päivittyy--> ajastin käyntiin-->timer_color Viikko5

static char time_buf[7];
static int  time_buf_len = 0;
static bool time_mode = false;  // true kun A tullut

static inline void time_mode_reset(void) {
    time_mode   = false; //tilakone "A":lle
    time_buf_len = 0;
    time_buf[0] = '\0';
}
void uart_task(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    char rc;

    while (1) {
        if (uart_poll_in(uart_dev, &rc) == 0) {
            unsigned char urc = (unsigned char)rc;

            if (time_mode) {
                if (urc == '\r' || urc == '\n') { time_mode_reset(); continue; }
                if (time_buf_len < 6) time_buf[time_buf_len++] = (char)urc;
                if (time_buf_len == 6) {
                    time_buf[6] = '\0';
                    int secs = time_parse(time_buf);  // validoinnit timeparsessa
                    if (secs >= 0) {
                        timer_delay_s = secs;
                        k_timer_stop(&timer);
                        k_timer_start(&timer, K_SECONDS(timer_delay_s), K_NO_WAIT);
                    } else {
                        PRINTK("Invalid time '%s' (ERROR=%d)\n", time_buf, secs);
                    }
                    time_mode_reset();
                }
                continue;
            }
            char c = (char)toupper(urc);
            if (c == 'A') { time_mode_reset(); time_mode = true; continue; }
            if (c == 'R' || c == 'Y' || c == 'G') {
                timer_color = c;
                struct seq_item *item = k_malloc(sizeof(*item));
                if (item) { item->value = c; k_fifo_put(&seq_fifo, item); }
                continue;
            }
            if (c == 'D') {
                bool new_state = !dbg_on;
                struct taskdbg_msg *m = k_malloc(sizeof(*m));
                if (m) {
                    m->ev  = 'D';
                    m->col = new_state ? '1' : '0';   // '1' = ON, '0' = OFF
                    k_fifo_put(&taskdbg_fifo, m);
                }
                dbg_on = new_state; 
                continue;
            }
        }
        k_msleep(5);
    }
}
// Dispatcher + LED taskit
static void dispatcher_task(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    PRINTK("Dispatcher started\n");

    while (1) {
        struct seq_item *it = k_fifo_get(&seq_fifo, K_FOREVER);
        char ch = it->value;
        k_free(it);

        switch (ch) {
            case 'R':
                k_mutex_lock(&red_mutex, K_FOREVER);
                red_trig = true;
                k_condvar_signal(&red_cv);
                k_mutex_unlock(&red_mutex);
                PRINTK("Dispatch -> RED\n");
                break;
            case 'Y':
                k_mutex_lock(&yellow_mutex, K_FOREVER);
                yel_trig = true;
                k_condvar_signal(&yellow_cv);
                k_mutex_unlock(&yellow_mutex);
                PRINTK("Dispatch -> YELLOW\n");
                break;
            case 'G':
                k_mutex_lock(&green_mutex, K_FOREVER);
                grn_trig = true;
                k_condvar_signal(&green_cv);
                k_mutex_unlock(&green_mutex);
                PRINTK("Dispatch -> GREEN\n");
                break;
            default:
                continue;
        }
        k_sem_take(&release_sem, K_FOREVER);
    }
}

static void red_led_task(void *, void *, void*) {
    taskdbg_push('S','R');
    while (1) {
        k_mutex_lock(&red_mutex, K_FOREVER);
        while (!red_trig) {
            k_condvar_wait(&red_cv, &red_mutex, K_FOREVER);
        }
        red_trig = false;
        k_mutex_unlock(&red_mutex);

        timing_t t0 = timing_counter_get();
        taskdbg_push('1','R');
        gpio_pin_set_dt(&red, 1);
        k_msleep(LIGHT_MS);
        taskdbg_push('0','R');
        gpio_pin_set_dt(&red, 0);
        timing_t t1 = timing_counter_get();

        uint64_t ns   = timing_cycles_to_ns(timing_cycles_get(&t0, &t1));
        uint64_t usec = ns / 1000ULL;

        struct meas_item *m = k_malloc(sizeof(*m));
        if (m) { m->value = 'R'; m->usec = usec; k_fifo_put(&meas_fifo, m); }

        k_sem_give(&release_sem);
    }
}

static void yellow_led_task(void *, void *, void*) {
    taskdbg_push('S','Y');
    while (1) {
        k_mutex_lock(&yellow_mutex, K_FOREVER);
        while (!yel_trig) {
            k_condvar_wait(&yellow_cv, &yellow_mutex, K_FOREVER);
        }
        yel_trig = false;
        k_mutex_unlock(&yellow_mutex);

        timing_t t0 = timing_counter_get();
        taskdbg_push('1','Y');
        gpio_pin_set_dt(&red, 1);
        gpio_pin_set_dt(&green, 1);
        k_msleep(LIGHT_MS);
        taskdbg_push('0','Y');
        gpio_pin_set_dt(&red, 0);
        gpio_pin_set_dt(&green, 0);
        timing_t t1 = timing_counter_get();

        uint64_t ns   = timing_cycles_to_ns(timing_cycles_get(&t0, &t1));
        uint64_t usec = ns / 1000ULL;

        struct meas_item *m = k_malloc(sizeof(*m));
        if (m) { m->value = 'Y'; m->usec = usec; k_fifo_put(&meas_fifo, m); }

        k_sem_give(&release_sem);
    }
}

static void green_led_task(void *, void *, void*) {
    taskdbg_push('S','G');
    while (1) {
        k_mutex_lock(&green_mutex, K_FOREVER);
        while (!grn_trig) {
            k_condvar_wait(&green_cv, &green_mutex, K_FOREVER);
        }
        grn_trig = false;
        k_mutex_unlock(&green_mutex);

        timing_t t0 = timing_counter_get();
        taskdbg_push('1','G');
        gpio_pin_set_dt(&green, 1);
        k_msleep(LIGHT_MS);
        taskdbg_push('0','G');
        gpio_pin_set_dt(&green, 0);
        timing_t t1 = timing_counter_get();

        uint64_t ns   = timing_cycles_to_ns(timing_cycles_get(&t0, &t1));
        uint64_t usec = ns / 1000ULL;

        struct meas_item *m = k_malloc(sizeof(*m));
        if (m) { m->value = 'G'; m->usec = usec; k_fifo_put(&meas_fifo, m); }

        k_sem_give(&release_sem);
    }
}
//Debug taski
static void debug_task(void *, void *, void *) {
    static uint8_t  seq_count = 0;
    static uint64_t seq_sum_us = 0;

    while (1) {
        struct taskdbg_msg *m = k_fifo_get(&taskdbg_fifo, K_FOREVER);
        if (m) {
            switch (m->ev) {
            case 'S':
                switch (m->col) {
                case 'R': printk("Red task started\n");    break;
                case 'Y': printk("Yellow task started\n"); break;
                case 'G': printk("Green task started\n");  break;
                }
                break;
            case '1':
                switch (m->col) {
                case 'R': printk("RED ON\n");    break;
                case 'Y': printk("YELLOW ON\n"); break;
                case 'G': printk("GREEN ON\n");  break;
                }
                break;
            case '0':
                switch (m->col) {
                case 'R': printk("RED OFF\n");    break;
                case 'Y': printk("YELLOW OFF\n"); break;
                case 'G': printk("GREEN OFF\n");  break;
                }
                break;
            case 'D':
                printk("DEBUG %s\n", (m->col=='1') ? "ON" : "OFF");
                break;
            default:
                break;
            }
            k_free(m);
        }

        struct meas_item *mm;
        while ((mm = k_fifo_get(&meas_fifo, K_NO_WAIT)) != NULL) {
            printk("TASK %c time: %llu us\n",
                   mm->value, (unsigned long long)mm->usec);

            seq_sum_us += mm->usec;
            seq_count++;
            if (seq_count == 3) {
                printk("Total (3 tasks): %llu us\n",
                       (unsigned long long)seq_sum_us);
                /* nollaa uutta kolmen sarjaa varten */
                seq_count = 0;
                seq_sum_us = 0;
            }
            k_free(mm);
        }

        k_yield();
    }
}
