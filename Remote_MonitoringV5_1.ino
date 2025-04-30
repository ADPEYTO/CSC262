// Remote Level Monitor 
// Allen Peyton
// 4.25.2025
// Using Adafruit 3.5" TFT + Swan 3.0 MCU
//4.15 Start adding the code to report to the Blue Notecarrier.
//4.25 Touch screen working.  Sensor working and updating the main screen.  Ready for local use.  Need to verify the Blues wireless section.
//4.29 Having trouble with the Swan 3.0 talking to the Notecarrier working on that issue.

// Libraries
#include <Adafruit_GFX.h>
#include <Adafruit_HX8357.h>
#include <Adafruit_TSC2007.h>
#include <Wire.h>
#include <Notecard.h>
#include <NotecardPseudoSensor.h>
#include <math.h>


// TFT/Touch Pins  Setup to use either Swan 3.0 of Huzzah 32 MCU.  Use the feather wing config to easily interface with display.
#define TFT_RST -1
#ifdef ESP32
  #define STMPE_CS 32
  #define TFT_CS   15
  #define TFT_DC   33
  #define SD_CS    14
#else
  #define STMPE_CS 6
  #define TFT_CS   9
  #define TFT_DC   10
  #define SD_CS    5
#endif

#define SENSOR_PIN A0

#define usbSerial Serial

#define SEND_INTERVAL 3600000  // 1 hour in milliseconds (1000 ms * 60 s * 60 min)

// Calibration
#define TSC_TS_MINX 300
#define TSC_TS_MAXX 3800
#define TSC_TS_MINY 185
#define TSC_TS_MAXY 3700

// Display
#define SCREEN_WIDTH  480
#define SCREEN_HEIGHT 320

// Sleep  -  Removing Sleep function.  It is not really needed. Code is still live but the triggers have been commented out.
#define SLEEP_TIMEOUT 1 * 60 * 1000
#define BACKLIGHT_PIN 4
bool isSleeping = false;
unsigned long lastActivityTime = 0;

// Settings Variables
int sensorValue = 0;  // Sensor variable
int sensorReadings[100]; // Or however many readings you want to store
double voltage = 0.00; // Voltage variable.  I have changed to a mapping function to go directly to psi.
float specificGravity = 1.00;    // Default
int tankDiameter = 102;           // Inches
int numTanks = 1;                // 1-9
int tankCapacity = 5000;        // 100 - 10000
double lvlPercent = 0.0; // Calculated as percentage of lvlGallons divided by (Capacity x Number of Tanks)
double lvlGallons = 0.0; //
double swVersion = 5.1; //Software version for startup screen.
float psi = 0.0; // PSI calculated from sensor value
float gallons = 0.0; // Calculated from inchesOfMaterial and gallonsPerInch
float psiPerInch = 0.0; // Calculated from specific gravity
float inchesOfMaterial = 0.0; // Calculated from psiPerInch and PSI
float gallonsPerInch = 0.0; // Calculated from tank diameter
float temperature = 0.0; // Temperature variable from the Notecard
int totalCapacity = 0; //  Used to calculate the percentage for the main screen.

// Timer for sending data.
unsigned long lastSendTime = 0;

// Add with other sensor variables
//const int AVG_SAMPLES = 10;       // Number of samples for moving average
//float psiReadings[AVG_SAMPLES];    // Storage for raw PSI samples
//int psiIndex = 0;                  // Current position in buffer
//float psiTotal = 0.0;              // Running total
//float sensorTotal = 0.0;


// Screen States
enum Screen { MAIN_SCREEN, SETTINGS_SCREEN, SG_SCREEN, DIAMETER_SCREEN, TANKS_SCREEN, CAPACITY_SCREEN };
Screen currentScreen = MAIN_SCREEN;


// Get the sensor value and average them.  Set to take 100 samples and average them.
int readAveragedAnalog(int pin, int samples = 100) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(200);  // fast averaging without slowing down UI
  }
  return sum / samples;
}

// Values for the data section of the adjustment screen.  This controlls the size of the rewrite box to erase the old data.
#define VALUE_AREA_X  11
#define VALUE_AREA_Y  101
#define VALUE_AREA_W  248
#define VALUE_AREA_H  148

