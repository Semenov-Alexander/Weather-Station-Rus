

/***
   Install the following libraries through Arduino Library Manager
   - Mini Grafx by Daniel Eichhorn
   - ESP8266 WeatherStation by Daniel Eichhorn
   - Json Streaming Parser by Daniel Eichhorn
   - simpleDSTadjust by neptune2
 ***/
#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <JsonListener.h>
#include <OpenWeatherMapCurrent.h>
#include <MiniGrafx.h>
#include <ILI9341_SPI.h>
#include <simpleDSTadjust.h>
#include "weathericons_mini.h"
#include "SansSerif_bold_11_Dialog.h"
#include "DSEG14_Classic_Mini.h"
#include "DHTesp.h"

#define DHTTYPE DHT22

#define MINI_BLACK 0
#define MINI_LIGHTGREY 1
#define MINI_YELLOW 2
#define MINI_BLUE 3
#define WIFI_SSID "****"
#define WIFI_PASS "***********"

#define WIFI_HOSTNAME "Weather-Station"

#define UTC_OFFSET +3
#define NTP_SERVERS "0.ru.pool.ntp.org", "1.ru.pool.ntp.org", "2.ru.pool.ntp.org" // change for different NTP (time servers)
#define NTP_MIN_VALID_EPOCH 1599868799 // August 1st, 2018

// defines the colors usable in the paletted 16 color frame buffer
uint16_t palette[] = {ILI9341_BLACK, // 0
                      ILI9341_LIGHTGREY, // 1
                      ILI9341_YELLOW, // 2
                      0x07FF,
                     }; //3
// Limited to 4 colors due to memory constraints
int BITS_PER_PIXEL = 2; // 2^2 =  4 colors
struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 0}; // Central European Summer Time = UTC/GMT +2 hours
struct dstRule EndRule = {"CET", Last, Sun, Oct, 2, 0};       // Central European Time = UTC/GMT +1 hour
unsigned long timing;
unsigned long timing_sec;
unsigned long timing_dht;
unsigned long timing_1s;
unsigned long timing_led;
unsigned int qual_dbm = 0;
const int UPDATE_INTERVAL_SECS = 60 * 60; // Update every 60 minutes
const int UPDATE_INTERVAL_1_SECS = 1; // Update every 1 sec
const int UPDATE_INTERVAL_SECS_DHT = 30; // Update every 30 sec
const int UPDATE_INTERVAL_ERR_DHT = 1; // Update every 1 sec
int UPDATE_INTERVAL_DHT;
const int UPDATE_INTERVAL_SECS_WIFI = 5; // Update every 5 sec
const int UPDATE_INTERVAL_SECS_LED = 3; // Update every 5 sec
const int RSSI_MAX =-50;// define maximum strength of signal in dBm
const int RSSI_MIN =-100;// define minimum strength of signal in dBm
String OPEN_WEATHER_MAP_APP_ID = "***************************";
String OPEN_WEATHER_MAP_LOCATION_ID = "532615";
uint16_t color;
int val;
int ledPower;

// Adjust according to your language
const String WDAY_NAMES[] = {"VOCKRECENYE", "PONEDELYNIK", "VTORNIK", "CREDA", "QETVERG", "PHTNISA", "CUBBOTA"};

// Pins for the ILI9341
#define TFT_DC D2
#define TFT_CS D1

#define DHTPIN D4
#define PIN_PHOTO_SENSOR A0
#define PIN_LED D8

//DHT dht(DHTPIN, DHTTYPE);
DHTesp dht;


ILI9341_SPI tft = ILI9341_SPI(TFT_CS, TFT_DC);
MiniGrafx gfx = MiniGrafx(&tft, BITS_PER_PIXEL, palette);

OpenWeatherMapCurrentData currentWeather;
simpleDSTadjust dstAdjusted(StartRule, EndRule);

const char* getMeteoconIconFromProgmem(String iconText);
const char* getMiniMeteoconIconFromProgmem(String iconText);

String temp;
float  hum;
String temp_0;
float  hum_0;
String tm_2 ;
String tm_3 ;
String hm ;

long lastDownloadUpdate = millis();
time_t dstOffset = 0;
int quality;
boolean sec_upd = true;
boolean bme_upd = true;
boolean wifi_upd = true;
bool frst = 0;
int temprtre = 0;
int temperature_sign = 0;
int pressure = 0;
int pressure_sign = 0;

