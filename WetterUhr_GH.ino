/********************************************************************

   Die Uhr TomSoft
   on ESP8266 - 12E NodeMCU with Digole 2.6"-Display

   The time is synchronised with a NTP-Server regularly
   NTP-Server: FritzBox  :-)

   The connections between the NodeMCU board the the display are
   as follows (I2C):

   ESP8266 Pin          Display Pin   Function
   --------------------------------------------------------------------
   D1 / GPIO5              CLK        I2C Clock
   D2 / GPIO4              DATA       I2C Data
       5V                  VCC        Power for the display
       GND                 GND        Ground for the display

   D5 / GPIO14             Data       On DHT module


   Used Libraries:
   TimeLib:            http://www.arduino.cc/playground/Code/Time
   Timezone:           https://github.com/JChristensen/Timezone
   DigoleSerial        http://www.digole.com
   DHT:                https://github.com/adafruit/DHT-sensor-library
   JsonListener:       https://github.com/squix78/json-streaming-parser

 *******************************************************************/

//****************************************
// Libraries

#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <JsonListener.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>

#include <TimeLib.h>
#include <Timezone.h>

#include <DHT.h>

#include "fonts.h"
#include "WundergroundClient.h"
#include "NTP.h"


/***************************
   Begin Settings
 **************************/

// Create LCD driver instance
#define LCDWidth 240                   // define screen width,height
#define LCDHeight 320
#define _Digole_Serial_I2C_
#define Ver 34                         // if the version of firmware on display is V3.3 and newer, use this

#include <DigoleSerial.h>
#include <Wire.h>
DigoleSerialDisp mydisp(&Wire, '\x27');

// Network
const char* ssid = "*****************";
const char* password = "*************";
IPAddress ip(192, 168, 178, 209);                           // Static IP
IPAddress gateway(192, 168, 178, 1);
IPAddress dns(192, 168, 178, 1);
IPAddress subnet(255, 255, 255, 0);

#define HOSTNAME "WETTERUHR"

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone CE(CEST, CET);

time_t prevTime = 0;
time_t prevDate = 0;

byte daylight = 0;           // 1 = day, 0 = night
byte prevDaylight = 0;

//define 8 bit color, see:https://en.wikipedia.org/wiki/8-bit_color
#define WHITE    0xFF
#define BLACK    0
#define RED      0xE0
#define GREEN    0x1A
#define BLUE     0x03
#define MAGENTA  0xF3
#define YELLOW   0xFC
#define DARKGREY 0x09
#define SKYBLUE  0x7B
#define GREY     0x72

// initialize DHT sensor
DHT dht(14, DHT22, 15);                                     // DHT22 an D5 bzw. GPIO14
float humidity, temperature, prevTemp;
unsigned long previousMillis = 0;
unsigned long currentMillis;

// Wunderground Settings
const boolean IS_METRIC = true;
const String WUNDERGRROUND_API_KEY = "**************";
const String WUNDERGRROUND_LANGUAGE = "DL";
const String WUNDERGROUND_COUNTRY = "DE";
const String WUNDERGROUND_CITY = "************";

WundergroundClient wunderground(IS_METRIC);

// flag changed in the ticker function every 10 minutes
const int UPDATE_INTERVAL_SECS = 10 * 60;                   // Update every 10 minutes
bool readyForWeatherUpdate = false;

// initialise ticker
Ticker ticker;

/***************************
   End Settings
 **************************/


//*************************************************
// Setup