// Hardware Objects
Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);
Adafruit_TSC2007 ts = Adafruit_TSC2007();

// Colors -  This is the color for all Midwest Industrial items.  Looks dull on screen but I cant change it.
uint16_t myColor = tft.color565(35, 98, 50); // Midwest green

//*************************** Communication Area ************************
//Build Objects
// Define where to find libraries below.
//**********  CRITICAL - Notecard will not function without this ****************
using namespace blues;

#define usbSerial Serial

// Define Product UID for Notecard - this tell Blues where to send the data in the Notehub environment.
#ifndef PRODUCT_UID
#define PRODUCT_UID "com.midwestind.allen.peyton:tank_level_monitor"
#pragma message "Please Define Project."
#endif

#define myProductID PRODUCT_UID

// Create Notecard objects
Notecard notecard;
NotecardPseudoSensor sensor(notecard);


// ====================== SCREEN DRAWING FUNCTIONS ======================
//  Load screen to identify the software version.  WOuld like to add the midwest logo to the load screen at some point.
void drawLoadScreen() {
  tft.fillScreen(HX8357_BLACK);
  tft.setCursor(10, 10);
  tft.setTextSize(2);
  tft.setTextColor(HX8357_WHITE);
  tft.print("Loading Midwest Remote Level Monitor...");
  tft.setCursor(10, 40);
  tft.print("Software Version: ");
  tft.print(swVersion);
  delay(1000);
}

// Main Screen.  Shows the critical data and allows access to settings and data sending.
void drawMainScreen() {
  tft.fillScreen(HX8357_BLACK);
  
  // Title Bar
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, myColor);
  tft.setTextSize(2);
  tft.setTextColor(HX8357_WHITE);
  tft.setCursor(SCREEN_WIDTH / 2 - 120, 10);
  tft.print("Midwest Level Monitor");

  // Settings Button
  tft.fillRect(10, 60, 220, 80, HX8357_WHITE);
  tft.setTextColor(HX8357_BLACK);
  tft.setCursor(60, 90);
  tft.print("Settings");

  // Send Data Button
  tft.fillRect(250, 60, 220, 80, HX8357_WHITE);
  tft.setCursor(300, 90);
  tft.print("Send Data");

  // Data Displays
  // Tank Percentage placeholder
  tft.drawRect(10, 160, 220, 140, HX8357_WHITE);
  tft.setCursor(60, 180);
  tft.setTextColor(HX8357_WHITE);
  tft.print("Percent");
  tft.setCursor(60, 220);
  tft.setTextSize(4);
  tft.fillRect(60, 220, 150, 40, HX8357_BLACK);
  tft.print(lvlPercent);

  // Gallons Percentage placeholder
  tft.drawRect(250, 160, 220, 140, HX8357_WHITE);
  tft.setCursor(310, 180);
  tft.setTextSize(2);
  tft.print("Gallons");
  tft.setCursor(300, 220);
  tft.setTextSize(4);
  tft.fillRect(300, 220, 150, 40, HX8357_BLACK);
  tft.print(gallons);
}


// Setting Screen section
void drawSettingScreen() {
  tft.fillScreen(HX8357_BLACK);
  
  // Title Bar
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, myColor);
  tft.setTextSize(2);
  tft.setTextColor(HX8357_WHITE);
  tft.setCursor(SCREEN_WIDTH / 2 - 40, 10);
  tft.print("Settings");

  // Back Button
  tft.fillRect(10, SCREEN_HEIGHT - 60, 100, 40, myColor);
  tft.setCursor(35, SCREEN_HEIGHT - 50);
  tft.print("Back");

  // Settings Buttons  ***********************************************************
  // Specific Gravity Section...
  tft.fillRect(10, 60, 220, 60, HX8357_WHITE);
  tft.setTextColor(HX8357_BLACK);
  tft.setCursor(25, 80);
  tft.print("Specific Gravity");
  
  // Tank Diameter setting...
  tft.fillRect(250, 60, 220, 60, HX8357_WHITE);
  tft.setCursor(260, 80);
  tft.print("Tank Dia (in)");
  
  // Number of Tanks
  tft.fillRect(10, 140, 220, 60, HX8357_WHITE);
  tft.setCursor(25, 160);
  tft.print("Number of Tanks");
  // Total Tank Capacity (Single Tank)
  tft.fillRect(250, 140, 220, 60, HX8357_WHITE);
  tft.setCursor(260, 160);
  tft.print("Capacity");
  
}

