/**
 * Eduardo Diaz
 * 
 * Synthesizing Birdsong with the RP2040
 * 
 * Inspired by https://vanhunteradams.com/Pico/Birds/Birdsong.html
 * 
 * 
 * KEYPAD CONNECTIONS
 *  - GPIO 9   -->  330 ohms  --> Pin 1 (button row 1)
 *  - GPIO 10  -->  330 ohms  --> Pin 2 (button row 2)
 *  - GPIO 11  -->  330 ohms  --> Pin 3 (button row 3)
 *  - GPIO 12  -->  330 ohms  --> Pin 4 (button row 4)
 *  - GPIO 13  -->     Pin 5 (button col 1)
 *  - GPIO 14  -->     Pin 6 (button col 2)
 *  - GPIO 15  -->     Pin 7 (button col 3)
 * 
 * A timer interrupt on core 0 generates a 400Hz beep
 * thru an SPI DAC, once per keypad press. A single protothread
 * blinks the LED.
 * 
 * SPI CONNECTIONS
 * GPIO 5 (pin 7) Chip select
 * GPIO 6 (pin 9) SCK/spi0_sclk
 * GPIO 7 (pin 10) MOSI/spi0_tx
 * GPIO 2 (pin 4) GPIO output for timing ISR
 * 3.3v (pin 36) -> VCC on DAC 
 * GND (pin 3)  -> GND on DAC 
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "hardware/sync.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"

// Include protothreads
#include "pt_cornell_rp2040_v1_4.h"

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0

// Macros for fixed-point arithmetic (faster than floating point)
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) 
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)( (((signed long long)(a)) << 15) / (b))

//Direct Digital Synthesis (DDS) parameters
#define two32 4294967296.0  // 2^32 (a constant)
#define Fs 44000
#define DELAY 22 // 1/Fs (in microseconds)

volatile unsigned int desired_frequency;
volatile unsigned int possible_keycode;

// the DDS units - core 0
// Phase accumulator and phase increment. Increment sets output frequency.
volatile unsigned int phase_accum_main_0;
volatile unsigned int phase_incr_main_0 = (400.0*two32)/Fs ;

// DDS sine table (populated in main())
#define sine_table_size 256
fix15 sin_table[sine_table_size] ;

// Values output to DAC
int DAC_output_0 ;

// Amplitude modulation parameters and variables
fix15 max_amplitude = int2fix15(1) ;    // maximum amplitude
fix15 attack_inc ;                      // rate at which sound ramps up
fix15 decay_inc ;                       // rate at which sound ramps down
fix15 current_amplitude_0 = 0 ;         // current amplitude (modified in ISR)
fix15 current_amplitude_1 = 0 ;         // current amplitude (modified in ISR)

// SPI data
uint16_t DAC_data_0 ; // output value

// DAC parameters (see the DAC datasheet)
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations (note these represent GPIO number, NOT pin number)
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define LDAC     8
#define LED      25
#define SPI_PORT spi0

//GPIO for timing the ISR
#define ISR_GPIO 2

// Timing parameters for beeps (units of interrupts)
#define ATTACK_TIME             1000
#define DECAY_TIME              1000
#define SUSTAIN_TIME            3500
#define BEEP_DURATION           5720

// Keypad pin configurations
#define BASE_KEYPAD_PIN 9
#define KEYROWS         4
#define NUMKEYS         12

unsigned int keycodes[12] = {   0x28, 0x11, 0x21, 0x41, 0x12,
                                0x22, 0x42, 0x14, 0x24, 0x44,
                                0x18, 0x48} ;
unsigned int scancodes[4] = {   0x01, 0x02, 0x04, 0x08} ;
unsigned int button = 0x70 ;


// State machine variables
volatile unsigned int STATE_KEY1_PRESSED = 0;
volatile unsigned int STATE_KEY2_PRESSED = 0;
volatile unsigned int STATE_KEY3_PRESSED = 0;
volatile unsigned int STATE_KEY3_PREV_PRESSED = 0;
volatile unsigned int freq_count_swoop = 0;
volatile unsigned int freq_count_chirp = 0;
volatile unsigned int freq_count_silence = 0;

// This timer ISR is called on core 0
static void alarm_irq(void) {

    // Assert a GPIO when we enter the interrupt
    gpio_put(ISR_GPIO, 1) ;

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

    if (STATE_KEY1_PRESSED) {
        if (STATE_KEY3_PRESSED) {
            printf("STATE_KEY3_PRESSED and STATE_KEY1_PRESSED\n");
            printf("STATE_KEY3_PRESSED and STATE_KEY1_PRESSED freq_count_silence: %d\n", freq_count_silence);
            // printf("STATE_KEY3_PRESSED and STATE_KEY1_PRESSED (time_us_32): %u\n", time_us_32());
            freq_count_silence = 0;
        }
        // Compute frequency for swoop
        desired_frequency = 260*sin((3.14/5200)*freq_count_swoop) + 1740;
        phase_incr_main_0 = (desired_frequency*two32)/Fs;
        // DDS phase and sine table lookup
        phase_accum_main_0 += phase_incr_main_0;
        DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
        sin_table[phase_accum_main_0>>24])) + 2048;

        // Ramp up amplitude
        if (freq_count_swoop < ATTACK_TIME) {
            current_amplitude_0 = (current_amplitude_0 + attack_inc) ;
        }
        // Ramp down amplitude
        else if (freq_count_swoop > BEEP_DURATION - DECAY_TIME) {
            current_amplitude_0 = (current_amplitude_0 - decay_inc) ;
        }

        // Mask with DAC control bits
        DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff))  ;

        // SPI write (no spinlock b/c of SPI buffer)
        spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;

        freq_count_swoop += 1;
        

        if (freq_count_swoop == 5720) {
            STATE_KEY1_PRESSED = 0;
            current_amplitude_0 = 0;
            freq_count_swoop = 0;
            // irq_set_enabled(ALARM_IRQ, false);
        }
    }

    if (STATE_KEY2_PRESSED) {
        // if (STATE_KEY3_PRESSED) {
        //     printf("STATE_KEY3_PRESSED and STATE_KEY2_PRESSED\n");
        //     printf("STATE_KEY3_PRESSED and STATE_KEY2_PRESSED freq_count_silence: %d\n", freq_count_silence);
        //     // printf("STATE_KEY3_PRESSED and STATE_KEY2_PRESSED (time_us_32): %u\n", time_us_32());
        //     freq_count_silence = 0;
        // }
        // Compute frequency for chirp
        desired_frequency = (1.84e-4) * pow(freq_count_chirp, 2) + 2000;
        phase_incr_main_0 = (desired_frequency*two32)/Fs;
        // DDS phase and sine table lookup
        phase_accum_main_0 += phase_incr_main_0;
        DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
        sin_table[phase_accum_main_0>>24])) + 2048;

        // Ramp up amplitude
        if (freq_count_chirp < ATTACK_TIME) {
            current_amplitude_0 = (current_amplitude_0 + attack_inc) ;
        }
        // Ramp down amplitude
        else if (freq_count_chirp > BEEP_DURATION - DECAY_TIME) {
            current_amplitude_0 = (current_amplitude_0 - decay_inc) ;
        }

        // Mask with DAC control bits
        DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff))  ;

        // SPI write (no spinlock b/c of SPI buffer)
        spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;

        freq_count_chirp += 1;

        if (freq_count_chirp == 5720) {
            STATE_KEY2_PRESSED = 0;
            current_amplitude_0 = 0;
            freq_count_chirp = 0;
            // irq_set_enabled(ALARM_IRQ, false);
        }
    }
    
    if (STATE_KEY3_PRESSED) {
        if (STATE_KEY3_PREV_PRESSED == 0) {
            STATE_KEY3_PREV_PRESSED = 1;
        }
        freq_count_silence += 1;
        // printf("freq_count_silence: %d\n", freq_count_silence);
        // printf("STATE_KEY3_PRESSED = %d\n", STATE_KEY3_PRESSED);
        // printf("STATE_KEY3_PRESSED (time_us_32): %u\n", time_us_32());
    }

    if (STATE_KEY3_PREV_PRESSED) {
        freq_count_silence += 1;
    }

    // De-assert the GPIO when we leave the interrupt
    gpio_put(ISR_GPIO, 0) ;

}

char keytext[40];

typedef enum {
    STATE_NOT_PRESSED,
    STATE_MAYBE_PRESSED,
    STATE_PRESSED,
    STATE_MAYBE_NOT_PRESSED
} KeypadState;

KeypadState current_state = STATE_NOT_PRESSED;

int scan_keypad() {
    int val;
    // Scan the keypad!
    // Some variables
    static int i ;
    static uint32_t keypad ;
    for (i=0; i<KEYROWS; i++) {
        // Set a row high
        gpio_put_masked((0xF << BASE_KEYPAD_PIN),
                        (scancodes[i] << BASE_KEYPAD_PIN)) ;
        // Small delay required
        sleep_us(1) ; 
        // Read the keycode
        keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F) ;
        // Break if button(s) are pressed
        if (keypad & button) break;
    }
    // If we found a button . . .
    if (keypad & button) {
        // Look for a valid keycode.
        for (i=0; i<NUMKEYS; i++) {
            if (keypad == keycodes[i]) {
                // Save keypad value
                val = keypad;
                break;
            }
        }
        // If we don't find one, report invalid keycode
        if (i==NUMKEYS) (val = -1) ;
    }
    // Otherwise, indicate invalid/non-pressed buttons
    else (val=-1) ;
    
    return val;
}

// This thread runs on core 0
static PT_THREAD (protothread_core_0(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    while(1) {

        switch (current_state) {
            case STATE_NOT_PRESSED:
                possible_keycode = scan_keypad();
                if (possible_keycode == -1) {
                    current_state = STATE_NOT_PRESSED;
                } else {
                    current_state = STATE_MAYBE_PRESSED;
                }
                break;
            case STATE_MAYBE_PRESSED:
                if (possible_keycode == scan_keypad()) {
                    // Print key to terminal
                    for (int i=0; i<NUMKEYS; i++) {
                        if (possible_keycode == keycodes[i]) {
                            printf("Key Pressed: %d\n", i);
                            STATE_KEY1_PRESSED = (possible_keycode == keycodes[1]);
                            STATE_KEY2_PRESSED = (possible_keycode == keycodes[2]);
                            STATE_KEY3_PRESSED = (possible_keycode == keycodes[3]);
                            printf("STATE_KEY3_PRESSED = %d\n", STATE_KEY3_PRESSED);
                            //irq_set_enabled(ALARM_IRQ, true);
                            //timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY;
                            break;
                        }
                    }
                    current_state = STATE_PRESSED;
                } else {
                    current_state = STATE_NOT_PRESSED;
                }
                break;
            case STATE_PRESSED:
                if (possible_keycode == scan_keypad()) {
                    // Do nothing remain in STATE_PRESSED state
                    current_state = STATE_PRESSED;
                } else {
                    current_state = STATE_MAYBE_NOT_PRESSED;
                }
                break;
            case STATE_MAYBE_NOT_PRESSED:
                if (possible_keycode == scan_keypad()) {
                    // Transition back to pressed state
                    current_state = STATE_PRESSED;
                } else {
                    current_state = STATE_NOT_PRESSED;
                }
                break;
        }

        PT_YIELD_usec(30000) ;
    }
    // Indicate thread end
    PT_END(pt) ;
}

int main() {
    // Initialize stdio
    stdio_init_all();

    // Map LED to GPIO port, make it low
    gpio_init(LED);
    gpio_set_dir(LED, GPIO_OUT);
    // Set LED to zero
    gpio_put(LED, 0);

    ////////////////// KEYPAD INITS ///////////////////////
    // Initialize the keypad GPIO's
    gpio_init_mask((0x7F << BASE_KEYPAD_PIN));
    // Set row-pins to output
    gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN));
    // Set all output pins to low
    gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0x0 << BASE_KEYPAD_PIN));
    // Turn on pulldown resistors for column pins (on by default)
    gpio_pull_down((BASE_KEYPAD_PIN + 4));
    gpio_pull_down((BASE_KEYPAD_PIN + 5));
    gpio_pull_down((BASE_KEYPAD_PIN + 6));

    ////////////////// DAC INITS ///////////////////////
    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000);
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // Setup the ISR-timing GPIO
    gpio_init(ISR_GPIO);
    gpio_set_dir(ISR_GPIO, GPIO_OUT);
    gpio_put(ISR_GPIO, 0);

    // Map LDAC pin to GPIO port, hold it low (could alternatively tie to GND)
    gpio_init(LDAC);
    gpio_set_dir(LDAC, GPIO_OUT);
    gpio_put(LDAC, 0);

    // set up increments for calculating bow envelope
    attack_inc = divfix(max_amplitude, int2fix15(ATTACK_TIME));
    decay_inc =  divfix(max_amplitude, int2fix15(DECAY_TIME));

    // Build the sine lookup table
    // scaled to produce values between 0 and 4096 (for 12-bit DAC)
    int ii;
    for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = float2fix15(2047*sin((float)ii*6.283/(float)sine_table_size));
    }

    // Enable the interrupt for the alarm (we're using Alarm 0)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    // Associate an interrupt handler with the ALARM_IRQ
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Enable the alarm interrupt
    irq_set_enabled(ALARM_IRQ, true);
    // Write the lower 32 bits of the target time to the alarm register, arming it.
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY;


    // Add core 0 threads
    pt_add_thread(protothread_core_0) ;

    // Start scheduling core 0 threads
    pt_schedule_start ;

}
