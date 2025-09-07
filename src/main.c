#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

// VÄRITOIMINTA 1p+ PAUSE TOIMINTO +1p

// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

// Configure buttons
#define BUTTON_1 DT_ALIAS(sw1) // pause + 1p
#define BUTTON_2 DT_ALIAS(sw2) // red on/off
#define BUTTON_3 DT_ALIAS(sw3) // yellow on/off
#define BUTTON_4 DT_ALIAS(sw4) // green on/off
#define BUTTON_5 DT_ALIAS(sw5) // blink yellow

static const struct gpio_dt_spec button_1 = GPIO_DT_SPEC_GET_OR(BUTTON_1, gpios, {0});
static const struct gpio_dt_spec button_2 = GPIO_DT_SPEC_GET_OR(BUTTON_2, gpios, {0});
//static const struct gpio_dt_spec button_3 = GPIO_DT_SPEC_GET_OR(BUTTON_3, gpios, {0});
//static const struct gpio_dt_spec button_4 = GPIO_DT_SPEC_GET_OR(BUTTON_4, gpios, {0});
//static const struct gpio_dt_spec button_5 = GPIO_DT_SPEC_GET_OR(BUTTON_5, gpios, {0});

static struct gpio_callback button_1_data;
static struct gpio_callback button_2_data;
//static struct gpio_callback button_3_data;
//static struct gpio_callback button_4_data;
//static struct gpio_callback button_5_data;

int init_button(void); 
int init_led(void);

// Red led thread initialization
#define STACKSIZE 1024
#define PRIORITY 5

void red_led_task(void *, void *, void*);
void green_led_task(void *, void *, void*);
void yellow_led_task(void *, void *, void*);

K_THREAD_DEFINE(red_thread,STACKSIZE,red_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(green_thread,STACKSIZE,green_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(yellow_thread,STACKSIZE,yellow_led_task,NULL,NULL,NULL,PRIORITY,0,0);

int led_state= 0;
volatile bool running= true; //vilkkuuko valo
volatile bool manual_red = false; // nappi 2 ohjausta varten

// Button interrupt handler
void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	
	running =!running;
	if(running){
		printk("button  (VOL +) pressed, lights continue\n");
	} else {
		printk("button (VOL +) pressed, lights paused\n");
	}	
}
void button_2_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    manual_red = !manual_red;
    if (!running) {
        // Jos ollaan pausella, ohjataan punainen suoraan
        gpio_pin_set_dt(&red, manual_red ? 1 : 0);
    }
    printk("button 2 (PLAY/PAUSE) pressed -> RED %s\n", manual_red ? "ON" : "OFF");
}

// Main program
int main(void)
{
	init_button();
	init_led();

	return 0;
}

// Initialize leds
int  init_led() {

	// Led pin initialization
	int ret = gpio_pin_configure_dt(&red, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		printk("Error: Led configure failed\n");		
		return ret;
	}
	// set led off
	gpio_pin_set_dt(&red,0);

	int kreen = gpio_pin_configure_dt(&green, GPIO_OUTPUT_ACTIVE);
	if (kreen < 0) {
		printk("Error: Led configure failed\n");		
		return kreen;
	}
	// set led off
	gpio_pin_set_dt(&green,0);

	int plue = gpio_pin_configure_dt(&blue, GPIO_OUTPUT_ACTIVE);
	if (plue < 0) {
		printk("Error: Led configure failed\n");		
		return plue;
	}
	// set led off
	gpio_pin_set_dt(&blue,0);

        led_state= 1;
	printk("Led initialized ok\n");
        
	return 0;
}

// Button initialization
int init_button() {

	int ret;
	if (!gpio_is_ready_dt(&button_1)) {
		printk("Error: button 1 is not ready\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&button_1, GPIO_INPUT);
	if (ret != 0) {
		printk("Error: failed to configure pin\n");
		return -1;
	}

	ret = gpio_pin_interrupt_configure_dt(&button_1, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error: failed to configure interrupt on pin\n");
		return -1;
	}

	gpio_init_callback(&button_1_data, button_1_handler, BIT(button_1.pin));
	gpio_add_callback(button_1.port, &button_1_data);
	printk("Set up button 1 ok\n");

	 // BUTTON 2
    if (!gpio_is_ready_dt(&button_2)) {
        printk("Error: button 2 is not ready\n");
        return -1;
    }
    ret = gpio_pin_configure_dt(&button_2, GPIO_INPUT);
    if (ret != 0) return ret;
    ret = gpio_pin_interrupt_configure_dt(&button_2, GPIO_INT_EDGE_TO_ACTIVE);
    if (ret != 0) return ret;
    gpio_init_callback(&button_2_data, button_2_handler, BIT(button_2.pin));
    gpio_add_callback(button_2.port, &button_2_data);
    printk("Set up button 2 ok\n");
		
	return 0;
}

// PUNAINEN Task to handle red led
void red_led_task(void *, void *, void*) {
	
	printk("Red led thread started\n");
	while (true) {
	    if (!running) {
        // Pause-tilassa ohjataan manuaalisen tilan mukaan
        if (manual_red) {
            gpio_pin_set_dt(&red, 1);
        } else {
            gpio_pin_set_dt(&red, 0);
        }
        k_msleep(100);
        continue;
        }


		if (running && led_state == 1){
        // 1. set led on 
		gpio_pin_set_dt(&red,1);
		printk("Red on\n");
		// 2. sleep for 2 seconds
		k_sleep(K_SECONDS(1));
		// 3. set led off
		gpio_pin_set_dt(&red,0);
		printk("Red off\n");
		// 4. sleep for 2 seconds
		k_sleep(K_SECONDS(1));
        led_state =2;
        }
        k_msleep(100);
	}
}

//KELTAINEN task to handle yellow light
void yellow_led_task(void *, void *, void*) {
	
	printk("yellow led thread started\n");
	while (true) {
        if(running && led_state == 2){
		// 1. set led on 
		gpio_pin_set_dt(&red,1);
        gpio_pin_set_dt(&green,1);

		printk("Yellow on\n");
		// 2. sleep for 2 seconds
		k_sleep(K_SECONDS(1));
		// 3. set led off
		gpio_pin_set_dt(&red,0);
        gpio_pin_set_dt(&green,0);
		printk("Yellow off\n");
		// 4. sleep for 2 seconds
		k_sleep(K_SECONDS(1));
        led_state = 3;
        }
        k_msleep(100);
	}
}

// VIHREÄ task to handle  green light
void green_led_task(void *, void *, void*) {
	
	printk("Green led thread started\n");
	while (true) {
        if (running && led_state == 3){
		// 1. set led on 
		gpio_pin_set_dt(&green,1);
		printk("green on\n");
		// 2. sleep for 2 seconds
		k_sleep(K_SECONDS(1));
		// 3. set led off
		gpio_pin_set_dt(&green,0);
		printk("green off\n");
		// 4. sleep for 2 seconds
		k_sleep(K_SECONDS(1));
        led_state = 1;
        }
        k_msleep(100);
	}
}


