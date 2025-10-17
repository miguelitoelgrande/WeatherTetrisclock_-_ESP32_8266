//#define MMdebug   // uncomment in debug situations (Netzwerk...)
// Variante ohne Ticker

//#MMpragma GCC optimize ("-Werror=return-type")
//-Werror=return-type
// "Generic ESP8266 Module" gives Warnings/Errors...
// removed in:  C:\Users\<user>\AppData\Local\Arduino15\packages\esp8266\hardware\esp8266\3.1.2\platform.txt
/**
   ESP8266 ESP-01 Clock and Weather Firmware
   added TetrisAnimationLibrary..

   This firmware uses an SSD1306 128x64 OLED display to show alternating screens:
   - Clock screen: shows day of week, current time (HH:MM), temperature & humidity, and date.
   - Weather screen: shows city name, current temperature, weather condition, humidity, wind speed, and pressure.

   Wi-Fi Configuration:
   If no configuration is found or if a button on GPIO3 is held at boot, the device
   starts as an access point (SSID: ESP01-Setup) and serves a configuration page.
   The page allows setting Wi-Fi SSID, password, city for weather, and timezone.
   Settings are saved in EEPROM and the device reboots to normal mode.

   Normal Operation:
   Connects to configured Wi-Fi and retrieves time via NTP (synchronized every 60s).
   Retrieves weather from wttr.in for the specified city (HTTPS GET request).
   Displays time and weather data on the OLED, switching screens every 15 seconds.

   Hardware:
   - ESP8266 ESP-01 (GPIO0=SDA, GPIO2=SCL for I2C OLED; GPIO3 as input for config button).
   - OLED display SSD1306 128x64 via I2C (address 0x3C).

   Note: Uses custom fonts (FreeMonoBold18pt7b for time, FreeMonoBold12pt7b for temperature).
   All other text uses default font. Display is rotated 180 degrees (setRotation(2)).
*/

/* Source: https://github.com/witnessmenow/WiFi-Tetris-Clock/blob/master/ESP32%20or%20TinyPICO/EzTimeTetrisClockESP32/EzTimeTetrisClockESP32.ino */
/*******************************************************************
    Tetris clock that fetches its time Using the EzTimeLibrary

    For use with the ESP32 or TinyPICO
    MM: Display: 128*64   SSD_1306
 *                                                                 *
    Written by Brian Lough
    YouTube: https://www.youtube.com/brianlough
    Tindie: https://www.tindie.com/stores/brianlough/
    Twitter: https://twitter.com/witnessmenow
 *******************************************************************/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #include <ESP8266WebServer.h>
  // Pin definitions (ESP-01 / ESP8266):
  const uint8_t SDA_PIN = 0;           // I2C SDA connected to GPIO0
  const uint8_t SCL_PIN = 2;           // I2C SCL connected to GPIO2
  const uint8_t BUTTON_PIN = 3;        // Button on GPIO3 (RX pin)
#elif defined(ESP32)
  #include <WiFi.h>
  #include <WebServer.h>
    // MM:
    // Pin definitions (ESP32):
    const uint8_t SDA_PIN = 21;           // I2C SDA connected to GPIO0
    const uint8_t SCL_PIN = 22;           // I2C SCL connected to GPIO2
    const uint8_t BUTTON_PIN = 16;        // Button on GPIO3 (RX pin)
#endif

#include <EEPROM.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeMonoBold18pt7b.h>  // MM: FreeMonoBold24pt7b
#include <Fonts/FreeMonoBold12pt7b.h>
#include <time.h>
///
//#include <Ticker.h>  // MM: last minute change: NO Ticker to run on ESP8266
#include <TetrisMatrixDraw.h>
// This library draws out characters using a tetris block amimation
// Can be installed from the library manager
// https://github.com/toblum/TetrisAnimation

// EEPROM addresses for configuration data:
const int EEPROM_SIZE = 512;
const int ADDR_SSID = 0;
const int ADDR_PASS = 70;
const int ADDR_CITY = 140;
const int ADDR_TZ   = 210;  // 4 bytes (int32) for timezone offset in seconds
const int ADDR_SIGNATURE = 500;  // 4-byte signature "CFG1" to indicate valid config

// Wi-Fi and server:
#if defined(ESP8266)
  ESP8266WebServer server(80);
  const char *AP_SSID = "MM-ESP01-Setup";  // Access Point SSID for config mode
#elif defined(ESP32)
// MM:
  WebServer server(80);
  const char *AP_SSID = "MM-ESP32-Setup";  // Access Point SSID for config mode
