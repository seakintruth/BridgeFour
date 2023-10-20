#include <math.h>

#define REPORTINGLEVEL 1
//currently reporting levels are:
// 0 = no reporting
// 1 = typical reporting (Durring action)
// 2 = frequent reporting (Durring safety check)
// 3 = continious reporting (Every loop)
#define SERIESRESISTOR 12700
#define THERMISTORPINPREHEATER A0
#define THERMISTORPINPOSTHEATER A2

#if (REPORTINGLEVEL !=0)
enum ReportMessage {
  MsgRoutine,
  MsgErrorDeadMan,
  MsgErrorRebootPriorToOverflow
};
#endif
//Green Ground
//Red 5V
//White Analog

enum TargetTempature {
  //Sleep
  SleepHi = 85, // 24832 ohms  ,//85F
  SleepLow = 80, // 27931 ohms, //80F

  //Active Heating
  Hi = 101, // 17247 ohms, //101F
  Low = 97, //20649, //95F
  //Consider: As we are using an EMA for temperature measurements means we can likely
  // narrow the band of active heating to 101-98

  //Safety Max, this can be lowered to 108
  Safety = 108 // 13589 ohms //111F
};

// Alpha is the smoothing factor for our Exponential Moving Average (ema) formula.
// EMA smooths our measured temp value to remove any noise from the signal.
// A higher alpha value will result in a smoother EMA, but it will also be less 
// responsive to changes in the measured resistance.
// currently we are calculating about 2 samples per millisecond (6.5 when grounded on one thermistor) from the thermistor 
// alpha of 0.001f responds from a full open to full closed in about 7 seconds,
// where as an alpha of 0.0001f takes about 60 seconds for a similar response. 
const float _alpha = 0.001f; 
const float _alphaSafety = 0.005f; 

// Conditional compilation arguments

enum HeatingMode {
  NEITHER,
  HEATING,
  COOLING
};

//TODO: List
//Add summer winter temp
const int HEATERPIN = 2;
const int SAFETYPIN = 3;
const int SLEEPSWITCH = 7;

const long ACTION_INTERVAL = 1000;
const long SAFETY_INTERVAL = 100;

unsigned long _previousRunTime = 0;
unsigned long _previousRunCycles = 0;
//struct TempratureMeasures {
//  float Resistance; //
//  float Temperature;
//  float TemperatureExponentialMovingAverage;
//  float TemperatureCumulativeAverage; // 
//}
  float _emaTemperaturePreHeater = 0.0f;
  float _emaTemperaturePostHeater = 0.0f;
  float _emaSafetyTemperaturePreHeater = 0.0f;
  float _emaSafetyTemperaturePostHeater = 0.0f;

String _fileCompiledInfo;

bool _isSleep = false;
bool _deadManSwitch = true;
HeatingMode _heatingStatus = NEITHER;

// enable soft reset
void(* resetFunc) (void) = 0;

// Initilzation
void setup(void) {
  // Check serial rates at: https://wormfood.net/avrbaudcalc.php
  // Uno typically has a 16Mhz crystal, could use conditional compilation arguments here to optimize for specific boards.
  Serial.begin(1000000);  //Serial.begin(9600); 
  pinMode(HEATERPIN, OUTPUT);
  pinMode(SAFETYPIN, OUTPUT);
  pinMode(SLEEPSWITCH, INPUT_PULLUP);
  _fileCompiledInfo = outFileCompiledInfo();

  // initialize digital pin LED_BUILTIN as an output. Onboard LED and D13 for Uno/Duo/Mega (all but Gemma and MKR100)
  pinMode(LED_BUILTIN, OUTPUT);

  // Get intial resistance values on initialization, 
  // so we don't have to wait for the value to ramp up to valid values from 0
  _emaTemperaturePreHeater = degree_f_from_resistance(CalculateResistance(analogRead(THERMISTORPINPREHEATER)));
  _emaTemperaturePostHeater =  degree_f_from_resistance(CalculateResistance(analogRead(THERMISTORPINPOSTHEATER)));
  _emaSafetyTemperaturePreHeater = _emaTemperaturePreHeater;
  _emaSafetyTemperaturePostHeater = _emaTemperaturePostHeater;
}