void setup() {

  // Initialize the LCD display
  mydisp.begin();

  // Set display orientation
  mydisp.setRotation(0);

  // Set display mode
  mydisp.setMode('!');         // set pixels operation mode as "COPY"

  // Clear the display
  clearScreen(BLACK, WHITE);

  // Connect to WiFi
  String hostname(HOSTNAME);
  WiFi.hostname(hostname);
  WiFi.mode(WIFI_STA);
  WiFi.config(ip, gateway, subnet, dns);
  WiFi.begin(ssid, password);

  int ccounter = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    //Serial.print(".");
    mydisp.setFont(18);            // font 9 x 18
    drawCenteredText(40, "Connecting to WiFi", BLACK );
    drawText( 80, 70, ccounter % 3 == 0 ? "O" : "o", BLACK );
    drawText(120, 70, ccounter % 3 == 1 ? "O" : "o", BLACK );
    drawText(160, 70, ccounter % 3 == 2 ? "O" : "o", BLACK );
    drawCenteredText(150, "TomSoft", GREEN );
    drawCenteredText(180, "Die WetterUhr", GREEN );
    ccounter++;
  }
  // output of connection details
  clearScreen(GREEN, BLACK);
  mydisp.setFont(18);
  drawText(0, 20, "Connected to:" , GREEN );
  drawText(10, 40, ssid, GREEN );
  drawText(0, 70, "IP adress: ", GREEN );
  //mydisp.setPrintPos(10, 50, 1);
  mydisp.setTextPosAbs(10, 90);
  mydisp.print(WiFi.localIP());
  drawCenteredText(320, "TomSoft", GREEN );
  delay(3000);

  // Load user font meteocons
  clearScreen(GREEN, BLACK);
  mydisp.setFont(18);
  drawText(0, 20, "Loading User Fonts,\n\rplease wait..." , GREEN );
  delay(1000);
  drawText(0, 60, "courB24r", GREEN);
  mydisp.downloadUserFont(sizeof(courB24r), courB24r, 0);
  delay(1000);
  drawText(0, 80, "Meteocons", GREEN);
  mydisp.downloadUserFont(sizeof(meteocons_20), meteocons_20, 1);
  delay(1000);

  // Initialize NTP Client and get the time
  initNTP();
  // Set the time provider to NTP
  setSyncProvider(getLocalTime);
  setSyncInterval(300);                                 // every 5 minutes

  // initialize DHT
  dht.begin();

  // Setup OTA
  ArduinoOTA.setHostname((const char *)hostname.c_str());
  ArduinoOTA.onProgress(drawOtaProgress);
  ArduinoOTA.onStart(drawOtaStart);
  ArduinoOTA.onEnd(drawOtaEnd);
  ArduinoOTA.begin();

  // get the weather data for the first time
  // draw the background and the data
  updateData();

  ticker.attach(UPDATE_INTERVAL_SECS, setReadyForWeatherUpdate);

}

//*************************************************
// Loop

void loop() {

  if (readyForWeatherUpdate) {
    updateData();
  }

  ArduinoOTA.handle();

  if ( now() != prevTime)  {                   // update the display only if the time has changed
    prevTime = now();
    currentMillis = millis();
    drawTime();                                // Draw the updated time
  }
  if ( day() != prevDate) {
    prevDate = day();
    drawDate();                                // Draw the updated date
  }

  daylight = isDayTime();
  if ( daylight != prevDaylight) {
    prevDaylight = daylight;
    drawDaylight();                            // Draw the updated icon
  }

  getDHT();
  if ( temperature != prevTemp) {
    prevTemp = temperature;
    currentMillis = millis();
    drawTemp();                                // Draw the updated temperature
  }

}

//*************************************************
// functions

time_t getLocalTime(void)                      // check for timezone and summertime (DST)
{
  return CE.toLocal(getNTPTime());
}

