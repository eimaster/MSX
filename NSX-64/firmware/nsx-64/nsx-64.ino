/*****************************************************************************
 *   _  _ _____  __    __ _ _  
 *  | \| / __\ \/ /__ / /| | | 
 *  | .` \__ \>  <___/ _ \_  _|
 *  |_|\_|___/_/\_\  \___/ |_| 
 * 
 *  Daniel Jose Viana, august 2019 - danjovic@hotmail.com
 * 
 * MSX adapter for Nintendo 64 controllers
 * This project lets you play MSX games using Nintendo 64 controllers. 
 * The several N64 controller buttons are mapped on a variety of combinations
 * for the MSX and the analog stick can act both as directional controls and
 * as standard Paddles.
 * 
 * This code is released under GPL V2.0 
 * 
 ***************************************************************************** 
 * Controls Mapping
 *   N64      MSX
 * D-UP       UP 
 * D-DOWN    DOWN
 * D-LEFT    LEFT
 * D-RIGHT   RIGHT
 *   A       TRG A
 *   B       TRG B
 *   Z       TRA A + Autofire
 *   L       UP + LEFT
 *   R       UP + RIGHT
 * START     LEFT + RIGHT
 * C-UP      TRGA + UP
 * C-DOWN    TRGA + DOWN   
 * C-LEFT    TRGA + LEFT
 * C-RIGHT   TRGA + RIGHT
 * X Axis    LEFT/RIGHT   PDL(5)[/6]
 * Y Axis    UP/DOWN      PDL(1)[/2]
 * 
*/

//#define DEBUG
#define debugPin 4
#define debugPin2 5
#define debugPin3 6

///////////////////////////////////////////////////////////////////////////////
//  Definitions
//  
#define MSX_PORT PORTB
#define MSX_DDR  DDRB

                     // AVR pin
#define MSX_UP    0  // PB0    
#define MSX_DOWN  1  // PB1
#define MSX_LEFT  2  // PB2
#define MSX_RIGHT 3  // PB3
#define MSX_TRGA  4  // PB4
#define MSX_TRGB  5  // PB5 

#define MSX_PULSE 2  // PD2/INT0 used to trigger paddle service
#define N64_Pin   7  // PD7 N64 controller data in/out line

#define _10ms 2499     // OCR1 values for timeout
#define _100ms 24999   
#define _63ms 15750    // 62.5ms round up-> half period of autofire 


///////////////////////////////////////////////////////////////////////////////
//  Libraries
//
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include "Nintendo.h"      //   https://github.com/NicoHood/Nintendo    


///////////////////////////////////////////////////////////////////////////////
// Variables
//
CN64Controller N64Controller(N64_Pin);  // Define a N64 Controller

static volatile uint8_t  msx_paddle_1_2   = 0; // Pin 1 - UP      
static volatile uint8_t  msx_paddle_3_4   = 0; // Pin 1 - DOWN      
static volatile uint8_t  msx_paddle_5_6   = 0; // Pin 1 - LEFT    
static volatile uint8_t  msx_paddle_7_8   = 0; // Pin 1 - RIGHT    
static volatile uint8_t  msx_paddle_9_10  = 0; // Pin 1 - TRIGGER A   
static volatile uint8_t  msx_paddle_11_12 = 0; // Pin 1 - TRIGGER B  

static volatile uint32_t elapsedTime = 0;
static          uint8_t  autoFireMod = 0;


///////////////////////////////////////////////////////////////////
// 
//  ___      _             
// / __| ___| |_ _  _ _ __ 
// \__ \/ -_)  _| || | '_ \
// |___/\___|\__|\_,_| .__/
//                   |_|
//
void setup() {
  N64Controller.begin();   // initialize n64 controller
  delayMicroseconds(100);  // wait some time between initialize and the first read 

  pinMode(MSX_PULSE,INPUT_PULLUP);
  populate_MSX(0xff); 
  attachInterrupt(digitalPinToInterrupt(MSX_PULSE), cycle_MSX_paddles, RISING);

  #ifdef DEBUG
  pinMode(debugPin,OUTPUT);
  digitalWrite(debugPin,LOW);
  pinMode(debugPin2,OUTPUT);
  digitalWrite(debugPin2,LOW);  
  pinMode(debugPin3,OUTPUT);
  digitalWrite(debugPin3,LOW);  
  #endif
  
  // Setup Timer 1 used for wake up processor
  setTimer1(_10ms);  //  this function enable interrupts 
 // sei();           //  so no need to enable them again

}