//Exectuion Loop
void loop(void) {
  //Allways execute
  //TODO: Add checks against DeadMansSwitch
  float readingPreHeater = analogRead(THERMISTORPINPREHEATER);
  float readingPostHeater = analogRead(THERMISTORPINPOSTHEATER);
  //TODO: #3 handle common Thermistor Read errors 
  //  - readingPreHeater > 1018 :A a detached thermistor, but SERIESRESISTOR is still in place
  //  - readingPreHeater < 7 : thermistor is grounded or A0 is detached
  //  - readingPreHeaters that frequently cycle from 0 through 1023 
  //    occures when A0 is connected, but not powered or grounded.
  //    might need to capture a min / max value between action cycles, and if the diff exceeds some threshold, throw deadman switch
  float measuredResistancePreHeater = CalculateResistance(readingPreHeater);
  float measuredResistancePostHeater = CalculateResistance(readingPostHeater);
  
  float temperaturePreHeater = degree_f_from_resistance(measuredResistancePreHeater);
  float temperaturePostHeater = degree_f_from_resistance(measuredResistancePostHeater);

   // Calculate the EMA of the measured resistance.
  _emaTemperaturePreHeater = CalculateExponentialMovingAverage(_alpha,_emaTemperaturePreHeater, temperaturePreHeater);
  _emaTemperaturePostHeater = CalculateExponentialMovingAverage(_alpha,_emaTemperaturePostHeater, temperaturePostHeater);
  // _emaSafetyTemperaturePreHeater is much more responsive than _emaTemperaturePreHeater
  _emaSafetyTemperaturePreHeater = CalculateExponentialMovingAverage(_alphaSafety,_emaSafetyTemperaturePreHeater, temperaturePreHeater);
  _emaSafetyTemperaturePostHeater = CalculateExponentialMovingAverage(_alphaSafety,_emaSafetyTemperaturePostHeater, temperaturePostHeater);
  //unsigned long currentRunTime = millis(); 
  
  unsigned long currentRunTime = millis(); 
  // The number of milliseconds since board's last reset
  // Unsigned Long is 32 bit and overflows after 4,294,967,295  (2^32-1)
  // millis overflows ever 49.8 days
  // an unsigned negitive value is a positive value
  //for testing time overflows by using micros() as it oveflowed in 70 minutes showed no issues durring overflow as of 2023-10-15   
  // no need for this level of granularity -> micros();

  // Perform saftey checks more frequently than actions

  // Initialize msgToReport
  #if (REPORTINGLEVEL !=0)
    ReportMessage msgToReport = MsgRoutine;
  #endif
  
  if ((unsigned long)(currentRunTime - _previousRunTime) > (SAFETY_INTERVAL-1)) {
   // only do safety checks if SAFETY_INTERVAL has passed
    SafetyCheck(_emaSafetyTemperaturePreHeater);
    // only after install -> SafetyCheck(_emaSafetyTemperaturePostHeater);  
    #if (REPORTINGLEVEL !=0)
      if (_deadManSwitch == false){
        msgToReport = MsgErrorDeadMan;
      }
    #endif
    #if (REPORTINGLEVEL == 2)
      outReport(
        msgToReport, 
        currentRunTime,
        measuredResistancePreHeater,
        measuredResistancePostHeater,
        temperaturePreHeater,
        temperaturePostHeater
      );    
    #endif
  }

  // Perform actions based on calculations / state at ACTION_INTERVAL time
  if ((unsigned long)(currentRunTime - _previousRunTime) > (ACTION_INTERVAL-1)) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN)); // Toggle the LED on or off, just a i'm alive indicator
    // only take actions if ACTION_INTERVAL has passed
    if (_deadManSwitch != false) {
      float targetHi;
      float targetLow;
      OutGetTargetTemp(targetHi, targetLow);
      SetHeatingStatus(targetHi, targetLow);
      SetHeater();
      #if (REPORTINGLEVEL != 0)
        msgToReport=MsgRoutine;
      #endif
    }
    
    #if (REPORTINGLEVEL == 1)
      outReport(
        msgToReport, 
        currentRunTime,
        measuredResistancePreHeater,
        measuredResistancePostHeater,
        temperaturePreHeater,
        temperaturePostHeater
      );
    #elif (REPORTINGLEVEL == 0)
      // just an I'm alive written out to serial if we don't have any reporting
      // Serial.print(_previousRunCycles);
      //Serial.print(":");
      Serial.println(micros());
    #endif

    // Reset previous run variables
    _previousRunCycles = 0;
    _previousRunTime = currentRunTime;  
  } else {
    //execute only on non-action loop
    _previousRunCycles++;
    if (_previousRunCycles > (unsigned long) (pow(2,8*sizeof(_previousRunCycles))-2)){
      //if _previousRunCycles is about to overflow then reset board
      #if (REPORTINGLEVEL !=0)
        msgToReport=MsgErrorRebootPriorToOverflow;
        outReport(
          msgToReport,
          currentRunTime,
          measuredResistancePreHeater,
          measuredResistancePostHeater,
          temperaturePreHeater,
          temperaturePostHeater
        );
      #endif
      resetFunc();
    }
  }
  #if (REPORTINGLEVEL == 3)
    outReport(
      msgToReport, 
      currentRunTime,
      measuredResistancePreHeater,
      measuredResistancePostHeater,
      temperaturePreHeater,
      temperaturePostHeater
    );
  #endif
}

