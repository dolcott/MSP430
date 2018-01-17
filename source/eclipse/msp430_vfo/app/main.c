/*
 * 1/13/18
 * Variable Frequency Oscillator Project
 *
 * Contains the following:
 * si5351 vfo breakout board from Adafruit.
 * SPI LCD to display the output frequency and channel
 * 3 output channels.
 * Rotery Encoder to change the frequency
 * User buttons
 * LEDs
 * Clock Configured to 16mhz
 *
 * I2C: P1.6 and P1.7
 * SPI:
 * P1.1 - UCA0SOMI	- slave out, master in
 * P1.2 - UCA0SIMO - slave in master out
 * P1.4 - UCA0CLK - serial clock
 * P1.5 - chip select - config as general output
 *
 *
 * User Controls:
 * Button on P1.3 - toggle display states
 * Rotary Encoder - P2.0, P2.1 - 2 bits - interrupted
 *
 * LCD Control lines - P2.3, P2.4 for reset and cmd/data
 * LCD is the Nokia 84x48 display with simple functions for
 * clear and text (no frame buffer for graphics)
 *
 * All of P1.x is consumed. Pins available on Port2:
 * P2.0, P2.1, P2.2, P2.3, P2.4, P2.5
 *
 * encoder:
 * two outputs and a ground line.  Add 10k pullups to the
 * outputs, put caps from output to ground(? saw this somewhere)
 * put the interrupt on both pins, rising and falling.  on interrupt
 * read the state and get the direction ffrom function that saves
 * last state.  Update freq
 *
 * Control the program using the simple tasker
 *
 * NOTE:
 * msp430-readelf: useful tool
 *
 *
 *
 *
 */

//includes
#include <msp430.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "i2c.h"
#include "si5351.h"
#include "encoder.h"
#include "task.h"
#include "nokia.h"

//prototypes
void dummyDelay(unsigned int delay);

void delay_ms(volatile int ticks);
void TimeDelay_Decrement(void);
void GPIO_init(void);
void TimerA_init(void);
void Interrupt_init(void);
void LED_RED_TOGGLE(void);

//Task Function Prototypes
void TaskFunction_RxTask(void);
void TaskFunction_LedTask(void);
void TaskFunction_DisplayTask(void);

//global variables
static volatile int TimeDelay;
static volatile uint8_t gDisplayMode;

//main program
int main(void)
{
	//disable the watchdog timer
	WDTCTL = WDTPW + WDTHOLD;
	
	TimerA_init();
	GPIO_init();		//leds and buttons
	Interrupt_init();	//button interrupts
	LCD_init();			//configure pins (p2.3 and 2.4) and registers
	i2c_init();			//si5351			-temporarily disable

	//wait a bit for vfo to power up
	delay_ms(50);

	//wait for the vfo to power up before
	//initializing
	//returns 1 if not powered up
	while (vfo_GetInitStatus())
	{
		delay_ms(10);
	}

	if (!vfo_GetInitStatus())
		vfo_init();			//si5351			-temporarily disable

	delay_ms(50);

	encoder_init();		//rotary encoder

	//set the initial frequency
	vfo_SetChannel0Frequency(7000000);

	//display initial startup routine on the lcd
	LCD_Clear(0x00);
	LCD_WriteString(0, "Line1");
	LCD_WriteString(1, "Line2");
	LCD_WriteString(2, "Line3");
	LCD_WriteString(3, "Line4");
	LCD_WriteString(4, "Line5");

	delay_ms(2000);
	LCD_Clear(0x00);



	Task_Init();		//init the tasker

	Task_AddTask("rxTask", TaskFunction_RxTask, 100, 0);
	Task_AddTask("led", TaskFunction_LedTask, 500, 1);
	Task_AddTask("display", TaskFunction_DisplayTask, 200, 2);

	//start the tasker - should not return from
	//this function as it's a while loop
	Task_StartScheduler();

	return 0;
}

void dummyDelay(unsigned int delay)
{
	volatile unsigned int temp = delay;
	while (temp > 0)
		temp--;
}

////////////////////////////////////////
void delay_ms(volatile int ticks)
{
	TimeDelay = ticks;

	//TimeDelay is static and gets decremented
	//in the TimerA ISR
	while (TimeDelay !=0);
}


/////////////////////////////////////////
//Function called from the TimerA ISR
void TimeDelay_Decrement(void)
{
	if (TimeDelay != 0x00)
	{
		TimeDelay--;
	}
}

//////////////////////////////////////
//Note: SPIB conflicts with P1.6, so the green led does not work
void GPIO_init(void)
{
	//setup bit 0 as output
	P1DIR |= BIT0;
	P1OUT &=~ BIT0;		//turn off

	//set up the user button bit 3
	//as input with pull up enabled
	P1DIR &=~ BIT3;		//input
	P1REN |= BIT3;		//enable pullup/down
	P1OUT |= BIT3;		//resistor set to pull up

}