//  Common class for the adjustment screen.  One constructor to manage all of the settings.  Add any new setting to constructor after creating the button in drawSettingScreen().
void drawAdjustmentScreen(const String& title, float value, float minVal, float maxVal, bool isInt = false) {
  tft.fillScreen(HX8357_BLACK);
  
  // Title
  tft.fillRect(0, 0, SCREEN_WIDTH, 40, myColor);
  tft.setTextColor(HX8357_WHITE);
  tft.setCursor(20, 10);
  tft.print(title);

  // Value Display
  tft.drawRect(10,100,250,150, HX8357_WHITE);
  tft.setTextSize(3);
  tft.setCursor(SCREEN_WIDTH/4 , SCREEN_HEIGHT/2);
  if(isInt) {
    tft.print((int)value);
  } else {
    tft.print(value, 1);
  }

  //  Plus and minus buttons ******************************************************
  //Up button
  tft.fillRect(280, 100, 60, 60, HX8357_GREEN); // Y=120
  tft.setTextColor(HX8357_BLACK);
  tft.setCursor(300, 120);
  tft.print("+");

  // Down Button (moved higher)
  tft.fillRect(280, 190, 60, 60, HX8357_RED); // Y=220
  tft.setTextColor(HX8357_WHITE);
  tft.setCursor(300, 210);
  tft.print("-");

  // Back to Settings
  tft.fillRect(SCREEN_WIDTH - 110, SCREEN_HEIGHT - 60, 100, 40, myColor);
  tft.setCursor(SCREEN_WIDTH - 90, SCREEN_HEIGHT - 50);
  tft.print("Back");
}

void updateAdjustmentValue(bool isInt) {
  tft.fillRect(VALUE_AREA_X, VALUE_AREA_Y, VALUE_AREA_W, VALUE_AREA_H, HX8357_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(HX8357_WHITE);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);

  // Screen Mode to set the various setting screen names.  Sets the title of the page as the setting name.
  switch(currentScreen) {
    case SG_SCREEN:
      tft.print(specificGravity, 2); // 2 decimal place
      break;
    case DIAMETER_SCREEN:
      tft.print(tankDiameter);
      break;
    case TANKS_SCREEN:
      tft.print(numTanks);
      break;
    case CAPACITY_SCREEN:
      tft.print(tankCapacity);
      break;
  }
}

// Silly little section to indicate that the data is being sent.  Like the loading icon on a webpage just a visual indicator that something is happening.
void drawSendingDataScreen(){
  tft.fillScreen(HX8357_BLACK);
  tft.setTextColor(HX8357_WHITE);
  tft.setTextSize(2);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data.");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data..");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data...");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data....");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data.....");
  delay(250);
  tft.fillScreen(HX8357_BLACK);
  tft.setTextColor(HX8357_WHITE);
  tft.setTextSize(2);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data.");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data..");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data...");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data....");
  delay(250);
  tft.setCursor(SCREEN_WIDTH/4, SCREEN_HEIGHT/2);
  tft.print("Sending Data.....");
  delay(250);
  // ==================================   Insert call to send data here ======================================== <<<<<<<<<----------------------------
  sendData();
  drawMainScreen();
  
}

// *********************** TOUCH & LOGIC HANDLING ******************************************
void adjustValue(bool increase) {
  switch(currentScreen) {

    // Area to set the parameters of the settings based on which setting screen you are on.  
    //If increase is true than add the first value else add the second value.  
    //Also setting the upper and lower limits.

    case SG_SCREEN:
      specificGravity = constrain(specificGravity + (increase ? 0.01 : -0.01), 0.80, 1.50);
      break;
    case DIAMETER_SCREEN:
      tankDiameter = constrain(tankDiameter + (increase ? 1 : -1), 48, 144);
      break;
    case TANKS_SCREEN:
      numTanks = constrain(numTanks + (increase ? 1 : -1), 1, 9);
      break;
    case CAPACITY_SCREEN:
      tankCapacity = constrain(tankCapacity + (increase ? 100 : -100), 100, 10000);
      break;
  }
  updateAdjustmentValue(currentScreen == SG_SCREEN ? false : true);
}