// ==================================================================================================================================
//                                                    connectWifi
// ==================================================================================================================================
void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.print("Connecting to WiFi ");
  Serial.print(WIFI_SSID);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.hostname(WIFI_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int i = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (i > 80) i = 0;
    drawProgress(i, "Connecting to WiFi '" + String(WIFI_SSID) + "'");
    i += 10;
    Serial.print(".");
  }
  drawProgress(100, "Connected to WiFi '" + String(WIFI_SSID) + "'");
  Serial.print("Connected...");
}

// ==================================================================================================================================
//                                                    setup
// ==================================================================================================================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_LED, OUTPUT);
  analogWrite(PIN_LED, 170);
  
  gfx.init();
  //  gfx.setRotation(2);
  gfx.fillBuffer(MINI_BLACK);
  gfx.commit();

  SPI.begin();
  dht.setup(DHTPIN, DHTesp::DHT22);
  connectWifi();

  Serial.println("Mounting file system...");
  bool isFSMounted = SPIFFS.begin();
  if (!isFSMounted) {
    Serial.println("Formatting file system...");
    drawProgress(50, "Formatting file system");
    SPIFFS.format();
  }
  drawProgress(100, "Formatting done");
  updateData();

}

// ==================================================================================================================================
//                                                    loop
// ==================================================================================================================================
void loop() {
  
  gfx.fillBuffer(MINI_BLACK);
  drawWifiQuality();
  drawWindows();  
  drawCurrentWeather();
  drawdht22();
  drawTime();
  gfx.commit();

  // Check if we should update weather information
  if (millis() - lastDownloadUpdate > 1000 * UPDATE_INTERVAL_SECS) {
    updateData();
    Serial.println ("UPDATE WITHER");
    lastDownloadUpdate = millis();
  }

  if (millis() - timing_led > 1000 * UPDATE_INTERVAL_SECS_LED) {

    val = analogRead(PIN_PHOTO_SENSOR);
    ledPower = map(val, 5, 900, 8, 180); // Преобразуем полученное значение в уровень PWM-сигнала. Чем меньше значение освещенности, тем меньше мощности мы должны подавать на светодиод через ШИМ.
    analogWrite(PIN_LED, ledPower); // Меняем яркость
    Serial.print("PHOTO_SENSOR:  "); Serial.print(val); Serial.print("     LedPower: "); Serial.println (ledPower);
    if ( val < 18 ) { 
      color = 1; 
      }
      else {
      color = 3;  
      }
    timing_led = millis();
  }

}

// ==================================================================================================================================
//                                                    updateData
// ==================================================================================================================================
void updateData() {
  time_t now;

  gfx.fillBuffer(MINI_BLACK);
  gfx.setFont(SansSerif_bold_11);

  drawProgress(10, "Updating time...");
  configTime(UTC_OFFSET * 3600, 0, NTP_SERVERS);
  while ((now = time(nullptr)) < NTP_MIN_VALID_EPOCH) {
    Serial.print(":");
    delay(300);
  }
  Serial.println();
  Serial.printf("Current time: %d\n", now);
  // calculate for time calculation how much the dst class adds.
  dstOffset = UTC_OFFSET * 3600 + dstAdjusted.time(nullptr) - now;
  Serial.printf("Time difference for DST: %d\n", dstOffset);

  drawProgress(50, "Updating conditions...");
  OpenWeatherMapCurrent *currentWeatherClient = new OpenWeatherMapCurrent();
  currentWeatherClient->setMetric(true);
  currentWeatherClient->setLanguage("en");
  currentWeatherClient->updateCurrentById(&currentWeather, OPEN_WEATHER_MAP_APP_ID, OPEN_WEATHER_MAP_LOCATION_ID);
  delete currentWeatherClient;
  currentWeatherClient = nullptr;
  Serial.printf("Free mem: %d\n",  ESP.getFreeHeap());

  delay(1000);
}

// ==================================================================================================================================
//                                                    drawProgress
// ==================================================================================================================================
void drawProgress(uint8_t percentage, String text) {

  gfx.fillBuffer(MINI_BLACK);
  gfx.drawPalettedBitmapFromPgm(47, 5, lilia_y);
  gfx.drawPalettedBitmapFromPgm(32, 155, BelFiore);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  gfx.setFont(SansSerif_bold_11);
  gfx.setColor(MINI_YELLOW);
  gfx.drawString(120, 250, text);
  gfx.setColor(MINI_LIGHTGREY);
  gfx.drawRect(10, 277, 240 - 20, 15);
  gfx.setColor(MINI_BLUE);
  gfx.fillRect(12, 279, 216 * percentage / 100, 11);

  gfx.commit();

}