#endif

// Display SSD1306:
Adafruit_SSD1306 display(128, 64, &Wire, -1);
bool displayInitialized = false;

// Time and weather:
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
unsigned long lastScreenSwitch = 0;
bool showWeatherScreen = false;
bool redrawScreen = true;
bool displayReady = false;
unsigned long lastWeatherFetch = 0;

// Configuration variables:
#if defined(MMdebug)
// MM: for convenience: initial config without eprom for Simulation in WOKWi
String wifiSSID = "Wokwi-GUEST";
String wifiPass = "";
String city = "Oerlikon";
int timezoneOffset = 2 * 60 * 60; // in seconds
#else
String wifiSSID = "";
String wifiPass = "";
String city = "";
int timezoneOffset = 0;  // in seconds
#endif


// Weather data variables:
String weatherTemp = "N/A";
String weatherCond = "";
String weatherHum = "";
String weatherWind = "";
String weatherPress = "";
bool weatherValid = false;

// Function prototypes:
void loadSettings();
void saveSettings();
void startConfigPortal();
void handleConfigForm();
void drawTimeScreen();
void drawWeatherScreen();
bool getWeather();

///////////
TetrisMatrixDraw tetris(display); // Main clock
TetrisMatrixDraw tetris2(display); // The "M" of AM/PM
TetrisMatrixDraw tetris3(display); // The "P" or "A" of AM/PM

//Timezone myTZ;
unsigned long oneSecondLoopDue = 0;

bool showColon = true;
volatile bool finishedAnimating = false;
bool displayIntro = true;

String lastDisplayedTime = "";
String lastDisplayedAmPm = "";


// Sets whether the clock should be 12 hour format or not.
bool twelveHourFormat = false;
//MM:  bool twelveHourFormat = true;

// If this is set to false, the number will only change if the value behind it changes
// e.g. the digit representing the least significant minute will be replaced every minute,
// but the most significant number will only be replaced every 10 minutes.
// When true, all digits will be replaced every minute.
//MM: bool forceRefresh = false;
bool forceRefresh = true;
// -----------------------------

// Ticker animationTicker;
// animationTicker.attach(0.05, animationHandler);