/////////////////////////////////////////
//Sets up the timer And the frequency
//of the clock, since the rate of overflow
//is connected to this
//
void TimerA_init(void)
{

	//set the clock frequency 16 mhz
	BCSCTL1 = CALBC1_16MHZ;
	DCOCTL = CALDCO_16MHZ;

//	BCSCTL1 = CALBC1_8MHZ;
//	DCOCTL = CALDCO_8MHZ;

//	BCSCTL1 = CALBC1_1MHZ;
//	DCOCTL = CALDCO_1MHZ;

	//Set up Timer A register TACTL
	TACTL = 0x0000;		//start from all all clear
	TACTL |= BIT9;		//use SMCLK as the source
	TACTL |= BIT7;		//use prescaler 8
	TACTL |= BIT6;
	TACTL |= BIT4;		//count up to TACCR0(set below)
	TACTL |= BIT1;		//enable the interrupt
	TACTL &=~ BIT0;		//clear all pending interrupts

	//set the countup value.  for 1ms delay,
	//the countup value depends on the clock freq.

	TACCR0 = 2000;		//use 2000 for 16 mhz
	//	TACCR0 = 125;		//use 125 for  1 mhz
	//	TACCR0 = 1000;		//use 1000 for 8 mhz

	//TACCTL0 - compare capture control register.
	//this has to be set up along with the timer interrupt
	//bit 4 enables the interrupts for this register
	//also called CCIE

	TACCTL0 = CCIE;		//compare, capture interrupt enable

}

///////////////////////////////////////////
void Interrupt_init(void)
{
	//enable all the interrupts
	__bis_SR_register(GIE);

	P1IE |= BIT3;		//enable button interrupt
	P1IFG &=~ BIT3;		//clear the flag

}

///////////////////////////////////////////
void LED_RED_TOGGLE(void)
{
	P1OUT ^= BIT0;
}




//////////////////////////////////////
//TimerA ISR
//Controls delay function and the
//tick rate for the task timer.
#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer_A(void)
{
	//clear the timer interrupt
	TACTL &=~ BIT0;

	//time tick for delay_ms()
	TimeDelay_Decrement();

	//manage the tick for the tasker
	Task_TimerISRHandler();
}


/////////////////////////////////////
//P1 ISR for the user button
#pragma vector = PORT1_VECTOR
__interrupt void Port_1(void)
{
	//dummy delay
	dummyDelay(2000);
	if (!(P1IN & BIT3))
	{
		//send a message to rxTask - toggle
		//send message to the receiver task with
		TaskMessage msg;
		msg.signal = TASK_SIG_USER_BUTTON;
		int index = Task_GetIndexFromName("rxTask");
		Task_SendMessage(index, msg);

	}


	//clear the interrupt flag - button
	P1IFG &=~ BIT3;

	//do something....
}



////////////////////////////////////
//Generic Receiver Task for all tasks
//in the system.  Checks for messages
//waiting and processes them.
//
void TaskFunction_RxTask(void)
{
	TaskMessage msg = {TASK_SIG_NONE};
	uint8_t index = Task_GetIndexFromName("rxTask");

	while (Task_GetNextMessage(index, &msg) > 0)
	{
		switch(msg.signal)
		{
			case TASK_SIG_NONE:		break;
			case TASK_SIG_ON:		break;
			case TASK_SIG_OFF:		break;
			case TASK_SIG_TOGGLE:	break;

			case TASK_SIG_ENCODER_LEFT:
			{
				vfo_DecreaseChannel0Frequency();
				break;
			}
			case TASK_SIG_ENCODER_RIGHT:
			{
				vfo_IncreaseChannel0Frequency();
				break;
			}
			case TASK_SIG_USER_BUTTON:
			{
				P1OUT ^= BIT0;
				break;
			}

			case TASK_SIG_LAST:		break;

			default:
				break;
		}
	}
}





////////////////////////////////////////
//Task Functions - LED task
//Function is run to completion
//See task add function for task
//frequency
void TaskFunction_LedTask(void)
{
	char buffer[12];
	uint8_t len, i = 0;
	for (i = 0 ; i < 12 ; i++)
		buffer[i] = 0x00;

	uint32_t freq = vfo_GetChannel0Frequency();

	len = LCD_DecimaltoBuffer(freq, buffer, 12);

	LCD_ClearRow(0, 0x00);
	LCD_ClearRow(1, 0x00);

	LCD_WriteString(0, "CH1 (HZ):");
	LCD_WriteStringLength(1, buffer, len);



	LED_RED_TOGGLE();


}


////////////////////////////////////
//DisplayTask -
//Update the contents of the display
//based on the display mode
//
void TaskFunction_DisplayTask(void)
{
	uint32_t freq = vfo_GetChannel0Frequency();


}