// ==================================================================================================================================
//                                                    drawWindows
// ==================================================================================================================================
void drawWindows() {

  //рамка время
  gfx.setColor(MINI_LIGHTGREY);
  gfx.drawRect(5, 5, 230, 83);
  //рамка mbe-280
  gfx.drawRect(5, 93, 230, 78);
  //рамка погоды
  gfx.drawRect(5, 176, 230, 139);
  //полоса-разделить дата-месяц
  gfx.setColor(MINI_LIGHTGREY);
  gfx.fillRect(194, 52, 31, 2);
  //квадрат-подложка дата
  gfx.fillRect(177, 38, 13, 13);
  //квадрат-подложка месяц
  gfx.fillRect(177, 55, 13, 13);
  //Значки дата и месяц в квадратах-подложках
  gfx.setColor(MINI_BLACK);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(SansSerif_bold_11_rus);
  gfx.drawString(178, 37, "D");
  gfx.drawString(178, 55, "M");

  //Подложки для текста темп и влажности
  gfx.setColor(MINI_LIGHTGREY);
  gfx.fillRect(15, 101, 110, 15);
  gfx.fillRect(132, 101, 93, 15);
  //Текст темп и владж на подложки
  gfx.setColor(MINI_BLACK);
  gfx.setFont(SansSerif_bold_11_rus);
  gfx.drawString(20, 102, "TEMPERATURA");
  gfx.drawString(136, 102, "VLAJNOCTY");
  gfx.setColor(MINI_LIGHTGREY);
//  gfx.drawString(15, 184, String(val));
//  gfx.drawString(15, 205, String(ledPower));

}

// ==================================================================================================================================
//                                                    drawTime
// ==================================================================================================================================
void drawTime() {

  gfx.setColor(color);
  char time_str[11];
  char *dstAbbrev;
  time_t now = dstAdjusted.time(&dstAbbrev);
  struct tm * timeinfo = localtime (&now);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(DSEG7_Classic_Mini_Bold_48);
  sprintf(time_str, "%02d  %02d\n", timeinfo->tm_hour, timeinfo->tm_min);
  gfx.drawString(13, 26, time_str);

  if ((millis() - timing_sec > 1000) && sec_upd) { // Вместо 10000 подставьте нужное вам значение паузы
    timing_sec = millis();
    gfx.drawString(87, 26, ":");
    sec_upd = !sec_upd;
  }
  if ((millis() - timing_sec > 500) && !sec_upd) { // Вместо 10000 подставьте нужное вам значение паузы
    timing_sec = millis();
    gfx.drawString(87, 26, " ");
    sec_upd = !sec_upd;
  }

  //день недели - текст
  gfx.setColor(color);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(SansSerif_bold_11_rus);
  gfx.drawString(15, 11, WDAY_NAMES[timeinfo->tm_wday]);

  //Текст дата и месяц в квадратах-подложках
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.setFont(DSEG7_Classic_Mini_Regular_20);
  gfx.drawString(224, 27, String(timeinfo->tm_mday));
  gfx.drawString(224, 56, String((timeinfo->tm_mon) + 1));

}