///////////
void printBottomDateLine()
{
   // Get current epoch time and derive day of week
  time_t epoch = timeClient.getEpochTime();
  // Calculate day of week (0=Sunday .. 6=Saturday)
  struct tm *tm = gmtime(&epoch);
  int wday = tm->tm_wday;  // tm_wday: days since Sunday (0-6)
  String dayName = "";
  switch (wday) {
    case 0: dayName = "Sonntag"; break;
    case 1: dayName = "Montag"; break;
    case 2: dayName = "Dienstag"; break;
    case 3: dayName = "Mittwoch"; break;
    case 4: dayName = "Donnerstag"; break;
    case 5: dayName = "Freitag"; break;
    case 6: dayName = "Samstag"; break;
  }

  int16_t x1, y1;
  uint16_t w, h;

  // Bottom left: day of week (in German)
  display.setFont(NULL); // default font // display.setFont(&FreeMonoBold12pt7b);
  display.setCursor(0, 56);
  display.print(dayName);
  
  // Bottom right: date dd.mm.yyyy
  tm = gmtime(&epoch);
  int day = tm->tm_mday;
  int month = tm->tm_mon + 1;
  int year = tm->tm_year + 1900;
  char dateBuf[12];
  snprintf(dateBuf, sizeof(dateBuf), "%02d.%02d.%04d", day, month, year);
  String dateStr = String(dateBuf);
  display.getTextBounds(dateStr, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(128 - w, 56);
  display.print(dateStr);
  display.display();
}


// This method is for controlling the tetris library draw calls
//void ICACHE_RAM_ATTR animationHandler()
void animationHandler()
{
  if (showWeatherScreen) return;   // dirty hack... just go back for original weathe screen...
  //  Serial.println("enter animationHandler()");
  // Not clearing the display and redrawing it when you
  // dont need to improves how the refresh rate appears
  if (!finishedAnimating) {

    display.clearDisplay();

    if (displayIntro) {
      //    Serial.println("drawText() call");
      // MM: showed that lengthy text longer than 9? chars leads to strange behaviour
      // finishedAnimating = tetris.drawText(1, 21);
      finishedAnimating = tetris.drawText(1, 42);
      //    Serial.println("drawText() return");
    } else {

      if (twelveHourFormat) {
        // Place holders for checking are any of the tetris objects
        // currently still animating.
        bool tetris1Done = false;
        bool tetris2Done = false;
        bool tetris3Done = false;

        // tetris1Done = tetris.drawNumbers(-6, 26, showColon);
        // tetris2Done = tetris2.drawText(56, 25);
        tetris1Done = tetris.drawNumbers(-10, 52, showColon);
        tetris2Done = tetris2.drawText(112, 52);

        // Only draw the top letter once the bottom letter is finished.
        if (tetris2Done) {
          // tetris3Done = tetris3.drawText(56, 15);
          tetris3Done = tetris3.drawText(112, 30);
        }

        finishedAnimating = tetris1Done && tetris2Done && tetris3Done;

      } else {
        // draw 24h format time
        //  finishedAnimating = tetris.drawNumbers(2, 26, showColon);
        // scale 3: finishedAnimating = tetris.drawNumbers(16, 50, showColon);
        finishedAnimating = tetris.drawNumbers(2, 52, showColon);
      }
    }
  }
  display.display();  // MM: actually draw display
}


void handleColonAfterAnimation() {

  // It will draw the colon every time, but when the colour is black it
  // should look like its clearing it.
  uint16_t colour =  showColon ? SSD1306_WHITE : SSD1306_BLACK;
  // The x position that you draw the tetris animation object
  // int x = twelveHourFormat ? -6 : 2;
  int x = twelveHourFormat ? -10 : 2;
  // The y position adjusted for where the blocks will fall from
  // (this could be better!)
  // int y = 26 - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
  int y = 54 - (TETRIS_Y_DROP_DEFAULT * tetris.scale);
  tetris.drawColon(x, y, colour);
  display.display(); // MM: draw..
}



void setMatrixTime() {

  String timeString = "";
  String AmPmString = "";
  if (twelveHourFormat) {
    // Get the time in format "1:15" or 11:15 (12 hour, no leading 0)
    // Check the EZTime Github page for info on time formatting
   // timeString = myTZ.dateTime("g:i");
     // Format time as HH:MM
  char timeBuf[6];
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());
  timeString = String(timeBuf);

    //If the length is only 4, pad it with a space at the beginning
    if (timeString.length() == 4) {
      timeString = " " + timeString;
    }

    //Get if its "AM" or "PM"
    // TODO TODO: replace myTZ functionality with equivalent in NTPclient...
  // AmPmString = myTZ.dateTime("A");
    if (lastDisplayedAmPm != AmPmString) {
      Serial.println(AmPmString);
      lastDisplayedAmPm = AmPmString;
      // Second character is always "M"
      // so need to parse it out
      tetris2.setText("M", forceRefresh);

      // Parse out first letter of String
      tetris3.setText(AmPmString.substring(0, 1), forceRefresh);
    }
  } else {
    // Get time in format "01:15" or "22:15"(24 hour with leading 0)
    //timeString = myTZ.dateTime("H:i");    
    //MM: test: timeString = myTZ.dateTime("23:i");
    // Format time as HH:MM
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());
    timeString = String(timeBuf);
  }

  // Only update Time if its different
  if (lastDisplayedTime != timeString) {  
    Serial.print("setMatrixTime(): timeString: ");
    Serial.println(timeString);
    lastDisplayedTime = timeString;
    tetris.setTime(timeString, forceRefresh);
    // Must set this to false so animation knows
    // to start again
    finishedAnimating = false;
  }

}