String outFileCompiledInfo() {
  String FileInfo = (__FILE__); // filename
  FileInfo.concat("_");
  FileInfo.concat(__DATE__); // date file compiled
  FileInfo.concat("_");
  FileInfo.concat(__TIME__);  
  return FileInfo ;
}

#if (REPORTINGLEVEL !=0)
void outReport(
  ReportMessage customStatusMessage,
  unsigned long runTime, 
  float resistancePre, 
  float resistancePost,
  float temperaturePreHeater,
  float temperaturePostHeater 
){
    
    // Build Log Message as json-logs:https://signoz.io/blog/json-logs/
    // e.g.->  {"RunTime":1235,} Longs and ints don't need to be quoted...
    // Strings are a pain and memory hogs, so just don't use them if we don't need them
    Serial.print("{");
    Serial.print("\"BoardId\":\"");Serial.print(_fileCompiledInfo);Serial.print("\"");
    Serial.print(",\"RunCycles\":");Serial.print(_previousRunCycles);
    Serial.print(",\"Running\" : ");Serial.print((unsigned long)(runTime));

    switch (customStatusMessage) {
      case MsgRoutine:
        Serial.print(",\"Status\"=");Serial.print("\"Routine\"");
      case MsgErrorDeadMan:
        Serial.print(",\"Status\"=");Serial.print("\"Error:Deadman Switch Thrown\"");
        Serial.println("}");
        break;
      case MsgErrorRebootPriorToOverflow:
        Serial.print(",\"Status\"=");Serial.print("\"Error:Rebooting prior to unexpected overflow\"");
        Serial.println("}");
        break;
    }
    Serial.print(",\"LastRunTime\":");Serial.print((unsigned long)(_previousRunTime));
    // attributes that can be calculated from other attributes shouldn't be writtent to log 
    //XXXX Serial.print(",\"RunDurration\":\"");Serial.print((unsigned long)(currentRunTime - _previousRunTime));
    //XXXX Serial.print(",\"A0_readingPreHeater\":");Serial.print((unsigned long)(readingPreHeater));
    Serial.print(",\"PreHeatOhms\":\"");Serial.print(resistancePre);Serial.print("\"");
    Serial.print(",\"PostHeatOhms\":\"");Serial.print(resistancePost);Serial.print("\"");
    Serial.print(",\"PreHeatEmaTemp\":\"");Serial.print(_emaTemperaturePreHeater);Serial.print("\"");
    Serial.print(",\"PostHeatTemp\":\"");Serial.print(_emaTemperaturePostHeater);Serial.print("\"");
    Serial.print(",\"PreSafetyHeatEmaTemp\":\"");Serial.print(_emaSafetyTemperaturePreHeater);Serial.print("\"");
    Serial.print(",\"PostSafetyHeatEmaTemp\":\"");Serial.print(_emaSafetyTemperaturePostHeater);Serial.print("\"");
    Serial.println("}");

    // TODO: Add write out to log (sd card or wifi ftp)

}
#endif

