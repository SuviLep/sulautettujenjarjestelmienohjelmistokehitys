#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/*
Viikko 2: 1+1+1p
 Liikennevalosimulaatio Nordic nRF5340 Audio DK:lla
  - Punainen, keltainen ja vihreä valo vilkkuvat järjestyksessä (yhdestä napista)
  - 5 nappia käytössä
  		- nappi/paussitus liikennevaloille
		- 3 nappia värien manuaaliseen ohjantaan
		- 1 nappi keltaisen valon vilkuttamiseen 
 */

// Led pin configurations
static const struct gpio_dt_spec red = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec blue = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios); //tehtävän annossa tämä piti tehdä myös siniselle


// Configure buttons
#define BUTTON_1 DT_ALIAS(sw0) // pause/play (VOL–)
#define BUTTON_2 DT_ALIAS(sw1) // red on/off (VOL+)
#define BUTTON_3 DT_ALIAS(sw2) // yellow on/off (PLAY/PAUSE)
#define BUTTON_4 DT_ALIAS(sw3) // green on/off (Button4)
#define BUTTON_5 DT_ALIAS(sw4) // yellow blink mode (Button5)

static const struct gpio_dt_spec button_1 = GPIO_DT_SPEC_GET(BUTTON_1, gpios);
static const struct gpio_dt_spec button_2 = GPIO_DT_SPEC_GET(BUTTON_2, gpios);
static const struct gpio_dt_spec button_3 = GPIO_DT_SPEC_GET(BUTTON_3, gpios);
static const struct gpio_dt_spec button_4 = GPIO_DT_SPEC_GET(BUTTON_4, gpios);
static const struct gpio_dt_spec button_5 = GPIO_DT_SPEC_GET(BUTTON_5, gpios);

//jokaiselle napille oma callback
static struct gpio_callback button_1_data;
static struct gpio_callback button_2_data;
static struct gpio_callback button_3_data;
static struct gpio_callback button_4_data;
static struct gpio_callback button_5_data;


// Alustusfunktiot
int init_button(void); 
int init_led(void);

// Red led thread initialization
#define STACKSIZE 1024 //Muistin varaus
#define PRIORITY 5

//Taskien prototyypit
void red_led_task(void *, void *, void*);
void green_led_task(void *, void *, void*);
void yellow_led_task(void *, void *, void*);

K_THREAD_DEFINE(red_thread,STACKSIZE,red_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(green_thread,STACKSIZE,green_led_task,NULL,NULL,NULL,PRIORITY,0,0);
K_THREAD_DEFINE(yellow_thread,STACKSIZE,yellow_led_task,NULL,NULL,NULL,PRIORITY,0,0);

int led_state= 0;
volatile bool running= true; //vilkkuuko valo
volatile bool manual_red = false; // nappi 2 play/pause-nappi ohjausta varten, REd
volatile bool manual_yellow = false; //
volatile bool manual_green = false;  
volatile bool yellow_blink_mode = false; 

// Buttoneiden keskeytykset
void button_1_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	
	running =!running;
	if(running){
		printk("button  PAUSE (VOL -) pressed, lights continue\n");
	} else {
		printk("button PAUSE (VOL -) pressed, lights paused\n");
	}	
}
void button_2_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    manual_red = !manual_red;
    if (!running) {
        // Jos ollaan pausella, ohjataan punainen suoraan
        gpio_pin_set_dt(&red, manual_red ? 1 : 0);
    }
    printk("button VOL + pressed -> RED %s\n", manual_red ? "ON" : "OFF");
}
void button_3_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    manual_yellow = !manual_yellow;
    if (!running) {
        gpio_pin_set_dt(&red, manual_yellow ? 1 : 0);
        gpio_pin_set_dt(&green, manual_yellow ? 1 : 0);
    }
    printk("button PLAY/PAUSE presssed -> YELLOW %s\n", manual_yellow ? "ON" : "OFF");
}

