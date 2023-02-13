#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <time.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TimeLib.h>

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>

typedef struct {
  char ssid[32];
  char password[32];
  char OWLocation[32];
  char OWApiKey[33];
} Settings;

const char* openWeatherApi = "https://api.openweathermap.org/data/2.5/%s?q=%s&units=metric&APPID=%s%s";
const char* weatherEndpoint = "weather";
const char* forecastEndpoint = "forecast";

const char* sun = "Sun";
const char* mon = "Mon";
const char* tue = "Tue";
const char* wed = "Wed";
const char* thu = "Thu";
const char* fri = "Fri";
const char* sat = "Sat";

const char *const weekdays[] = {sun, mon, tue, wed, thu, fri, sat};

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
  int afternoonTemp;
  char afternoonWeather[4];
  char todaySunrise[6];
  char todaySunset[6];
  forecastDay forecast[3];
};

State state;

void setup() {
  Serial.begin(115200);

  byte refresh = B011;  // bus|forecast|weather
  Serial.println("");
  Serial.print(F("Setup start: "));
  refreshData(refresh);
  printState();

  Serial.println(ESP.getFreeHeap(), DEC);
  refreshDisplay(refresh);

  Serial.print(F("Setup end: "));
  Serial.println(ESP.getFreeHeap(), DEC);
}

void printState() {
  Serial.print(F("dt: "));
  Serial.println(state.dt);
  Serial.print(F("offset: "));
  Serial.println(state.offset);
  Serial.print(F("temp: "));
  Serial.println(state.currentTemp);
  Serial.print(F("weather: "));
  Serial.println(state.currentWeather);
  Serial.print(F("afternoon temp: "));
  Serial.println(state.afternoonTemp);
  Serial.print(F("afternoon weather: "));
  Serial.println(state.afternoonWeather);
  Serial.print(F("sunset: "));
  Serial.println(state.todaySunset);
  Serial.print(F("sunrise: "));
  Serial.println(state.todaySunrise);
  for (int i = 0; i < 3; i++) {
    Serial.printf("day D+%d: ", i + 1);
    Serial.println(state.forecast[i].day);
    Serial.printf("temp morning D+%d: ", i + 1);
    Serial.println(state.forecast[i].morningTemp);
    Serial.printf("weather morning  D+%d: ", i + 1);
    Serial.println(state.forecast[i].morningWeather);
    Serial.printf("temp afternoon D+%d: ", i + 1);
    Serial.println(state.forecast[i].afternoonTemp);
    Serial.printf("weather afternoon  D+%d: ", i + 1);
    Serial.println(state.forecast[i].afternoonWeather);
  }
}

void refreshData(byte refresh) {
  if (refresh <= 0) {
    return;
  }

  if (!LittleFS.begin()) {
    Serial.println(F("An Error has occurred while mounting LittleFS"));
    return;
  }
  Settings settings;
  loadSettings(&settings);

  connectToWifi(&settings);
  setClock();

  BearSSL::CertStore certStore;

  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0) {
    Serial.println(F("No certs found. Did you run certs-from-mozilla.py and upload the LittleFS directory before running?\n"));
    return;
  }

  BearSSL::WiFiClientSecure *bear = new BearSSL::WiFiClientSecure();
  bear->setCertStore(&certStore);

  if (refresh & 1) refreshWeather(&settings, bear);
  Serial.println(ESP.getFreeHeap(), DEC);
  if (refresh >> 1 & 1) refreshForecast(&settings, bear);

  delete bear;
  bear = NULL;

  disconnectWifi();
  LittleFS.end();
}

void displayWeather(GxEPD2_3C < GxEPD2_583c_Z83, GxEPD2_583c_Z83::HEIGHT / 4 > display)
{
  display.setRotation(0);
  display.setFont(&FreeMonoBold24pt7b);
  display.setTextColor(GxEPD_BLACK);
  int16_t tbx, tby; uint16_t tbw, tbh;
  uint16_t x = 30;
  uint16_t y = 50;
  display.setFullWindow();
  display.firstPage();

  unsigned long now_t = state.dt - state.offset;
  Serial.print(F("now: "));
  Serial.println(now_t);
  char date[11];
  sprintf(date, "%02d/%02d/%02d", month(now_t), day(now_t), year(now_t));
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print(weekdays[weekday(now_t) - 1]);
    display.setCursor(x + 100, y);
    display.print(date);
    display.setCursor(x, y + 50);
    display.print(state.currentTemp);
    display.setCursor(x, y + 100);
    display.print(state.currentWeather);
  }
  while (display.nextPage());
}

void refreshDisplay(byte refresh) {
  if (refresh <= 0) {
    return;
  }

  GxEPD2_3C < GxEPD2_583c_Z83, GxEPD2_583c_Z83::HEIGHT / 4 > display(GxEPD2_583c_Z83(/*CS=D8*/ SS, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4)); // GDEW0583Z83 648x480, GD7965
  display.init(115200, true, 2, false);
  displayWeather(display);
  display.hibernate();
}

