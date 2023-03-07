#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TimeLib.h>

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include "fonts/FreeMonoBold48pt7b.h"
#include "fonts/FreeMonoBold64pt7b.h"

#include <FS.h>

typedef struct {
  char ssid[32];
  char password[32];
  char OWLocation[32];
  char OWApiKey[33];
} Settings;

struct forecastDay {
  char day[4];
  int morningTemp;
  char morningWeather[4] = "";
  int afternoonTemp;
  char afternoonWeather[4] = "";
};

struct State {
  unsigned long dt;
  int offset;
  int currentTemp;
  char currentWeather[4];
  int laterTime;
  int laterTemp;
  char laterWeather[4];
  char todaySunrise[6];
  char todaySunset[6];
  forecastDay forecast[3];
};

typedef GxEPD2_3C < GxEPD2_583c_Z83, GxEPD2_583c_Z83::HEIGHT/2> Display;

void drawBitmapFromSpiffs(const char *filename, int16_t x, int16_t y, bool with_color = true);
void refreshData(byte refresh);
void printState();
void refreshDisplay(byte refresh);
void loadSettings(Settings* settings);
void connectToWifi(Settings *settings);
void disconnectWifi();
void setClock();
void refreshWeather(Settings *settings, WiFiClientSecure *client);
void refreshForecast(Settings *settings, WiFiClientSecure *client);