/////////// setup() - the Setup routinge WIFI mode to setup config in EEPROM
void setup() {

  Serial.begin(115200);
  Serial.println();
  Serial.println(F("Booting..."));

  // Initialize display
  Wire.begin(SDA_PIN, SCL_PIN);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    displayInitialized = true;
    display.setRotation(2);
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.print("Booting...");
    display.display();
    displayReady = true;
  } else {
    Serial.println(F("SSD1306 allocation failed"));
    // Leave displayInitialized as false
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  // Initialize EEPROM and load stored settings if available
  EEPROM.begin(EEPROM_SIZE);
  bool configMode = false;

  ///////////////////////////////
#if defined(MMdebug)
  // MM: save settings for convenience, without init mode...
  saveSettings();
#endif
  //////////////////////////////////

  // Check if config signature is present in EEPROM
  if (EEPROM.read(ADDR_SIGNATURE) == 'C' &&
      EEPROM.read(ADDR_SIGNATURE + 1) == 'F' &&
      EEPROM.read(ADDR_SIGNATURE + 2) == 'G' &&
      EEPROM.read(ADDR_SIGNATURE + 3) == '1') {
    Serial.println(F("Config signature found in EEPROM."));
  } else {
    Serial.println(F("No config signature found (EEPROM uninitialized)."));
  }

  // Check button press in first 300ms of boot
  bool buttonPressed = false;
  unsigned long startTime = millis();
  while (millis() - startTime < 300) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      buttonPressed = true;
    }
    delay(10);
  }
  if (buttonPressed) {
    Serial.println(F("Config button held - entering AP configuration mode."));
  }

  // Determine if we should start config portal
  if (EEPROM.read(ADDR_SIGNATURE) != 'C' || EEPROM.read(ADDR_SIGNATURE + 1) != 'F' ||
      EEPROM.read(ADDR_SIGNATURE + 2) != 'G' || EEPROM.read(ADDR_SIGNATURE + 3) != '1' ||
      buttonPressed) {
    configMode = true;
  }

  if (configMode) {
    // Load existing settings (if any) to pre-fill form
    if (EEPROM.read(ADDR_SIGNATURE) == 'C' && EEPROM.read(ADDR_SIGNATURE + 1) == 'F' &&
        EEPROM.read(ADDR_SIGNATURE + 2) == 'G' && EEPROM.read(ADDR_SIGNATURE + 3) == '1') {
      loadSettings();
    }
    // Start configuration portal (Access Point mode)
    startConfigPortal();
    // After configuration, ESP will reboot. If not rebooted (e.g. user didn't submit),
    // it will remain in AP mode and handleClient in loop.
  } else {
    // Load settings from EEPROM
    loadSettings();
    Serial.print(F("Connecting to WiFi: "));
    Serial.println(wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPass.c_str());
    // Wait up to 10 seconds for connection
    unsigned long wifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
      delay(500);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println(F("WiFi connected."));
      Serial.print(F("IP Address: "));
      Serial.println(WiFi.localIP());
    } else {
      Serial.println(F("WiFi connection failed. Starting AP mode instead."));
      startConfigPortal();
      return; // Exit setup to avoid running normal mode without WiFi
    }
    // Setup NTP client with stored timezone offset
    timeClient.setPoolServerName("pool.ntp.org");
    timeClient.setTimeOffset(timezoneOffset);
    timeClient.setUpdateInterval(900000);  // 15 mins interval
    timeClient.begin();
    // Perform initial NTP update
    timeClient.update();

    // Prepare first weather fetch
    lastWeatherFetch = 0; // force immediate fetch on first weather screen display
    weatherValid = false;

    Serial.println(F("Getting initial weather..."));
    weatherValid = getWeather();
    lastWeatherFetch = millis();
    if (weatherValid) {
      Serial.println(F("Initial weather fetch successful."));
    } else {
      Serial.println(F("Initial weather fetch failed."));
    }

    ////////////////////////////
    // MM: tetris Stuff..
    // MM: very important fix!! the hardcoded colors of the TetrisAnimation library do NOT work with Monochrome OLED SSD1306
    for (int i = 0; i < 9; i++) {
      tetris.tetrisColors[i] = SSD1306_WHITE;
      tetris2.tetrisColors[i] = SSD1306_WHITE;
      tetris3.tetrisColors[i] = SSD1306_WHITE;
    }
    tetris.scale = 2;
    tetris2.scale = 2; tetris3.scale = 2;

    finishedAnimating = false;
    tetris.setText("TETRISCLK");  // MM: hangs on TETRISCLOCK.. so length of text seems to matter in library call..
    //tetris.setText("!#$%&'()");
    Serial.println("TETRISCLK");
    // Start the Animation Timer
    //animationTicker.attach(0.05, animationHandler);
    //MM: ESP8266 crashes on Ticker // TODO: void ICACHE_RAM_ATTR animationHandler()
   // animationTicker.attach_ms(50, animationHandler);
  /*
   for (int mm=0; mm<100; mm++){
      animationHandler();
      delay(50);
   }
    delay(6000);
*/
    // Wait for the animation to finish
    while (!finishedAnimating)
    {
      animationHandler();
      delay(50); // waiting for intro to finish
    }
    delay(2000);
    finishedAnimating = false;
    displayIntro = false;
    tetris.scale = 4;
  }
  Serial.println("setup() complete. going into loop() and animationTicker/Handle");
}

/* ------------------------------------
  /////// loop() - The Main program loop
  ------------------------------------ */
