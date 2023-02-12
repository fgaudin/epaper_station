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

GxEPD2_3C < GxEPD2_583c_Z83, GxEPD2_583c_Z83::HEIGHT / 4 > display(GxEPD2_583c_Z83(/*CS=D8*/ SS, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4)); // GDEW0583Z83 648x480, GD7965

struct Settings {
  char ssid[32];
  char password[32];
  char OWLocation[32];
  char OWApiKey[33];
};
Settings settings;

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
  int currentTemp;
  char currentWeather[4];
  int afternoonTemp;
  char afternoonWeather[4];
  char todaySunrise[6];
  char todaySunset[6];
  forecastDay forecast[3];
};

State state;

BearSSL::CertStore certStore;

HTTPClient http;

void loadSettings() {
  File file = LittleFS.open("/settings.json", "r");
  if(!file){
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
  strlcpy(settings.ssid,          // <- destination
          doc["ssid"],            // <- source
          sizeof(settings.ssid)); // <- destination's capacity
  strlcpy(settings.password,
          doc["password"],
          sizeof(settings.password));
  strlcpy(settings.OWLocation,
          doc["OWLocation"],
          sizeof(settings.OWLocation));
  strlcpy(settings.OWApiKey,
          doc["OWApiKey"],
          sizeof(settings.OWApiKey));
    

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
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print(F("Current time: "));
  Serial.print(asctime(&timeinfo));
}

void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.ssid, settings.password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  Serial.println(WiFi.localIP());
}

void refreshWeather(BearSSL::WiFiClientSecure *bear) {
  char url[128];
  sprintf(url, openWeatherApi, weatherEndpoint, settings.OWLocation, settings.OWApiKey, "");

  http.begin(dynamic_cast<WiFiClient&>(*bear), url);
  int httpCode = http.GET();

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, http.getString());

  state.currentTemp = round((float)doc["main"]["temp"]);
  strcpy(state.currentWeather, doc["weather"][0]["icon"]);
  unsigned int sunrise = (int)(doc["sys"]["sunrise"]) + (int)(doc["timezone"]);
  unsigned int sunset = (int)(doc["sys"]["sunset"]) + (int)(doc["timezone"]);

  sprintf(state.todaySunrise, "%02d:%02d", hour(sunrise), minute(sunrise));
  sprintf(state.todaySunset, "%02d:%02d", hour(sunset), minute(sunset));
}

void refreshForecast(BearSSL::WiFiClientSecure *bear) {
  char url[128];
  sprintf(url, openWeatherApi, forecastEndpoint, settings.OWLocation, settings.OWApiKey, "&cnt=30");
  Serial.println(url);

  http.begin(dynamic_cast<WiFiClient&>(*bear), url);
  http.useHTTP10(true);
  int httpCode = http.GET();
  Serial.println(httpCode);

  DynamicJsonDocument doc(16384);  // https://arduinojson.org/v6/assistant/
  DeserializationError error = deserializeJson(doc, http.getStream());
  if (error)
    Serial.println(error.f_str());

  int dayIndex = 0;

  unsigned int now = millis() / 1000  + (int)(doc["city"]["timezone"]);

  for (JsonObject list_item : doc["list"].as<JsonArray>()) {
    unsigned long t = (int)(list_item["dt"]) + (int)(doc["city"]["timezone"]);
    if ((hour(t) >= 7 && hour(t) <10) || (hour(t) >= 16 && hour(t) <19)) {
      if(day(t) == day(now)) {
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
        }
      }
    }
  }
}

void displayWeather()
{
  const char * HelloWorld = "Francois is here";
  display.setRotation(0);
  display.setFont(&FreeMonoBold24pt7b);
  display.setTextColor(GxEPD_RED);
  int16_t tbx, tby; uint16_t tbw, tbh;
  display.getTextBounds(HelloWorld, 0, 0, &tbx, &tby, &tbw, &tbh);
  // center the bounding box by transposition of the origin:
  uint16_t x = ((display.width() - tbw) / 2) - tbx;
  uint16_t y = ((display.height() - tbh) / 2) - tby;
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print(state.currentTemp);
  }
  while (display.nextPage());
}

void setup() {
  Serial.begin(115200);
  
  if(!LittleFS.begin()){
    Serial.println(F("An Error has occurred while mounting LittleFS"));
    return;
  }

  loadSettings();
  connectToWifi();
  setClock();

  int numCerts = certStore.initCertStore(LittleFS, PSTR("/certs.idx"), PSTR("/certs.ar"));
  Serial.printf("Number of CA certs read: %d\n", numCerts);
  if (numCerts == 0) {
    Serial.printf("No certs found. Did you run certs-from-mozilla.py and upload the LittleFS directory before running?\n");
    return;  // Can't connect to anything w/o certs!
  }

  BearSSL::WiFiClientSecure *bear = new BearSSL::WiFiClientSecure();
  // Integrate the cert store with this connection
  bear->setCertStore(&certStore);

  refreshWeather(bear);
  // refreshForecast(bear);

  delete bear;
  bear = NULL;

  display.init(115200, true, 2, false);
  displayWeather();
  display.hibernate();
}

void loop() {
  Serial.println("Awake, going to sleep");
  // ESP.deepSleep(100*1000*1000);
  delay(100000);
}
