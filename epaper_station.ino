#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <CertStoreBearSSL.h>
#include <time.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

struct Settings {
  char ssid[32];
  char password[32];
  char OWLocation[32];
  char OWApiKey[33];
};
Settings settings;

const char* openWeatherApi = "https://api.openweathermap.org/data/2.5/%s?q=%s&units=metric&APPID=%s";
const char* weatherEndpoint = "weather";
const char* forecastEndpoint = "forecast5";
char weatherUrl[128];

BearSSL::CertStore certStore;

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

  HTTPClient http;
  Serial.println(weatherUrl);
  sprintf(weatherUrl, openWeatherApi, weatherEndpoint, settings.OWLocation, settings.OWApiKey);
  Serial.println(weatherUrl);

  http.begin(dynamic_cast<WiFiClient&>(*bear), weatherUrl);
  int httpCode = http.GET();
  Serial.println(httpCode);

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, http.getString());

  double temp = doc["main"]["temp"];
  Serial.print("temp: ");
  Serial.println(temp);

}

void loop() {
  Serial.println("Awake, going to sleep");
  // ESP.deepSleep(100*1000*1000);
  delay(100000);
}
