#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <time.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <TimeLib.h>

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
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
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

  Serial.println(F("WiFi connected"));
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
}

void refreshWeather(BearSSL::WiFiClientSecure *bear) {
  char url[128];
  sprintf(url, openWeatherApi, weatherEndpoint, settings.OWLocation, settings.OWApiKey, "");
  Serial.println(url);

  http.begin(dynamic_cast<WiFiClient&>(*bear), url);
  int httpCode = http.GET();
  Serial.println(httpCode);

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, http.getString());

  float temp = doc["main"]["temp"];
  const char *weatherIcon = doc["weather"][0]["icon"];
  unsigned long sunrise = (int)(doc["sys"]["sunrise"]) + (int)(doc["timezone"]);
  unsigned long sunset = (int)(doc["sys"]["sunset"]) + (int)(doc["timezone"]);

  char sunrise_t[6];
  sprintf(sunrise_t, "%02d:%02d", hour(sunrise), minute(sunrise));
  char sunset_t[6];
  sprintf(sunset_t, "%02d:%02d", hour(sunset), minute(sunset));

  Serial.print("temp: ");
  Serial.println(temp);
  Serial.print("weatherIcon: ");
  Serial.println(weatherIcon);
  Serial.print("sunrise: ");
  Serial.println(sunrise_t);
  Serial.print("sunset: ");
  Serial.println(sunset_t);

}

struct forecastDay {
  char day[4];
  float morningTemp;
  char morningWeather[4] = "";
  float afternoonTemp;
  char afternoonWeather[4] = "";
};

float todayAfternoonTemp;
char todayAfternoonWeather[4] = "";

char * weekdays[7] = {
  "Sun",
  "Mon",
  "Tue",
  "Wed",
  "Thu",
  "Fri",
  "Sat"
};

void refreshForecast(BearSSL::WiFiClientSecure *bear) {
  char url[128];
  sprintf(url, openWeatherApi, forecastEndpoint, settings.OWLocation, settings.OWApiKey, "&cnt=30");
  Serial.println(url);

  http.begin(dynamic_cast<WiFiClient&>(*bear), url);
  // http.useHTTP10(true);
  int httpCode = http.GET();
  Serial.println(httpCode);

  DynamicJsonDocument doc(16384);  // https://arduinojson.org/v6/assistant/
  DeserializationError error = deserializeJson(doc, http.getStream());
  if (error)
    Serial.println(error.f_str());

  char buff[32];
  forecastDay forecast[3];
  int dayIndex = 0;

  unsigned int now = millis() / 1000  + (int)(doc["city"]["timezone"]);

  for (JsonObject list_item : doc["list"].as<JsonArray>()) {
    unsigned long t = (int)(list_item["dt"]) + (int)(doc["city"]["timezone"]);
    if ((hour(t) >= 7 && hour(t) <10) || (hour(t) >= 16 && hour(t) <19)) {
      if(day(t) == day(now)) {
        todayAfternoonTemp = list_item["main"]["temp"];
        strcpy(todayAfternoonWeather, list_item["weather"][0]["icon"]);
      } else {
        if (strcmp(forecast[dayIndex].morningWeather, "") == 0) {
          strcpy(forecast[dayIndex].day, weekdays[weekday(t) - 1]);
          forecast[dayIndex].morningTemp = list_item["main"]["temp"];
          strcpy(forecast[dayIndex].morningWeather, list_item["weather"][0]["icon"]);
        } else {
          forecast[dayIndex].afternoonTemp = list_item["main"]["temp"];
          strcpy(forecast[dayIndex].afternoonWeather, list_item["weather"][0]["icon"]);
          dayIndex++;
        }
      }
    }
  }
  Serial.println(todayAfternoonTemp);
  Serial.println(todayAfternoonWeather);
  for (int i=0; i<3; i++) {
    Serial.print("forecast for: ");
    Serial.println(forecast[i].day);
    Serial.print("m temp: "); Serial.println(forecast[i].morningTemp);
    Serial.print("m icon: "); Serial.println(forecast[i].morningWeather);
    Serial.print("a temp: "); Serial.println(forecast[i].afternoonTemp);
    Serial.print("a icon: "); Serial.println(forecast[i].afternoonWeather);
  }
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
  refreshForecast(bear);
}

void loop() {
  Serial.println("Awake, going to sleep");
  // ESP.deepSleep(100*1000*1000);
  delay(100000);
}