///////////////////////////////////////////////////////////////////
//
//  _                  
// | |   ___  ___ _ __ 
// | |__/ _ \/ _ \ '_ \
// |____\___/\___/ .__/
//               |_|
//
void loop() {

  // Sample and Update Outputs
  do_N64(); 
  
  #ifdef DEBUG  // mark end of main loop execution
  digitalWrite(debugPin2,LOW); 
  #endif 
 
  // Put CPU to Sleep
  set_sleep_mode(SLEEP_MODE_IDLE);
  sleep_enable();         // Note: As Timer 0 is used by Arduino
  power_timer0_disable(); // housekeeping it should be turned off 
  sleep_mode();           // otherwise it will wake up CPU and hassle
  sleep_disable();        // the interrupt interleave mechanism used 
                          // by this code
                          
  #ifdef DEBUG  // mark begin of main execution (of next cycle)
  digitalWrite(debugPin2,HIGH);   
  #endif
}


///////////////////////////////////////////////////////////////////
//
//  ___             _   _             
// | __|  _ _ _  __| |_(_)___ _ _  ___
// | _| || | ' \/ _|  _| / _ \ ' \(_-<
// |_| \_,_|_||_\__|\__|_\___/_||_/__/
// 

///////////////////////////////////////////////////////////////////
// Timer 1 CTC Interrupt handler
//
ISR(TIMER1_COMPA_vect)
{
  setTimer1(_10ms); 
  elapsedTime += 2500;  // add 2500 counts for each 10ms interrupt
  #ifdef DEBUG
  digitalWrite(debugPin,HIGH);
  digitalWrite(debugPin,LOW);
  #endif
}


///////////////////////////////////////////////////////////////////
// Timer 1 timeout configuration
//
inline void setTimer1 (uint16_t overflowTicks) {
	cli();       // disable interrupts
	TCCR1A = 0; 
	TCCR1B = 0;
	TCNT1  = 0;  // initialize counter value to 0
	OCR1A = overflowTicks; // set compare register 

	TCCR1B |= (1 << WGM12);	                 // CTC mode
	TCCR1B |= (0<<CS12)|(1<<CS11)|(1<<CS10); // prescaler = 64
	TIMSK1 |= (1 << OCIE1A);                 // ISR Timer1/Compare
	
	sei(); // enable interrupts
	}


///////////////////////////////////////////////////////////////////
// External IRQ Interrupt handler
//
void cycle_MSX_paddles(void) {
  uint8_t i,j;
  uint8_t msx_buttons = ~MSX_DDR; // save button state
  
  #ifdef DEBUG   // mark begin of paddle service
  digitalWrite(debugPin3,HIGH); 
  #endif
  
  populate_MSX(0xff);   // start by rising all pins 
  for (i=0;i<255;i++){  // paddle range up to 255 counts of 12us
    j=0;                // all bits initially low
                        // keep bits set while paddle value is less
                        // than iteration variable
    if (i < msx_paddle_1_2  ) j |= (1<<MSX_UP);  
    if (i < msx_paddle_3_4  ) j |= (1<<MSX_DOWN); 
    if (i < msx_paddle_5_6  ) j |= (1<<MSX_LEFT);         
    if (i < msx_paddle_7_8  ) j |= (1<<MSX_RIGHT); 
    if (i < msx_paddle_9_10 ) j |= (1<<MSX_TRGA); 
    if (i < msx_paddle_11_12) j |= (1<<MSX_TRGB);
    
    populate_MSX(j);    // output all bits at the same time
    
    if (j==0) break;    // no need to iterate further after
                        // all bits went low 
     
    // hand tuned delay to make each iteration last 12us                    
    delayMicroseconds(10);
    asm volatile ( "nop\n"
                   "nop\n" 
                   "nop\n" );
    }

    delayMicroseconds(50);     // allow some time for Z80 to detect 
                               // the end of paddle timing cycle
                                
    populate_MSX(msx_buttons); // restore button state
 
    elapsedTime+=TCNT1;  // Autofire modulator, DDS style  
                         // Add elapsed time since last interrupt 
                         // it will compensate for different game 
                         // loop rates.
         
    setTimer1(_100ms);   // rise Timer1 timeout value, so the CTC
                         // interrupt starves while paddle reading
                         // pulses are being generated
    
    #ifdef DEBUG  // mark end of paddle service
    digitalWrite(debugPin3,LOW); 
    #endif      
  }