// ==================================================================================================================================
//                                                    drawCurrentWeather
// ==================================================================================================================================
void drawCurrentWeather() {

  gfx.drawPalettedBitmapFromPgm(16, 183, getMiniMeteoconIconFromProgmem(currentWeather.icon));

  gfx.setColor(MINI_LIGHTGREY);
  gfx.fillRect(84, 183, 64, 15);
  gfx.fillRect(161, 183, 64, 15);
  gfx.setColor(MINI_BLACK);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(SansSerif_bold_11);
  gfx.drawString(98, 184, "TEMP");
  gfx.drawString(177, 184, "FEEL");

  gfx.setColor(MINI_LIGHTGREY);
  gfx.fillRect(16, 239, 55, 15);
  gfx.fillRect(84, 239, 64, 15);
  gfx.fillRect(161, 239, 64, 15);
  gfx.setColor(MINI_BLACK);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setFont(SansSerif_bold_11);
  gfx.drawString(25, 240, "WIND");
  gfx.drawString(100, 240, "HUM");
  gfx.drawString(177, 240, "PRES");

  gfx.setColor(color);
  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.setFont(DSEG7_Classic_Mini_Bold_30);
  
  gfx.drawString(138, 200, String(currentWeather.temp, 0));
  int new_temperature = currentWeather.temp;

  if ( new_temperature == temprtre )  
  {
  temperature_sign = 0;
  } else {
  temperature_sign = ( new_temperature > temprtre ) ? 1 : -1;
  }
  temprtre = new_temperature;
  
  gfx.drawString(215, 200, String(currentWeather.feels_like, 0) );
  gfx.setFont(Dialog_plain_30);
  gfx.drawString(151, 196, "°" );
  gfx.drawString(228, 196, "°" );

  gfx.setFont(DSEG7_Classic_Mini_Bold_24);
  gfx.drawString(56, 257, String((currentWeather.windSpeed), 0));
  String hum_now = String(currentWeather.humidity);
  gfx.drawString(127, 257, String(hum_now));
  
  String pres_str_now = String(currentWeather.pressure) ;
  int new_pressure = currentWeather.pressure;

  if ( new_pressure == pressure )  
  {
  pressure_sign = 0;
  } else {
  pressure_sign = ( new_pressure > pressure ) ? 1 : -1;
  }
  pressure = new_pressure;

  int pres_mm_now = pres_str_now.toInt() * 0.75 ;
  gfx.drawString(223, 257, String(pres_mm_now));

  gfx.setFont(Dialog_bold_18);
  gfx.drawString(145, 265, "%" );
  gfx.setFont(SansSerif_bold_11);
  gfx.drawString(69, 258, "M" );
  gfx.fillRect(57, 271, 13, 2);
  gfx.drawString(68, 272, "S" );

  //Подложка для описания погоды
  gfx.setColor(MINI_LIGHTGREY);
  gfx.drawRect(13, 291, 213, 18);
  //Текст описания погоды
  gfx.setFont(SansSerif_bold_11_rus);
  gfx.setTextAlignment(TEXT_ALIGN_CENTER);
  String wd0 = String (currentWeather.description);
  gfx.drawString(120, 294, getRusTextMeteo(wd0));


  gfx.setColor(MINI_LIGHTGREY);
  gfx.setFont(WIFI_14);

  // temperature trend
  switch ( temperature_sign )
  {
    case 1:
    gfx.drawString(223, 5, "8"); //вверх
      break;
    case -1:
    gfx.drawString(223, 8, "9"); //вниз
      break;
    default:
      break;
  }

  // pressure trend
  switch ( pressure_sign )
  {
    case 1:
    gfx.drawString(223, 15, "8"); //вверх
      break;
    case -1:
    gfx.drawString(223, 18, "9"); //вниз
      break;
    default:
      break;
  }


}


