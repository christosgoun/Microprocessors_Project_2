#include "platform.h" 
#include "gpio.h" 
#include "uart.h" 
#include "timer.h" 
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* System State Definitions  */
typedef enum {
    STATE_IDLE,  //when the system waits for the input
    STATE_EXECUTING_PROFILE, //when the led is blinking with the given frequency
    STATE_ESTOP, //emergency stop
		STATE_ESTOP_AWAITING_PWD  //waiting for the "Unlock" password
} SystemState;

/* Global Timing Variables (incremented by SysTick)(instead of delay())  */
//every variable that changes its value due to interrupts, needs to be volatile so we load the value from RAM and not registers
volatile uint32_t sys_ticks = 0;  //the counter that increments its value every 1ms because of SysTick Timer
volatile uint32_t last_rx_time = 0;  //stores the moment that the last character was recieved from UART (4 seconds)
uint32_t profile_start_time = 0; // it is used to count the time that each digit is executed (2s)
uint32_t led_toggle_time = 0; //it is used to change the state of the LED, given teh frequency
volatile uint32_t estop_override_time = 0; //counts 5 seconds to accept the UNLOCK password

/* UART Communication Variables  */
#define MAX_BUFFER_SIZE 64  
//MAX_BUFFER_SIZE=64, this is used instead of dynamic allocation in prder to have a waitng queue
char rx_buffer[MAX_BUFFER_SIZE]; //define the array
volatile uint8_t rx_index = 0; //in which position in the buffer will we add the next element
volatile uint8_t rx_complete = 0; 
//if we press enter then uint8_t rx_complete=1 and the command is being processed in main (flag)
volatile uint8_t rx_active = 0; // Tracks if an input sequence has started, it starts teh 4 seconds countdown

/* Operating Profile Variables */
SystemState current_state = STATE_IDLE; //tracks the current state
char current_profile[MAX_BUFFER_SIZE];  //coppies the rx_buffer
uint8_t profile_digit_index = 0;  //which digit is being executed
uint8_t profile_length = 0; //the total length of the input
uint8_t loop_profile = 0; //if we have - at the end then the flag is 1 and we have an endless loop

// --- Interrupt Callback Functions ---

/* 1. Timer Callback: Triggered every 1ms */
void systick_callback(void) {
    sys_ticks++;
}

/* 2. UART Callback: Triggered on every byte received  */
void uart_rx_isr(uint8_t c) {   
    if (current_state == STATE_ESTOP) {  // If system is locked in ESTOP, ignore inputs except during password entry
        uart_print("[ERROR] SYSTEM LOCKED\r\n");
        return;
    }

    // Process termination characters (Enter key)
    if (c == '\r' || c == '\n') {  //if enter
        if (rx_index > 0) {   //check if buffer is not empty
            rx_buffer[rx_index] = '\0'; // add the /0 at the end and make the string c-"suitable"
            rx_complete = 1;   //the command is ready to be processed in main
            rx_active = 0;  //we have presses enter so the number sequence is complete and we don't need the 4 seconds
        }
    } else {
        // Buffer the incoming character
        if (rx_index < MAX_BUFFER_SIZE - 1) {  //overflow check
            rx_buffer[rx_index++] = (char)c; //1st stores the char and then increments teh index to store teh next char
            last_rx_time = sys_ticks; 
					//Reset inactivity timer for 4s timeout fow every new char recieved (sys_ticks - last_rx_time=0)
            rx_active = 1;  //a new input is active
        }
    }
}

/* 3. EXTI Callback: Triggered by the Emergency Stop Button */
void button_exti_callback(int status) {
    (void)status; // Suppresses "unused parameter" warning

    if (current_state != STATE_ESTOP && current_state != STATE_ESTOP_AWAITING_PWD) {  
			//check if we are not on emergency mode
        current_state = STATE_ESTOP; //we enter emergency mode
        gpio_set(P_LED_R, LED_ON); // LED remains solid ON, process_blinking does not affect the LED
        rx_index = 0;              // Discard any partial inputs
        rx_active = 0;             // Discard any partial inputs
        uart_print("\r\n*** EMERGENCY STOP ACTIVATED ***\r\n");
    } 
    else if (current_state == STATE_ESTOP) {
        // Second button press initiates the unlock procedure
        current_state = STATE_ESTOP_AWAITING_PWD; // we are on the "waiting for the ""UNLOCK"" password, mode
        estop_override_time = sys_ticks; //store the time so the 5 seconds countdown stops
        rx_index = 0;  // Discard any partial inputs so UNLOCK is not affected
        rx_active = 0;  // Discard any partial inputs so UNLOCK is not affected
        uart_print("\r\nOverride requested. Awaiting password...\r\n");
    }
}

// --- Logic Implementation Functions ---

/* Analyses the input buffer and sets up the execution parameters */
void start_profile_execution(void) {
    strcpy(current_profile, rx_buffer); 
	//store the content of UART buffer in an array so the new command can be recieved without destroying the current being executed
    profile_length = strlen(current_profile); //calculate how many total chars has the user send
    
    // Check if the profile should loop (indicated by a dash '-') 
    if (current_profile[profile_length - 1] == '-') {
        loop_profile = 1; //flag for eternal loop
        profile_length--; // Exclude the dash from the functional sequence, because it can handle only numbers
    } else {
        loop_profile = 0;
    }

    profile_digit_index = 0; //executions starts from the first digit
    profile_start_time = sys_ticks; //stores the current time so the 2 seconds countdown starts
    current_state = STATE_EXECUTING_PROFILE;
    
    char msg[64]; 
    sprintf(msg, "Starting Profile: %s\r\n", rx_buffer); //stores the string +the digit sequence on the msg variable
    uart_print(msg); //sends the message (msg) to teraterm
}

