#ifndef Serial
  #include <Arduino.h>
#endif
#ifndef digitalReadFast
  #include <digitalWriteFast.h>
#endif
#ifndef SleepHi
  #include "../include/config.h"
#endif
#ifndef CalculateExponentialMovingAverage
  #include "../lib/HottubCalculations/src/calculations.cpp"
#endif
#ifndef ThrowDeadMansSwitch
  #include "../lib/HottubHeaterController/src/heaterControl.cpp"
#endif
#if REPORTINGFREQUENCY != 0
  #ifndef outBoard_Id 
    #include "../lib/NaiveLogger/src/naiveLogger.cpp"
  #endif
#endif
// For LCD Display use the softwareserial library to create a new "soft" serial port
// for the display. This prevents display corruption when uploading code.
#include <SoftwareSerial.h>

// Now using patformio instead of the arduino ide / arduino vscode extention
// Converting from .ino to a .cpp source file as C++ file...
// This means that we need to declare each custom function see:
// https://docs.platformio.org/en/latest/faq/ino-to-cpp.html


//----------------------------------------------------------------
// Global variables
//----------------------------------------------------------------
// by convention every variable begining with an underscore in this project is a global varaible.
// really need to refactor these from external global variables to some other method of 
// passing this information arround, could use pointers? 
// normally we just use unsigned int and long, the arduino uno is a 8-bit machine, 
// so using sized ints to save space. https://www.gnu.org/software/libc/manual/html_node/Integers.html
unsigned int _heatingStatusRequest ;
unsigned long _previousRunTime ;
unsigned long _previousRunCycles ;

bool _isSleep;
bool _deadManSwitchHoldConnected;


float _emaResistancePreHeater;
float _emaResistancePostHeater;
float _emaTemperaturePreHeater;
float _emaTemperaturePostHeater;
float _emaSafetyPreResistance;
float _emaSafetyPostResistance;
// consider using a struct for all the temperature measures related to a themistor...
//struct TempratureMeasures {
//  float Resistance;
//  float Temperature;
//  float TemperatureExponentialMovingAverage;
//  float TemperatureCumulativeAverage;
//  unsigned int ThermistorId; // what specific thermistor are we talking about?
//}
// 

// Setup LCD
SoftwareSerial LCD_Serial(Config::LCD_PIN_TX,Config::LCD_PIN_RX);
//create string arrays for the LCD
char _temp_string[10];
char _target_string[10];


// enable soft reset
void(* resetFunc) (void) = 0;

// Initilzation
void setup(void) {
  // Setup LCD Software Serial 
  LCD_Serial.begin(9600); // set up serial port for 9600 baud

  // Check serial rates at: https://wormfood.net/avrbaudcalc.php
  // Uno typically has a 16Mhz crystal, could use conditional compilation arguments here to optimize for specific boards.
  pinMode(Config::HEATERPIN, OUTPUT);
  pinMode(Config::SAFETYPIN, OUTPUT);
  pinMode(Config::RUNINDICATORPIN, OUTPUT);
  pinMode(Config::SLEEPSWITCH, INPUT_PULLUP);
  #if (REPORTINGFREQUENCY !=0)
    Serial.begin(Config::SEARIALBAUDRATE); 
    naiveLogger::outBoard_Id();
  #endif
  // initialize digital pin LED_BUILTIN as an output. Onboard LED and D13 for Uno/Duo/Mega (all but Gemma and MKR100)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWriteFast(Config::RUNINDICATORPIN, !digitalReadFast(Config::RUNINDICATORPIN)); 
  // initalize global variables
  _previousRunTime = 0;
  _previousRunCycles = 0;
  _isSleep = false;
  // deadman switch is a held open normally closed relay that enables the rest of the curcuit.
  _deadManSwitchHoldConnected = true;
  _heatingStatusRequest = heaterController::HeatingMode::NEITHER;
  // Get intial resistance and temperature values on initialization, analogRead
  _emaResistancePreHeater = CalculateResistance(analogRead(Config::THERMISTORPINPREHEATER),Config::SERIESRESISTOR);
  _emaResistancePostHeater = CalculateResistance(analogRead(Config::THERMISTORPINPOSTHEATER),Config::SERIESRESISTOR);
  _emaSafetyPreResistance = _emaResistancePreHeater;
  _emaSafetyPostResistance = _emaResistancePostHeater;
  _emaTemperaturePreHeater = degree_f_from_resistance(_emaResistancePreHeater);
  _emaTemperaturePostHeater =  degree_f_from_resistance(_emaResistancePostHeater);
  digitalWriteFast(Config::SAFETYPIN, true);
  heaterController::SafetyCheck(
    _emaSafetyPreResistance,
    _deadManSwitchHoldConnected
  );
   heaterController::SafetyCheck(
    _emaSafetyPostResistance,
    _deadManSwitchHoldConnected
  );
}