// Handle the touch screen function.  A little slow but it functions.
void handleTouch(int16_t touchX, int16_t touchY) {
  if (currentScreen == MAIN_SCREEN) {
    // Main screen buttons sets the area for touch to activate.
    if (touchX >= 10 && touchX <= 230 && touchY >= 60 && touchY <= 140) {
      currentScreen = SETTINGS_SCREEN;
      drawSettingScreen();
    }
    else if (touchX >= 80 && touchX<= 180 && touchY >= 175 && touchY <= 305){
      drawSendingDataScreen();
    }
  } 
  else if (currentScreen == SETTINGS_SCREEN) {
    // Back to Main
    if (touchX >= 400 && touchX <= 440 && touchY >= 10 && touchY <= 30) {
      currentScreen = MAIN_SCREEN;
      drawMainScreen();
    }
    // SG Adjustment
    else if (touchX >=70 && touchX <=160 && touchY >=0 && touchY <=150) {
      currentScreen = SG_SCREEN;
      drawAdjustmentScreen("Specific Gravity", specificGravity, 0.85, 1.5, false);
    }
    // Diameter Adjustment
    else if (touchX >=100 && touchX <=180 && touchY >=170 && touchY <=305) {
      currentScreen = DIAMETER_SCREEN;
      drawAdjustmentScreen("Tank Diameter", tankDiameter, 48, 120, true);
    }
    // Tanks Adjustment
    else if (touchX >=220 && touchX <=290 && touchY >= 0&& touchY <=150) {
      currentScreen = TANKS_SCREEN;
      drawAdjustmentScreen("Number of Tanks", numTanks, 1, 9, true);
    }
    // Tanks Capacity
    else if (touchX >= 220 && touchX <= 290 && touchY >= 170 && touchY <= 305) {
      currentScreen = CAPACITY_SCREEN;
      drawAdjustmentScreen("Tank Capacity", tankCapacity, 100, 10000, true);
    }
  }
  else if (currentScreen == SG_SCREEN || currentScreen == DIAMETER_SCREEN || currentScreen == TANKS_SCREEN || currentScreen == CAPACITY_SCREEN) {
    // Up Button: Y=120 to 200 (120+80)
    if (touchX >=150 && touchX <=200 && touchY >=200 && touchY <=230) { 
      adjustValue(true);
    }
    // Down Button: Y=220 to 300 (220+80)
    else if (touchX >=300 && touchX <=340 && touchY >=190 && touchY <=220) {
      adjustValue(false);
    }
    // Back to Settings
    else if (touchX >= SCREEN_WIDTH-110 && touchY >= SCREEN_HEIGHT-60) {
      currentScreen = SETTINGS_SCREEN;
      drawSettingScreen();
    }
  }
  Serial.print("X: "); Serial.print(touchX);
  Serial.print(" Y: "); Serial.println(touchY);
}


//  Where the rubber hits the road.  Gets the data from the sensor and does all the calculations.
void checkSensor() {
  // Take new reading
  sensorValue = readAveragedAnalog(SENSOR_PIN, 100);
  

  // Calibration values (adjust based on your sensor)
  const int sensorMin = 155;   // ADC value at 0 psi
  const int sensorMax = 3686;  // ADC value at 10 psi

  // Custom floating-point map function
  psi = mapFloat(sensorValue, sensorMin, sensorMax, 0.0, 10.0);
  
  // Clamp to valid range (0-10 psi)
  psi = constrain(psi, 0.0, 10.0);

  

  // Caluculate the PSI reading per inch of material above the sensor.  Only reads material ABOVE the sensor.
  psiPerInch = specificGravity * .03608;
  

  //  Calculates the level of material based on PSI per inch.
  inchesOfMaterial = psi / psiPerInch;
 

  //  Calculates the gallon per inch based on the diameter of the tank.  Basically area of the circle of the tank divided by the cudic inches in a gallon.
  float radius = tankDiameter / 2.0;
  gallonsPerInch = (PI * (radius * radius)) / 231.0;
  gallons = inchesOfMaterial * gallonsPerInch;

  lvlGallons = gallons;

  //  Fill Percentage of the tank 
  totalCapacity = tankCapacity * numTanks;
  lvlPercent = gallons / totalCapacity * 100;

}