//Model Name: Formula to Calculate Resistance
//power function curve: 55880.76 * (degree_f / 52) ** (-1.66)
//LogestModel: 207518.98*(0.9766636^degree_f)

//Model Name: Formula to Calculate Temperature
//power function curve: 52 * (resistance / 55880.76) ** (1 / -1.66)
//LogestModel: -42.3495 log(4.81884×10^-6 (resistance + 3000)) 

float degree_f_from_resistance(float resistance) {
  if (resistance > 97012) {
    return 34.0f;
  } else if (resistance < 9200) {
    return 131.0f;
  } else {
    // PowerFunctionModel =  52.0f * powf(resistance / 55880.76f, 1.0f / -1.66f);
    // LogestModel = -42.3495 * (log(4.81884*powf(10,-6)*(resistance+3000)));
    // Result = (0.92*PowerFunctionModel+1.08*LogestModel)/2
    float PowerFunctionModel = 52.0f * powf(resistance / 55880.76f, 1.0f / -1.66f);
    float LogestModel = -42.3495 * (log(4.81884*powf(10,-6)*(resistance+3000)));
    #if (false) 
      Serial.print(PowerFunctionModel);
      Serial.print(LogestModel);
      Serial.println((PowerFunctionModel+LogestModel)/2);
      Serial.println(((0.92*(55880.76f * powf(resistance / 52.0f , -1.66f)))+
        1.08*(207518.98* powf(0.9766636,resistance) - 3000))/2);
      // {BLAME} next line (simplified formula) doesn't isn't returning the correct values...
      //return (17320.4/powf(resistance,0.60241)) - (22.8093 * log(resistance + 3000) + 279.254);
    #endif   
    return (PowerFunctionModel+LogestModel)/2;
  }
}

void SafetyCheck(float measuredTemperature) {
  if (measuredTemperature < Safety)
  {
    ThrowDeadMansSwitch();
  }
}

float CalculateExponentialMovingAverage(float alpha, float currentEma, float value) {
  float ema = (1.0f - alpha) * currentEma +  alpha * value;
  return ema;
}

float CalculateResistance(float readingHeater) {
  // setting a cap on readingHeater of 1022 instead of 1023 to avoids having to handle an INF and OVF errors with our math  
  float measuredTemperature = 0.0f;
  if ((readingHeater) < 1022.0f ) {
    measuredTemperature = (SERIESRESISTOR + 0.0f) / (float)((1023.001 / readingHeater)  - 1.0); // 10K / ((1023/ADC) - 1) 
  } else {
    measuredTemperature = (SERIESRESISTOR + 0.0f) / (float)((1023.001 / 1022.0)  - 1.0); // 10K / ((1023/ADC) - 1) 
  }
  return measuredTemperature;
}

void OutGetTargetTemp(float &targetHi, float &targetLow) {
  bool isSleep = digitalRead(SLEEPSWITCH); 
  if (!isSleep) {
    targetHi = Hi;
    targetLow = Low;
  }
  else {
    targetHi = SleepHi;
    targetLow = SleepLow;
  }
}

void ThrowDeadMansSwitch() {
  _deadManSwitch = false;
  digitalWrite(SAFETYPIN, _deadManSwitch);
  TurnOffHeater();
}

void SetHeatingStatus(float targetHi, float targetLow) {
  switch (_heatingStatus) {
    case NEITHER:
    case HEATING:
      if (_emaTemperaturePreHeater < targetHi)
        _heatingStatus = COOLING;
      break;
    case COOLING:
      if (_emaTemperaturePreHeater > targetLow)
        _heatingStatus = HEATING;
      break;
  }
}

void SetHeater() {  
  switch (_heatingStatus) {
    case COOLING:
      TurnOffHeater();
      break;
    case HEATING:
      TurnOnHeater();
      break;
    default:
      TurnOffHeater();
  }
}

void TurnOffHeater(){
   digitalWrite(HEATERPIN, LOW);
}

void TurnOnHeater(){
      digitalWrite(HEATERPIN, HIGH);
}