//Exectuion Loop
void loop(void) {
  //Allways execute
  //TODO: Add checks against DeadMansSwitch
  float readingPreHeater = analogRead(Config::THERMISTORPINPREHEATER);
  float readingPostHeater = analogRead(Config::THERMISTORPINPOSTHEATER);
  //TODO: #3 handle common Thermistor Read errors 
  //  - readingPreHeater > 1018 :A a detached thermistor, but SERIESRESISTOR is still in place
  //  - readingPreHeater < 7 : thermistor is grounded or A0 is detached
  //  - readingPreHeaters that frequently cycle from 0 through 1023 
  //    occures when A0 is connected, but not powered or grounded.
  //    might need to capture a min / max value between action cycles, and if the diff exceeds some threshold, throw deadman switch
  float measuredResistancePreHeater = CalculateResistance(readingPreHeater,Config::SERIESRESISTOR);
  float measuredResistancePostHeater = CalculateResistance(readingPostHeater,Config::SERIESRESISTOR);
  float temperaturePreHeater = degree_f_from_resistance(measuredResistancePreHeater);
  float temperaturePostHeater = degree_f_from_resistance(measuredResistancePostHeater);

   // Calculate the EMA of the measured resistance.
  CalculateExponentialMovingAverage(Config::alpha,_emaTemperaturePreHeater, temperaturePreHeater);
  CalculateExponentialMovingAverage(Config::alpha,_emaTemperaturePostHeater, temperaturePostHeater);
  // _emaSafetyPreResistance should be more responsive than _emaTemperaturePreHeater
  CalculateExponentialMovingAverage(Config::alphaSafety,_emaSafetyPreResistance, measuredResistancePreHeater);
  CalculateExponentialMovingAverage(Config::alphaSafety,_emaSafetyPostResistance, measuredResistancePostHeater);
  long currentRunTime = millis(); 
  // The number of milliseconds since board's last reset
  // Unsigned Long is 32 bit (long) and overflows after 4,294,967,295  (2^32-1)
  // millis overflows ever 49.8 days
  // an unsigned negitive value is a positive value
  // for testing time overflows by using micros() as it oveflowed in 70 minutes showed no issues durring overflow as of 2023-10-15   
  // no need for this level of granularity -> micros();
  // Perform saftey checks more frequently than actions
  // Initialize msgToReport as a variable of type enum ReportMessage
  #if (REPORTINGFREQUENCY !=0)
    naiveLogger::ReportMessage msgToReport;
    msgToReport = naiveLogger::ReportMessage::MsgRoutine;
  #endif
  if ((long)(currentRunTime - _previousRunTime) > (Config::SAFETY_INTERVAL-1)) {
    // only do safety checks if Config::SAFETY_INTERVAL has passed
    heaterController::SafetyCheck(
      _emaSafetyPreResistance,
      _deadManSwitchHoldConnected
    );
    heaterController::SafetyCheck(
      _emaSafetyPostResistance,
      _deadManSwitchHoldConnected
    );
    // only after install -> heaterController::SafetyCheck(_emaSafetyPostResistance);  
    #if (REPORTINGFREQUENCY !=0)
      if (_deadManSwitchHoldConnected == false){
        msgToReport = naiveLogger::ReportMessage::MsgErrorDeadMan;
      };
    #endif
    #if (REPORTINGFREQUENCY == 2)
      outReport(
        msgToReport, 
        measuredResistancePreHeater,
        measuredResistancePostHeater,
        temperaturePreHeater,
        temperaturePostHeater,
        _previousRunCycles,
        _previousRunTime,
        _emaTemperaturePreHeater,
        _emaTemperaturePostHeater,
        _emaSafetyPreResistance,
        _emaSafetyPostResistance,
        _heatingStatusRequest
      );
    #endif
  }

  // Perform actions based on calculations / state at ACTION_INTERVAL time
  if ((long)(currentRunTime - _previousRunTime) > (Config::ACTION_INTERVAL-1)) {
    // I'm alive  indicator...
    digitalWriteFast(LED_BUILTIN, !digitalReadFast(LED_BUILTIN)); 
    // Print out to the LCD    
    // if (millis() > 500 ){ // only print to display after it has booted up (500 milliseconds).
      LCD_Serial.write(254); // move cursor to beginning of first line
      LCD_Serial.write(128);
      LCD_Serial.write("                "); // clear display
      LCD_Serial.write("                "); // clear display
      LCD_Serial.write(254); // move cursor to beginning of first line
      LCD_Serial.write(128);
      LCD_Serial.write("TEMP:           "); // clear display
      LCD_Serial.write("TARGET TEMP:    "); // clear display second line
      LCD_Serial.write(254); // move cursor to beginning of first line
      LCD_Serial.write(128);
      sprintf(_temp_string,"%4d",_emaTemperaturePreHeater); // create strings from the numbers
      sprintf(_target_string,"%4d",Config::Hi); // right-justify to 4 spaces
      LCD_Serial.write("Hello, world!");
      LCD_Serial.write(_temp_string);
    // }
    // toggle run/saftey indicator if saftey pin has switched
    if (!digitalReadFast(Config::SAFETYPIN)){
      digitalWriteFast(Config::RUNINDICATORPIN, !digitalReadFast(Config::RUNINDICATORPIN)); 
    }
    // only take actions if ACTION_INTERVAL has passed
    float targetHi;
    float targetLow;
    // sets what our hi and low should be
    heaterController::OutGetTargetTemp(targetHi, targetLow);
    heaterController::SetHeatingStatus(targetHi, targetLow,_heatingStatusRequest,_emaTemperaturePreHeater);

    #if (REPORTINGFREQUENCY == 1)
      naiveLogger::outReport(
        msgToReport, 
        measuredResistancePreHeater,
        measuredResistancePostHeater,
        temperaturePreHeater,
        temperaturePostHeater,
        _previousRunCycles,
        _previousRunTime,
        _emaTemperaturePreHeater,
        _emaTemperaturePostHeater,
        _emaSafetyPreResistance,
        _emaSafetyPostResistance,
        _heatingStatusRequest
      );
    #endif
    if (_deadManSwitchHoldConnected) {
      heaterController::SetHeater(_heatingStatusRequest);
    }
    #if (REPORTINGFREQUENCY != 0)
      msgToReport=naiveLogger::ReportMessage::MsgRoutine;
    #endif
    #if (REPORTINGFREQUENCY == 1)
      naiveLogger::outReport(
        msgToReport, 
        measuredResistancePreHeater,
        measuredResistancePostHeater,
        temperaturePreHeater,
        temperaturePostHeater,
        _previousRunCycles,
        _previousRunTime,
        _emaTemperaturePreHeater,
        _emaTemperaturePostHeater,
        _emaSafetyPreResistance,
        _emaSafetyPostResistance,
        _heatingStatusRequest
      );
    #endif

    // Reset previous run variables
    _previousRunCycles = 0;
    _previousRunTime = currentRunTime;  
  } else {
    //execute only on non-action loop
    _previousRunCycles++;
    if (_previousRunCycles > (unsigned long) (pow(2,8*sizeof(_previousRunCycles))-2)){
      //if _previousRunCycles is about to overflow then reset board
      #if (REPORTINGFREQUENCY !=0)
        msgToReport=naiveLogger::ReportMessage::MsgErrorRebootPriorToOverflow;
        outReport(
          msgToReport,
          measuredResistancePreHeater,
          measuredResistancePostHeater,
          temperaturePreHeater,
          temperaturePostHeater,
          _previousRunCycles,
          _previousRunTime,
          _emaTemperaturePreHeater,
          _emaTemperaturePostHeater,
          _emaSafetyPreResistance,
          _emaSafetyPostResistance,
          _heatingStatusRequest
        );
      #endif
      resetFunc();
    }
  }
  #if (REPORTINGFREQUENCY == 3)
    outReport(
      msgToReport,
      measuredResistancePreHeater,
      measuredResistancePostHeater,
      temperaturePreHeater,
      temperature_heatingStatusRequestPostHeater,
      _previousRunCycles,
      _previousRunTime,
      _emaTemperaturePreHeater,
      _emaTemperaturePostHeater,
      _emaSafetyPreResistance,
      _emaSafetyPostResistance,
      _heatingStatusRequest
    );
  #endif
}