void loop() {
  // If in config portal mode, handle web server
  if (WiFi.getMode() == WIFI_AP) {
    server.handleClient();
    // In AP mode, do not run normal display loop
    return;
  }

  // Regular mode: update time and handle display
  timeClient.update();
  unsigned long now = millis();

  // Switch screen every 30 seconds
  if (now - lastScreenSwitch > 30000) {
    showWeatherScreen = !showWeatherScreen;
    redrawScreen = true;  // MM: well, also for time screen
    finishedAnimating = false; // MM: important..
    lastScreenSwitch = now;
    // If switching to weather screen, update weather data (limit fetch frequency)
    if (showWeatherScreen) {

      // Update weather every 15 minutes
      if (millis() - lastWeatherFetch > 900000UL || !weatherValid) {
        Serial.println(F("Updating weather data..."));
        weatherValid = getWeather();
        lastWeatherFetch = millis();
        if (weatherValid) {
          Serial.println(F("Weather update successful."));
        } else {
          Serial.println(F("Weather update failed or data invalid."));
        }
      }
    }
  }

  // Draw the appropriate screen
  if (showWeatherScreen) {
     if (redrawScreen) { // should we actually draw (avoid flicker)
        Serial.println(F("call drawWeatherScreen()"));
        drawWeatherScreen();
        redrawScreen = false;
     }
  } else {
     // time for the time screen.... was "drawTimeScreen();"
      unsigned long now = millis();
   //   Serial.println("looping the Tetris timing ...");
      if (now > oneSecondLoopDue) {
        // We can call this often, but it will only
        // update when it needs to
        setMatrixTime();
        showColon = !showColon;
        //finishedAnimating = false; // MM: bereits im setMatrixTime() !!
        if (!finishedAnimating) redrawScreen = true; // Hack
         // Wait for the animation to finish
         while (!finishedAnimating)
         {
           animationHandler();
           delay(50); // waiting for intro to finish
         }
         // delay(2000);

        // To reduce flicker on the screen we stop clearing the screen
        // when the animation is finished, but we still need the colon to
        // to blink
        if (finishedAnimating) {
        /////////////
         if (redrawScreen) { // should we actually draw (avoid flicker)
          Serial.println(F("call printBottomDateLine()"));
          printBottomDateLine();
          redrawScreen = false;
         }
        /////////////////////
        Serial.println(F("call handleColonAfterAnimation()"));
        handleColonAfterAnimation();
        }
        oneSecondLoopDue = now + 1000;
      }
  }

  // Small delay to yield to system
  delay(10);
}

void startConfigPortal() {
  // Stop any existing WiFi and start AP
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print(F("Started AP mode with SSID "));
  Serial.print(AP_SSID);
  Serial.print(F(". Connect and browse to http://"));
  Serial.println(apIP);

  if (displayReady) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("AP mode");
    display.setCursor(0, 10);
    //MM:
    // display.println("SSID: MM-ESP01-Setup");
    display.print("SSID: ");
    display.println(AP_SSID);

    display.setCursor(0, 20);
    display.println("IP: 192.168.4.1");
    display.display();
  }
  // Setup web server routes
  server.on("/", HTTP_GET, []() {
    // HTML page for config
    String page = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    page += String("<title>ESP8266 Setup</title><style>") +
            ".container{max-width:300px;margin:40px auto;padding:20px;background:#f7f7f7;border:1px solid #ccc;border-radius:5px;}" +
            "body{text-align:center;font-family:sans-serif;}h2{margin-bottom:15px;}label{display:block;text-align:left;margin-top:10px;}" +
            "input, select{width:100%;padding:8px;margin-top:5px;border:1px solid #ccc;border-radius:3px;}" +
            "input[type=submit]{margin-top:15px;background:#4caf50;color:white;border:none;cursor:pointer;border-radius:3px;font-size:16px;}" +
            "input[type=submit]:hover{background:#45a049;}" +
            "</style></head><body><div class='container'>";
    page += "<h2>Device Configuration</h2><form method='POST' action='/'>";
    // WiFi SSID field
    page += "<label>Wi-Fi SSID:</label><input type='text' name='ssid' value='" + wifiSSID + "' required>";
    // Password field
    page += "<label>Password:</label><input type='password' name='pass' value='" + wifiPass + "' placeholder=''>";
    // City field
    page += "<label>City:</label><input type='text' name='city' value='" + city + "' required>";
    // Timezone dropdown
    page += "<label>Timezone:</label><select name='tz'>";
    // Populate timezone options from UTC-12 to UTC+14
    for (int tzHour = -12; tzHour <= 14; ++tzHour) {
      long tzSeconds = tzHour * 3600;
      String option = "<option value='" + String(tzSeconds) + "'";
      if (tzSeconds == timezoneOffset) {
        option += " selected";
      }
      option += ">UTC";
      if (tzHour >= 0) option += "+" + String(tzHour);
      else option += String(tzHour);
      option += "</option>";
      page += option;
    }
    page += "</select>";
    // Submit button
    page += "<input type='submit' value='Save'></form></div></body></html>";
    server.send(200, "text/html", page);
  });

  server.on("/", HTTP_POST, handleConfigForm);
  server.begin();
  Serial.println(F("HTTP server started for config portal."));
}