void updateData() {
  clearScreen(GREEN, BLACK);
  mydisp.setFont(18);
  drawText(0, 20, "Loading Weather data,\n\rplease wait..." , GREEN );
  drawText(0, 60, "Updating conditions...", GREEN);
  wunderground.updateConditions(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawText(0, 80, "Updating forecasts...", GREEN);
  wunderground.updateForecast(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  drawText(0, 100, "Updating astronomy...", GREEN);
  wunderground.updateAstronomy(WUNDERGRROUND_API_KEY, WUNDERGRROUND_LANGUAGE, WUNDERGROUND_COUNTRY, WUNDERGROUND_CITY);
  readyForWeatherUpdate = false;
  drawText(10, 120, "->Done!", GREEN);
  delay(1000);
  drawBackground();
  setUpDisplay();
}

// Basic functions for the display
// Draw a text string at specified location with specified color
void drawText(int x, int y, String text, uint8_t color) {
  mydisp.setColor(color);
  //mydisp.setPrintPos(x, y, 1);
  mydisp.setTextPosAbs(x, y);
  mydisp.print(text);
}

// Draw a text string centered on display with y position
void drawCenteredText(int y, String text, uint8_t color) {
  mydisp.setColor(color);
  //mydisp.setPrintPos(xOffsetForCenteredText(text), y, 1);
  mydisp.setTextPosAbs(xOffsetForCenteredText(text), y);
  mydisp.print(text);
}

// Calculate the x offset necessary to center a text string
int xOffsetForCenteredText(String text) {
  return (LCDWidth - getTextWidth(text)) / 2;
}

// Get the width in pixels of a text string
int getTextWidth(String text) {
  int len = text.length();
  // Chars are 8 px wide with 1 px space = 9, Font #18
  return 9 * len;
}

void drawOtaStart() {
  clearScreen(GREEN, BLACK);
}

void drawOtaProgress(unsigned int progress, unsigned int total) {
  mydisp.setFont(18);
  drawCenteredText(80, "OTA Update", GREEN);
  //mydisp.setPrintPos(40, 100, 1);
  mydisp.setTextPosAbs(60, 120);
  mydisp.setColor(GREEN);
  mydisp.print("Progress: ");
  mydisp.print(progress / (total / 100));
  mydisp.println(" %");
}

void drawOtaEnd() {
  clearScreen(GREEN, BLACK);
  mydisp.setFont(18);
  drawCenteredText(40, "OTA Update successfull!", GREEN);
  drawCenteredText(70, "Now reseting!", GREEN);
}

void drawDate() {
  mydisp.setBgColor(GREY);
  String wkdayName[] = {"Platzhalter", "Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
  String wkday = wkdayName[weekday()];
  mydisp.setFont(18);
  drawText(10, 80, wkday, YELLOW);
  time_t n = now();
  int cw = weekOfYear(day(n), month(n), year(n));
  String CalWeek = "Woche: " + String(cw);
  drawText(150, 80, CalWeek, YELLOW);
  mydisp.setFont(200);
  String date = ((day() < 10) ? "0" + String(day()) : String(day())) + "." + ((month() < 10) ? "0" + String(month()) : String(month())) + "." + String(year());
  drawText(20, 60, date, YELLOW);
  mydisp.setBgColor(BLACK);
}

void drawTime() {
  mydisp.setBgColor(GREY);
  mydisp.setFont(200);
  String time = ((hour() < 10) ? "0" + String(hour()) : String(hour())) + ":" + ((minute() < 10) ? "0" + String(minute()) : String(minute())) + ":" + ((second() < 10) ? "0" + String(second()) : String(second()));
  drawText(40, 30, time, YELLOW);
  mydisp.setFont(18);
  mydisp.setBgColor(BLACK);
}

void drawTemp() {
  String t = String(temperature);
  int l = t.length();
  t.remove(l - 1);
  String temp = t + " C";
  mydisp.setFont(18);
  drawText(80, 105, "Innen:", WHITE);
  mydisp.setFont(200);
  drawText(80, 135, temp, temperature > 20 ? RED : BLUE);
  mydisp.setFont(18);
}

void drawDaylight() {
  delay(1000);
  if (isDayTime()) {
    //mydisp.drawBitmap256(0, 79, ICON_WIDTH, ICON_HEIGHT, Tag);
    daylight = 1;
  } else  {
    //mydisp.drawBitmap256(0, 79, ICON_WIDTH, ICON_HEIGHT, Nacht);
    daylight = 0;
  }
  delay(1000);
}

void drawForecast() {
  drawForecastDetails(  0, 240, 2);
  drawForecastDetails( 80, 240, 4);
  drawForecastDetails(160, 240, 6);
}

void drawForecastDetails(int x, int y, int dayIndex) {
  mydisp.setFont(18);
  String fday = wunderground.getForecastTitle(dayIndex).substring(0, 3);
  drawText(x + 20, y, fday, WHITE);
  mydisp.setFont(201);
  drawText(x + 20, y + 32, wunderground.getForecastIcon(dayIndex), WHITE);
  mydisp.setFont(18);
  mydisp.setTextPosAbs(x + 20, y + 55);
  mydisp.setColor(BLUE);
  mydisp.print(wunderground.getForecastLowTemp(dayIndex));
  mydisp.setColor(WHITE);
  mydisp.print("|");
  mydisp.setColor(RED);
  mydisp.print(wunderground.getForecastHighTemp(dayIndex));
}

// Determine day time or night time for proper icon selection
// daytime is time between sunrise and sunset
bool isDayTime() {
  int hours = hour();
  int minutes = minute();
  String SunriseTime = wunderground.getSunriseTime();
  String SrHour = SunriseTime.substring(0, 2);
  int SrHourInt = SrHour.toInt();
  //String SrMinute = SunriseTime.substring(3);
  //int SrMinuteInt = SrMinute.toInt();
  String SunsetTime = wunderground.getSunsetTime();
  String SsHour = SunsetTime.substring(0, 2);
  int SsHourInt = SsHour.toInt();
  //String SsMinute = SunsetTime.substring(3);
  //int SsMinuteInt = SsMinute.toInt();
  if ((hours >= SrHourInt) && (hours <= SsHourInt)) {  // Prüfung auf Minute noch einfügen!!
    return true;
  }
  else  {
    return false;
  }
}

void drawCurrentWeather() {
  mydisp.setColor(WHITE);
  mydisp.setFont(18);
  drawText(80, 165, "Aussen:", WHITE);
  drawText(80, 185, wunderground.getWeatherText(), WHITE);
  mydisp.setFont(200);
  int temperatur = wunderground.getCurrentTemp().toInt();
  String temp = wunderground.getCurrentTemp() + " C";
  drawText(80, 215, temp, temperatur > 20 ? RED : BLUE);
  byte weatherIcon = wunderground.getTodayIconByte();
  switch (weatherIcon) {
    case 1:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, chance_of_snow);
      break;
    case 2:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, chance_of_rain);
      break;
    case 3:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, chance_of_sleet);
      break;
    case 4:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, chance_of_snow);
      break;
    case 5:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, chance_of_storm);
      break;
    case 6:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, sunny);
      break;
    case 7:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, cloudy);
      break;
    case 8:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, flurries);
      break;
    case 9:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, fog);
      break;
    case 10:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, hazy);
      break;
    case 11:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, mostly_cloudy);
      break;
    case 12:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, mostly_sunny);
      break;
    case 13:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, mostly_sunny);
      break;
    case 14:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, mostly_cloudy);
      break;
    case 15:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, sleet);
      break;
    case 16:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, rain);
      break;
    case 17:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, snow);
      break;
    case 18:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, sunny);
      break;
    case 19:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, thunderstorm);
      break;
    default:
      mydisp.drawBitmap256(20, 160, W_ICON_WIDTH, W_ICON_HEIGHT, sunny);
      break;
  }
  delay(500);
  mydisp.setFont(18);
}

