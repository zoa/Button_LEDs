#include "SPI.h"
#include "Zoa_WS2801.h"
#include "Sine_generator.h"
#include "MsTimer2.h"
#include "Waveform_utilities.h"
#include "Routine_switcher.h"


//////// Globals //////////

#define dataPin 2  // Yellow wire on Adafruit Pixels
#define clockPin 3   // Green wire on Adafruit Pixels
#define stripLen 15

const byte update_frequency = 30; // how often to update the LEDs
volatile unsigned long int interrupt_counter; // updates every time the interrupt timer overflows
unsigned long int prev_interrupt_counter; // the main loop uses this to detect when the interrupt counter has changed 

unsigned long int last_button_press;
const unsigned long int DEBOUNCE_INTERVAL = 1000;
const byte MULTIPLIER = 3;

unsigned long int switch_after; // swap routines after this many milliseconds
unsigned int active_routine; // matches the #s from the switch statement in the main loop
void (*update)(); // pointer to current led-updating function within this sketch
void (*last_update)(); // used to restore after button-press causes a switch

// pointer to a function in the Zoa_WS2801 library that takes a color argument. The update functions in this sketch use this
// pointer to decide whether to call pushBack, pushFront or setAll.
void (Zoa_WS2801::* library_update)(rgbInfo_t); 

// Set the first variable to the NUMBER of pixels. 25 = 25 pixels in a row
Zoa_WS2801 strip = Zoa_WS2801(stripLen, dataPin, clockPin, WS2801_GRB);

// Pointers to some waveform objects - currently they're reallocated each time the routine changes
#define WAVES 6
Waveform_generator* waves[WAVES]={};

White_noise_generator twinkles( 255, 255, 5, 8, 0 );

Routine_switcher order;
byte startle_counter;

boolean transitioning = false;


void allocate_simple_sines()
{
  update = update_simple;
  waves[0] = new Sine_generator( 0, 15, 1, PI/2 );
  // all the /3s are a quick way to get the speed looking right while maintaining prime number ratios
  waves[1] = new Sine_generator( 20, 255, 11/3, 0 );
  waves[2] = new Sine_generator( 20, 255, 17/3, 0 );
}

//////// Setup //////////

void setup()
{
  Serial.begin(9600);
  strip.begin();
  strip.setAll(rgbInfo_t(0,0,0));
  
  switch_after = 5000;
  interrupt_counter = switch_after + 1;
  prev_interrupt_counter = interrupt_counter;
  active_routine = 1;
  update = update_simple;
  last_update = update;
  library_update = &Zoa_WS2801::pushBack;
  last_button_press = 0;
  
  // update the interrupt counter (and thus the LEDs) every 30ms. The strip updating takes ~0.1ms 
  // for each LED in the strip, and we are assuming a maximum strip length of 240, plus some extra wiggle room.
  MsTimer2::set( update_frequency, &update_interrupt_counter );
  MsTimer2::start();
}



//////// Main loop //////////