void handleConfigForm() {
  // Get form values
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String newCity = server.arg("city");
  String tzStr = server.arg("tz");

  Serial.println(F("Received configuration:"));
  Serial.print(F("SSID: ")); Serial.println(ssid);
  Serial.print(F("Password: ")); Serial.println(pass);
  Serial.print(F("City: ")); Serial.println(newCity);
  Serial.print(F("Timezone (s): ")); Serial.println(tzStr);

  if (ssid.length() > 0 && newCity.length() > 0 && tzStr.length() > 0) {
    wifiSSID = ssid;
    wifiPass = pass;
    city = newCity;
    timezoneOffset = tzStr.toInt();
    // Save to EEPROM
    saveSettings();
    // Send response page
    server.send(200, "text/html", "<html><body><h3>Settings saved. Rebooting...</h3></body></html>");
    delay(1000);
    ESP.restart();
  } else {
    server.send(400, "text/html", "<html><body><h3>Invalid input, please fill all required fields.</h3></body></html>");
  }
}

///////// loadSettings()
void loadSettings() {
  // Read SSID
  uint8_t len = EEPROM.read(ADDR_SSID);
  if (len > 0 && len < 0xFF) {
    char buf[80];
    for (int i = 0; i < len; ++i) {
      buf[i] = char(EEPROM.read(ADDR_SSID + 1 + i));
    }
    buf[len] = '\0';
    wifiSSID = String(buf);
  } else {
    wifiSSID = "";
  }
  // Read Password
  len = EEPROM.read(ADDR_PASS);
  if (len > 0 && len < 0xFF) {
    char buf[80];
    for (int i = 0; i < len; ++i) {
      buf[i] = char(EEPROM.read(ADDR_PASS + 1 + i));
    }
    buf[len] = '\0';
    wifiPass = String(buf);
  } else {
    wifiPass = "";
  }
  // Read City
  len = EEPROM.read(ADDR_CITY);
  if (len > 0 && len < 0xFF) {
    char buf[80];
    for (int i = 0; i < len; ++i) {
      buf[i] = char(EEPROM.read(ADDR_CITY + 1 + i));
    }
    buf[len] = '\0';
    city = String(buf);
  } else {
    city = "";
  }
  // Read Timezone offset (int32)
  uint32_t b0 = EEPROM.read(ADDR_TZ);
  uint32_t b1 = EEPROM.read(ADDR_TZ + 1);
  uint32_t b2 = EEPROM.read(ADDR_TZ + 2);
  uint32_t b3 = EEPROM.read(ADDR_TZ + 3);
  uint32_t raw = (b0 & 0xFF) | ((b1 & 0xFF) << 8) | ((b2 & 0xFF) << 16) | ((b3 & 0xFF) << 24);
  timezoneOffset = (int) raw;
}

///////// saveSettings()
void saveSettings() {
  // Write SSID
  uint8_t len = wifiSSID.length();
  if (len > 0) {
    EEPROM.write(ADDR_SSID, len);
    for (int i = 0; i < len; ++i) {
      EEPROM.write(ADDR_SSID + 1 + i, wifiSSID[i]);
    }
  } else {
    EEPROM.write(ADDR_SSID, 0);
  }
  // Write Password
  len = wifiPass.length();
  if (len > 0) {
    EEPROM.write(ADDR_PASS, len);
    for (int i = 0; i < len; ++i) {
      EEPROM.write(ADDR_PASS + 1 + i, wifiPass[i]);
    }
  } else {
    EEPROM.write(ADDR_PASS, 0);
  }
  // Write City
  len = city.length();
  if (len > 0) {
    EEPROM.write(ADDR_CITY, len);
    for (int i = 0; i < len; ++i) {
      EEPROM.write(ADDR_CITY + 1 + i, city[i]);
    }
  } else {
    EEPROM.write(ADDR_CITY, 0);
  }
  // Write Timezone (int32)
  int tz = timezoneOffset;
  EEPROM.write(ADDR_TZ, tz & 0xFF);
  EEPROM.write(ADDR_TZ + 1, (tz >> 8) & 0xFF);
  EEPROM.write(ADDR_TZ + 2, (tz >> 16) & 0xFF);
  EEPROM.write(ADDR_TZ + 3, (tz >> 24) & 0xFF);
  // Write signature 'CFG1'
  EEPROM.write(ADDR_SIGNATURE, 'C');
  EEPROM.write(ADDR_SIGNATURE + 1, 'F');
  EEPROM.write(ADDR_SIGNATURE + 2, 'G');
  EEPROM.write(ADDR_SIGNATURE + 3, '1');
  // Commit changes to EEPROM
  EEPROM.commit();
  Serial.println(F("Configuration saved to EEPROM."));
}