void drawBackground () {
  clearScreen(WHITE, BLACK);
  mydisp.setColor(GREY);
  mydisp.drawBox(0, 0, 240, 85);
  mydisp.drawBitmap256(20, 100, W_ICON_WIDTH, W_ICON_HEIGHT, haus_innen);
  delay(500);
  mydisp.setColor(WHITE);
  //mydisp.drawHLine(10,  85, 220);
  mydisp.drawHLine(10, 145, 220);
  mydisp.drawHLine(10, 225, 220);
  mydisp.drawHLine(10, 300, 220);
  drawCenteredText(320, "(c) 2016 TomSoft", BLUE );
}

void setUpDisplay() {
  drawDate();
  drawTime();
  drawDaylight();
  prevDaylight = daylight;
  drawTemp();
  drawCurrentWeather();
  drawForecast();
}

void clearScreen(uint8_t fgcolor, uint8_t bgcolor) {
  mydisp.setBgColor(bgcolor);
  mydisp.setColor(fgcolor);
  mydisp.clearScreen();
}

void getDHT() {
  if (currentMillis - previousMillis >= 3000) {
    previousMillis = currentMillis;
    humidity = dht.readHumidity();
    temperature = dht.readTemperature(false);
    if (isnan(humidity) || isnan(temperature)) {
      temperature = prevTemp;
      return;
    }
  }
}