///////////////////////////////////////////////////////////////////
// Sample N64 controller and update outputs and Paddle variables
//
void do_N64() {
  uint8_t msx_buttons;

    //process autofire 
    cli ();
       if (elapsedTime > _63ms ) { // half of a 8Hz cycle (62.5ms)
           autoFireMod ^= 0xff;    // toggle autofire modulator state
           elapsedTime = elapsedTime % _63ms; // keep account of 
                                              // exceeding time 
        }
    sei ();
    
    msx_buttons = 0xff;  // start with none select

    if ( N64Controller.read()) {  // sample/check controller presence
      auto status = N64Controller.getStatus();
      auto report = N64Controller.getReport();
      if (status.device!=NINTENDO_DEVICE_N64_NONE) { 

        // Directional buttons (D-Pad)
        if (report.dup   ) msx_buttons &= ~(1<<MSX_UP);    // UP
        if (report.ddown ) msx_buttons &= ~(1<<MSX_DOWN);  // DOWN
        if (report.dleft ) msx_buttons &= ~(1<<MSX_LEFT);  // LEFT
        if (report.dright) msx_buttons &= ~(1<<MSX_RIGHT); // RIGHT

		
        // Digital Equivalent to directional buttons (Analog stick). 
		//  From center: up and right is positive, down and left is negative.

        // Before activate UP check that DOWN is not activated
		if ( (report.yAxis >  16 ) && (msx_buttons & (1<<MSX_DOWN ) )) msx_buttons &= ~(1<<MSX_UP    );
        // Before activate DOWN check that UP is not activated
		if ( (report.yAxis < -16 ) && (msx_buttons & (1<<MSX_UP   ) )) msx_buttons &= ~(1<<MSX_DOWN  );

        // Before activate LEFT check that RIGHT is not activated
		if ( (report.xAxis < -16 ) && (msx_buttons & (1<<MSX_RIGHT) )) msx_buttons &= ~(1<<MSX_LEFT    );
        // Before activate RIGHT check that LEFT is not activated
		if ( (report.xAxis >  16 ) && (msx_buttons & (1<<MSX_LEFT ) )) msx_buttons &= ~(1<<MSX_RIGHT  );
	
        // Trigger buttons
        if (report.a) msx_buttons &= ~(1<<MSX_TRGA);  // A
        if (report.b) msx_buttons &= ~(1<<MSX_TRGB);  // B
        if ((report.z) && (autoFireMod) ) msx_buttons &= ~(1<<MSX_TRGA);  // Z

        // Shoulder buttons are mapped as upper diagonals
        // Before activate L SHOULDER (UP+LEFT) check that RIGHT is not activated		
		if ( (report.l) && (msx_buttons & (1<<MSX_RIGHT)) )  msx_buttons &= ~((1<<MSX_UP) | (1<<MSX_LEFT) );		
        // Before activate R SHOULDER (UP+RIGHT) check that LEFT is not activated		
		if ( (report.r) && (msx_buttons & (1<<MSX_LEFT )) )  msx_buttons &= ~((1<<MSX_UP) | (1<<MSX_RIGHT) );

        // C buttons are mapped as Trigger A plus directionals
		// Before activate C-UP (TRG A + UP) check that DOWN is not activated	
		if ( (report.cup  ) &&  (msx_buttons & (1<<MSX_DOWN )) ) msx_buttons &= ~((1<<MSX_TRGA) | (1<<MSX_UP) );
		// Before activate C-DOWN (TRG A + DOWN) check that UP is not activated	
		if ( (report.cdown ) &&  (msx_buttons & (1<<MSX_UP   )) ) msx_buttons &= ~((1<<MSX_TRGA) | (1<<MSX_DOWN) );
		// Before activate C-LEFT (TRG A + LEFT) check that RIGHT is not activated	
		if ( (report.cleft ) &&  (msx_buttons & (1<<MSX_RIGHT)) ) msx_buttons &= ~((1<<MSX_TRGA) | (1<<MSX_LEFT) );		
		// Before activate C-RIGHT (TRG A + RIGHT) check that LEFT is not activated	
		if ( (report.cright) &&  (msx_buttons & (1<<MSX_LEFT )) ) msx_buttons &= ~((1<<MSX_TRGA) | (1<<MSX_RIGHT) );	

        // START button
        // Before activate START (LEFT + RIGHT) check neither LEFT nor RIGTH are activated
		if ( (report.start) && ( msx_buttons & ((1<<MSX_LEFT)|(1<<MSX_RIGHT)) ) )  msx_buttons &= ~((1<<MSX_LEFT) | (1<<MSX_RIGHT) );
		

        // fill Analog values (convert to unsigned) 
        msx_paddle_5_6 = (uint8_t) (128+report.xAxis);
        msx_paddle_1_2 = (uint8_t) (128+report.yAxis);   

      } // if device is known
    } // if controller.read()

    populate_MSX(msx_buttons);

}


///////////////////////////////////////////////////////////////////
// Define logic levels at MSX joystick port
//
inline void populate_MSX(uint8_t buttons) { 
  MSX_PORT = buttons;
  MSX_DDR  = ~buttons;
  }