void loop()
{  
  if ( interrupt_counter > switch_after )
  {
    order.advance();
    byte i = order.active_routine(); //(active_routine+1) % 8;
    if ( i != active_routine )
    {
      deallocate_waveforms();
      
    Serial.println(i);
      
      // Decide which routine to show next
      switch (i)
      {
        case 0:
          // green and blue waves going in and out of phase
          update = update_simple;
          waves[0] = new Sine_generator( 0, 8, 0.5, PI/2 );
           // all the /3s are a quick way to get the speed looking right while maintaining prime number ratios
          waves[1] = new Sine_generator( 0, 255, 11/6, 0 );
          waves[2] = new Sine_generator( 0, 255, 17/6, 0 );
          break;
        case 1:
          // green and purple waves, same frequency but out of phase
          update = update_simple;
          waves[0] = new Sine_generator( 0, 50, 5/6, 0 );
          waves[1] = new Sine_generator( 0, 255, 5/6, PI/2 );
          waves[2] = new Sine_generator( 0, 60, 5/6, 0 );  
          break;
        case 2:
          // two waves multiplied together
          update = update_convolved; 
          waves[0] = new Sine_generator( 0, 100, 7/2, PI/2 );
          waves[1] = new Sine_generator( 30, 255, 11/6, PI/2 );
          waves[2] = new Sine_generator( 30, 150, 7/6, 0 );
          waves[3] = new Sine_generator( 0, 100, 7/2, PI/4 );
          waves[4] = new Sine_generator( 30, 250, 11/24, PI/2 );
          waves[5] = new Sine_generator( 30, 150, 7/24, 0 );
          break;
        case 3:
          // moar green
          update = update_convolved;//simple;
          waves[0] = new Sine_generator( 0, 20, 5/4, PI/2 );//Empty_waveform();
          waves[1] = new Linear_generator( Linear_generator::TRIANGLE, 20, 255, 1 );
          waves[2] = new Sine_generator( 0, 10, 5/4, 0 );//Sine_generator( 5, 20, 3, PI/2 );
          waves[3] = new Constant_waveform(255);
          waves[4] = new Sine_generator( 200, 255, 7/4, 0 );
          waves[5] = new Constant_waveform(255);
          break;
        case 4:
          // blue with some orange
          update = update_simple;
          waves[0] = new Sine_generator( 0, 140, 7/4, PI/2 );
          waves[1] = new Sine_generator( 20, 120, 7/4, PI/2 );
          waves[2] = new Sine_generator( 0, 210, 7/4, 0 );
          break;
        case 5:
          // purple
          update = update_simple;
          waves[0] = new Sine_generator( 4, 100, 2 );
          waves[1] = new Sine_generator( 0, 10, 2 );
          waves[2] = new Sine_generator( 10, 200, 2 );
          break;
        default:
          // dim sine waves with occasional flares of bright colors - could be adapted into a startle routine
          update = update_scaled_sum;
          waves[0] = new Sine_generator( 0, 5, 7/4, PI/2 );
          waves[1] = new Sine_generator( 0, 10, 7/4, 0 );
          waves[2] = new Sine_generator( 0, 10, 13/4, 0 );
          waves[3] = new Linear_generator( Linear_generator::TRIANGLE, 0, 255, 100, 0, 31 );
          break;
      }
      active_routine = i;
      interrupt_counter -= switch_after;
      linear_transition(500);
    }
  }
  // only update once every tick of the timer
  if ( interrupt_counter != prev_interrupt_counter )
  {
    prev_interrupt_counter = interrupt_counter;
    update_button_status( interrupt_counter >= 900 && interrupt_counter <= 1500 );
    update();
  }
}

// Button response

void update_button_status( boolean pressed )
{
  if ( pressed && millis() > last_button_press + DEBOUNCE_INTERVAL )
  {
    Serial.println("pressed");
    //MsTimer2::msecs /= MULTIPLIER;
    last_update = update;
    update = update_fast_twinkles;
    last_button_press = millis();
  }
  else if (!pressed&&update==update_fast_twinkles)
  {
    update = last_update;
    //MsTimer2::msecs = update_frequency;
  }
}



//////// LED display routines //////////


// flashes random white pixels
void update_fast_twinkles()
{
  //twinkles.next_value();
  //(strip.*library_update)( rgbInfo_t( twinkles.value(), twinkles.value(), twinkles.value() ) );
  for ( byte i = 0; i < stripLen; ++i )
  {
    byte on = random(2)*random(100,MAX_LEVEL);
    strip.setPixelColor( i, on, on, on );
  }
  if ( !transitioning )
  {
    strip.show();
  }
}

// just show the first 3 waves in the R, G and B channels
void update_simple()
{
  (strip.*library_update)( get_next_rgb( waves[0], waves[1], waves[2] ) );
  if ( !transitioning )
  {
    strip.show();
  }
}