void loadSettings(Settings* settings) {
  File file = LittleFS.open("/settings.json", "r");
  if (!file) {
    Serial.println(F("Failed to open settings"));
    return;
  }

  // Allocate a temporary JsonDocument
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/v6/assistant to compute the capacity.
  StaticJsonDocument<256> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error)
    Serial.println(F("Failed to read file"));

  // Copy values from the JsonDocument to the Config
  strlcpy(settings->ssid,          // <- destination
          doc["ssid"],            // <- source
          sizeof(settings->ssid)); // <- destination's capacity
  strlcpy(settings->password,
          doc["password"],
          sizeof(settings->password));
  strlcpy(settings->OWLocation,
          doc["OWLocation"],
          sizeof(settings->OWLocation));
  strlcpy(settings->OWApiKey,
          doc["OWApiKey"],
          sizeof(settings->OWApiKey));

  // Close the file (Curiously, File's destructor doesn't close the file)
  file.close();
  Serial.println(F("Settings loaded"));
}

void setClock() {
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print(F("Waiting for NTP time sync: "));
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(F("."));
    now = time(nullptr);
  }
  Serial.println(F(""));
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print(F("Current time: "));
  Serial.print(asctime(&timeinfo));
}

void connectToWifi(Settings *settings) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings->ssid, settings->password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }
  // Serial.println("");

  // Serial.println(WiFi.localIP());
}

void disconnectWifi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

void refreshWeather(Settings *settings, BearSSL::WiFiClientSecure *bear) {
  char url[128];
  sprintf(url, openWeatherApi, weatherEndpoint, settings->OWLocation, settings->OWApiKey, "");
  Serial.println(url);

  HTTPClient http;
  http.begin(dynamic_cast<WiFiClient&>(*bear), url);
  http.useHTTP10(true);
  int httpCode = http.GET();

  StaticJsonDocument<1024> doc;
  deserializeJson(doc, http.getStream());

  state.dt = doc["dt"];
  state.offset = doc["timezone"];
  state.currentTemp = round((float)doc["main"]["temp"]);
  strcpy(state.currentWeather, doc["weather"][0]["icon"]);
  unsigned int sunrise = (int)(doc["sys"]["sunrise"]) + (int)(doc["timezone"]);
  unsigned int sunset = (int)(doc["sys"]["sunset"]) + (int)(doc["timezone"]);

  sprintf(state.todaySunrise, "%02d:%02d", hour(sunrise), minute(sunrise));
  sprintf(state.todaySunset, "%02d:%02d", hour(sunset), minute(sunset));
}

void refreshForecast(Settings *settings, BearSSL::WiFiClientSecure *bear) {
  char url[128];
  sprintf(url, openWeatherApi, forecastEndpoint, settings->OWLocation, settings->OWApiKey, "&cnt=30");
  Serial.println(url);

  HTTPClient http;
  http.begin(dynamic_cast<WiFiClient&>(*bear), url);
  http.useHTTP10(true);
  int httpCode = http.GET();
  Serial.println(httpCode);

  DynamicJsonDocument doc(4096);  // https://arduinojson.org/v6/assistant/
  StaticJsonDocument<160> filter;
  filter["city"]["timezone"] = true;

  JsonObject filter_list_0 = filter["list"].createNestedObject();
  filter_list_0["dt"] = true;
  filter_list_0["main"]["temp"] = true;
  filter_list_0["weather"][0]["icon"] = true;

  DeserializationError error = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  if (error)
    Serial.println(error.f_str());

  int dayIndex = 0;

  unsigned int now = millis() / 1000  + (int)(doc["city"]["timezone"]);

  int offset = doc["city"]["timezone"];

  for (JsonObject list_item : doc["list"].as<JsonArray>()) {
    unsigned long t = ((int)list_item["dt"]) + offset;
    if ((hour(t) >= 7 && hour(t) < 10) || (hour(t) >= 16 && hour(t) < 19)) {
      if (day(t) == day(now)) {
        state.afternoonTemp = list_item["main"]["temp"];
        strcpy(state.afternoonWeather, list_item["weather"][0]["icon"]);
      } else {
        if (strcmp(state.forecast[dayIndex].morningWeather, "") == 0) {
          strcpy(state.forecast[dayIndex].day, weekdays[weekday(t) - 1]);
          state.forecast[dayIndex].morningTemp = list_item["main"]["temp"];
          strcpy(state.forecast[dayIndex].morningWeather, list_item["weather"][0]["icon"]);
        } else {
          state.forecast[dayIndex].afternoonTemp = list_item["main"]["temp"];
          strcpy(state.forecast[dayIndex].afternoonWeather, list_item["weather"][0]["icon"]);
          dayIndex++;
          if (dayIndex >= 3) {
            break;
          }
        }
      }
    }
  }
  Serial.println(F("done"));
}

void loop() {
  Serial.println("Loop");
  Serial.println(ESP.getFreeHeap(), DEC);
  // ESP.deepSleep(100*1000*1000);
  delay(100000);
}