void button_4_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    manual_green = !manual_green;
    if (!running) {
        gpio_pin_set_dt(&green, manual_green ? 1 : 0);
    }
    printk("button 4 pressed -> GREEN %s\n", manual_green ? "ON" : "OFF");
}

void button_5_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    yellow_blink_mode = !yellow_blink_mode;
    printk("button 5 pressed -> YELLOW BLINK MODE %s\n", yellow_blink_mode ? "ON" : "OFF");
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
	 // Jokaiselle napille: tarkistus -> konfigurointi -> keskeytyksen kytkentä -> callback
	int ret; //return value varten
    if (!gpio_is_ready_dt(&button_1)) { printk("Error: button 1 not ready\n"); return -1; }
    ret = gpio_pin_configure_dt(&button_1, GPIO_INPUT); if (ret) return ret;
    ret = gpio_pin_interrupt_configure_dt(&button_1, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;
    gpio_init_callback(&button_1_data, button_1_handler, BIT(button_1.pin));
    gpio_add_callback(button_1.port, &button_1_data);
    printk("Set up button 1 ok\n");

    if (!gpio_is_ready_dt(&button_2)) { printk("Error: button 2 not ready\n"); return -1; }
    ret = gpio_pin_configure_dt(&button_2, GPIO_INPUT); if (ret) return ret;
    ret = gpio_pin_interrupt_configure_dt(&button_2, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;
    gpio_init_callback(&button_2_data, button_2_handler, BIT(button_2.pin));
    gpio_add_callback(button_2.port, &button_2_data);
    printk("Set up button 2 ok\n");

    if (!gpio_is_ready_dt(&button_3)) { printk("Error: button 3 not ready\n"); return -1; }
    ret = gpio_pin_configure_dt(&button_3, GPIO_INPUT); if (ret) return ret;
    ret = gpio_pin_interrupt_configure_dt(&button_3, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;
    gpio_init_callback(&button_3_data, button_3_handler, BIT(button_3.pin));
    gpio_add_callback(button_3.port, &button_3_data);
    printk("Set up button 3 ok\n");

    if (!gpio_is_ready_dt(&button_4)) { printk("Error: button 4 not ready\n"); return -1; }
    ret = gpio_pin_configure_dt(&button_4, GPIO_INPUT); if (ret) return ret;
    ret = gpio_pin_interrupt_configure_dt(&button_4, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;
    gpio_init_callback(&button_4_data, button_4_handler, BIT(button_4.pin));
    gpio_add_callback(button_4.port, &button_4_data);
    printk("Set up button 4 ok\n");

    if (!gpio_is_ready_dt(&button_5)) { printk("Error: button 5 not ready\n"); return -1; }
    ret = gpio_pin_configure_dt(&button_5, GPIO_INPUT); if (ret) return ret;
    ret = gpio_pin_interrupt_configure_dt(&button_5, GPIO_INT_EDGE_TO_ACTIVE); if (ret) return ret;
    gpio_init_callback(&button_5_data, button_5_handler, BIT(button_5.pin));
    gpio_add_callback(button_5.port, &button_5_data);
    printk("Set up button 5 ok\n");
	return 0;
}

// PUNAINEN Task to handle red led
void red_led_task(void *, void *, void*) {
	
	printk("Red led thread started\n");
	while (true) {
	    
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
        if (yellow_blink_mode) {
            // Vilkutetaan keltaista, käytetään punaista + vihreää
            gpio_pin_set_dt(&red, 1);
            gpio_pin_set_dt(&green, 1);
            k_sleep(K_MSEC(500));
            gpio_pin_set_dt(&red, 0);
            gpio_pin_set_dt(&green, 0);
            k_sleep(K_MSEC(500));
            continue; // ohitetaan muu logiikka vilkutuksen aikana
        }

        if (running && led_state == 2) {
            // Normi keltainen sekvenssi
            gpio_pin_set_dt(&red,1);
            gpio_pin_set_dt(&green,1);
            printk("Yellow on\n");
            k_sleep(K_SECONDS(1));
            gpio_pin_set_dt(&red,0);
            gpio_pin_set_dt(&green,0);
            printk("Yellow off\n");
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