void setReadyForWeatherUpdate() {
  readyForWeatherUpdate = true;
}

int DayOfYear(int d, int m, int y)  {                                   // Tag, Monat, Jahr; Jahr unbedingt vierstellig

  int t = 0;
  int s = 0;                                                            // 1 = Schaltjahr
  int mtag[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};   // Anzahl der Tage pro Monat
  if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) s = 1;              // Schaltjahrescheck
  if (s) mtag[2] = 29;                                                  // Bei einem Schaltjahr hat der Februar 29 Tage
  do
  {
    m--;
    t += mtag[m];                                                       // Tage der Vormonate in einer Schleife addieren
  } while (m > 1);
  return (t + d);                                                       // Ergebnis = Tage aller Vormonate + Tag des aktuellen Monats
}

int weekOfYear(int d, int m, int y)  {                                   // Tag, Monat, Jahr; Jahr unbedingt vierstellig

  int s, t, w, t1, w1;
  int r = 0;                                                            // 1 = Schaltjahr
  int mtag[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};   // Anzahl der Tage pro Monat
  if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) r = 1;              // Schaltjahrescheck
  t = DayOfYear(d, m, y);                                               // laufenden Tag des Jahres ermitteln
  w = DayOfWeek(1, 1, y) - 2;                                           // Wochentag des 1. Januars ermitteln und anpassen
  if (w > 3) w = w - 7;                                                 // Wochentag anpassen
  s = ((t - 1 + w) / 7) + 1;                                            // Kalenderwoche ermitteln
  if (s == 1)  {                                                        // wenn KW 1 prüfen ob nicht doch letzte KW vom Vorjahr
    w = w * (-1);
    if (w >= d) {
      t1 = DayOfYear(31, 12, y - 1);                                    // KW vom 31.12. des Vorjahres ermitteln
      w1 = DayOfWeek(1, 1, y - 1) - 2;
      if (w1 > 3) w1 = w1 - 7;
      s = ((t1 - 1 + w1) / 7) + 1;
    }
  }
  else  {                                                              // wenn KW 53 erreicht prüfen ob nicht doch KW 1 vom nächsten Jahr
    if (s == 53 && w == 2 && r == 1)                                   // Jahr ist Schaltjahr und beginnt mit Mittwoch, dann passt KW 53
      s = 53;
    else if (s == 53 && w != 3) s = 1;                                 // Jahr beginnt nicht mit Donnerstag, dann KW 1 nächstes Jahr
  }
  return (s);
}

int DayOfWeek(int d, int m, int y) {                                   // Tag, Monat, Jahr; Jahr unbedingt vierstellig

  int s = 0;                                                           // 1 = Schaltjahr
  int mtag[13] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};  // Anzahl der Tage pro Monat
  if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) s = 1;             // Schaltjahrescheck
  if (m < 3) {
    m += 13;
    y--;
  }
  else  m++;                                                          // Jahresanfang auf März verschieben
  s = d + 26 * m / 10 + y + y / 4 - y / 100 + y / 400 + 6;            // eigentliche Berechnung
  return (s % 7 + 1);                                                 // Ergebnis anpassen
}