// Floating-point version of Arduino's map()
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


// Function to update the Main Screen Values.  Blah Blah Blah.  Only need to update the percentage and the gallons.
void updateMainScreenValues() {
  // Update Percent
  tft.fillRect(60, 220, 150, 40, HX8357_BLACK);
  tft.setCursor(60, 220);
  tft.setTextSize(4);
  tft.setTextColor(HX8357_WHITE);
  tft.print(lvlPercent, 1); // Show 1 decimal place

  // Update Gallons
  tft.fillRect(300, 220, 150, 40, HX8357_BLACK);
  tft.setCursor(300, 220);
  tft.setTextSize(4);
  tft.setTextColor(HX8357_WHITE);
  tft.print(gallons, 0); // Show 1 decimal place
}

// ====================== ARDUINO CORE FUNCTIONS ======================
// Begining all the parts.
void setup() {
  Serial.begin(115200);
  SPI.begin();
  delay(100);
  Wire.begin(PB7, PB6);
  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);
  
  tft.begin();
  tft.setRotation(3);
  delay(100);
  ts.begin(0x48, &Wire);
  delay(100);

usbSerial.begin(115200);
    const size_t usb_timeout_ms = 3000;
    for (const size_t start_ms = millis(); !usbSerial && (millis() - start_ms) < usb_timeout_ms;)
        ;

  // Initialize Notecard
      notecard.begin();
      
       J *req = notecard.newRequest("hub.set");

        if (myProductID[0])
        {
          JAddStringToObject(req, "product", myProductID);
        }

      JAddStringToObject(req, "mode", "continuous");

      notecard.sendRequestWithRetry(req, 5); // 5 seconds

  drawLoadScreen();
  drawMainScreen();
  lastActivityTime = millis();
  lastSendTime = millis();

//for(int i=0; i<AVG_SAMPLES; i++) {
//    sensorReadings[i] = analogRead(A0); // Initial real reading
 //   sensorTotal += sensorReadings[i];
 // }
  
}
//************************************   Need to work on this.  *****************************************
void sendData(){

 J *req = notecard.newRequest("note.add");
    if (req != NULL)
    {
        JAddBoolToObject(req, "sync", true);
        J *body = JAddObjectToObject(req, "body");
        if (body != NULL)
        {
            JAddNumberToObject(body, "psi", psi);
            JAddNumberToObject(body, "percent", lvlPercent);
            JAddNumberToObject(body, "gallons", lvlGallons);
        }
        notecard.sendRequest(req);
    }
    Serial.println("output");
} 
void loop() {
  TS_Point p = ts.getPoint();
  if (p.z > 10) {
    int16_t touchX = map(p.x, TSC_TS_MINX, TSC_TS_MAXX, 0, SCREEN_WIDTH);
    int16_t touchY = map(p.y, TSC_TS_MINY, TSC_TS_MAXY, SCREEN_HEIGHT, 0);
    handleTouch(touchX, touchY);
    lastActivityTime = millis();
  }

 
  delay(10);
  checkSensor();
    if (currentScreen == MAIN_SCREEN) {
    updateMainScreenValues();
  }
  delay(10);

   unsigned long currentTime = millis();
  
  if (currentTime - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = currentTime;

    sendData();

    //int raw = analogRead(A0);
    //Serial.println(raw);
    //delay(500);
    //Serial.print("PSI: ");
    //Serial.print(psi);
    delay(10);
  }
}

// ====================== SLEEP FUNCTIONS ======================
//void enterSleepMode() {
//  tft.fillScreen(HX8357_BLACK);
//  digitalWrite(BACKLIGHT_PIN, LOW);
//  isSleeping = true;
//}

void wakeUp() {
  digitalWrite(BACKLIGHT_PIN, HIGH);
  drawMainScreen();
  isSleeping = false;
}