/* Handles the LED toggling based on the current frequency digit (1-9 Hz) */
void process_blinking(void) {
    char current_char = current_profile[profile_digit_index]; //we store the current digit we execute
    
    // If digit is '0', the LED remains OFF 
    if (current_char == '0') {
        gpio_set(P_LED_R, LED_OFF);
        return;
    }
    
    int freq = current_char - '0';  //converting ASCII to the actual value
    if (freq > 0 && freq <= 9) {
        // Calculate toggle interval (ms) for requested frequency
        uint32_t toggle_interval = 1000 / (2 * freq);  //we want twice the frequency in changes of LED state (on/off)
        
        if (sys_ticks - led_toggle_time >= toggle_interval) { //current time-time when the previous change happened>= the calculated time
					//if we have reached the twice the frequency time, time to change the LED state
            gpio_toggle(P_LED_R); //change the LED state
            led_toggle_time = sys_ticks;  //make 0 the diiference so we can measure the next change
        }
    }
}

// --- Application Entry Point ---

int main(void) {
    /* Peripheral Initialization */
    uart_init(115200); // the same speed at tera term
    uart_enable();
    
    // Initialize SysTick to 1ms (1000 microseconds)
    timer_init(1000);
    timer_set_callback(systick_callback); //every 1ms sys_tick++
    timer_enable(); 

    /* GPIO Setup for LED and Switch  */
    gpio_set_mode(P_LED_R, Output); //user led is output
    gpio_set_mode(P_SW, Input); //button is input
    
    /* Interrupt configuration  */
    uart_set_rx_callback(uart_rx_isr); //for every new char, call uart_rx_isr
    
    // Setup EXTI trigger on falling edge for the onboard button
    gpio_set_trigger(P_SW, Rising); //the button will be triggered on the rise, because the switch is active low
    gpio_set_callback(P_SW, button_exti_callback); //when the trigger happens, then the estop fuction is called

    /* --- NVIC Interrupt Priority Configuration --- */
    // 1. Highest Priority (1): Emergency Stop (EXTI on PC13)
    NVIC_SetPriority(EXTI15_10_IRQn, 1); 
    // 2. Medium Priority (2): System Timers (SysTick)
    NVIC_SetPriority(SysTick_IRQn, 2);
    // 3. Lowest Priority (3): Serial Communication (UART)
    NVIC_SetPriority(USART2_IRQn, 3);

    uart_print("System Initialized. Awaiting Profile...\r\n");

    /* Main non-blocking loop  */
    while (1) {
        // Handle completed command inputs
			if (rx_complete) { //check if the "Enter" has been pressed (rx_complete==1)
            rx_complete = 0; //reset the flag
            
            if (current_state == STATE_ESTOP_AWAITING_PWD) { //check if we are in waiting mode for the "Unlock"
                // Verify the "UNLOCK" command [ 	
                if (strcmp(rx_buffer, "UNLOCK") == 0) {
                    current_state = STATE_IDLE;
                    gpio_set(P_LED_R, LED_OFF); //turn off the led
                    uart_print("System Unlocked. Awaiting new profile...\r\n");
                } else { //if password id wrong
                    current_state = STATE_ESTOP;
                    uart_print("Wrong Password. System Locked.\r\n");
                }
            } 
            else if (current_state != STATE_ESTOP) {
                // Any new valid profile overrides the current execution immediately
                start_profile_execution();
            }
            rx_index = 0; //clears buffer index, new command will be written on the start of the buffer
        }

        // Safety: 4-second timeout for incomplete profile inputs 
        if (rx_active && current_state != STATE_ESTOP && current_state != STATE_ESTOP_AWAITING_PWD) {
					//check if buffer in not empty(the user has started typing) and we are in a normal mode 
            if (sys_ticks - last_rx_time >= 4000) {                
                rx_index = 0; //clear buffer
                rx_active = 0; //we ahev to write from the start the numbers
								uart_print("\r\n[TIMEOUT] Input cancelled. Please start over.\r\n");
            }
        }

        // Safety: 5-second timeout for UNLOCK password submission
        if (current_state == STATE_ESTOP_AWAITING_PWD) {
            if (sys_ticks - estop_override_time >= 5000) {
                current_state = STATE_ESTOP;
                rx_index = 0; 
                rx_active = 0;
                uart_print("\r\n[TIMEOUT] Password entry expired. System locked.\r\n");
            }
        }

        // Profile Execution Logic (Non-blocking)
        if (current_state == STATE_EXECUTING_PROFILE) {
            process_blinking();
            
            // Advance to the next digit every 2 seconds
            if (sys_ticks - profile_start_time >= 2000) {
                profile_digit_index++; //next digit
                profile_start_time = sys_ticks;
                
                // End of sequence checks
                if (profile_digit_index >= profile_length) {
                    if (loop_profile) { // Continuous loop if the last char is -
                        profile_digit_index = 0; //we start again from the 1st digit
                    } else { 
                        current_state = STATE_IDLE;
                        gpio_set(P_LED_R, LED_OFF); //LED close
                        uart_print("Profile Execution Finished.\r\n");
                    }
                }
            }
        }
    }
}