void drawWeatherScreen() {
  if (!displayInitialized) {
    return;
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setFont(NULL);

  // Top center: city name
  String cityName = city;
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(cityName, 0, 0, &x1, &y1, &w, &h);
  int cityX = (128 - w) / 2;
  display.setCursor(cityX, 0);
  display.print(cityName);
  // Middle center: temperature (big font FreeMonoBold12pt7b)
  display.setFont(&FreeMonoBold12pt7b);
  if (weatherValid && weatherTemp != "N/A") {
    // weatherTemp is e.g. "+12" or "-3" as string (cleaned)
    String tempNum = weatherTemp;
    // Center the numeric part
    display.getTextBounds(tempNum, 0, 30, &x1, &y1, &w, &h);
    int tempX = (128 - (w + 12)) / 2; // leave space for degree and C (~12px)
    int tempY = 30; // baseline for 12pt font around mid screen
    display.setCursor(tempX, tempY);
    display.print(tempNum);
    // Now draw degree symbol and 'C' in default font after the number
    //display.setFont(NULL);
    // Place small degree symbol near top of big text and 'C' after it
    int degX = tempX + w + 3; // position degree just right of number
    int degY = tempY - 16;    // raise small text (approx half big font height)
    if (degY < 0) degY = 0;
    display.setCursor(degX, degY);
    //display.print((char)247);
    display.drawCircle(degX, degY, 2, SSD1306_WHITE);
    display.setCursor(degX + 3, tempY);
    display.setFont(&FreeMonoBold12pt7b);
    display.print("C");
    display.setFont(NULL);
  } else {
    // If weather not available, show N/A in big font
    String na = "N/A";
    display.getTextBounds(na, 0, 30, &x1, &y1, &w, &h);
    int naX = (128 - w) / 2;
    display.setCursor(naX, 30);
    display.print(na);
    // No degree symbol or 'C' in this case
  }
  // Below temperature: condition string (centered)
  display.setFont(NULL);
  String cond = weatherValid ? weatherCond : "";
  cond.trim();
  display.getTextBounds(cond, 0, 40, &x1, &y1, &w, &h);
  int condX = (128 - w) / 2;
  display.setCursor(condX, 40);
  display.print(cond);
  // Bottom right: H:% W:m/s P:mm
  display.setFont(NULL);
  if (weatherValid && weatherTemp != "N/A") {
    String bottomStr = String("H:") + weatherHum;
    bottomStr += " W:" + weatherWind;
    if (weatherWind != "N/A") bottomStr += "m/s";
    bottomStr += " P:" + weatherPress + "mm";
    display.getTextBounds(bottomStr, 0, 56, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 56);
    display.print(bottomStr);
  } else {
    String bottomStr = "H:N/A W:N/A P:N/A";
    display.getTextBounds(bottomStr, 0, 56, &x1, &y1, &w, &h);
    display.setCursor(128 - w, 56);
    display.print(bottomStr);
  }
  display.display();
}



//////// getWeather()
bool getWeather() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi not connected, cannot get weather."));
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate validation for simplicity
  const char* host = "wttr.in";
  // MM: added https and explicit host
  // String url = "https://wttr.in/" + city + "?lang=de&format=%25t|%25C|%25h|%25w|%25P";
  String url = "/" + city + "?lang=de&format=%25t|%25C|%25h|%25w|%25P";

  Serial.print(F("Connecting to weather server: "));
  Serial.println(host);

  // MM: extra debugging
  int conerr;
  conerr = client.connect(host, 443);
  if (!conerr) {
    Serial.println(F("Connection failed."));
    Serial.println(conerr);
    return false;
  }

  Serial.print(F("Sending Request for URL: "));
  Serial.println(url);

  // Send GET request
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: MM-ESP8266\r\n" +
               "Connection: close\r\n\r\n");
  // Read full response as raw text
  String response = "";
  unsigned long timeout = millis() + 5000;
  while (millis() < timeout && client.connected()) {
    while (client.available()) {
      char c = client.read();
      response += c;
    }
  }
  client.stop();

  Serial.println(F("---- RAW RESPONSE ----"));
  Serial.println(response);
  Serial.println(F("----------------------"));

  // --- Extract line with weather data ---
  String result = "";
  int from = 0;
  while (from >= 0) {
    int to = response.indexOf('\n', from);
    if (to == -1) break;
    String line = response.substring(from, to);
    line.replace("\r", "");
    line.trim();

    Serial.print("DEBUG LINE: >");
    Serial.print(line);
    Serial.println("<");

    if (line.indexOf('|') != -1) {
      result = line;
      break;
    }

    from = to + 1;
  }

  // If no \n at the end — catch the remaining part
  if (result.length() == 0 && from < response.length()) {
    String line = response.substring(from);
    line.replace("\r", "");
    line.trim();
    Serial.print("FALLBACK LINE: >");
    Serial.print(line);
    Serial.println("<");
    if (line.indexOf('|') != -1) {
      result = line;
    }
  }

  Serial.print(F("Weather raw response: "));
  Serial.println(result);

  if (result.length() == 0) {
    Serial.println(F("No weather data found."));
    return false;
  }

  // Parse fields: temp|cond|hum|wind|press
  int idx1 = result.indexOf('|');
  int idx2 = result.indexOf('|', idx1 + 1);
  int idx3 = result.indexOf('|', idx2 + 1);
  int idx4 = result.indexOf('|', idx3 + 1);
  if (idx1 < 0 || idx2 < 0 || idx3 < 0 || idx4 < 0) return false;

  String tempStr   = result.substring(0, idx1);
  String condStr   = result.substring(idx1 + 1, idx2);
  String humStr    = result.substring(idx2 + 1, idx3);
  String windStr   = result.substring(idx3 + 1, idx4);
  String pressStr  = result.substring(idx4 + 1);

  // Clean Temperature: remove degree symbol and 'C'
  tempStr.replace("°C", "");
  tempStr.replace("°", "");
  tempStr.replace("C", "");
  tempStr.trim();
  weatherTemp = tempStr; // keep + or - sign if present

  // Clean Condition:
  condStr.trim();
  weatherCond = condStr;

  // Clean Humidity: ensure '%' present
  humStr.trim();
  if (!humStr.endsWith("%")) {
    weatherHum = humStr + "%";
  } else {
    weatherHum = humStr;
  }

  // Clean Wind: extract numeric part (km/h)
  String windNum = "";
  for (uint i = 0; i < windStr.length(); ++i) {
    char c = windStr.charAt(i);
    if ((c >= '0' && c <= '9') || c == '.') {
      windNum += c;
    } else if (c == ' ' && windNum.length() > 0) {
      break;
    }
  }
  if (windNum.length() == 0) {
    weatherWind = "N/A";
  } else {
    float windKmh = windNum.toFloat();
    float windMs = windKmh / 3.6;
    int windMsRounded = (int)round(windMs);
    weatherWind = String(windMsRounded);
  }

  // Clean Pressure: extract numeric part (hPa)
  String pressNum = "";
  for (uint i = 0; i < pressStr.length(); ++i) {
    char c = pressStr.charAt(i);
    if ((c >= '0' && c <= '9') || c == '.') {
      pressNum += c;
    } else if (!pressNum.isEmpty()) {
      break;
    }
  }
  if (pressNum.length() == 0) {
    weatherPress = "N/A";
  } else {
    float pressHpa = pressNum.toFloat();
    float pressMm = pressHpa * 0.75006;
    int pressRounded = (int) round(pressMm);
    weatherPress = String(pressRounded);
  }

  Serial.println(F("Parsed weather data:"));
  Serial.print(F("Temp=")); Serial.println(weatherTemp);
  Serial.print(F("Cond=")); Serial.println(weatherCond);
  Serial.print(F("Hum=")); Serial.println(weatherHum);
  Serial.print(F("Wind(m/s)=")); Serial.println(weatherWind);
  Serial.print(F("Pressure(mmHg)=")); Serial.println(weatherPress);

  // Validate critical fields
  if (weatherTemp == "" || weatherCond == "") {
    return false;
  }
  return true;
}