// ==================================================================================================================================
//                                                    drawdht22
// ==================================================================================================================================
void drawdht22() {

    if (!frst) {
        hum = dht.getHumidity();
        temp = dht.getTemperature();
        frst = 1;
    }

if ( millis() - timing_dht > (UPDATE_INTERVAL_DHT * 1000) ) { // Update every 30 sec
      timing_dht = millis();

      hum_0 = dht.getHumidity();
      temp_0 = dht.getTemperature();

  if ( isnan(hum_0) ) {
    Serial.println("Failed to read from DHT sensor!");
    UPDATE_INTERVAL_DHT = UPDATE_INTERVAL_ERR_DHT;
    dht.setup(DHTPIN, DHTesp::DHT22);
    delay (100);
    return;
  }
      UPDATE_INTERVAL_DHT = UPDATE_INTERVAL_SECS_DHT ;
      hum = hum_0;
      temp = temp_0;
      bme_upd = true;
  }
  
  int tm_0 = temp.indexOf(".");
  String tm_2 = temp.substring(0, tm_0);
  String tm_3 = temp.substring(tm_0 + 1, tm_0 + 2);

  String hm = String ( round (hum)*1);
  int hm2 = hm.length();
  if (hm2 == 5) { hm.remove(hm2 - 3);  }
    
  
if (bme_upd) {
  
  if (millis() - timing_1s > (UPDATE_INTERVAL_1_SECS * 1000)) { // Подставьте нужное вам значение паузы
  timing_1s = millis();
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.setFont(DSEG7_Classic_Mini_Bold_48);
  gfx.drawString(111, 79, ".");
  }
  
}

  gfx.setFont(DSEG7_Classic_Mini_Bold_40);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(color);


  gfx.drawString(20, 121, tm_2);
  gfx.drawString(138, 121, hm);

  gfx.setFont(Dialog_plain_36);
  gfx.drawString(75, 130, ".");

  gfx.setFont(DSEG7_Classic_Mini_Bold_30);
  gfx.drawString(88, 131, tm_3);

  gfx.setFont(Dialog_plain_30);
  gfx.drawString(109, 117, "°");
  gfx.drawString(198, 134, "%");

  bme_upd = false;

}
/*
// ==================================================================================================================================
//                                                    drawbme280
// ==================================================================================================================================
void drawbme280() {

  BME280::TempUnit tempUnit(BME280::TempUnit_Celsius);
  BME280::PresUnit presUnit(BME280::PresUnit_bar);


  if (millis() - timing_280 > (UPDATE_INTERVAL_SECS_BME280 * 1000)) { // Update every 30 sec
    timing_280 = millis();
    bme.read(pres, temp, hum, tempUnit, presUnit);
    bme_upd = true;
  }

  String tm = String (temp * 1);
  int tm_0 = tm.indexOf(".");
  String tm_2 = tm.substring(0, tm_0);
  String tm_3 = tm.substring(tm_0 + 1, tm_0 + 2);

  String hm = String ( round (hum * 1));
  int hm2 = hm.length();
  if (hm2 == 5) {
    hm.remove(hm2 - 3);
  }
  String pr = String (round (pres * 750.06168)) ;
  int pr2 = pr.length();
  if (pr2 == 6) {
    pr.remove(pr2 - 3);
  }
  

if (bme_upd) {

  if (millis() - timing_1s > (UPDATE_INTERVAL_1_SECS * 1000)) { // Подставьте нужное вам значение паузы
  timing_1s = millis();
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_YELLOW);
  gfx.setFont(DSEG7_Classic_Mini_Bold_48);
  gfx.drawString(111, 79, ".");
  }
  
}

  gfx.setFont(DSEG7_Classic_Mini_Bold_40);
  gfx.setTextAlignment(TEXT_ALIGN_LEFT);
  gfx.setColor(MINI_BLUE);
  gfx.drawString(20, 121, tm_2);
  gfx.drawString(138, 121, hm);

  gfx.setFont(Dialog_plain_36);
  gfx.drawString(75, 130, ".");

  gfx.setFont(DSEG7_Classic_Mini_Bold_30);
  gfx.drawString(88, 131, tm_3);

  gfx.setFont(Dialog_plain_30);
  gfx.drawString(109, 117, "°");
  gfx.drawString(198, 134, "%");

    bme_upd = false;

}
*/
// ==================================================================================================================================
//                                                    drawWifiQuality
// ==================================================================================================================================
void drawWifiQuality() {

  if (wifi_upd) {
  qual_dbm = WiFi.RSSI();
    wifi_upd = false;
  }

  if (millis() - timing > (UPDATE_INTERVAL_SECS_WIFI * 1000)) { // Подставьте нужное вам значение паузы
    timing = millis();
    //    Serial.println ("10 seconds");
  qual_dbm = WiFi.RSSI();
  }

  String q_dbm = String (qual_dbm);

  if (qual_dbm <= RSSI_MIN)
  {
    quality = 0;
  }
  else if (qual_dbm >= RSSI_MAX)
  {
    quality = 100;
  }
  else
  {
    quality = 2 * (qual_dbm + 100);
  }

  gfx.setTextAlignment(TEXT_ALIGN_RIGHT);
  gfx.setColor(MINI_LIGHTGREY);
  gfx.setFont(SansSerif_bold_11);
  gfx.drawString(198, 11, String(quality) + "%");

  drawWifiLine (quality);

}

// ==================================================================================================================================
//                                                    drawWifiLine
// ==================================================================================================================================
void drawWifiLine (int quality) {

  gfx.setColor(MINI_LIGHTGREY);
  gfx.setFont(WIFI_14);

  if (quality >= 70) {
  gfx.drawString(223, 5, "4");
  }
  else if (quality >= 60) {
  gfx.drawString(223, 5, "3");
  }
  else if (quality >= 50) {
  gfx.drawString(223, 5, "2");
  }
  else if (quality >= 40) {
  gfx.drawString(223, 5, "1");
  }
  else if (quality < 40) {
    gfx.setColor(MINI_YELLOW);
  gfx.drawString(223, 5, "0");
  }
}