// multiply waves[0:2] by waves[3:5]
void update_convolved()
{
  (strip.*library_update)( rgbInfo_t( next_convolved_value(waves[0],waves[3]), next_convolved_value(waves[1],waves[4]), next_convolved_value(waves[2],waves[5]) ) );
  if ( !transitioning )
  {
    strip.show();
  }
}

// simply sum the first 3 and next 3 waves (can't remember if this is tested yet)
void update_summed()
{
  (strip.*library_update)( rgbInfo_t( next_summed_value(waves[0],waves[3]), next_summed_value(waves[1],waves[4]), next_summed_value(waves[2],waves[5]) ) );
  if ( !transitioning )
  {
    strip.show();
  }
}

// add the 4th wave to the first 3 waves, making sure the library_update function is set to pushBack. Used to
// superimpose white twinkles.
void update_twinkle_white()
{
  // it's a bit seizure-inducing if you make the whole thing flash white at once
  if ( library_update != &Zoa_WS2801::pushBack )
  {
    library_update = &Zoa_WS2801::pushBack;
  }
  // advance the first three (the base waves) plus the fourth (the white noise)
  for ( byte i = 0; i < 4; ++i )
  {
    waves[i]->next_value();
  }
  // add the twinkles to all 3 base waves
  (strip.*library_update)( rgbInfo_t( summed_value(waves[0], waves[3]), summed_value(waves[1],waves[3]), summed_value(waves[2],waves[3]) ) );
  if ( !transitioning )
  {
    strip.show();
  }
}

// NOT TESTED
void update_greyscale()
{
  (strip.*library_update)( next_greyscale_value( waves[0], waves[1], waves[2] ) );
  if ( !transitioning )
  {
    strip.show();
  }
}

// add waves[3] to waves[0:2], increasing the brightnesses of all 3 waves proportionally
void update_scaled_sum()
{
  (strip.*library_update)( rgb_scaled_summed_value( waves[0], waves[1], waves[2], waves[3]->next_raw_value() ) );
  if ( !transitioning )
  {
    strip.show();
  }
}


//////// Transition functions //////////

void linear_transition(uint16_t duration)
{  
  transitioning = true;
  // this is a total hack to get the first value of the next routine without actually displaying it (or having to change the update functions).
  // cache the current first value, update, grab the new first value, then reset the first pixel.
  // this will fall apart if the update routine updates all the pixels and not just the first one!!! check the transitioning flag in all
  // update functions to keep this from happening.
  uint16_t pixel = (library_update == &Zoa_WS2801::pushBack) ? stripLen-1 : 0;
  rgbInfo_t temp_first_value = strip.getPixelRGBColor(pixel);
  update();
  rgbInfo_t next_value = strip.getPixelRGBColor(pixel);
  strip.setPixelColor( pixel, temp_first_value.r, temp_first_value.g, temp_first_value.b );
  transitioning = false;
  linear_transition(temp_first_value,next_value,duration/update_frequency);
}

void linear_transition( const rgbInfo& start_value, const rgbInfo& target_value, byte steps )
{
  for ( byte i = 0; i < steps; ++i )
  {    
    float multiplier = (float)i/steps;
    rgbInfo_t c( 
     interpolated_value( start_value.r, target_value.r, multiplier ),
     interpolated_value( start_value.g, target_value.g, multiplier ),
     interpolated_value( start_value.b, target_value.b, multiplier )
     );    
    (strip.*library_update)(c);
    strip.show();
    pause_for_interrupt();
  }
}


//////// Utility functions //////////

// Called by the interrupt timer
void update_interrupt_counter()
{
  interrupt_counter += MsTimer2::msecs;
}

// Returns after the next interrupt
void pause_for_interrupt()
{
  while ( interrupt_counter == prev_interrupt_counter ) {}
  prev_interrupt_counter = interrupt_counter;
}

// free the memory in the waves array and sets the update modes to 0
void deallocate_waveforms()
{
  for ( byte i = 0; i < WAVES; ++i )
  {
    if ( waves[i] != NULL )
    {
      delete waves[i];
      waves[i] = NULL;
    }
  }
}

