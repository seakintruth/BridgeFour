#pragma once
#include <Arduino.h>
// These are in the global namespace so that they can be used as conditional compliation arguments
#define DEBUG false // false
#define REPORTINGFREQUENCY 1 // 1
//currently reporting levels are:
// 0 = no reporting
// 1 = typical reporting (Durring action)
// 2 = frequent reporting (Durring safety check)
// 3 = continious reporting (Every loop) over serial, slows cycles per action from 616 hz to 60 hz.
// {todo} 4 = report every 10 seconds

namespace Config {
  // Our project configuration items are found here
  // Why are these assigned to macros instead of simply being constants?
  const float SERIESRESISTOR = 10000; // installed resistor in parallel with the temp thermistor
  const int THERMISTORPINPREHEATER = A0;
  const int THERMISTORPINPOSTHEATER = A2;
  const unsigned long SEARIALBAUDRATE = 1000000; 
  //long is an integer range from 0 to 4,294,967,295, but a unit16_t is only 0 to 65,535 

  const float alpha = 0.0005f; 
  const float alphaSafety = 0.005f; 
  // Alpha is the smoothing factor for our Exponential Moving Average (ema) formula.
  // EMA smooths our measured temp value to remove any noise from the signal.
  // A higher alpha value will result in a smoother EMA, but it will also be less 
  // responsive to changes in the measured resistance.
  // currently we are sampling at about 616 hz from the thermistor 
  // as we add complexity to the sketch we will be sampling at a slower rate, 
  // so it's important to continue to increase our alpha to maintain responsiveness, and test.

  // [TODO] it is possible to calculate the alpha so that it's 
  // responsive to N degree / N time period based on measured sample rate

  // These are just the defaults, eventually we may want to override these with a UI.
  // also as an enum we can't use decimal values, may need to use ohms, or switch to another type, like struct of floats
  enum TargetTempature  {
    //Sleep heating range
    SleepHi = 96,     // 19225 ohms            
    SleepLow = 98,    // 15450 ohms            
    //Active Heating range
    Hi = 103,         // 16850 ohms            
    Low = 101,        // 17350 ohms            
    //Safety Max
    SafetyMax = 14710 //110 F  //14710 ohms
  };
  const long ACTION_INTERVAL = 1000; // in milliseconds
  const long SAFETY_INTERVAL = 100;  // in milliseconds
  // Pin assignments
  const int LCD_PIN_TX = 2;
  const int LCD_PIN_RX = 3;
  const int RUNINDICATORPIN = 4;
  const int HEATERPIN = 5;
  const int SAFETYPIN = 6;
  const int SLEEPSWITCH = 7;  
 }