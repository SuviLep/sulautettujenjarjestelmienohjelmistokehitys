#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

// Red led thread initialization
#define STACKSIZE 500
#define PRIORITY 5


void red_led_task(void *, void *, void*);
void green_led_task(void *, void *, void*);
void yellow_led_task(void *, void *, void*);

K_THREAD_DEFINE(red_thread,STACKSIZE,red_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(green_thread,STACKSIZE,green_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(yellow_thread,STACKSIZE,yellow_led_task,NULL,NULL,NULL,PRIORITY,0,0);

int led_state= 0;

// Main program
int main(void)
{
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

// PUNAINEN Task to handle red led
void red_led_task(void *, void *, void*) {
	
	printk("Red led thread started\n");
	while (true) {
		if (led_state == 1){
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
                if(led_state == 2){
		// 1. set led on 
		gpio_pin_set_dt(&red,1);
                gpio_pin_set_dt(&green,1);

		printk("Blue on\n");
		// 2. sleep for 2 seconds
		k_sleep(K_SECONDS(1));
		// 3. set led off
		gpio_pin_set_dt(&red,0);
                gpio_pin_set_dt(&green,0);
		printk("Blue off\n");
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
                if (led_state == 3){
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



