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
#include "fonts/FreeMonoBold48pt7b.h"
#include "fonts/FreeMonoBold64pt7b.h"

#include <FS.h>
#define FileClass fs::File

typedef struct {
  char ssid[32];
  char password[32];
  char OWLocation[32];
  char OWApiKey[33];
} Settings;

const char* openWeatherApi = "https://api.openweathermap.org/data/2.5/%s?q=%s&units=metric&APPID=%s%s";
const char* weatherEndpoint = "weather";
const char* forecastEndpoint = "forecast";

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

typedef GxEPD2_3C < GxEPD2_583c_Z83, GxEPD2_583c_Z83::HEIGHT / 4 > Display;

void drawBitmapFromSpiffs(Display display, const char *filename, int16_t x, int16_t y, bool with_color = true);

void setup() {
  Serial.begin(115200);
  if (!LittleFS.begin()) {
    Serial.println(F("An Error has occurred while mounting LittleFS"));
    return;
  }

  byte refresh = B011;  // bus|forecast|weather
  Serial.println("");
  Serial.print(F("Setup start: "));
  refreshData(refresh);
  printState();

  Serial.println(ESP.getFreeHeap(), DEC);
  refreshDisplay(refresh);

  LittleFS.end();
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

void toWeekdayStr(char * dest, int weekday /* 1-indexed */) {
  const char *const weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  strcpy(dest, weekdays[weekday-1]);
}

void toMonthStr(char * dest, int monthIdx /* 1-indexed */) {
  const char *const months[] = {
    "Jan",
    "Feb",
    "Mar",
    "Apr",
    "May",
    "Jun",
    "Jul",
    "Aug",
    "Sept",
    "Oct",
    "Nov",
    "Dec"
  };
  strcpy(dest, months[monthIdx-1]);
}

void refreshData(byte refresh) {
  if (refresh <= 0) {
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
}

void clearDisplay(Display display) {
  display.setFullWindow();
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);
  }
  while (display.nextPage());
}

void displayDate(Display display) {
  uint16_t w = 300;
  uint16_t h = 280;
  

  uint16_t x = 0;
  uint16_t y = 0;
  int16_t tbx, tby; uint16_t tbw, tbh;

  display.setRotation(0);
  display.setPartialWindow(0, 0, w, h);
  display.firstPage();

  unsigned long now_t = state.dt + state.offset;
  
  char weekdayStr[4] = "";
  toWeekdayStr(weekdayStr, weekday(now_t));
  char monthStr[4] = "";
  toMonthStr(monthStr, month(now_t));
  char dayStr[3];
  sprintf(dayStr, "%02d", day(now_t));

  int leftCol = 0;

  do
  {
    display.fillScreen(GxEPD_WHITE);

    // day of week
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeMonoBold64pt7b);
    display.getTextBounds(weekdayStr, 0, 0, &tbx, &tby, &tbw, &tbh);

    leftCol = tbw + 10;
    x = (leftCol - tbw) / 2;
    y = tbh + 15;
    display.setCursor(x, y);
    display.print(weekdayStr);

    // month
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold24pt7b);
    display.getTextBounds(monthStr, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = (leftCol - tbw) / 2;
    y = y + tbh + 30;
    display.setCursor(x, y);
    display.print(monthStr);

    // day
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeMonoBold64pt7b);
    display.getTextBounds(dayStr, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = (leftCol - tbw) / 2;
    y = y + tbh + 30;
    display.setCursor(x, y);
    display.print(dayStr);
  }
  while (display.nextPage());
}

void displayWeather(Display display)
{
  uint16_t x = 0;
  uint16_t y = 320;
  uint16_t w = 300;
  uint16_t h = display.height() - y - 1;

  int16_t tbx, tby; uint16_t tbw, tbh;
  
  char temp[4] = "";
  sprintf(temp,"% 2d", state.currentTemp);
  
  const int bigIcons = 96;

  display.setRotation(0);
  display.setPartialWindow(x, y, w, h);
  display.firstPage();
  
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // temp
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold48pt7b);
    display.getTextBounds(temp, 0, 0, &tbx, &tby, &tbw, &tbh);
    tbx = 5;
    tby = y + tbh + 10;
    display.setCursor(tbx, tby);
    display.print(temp);

    tbx = tbx + tbw + 40;
    tby = tby - bigIcons + 10;
    char icon[11] = "";
    strcat(icon, state.currentWeather);
    strcat(icon, "_96");
    strcat(icon, ".bmp");
    drawBitmapFromSpiffs(display, icon, tbx, tby, false);
  }
  while (display.nextPage());
}


void displayForecast(Display display) {
  const uint16_t initial_x = 450;
  const uint16_t initial_y = 0;
  uint16_t x = initial_x;
  uint16_t y = initial_y;
  const uint16_t w = 648 - x - 1;
  const uint16_t h = 460;

  int16_t tbx, tby; uint16_t tbw, tbh;

  char weekdayStr[4] = "";
  char temp[4] = "";

  const int iconSize = 48;
  
  display.setRotation(0);
  display.setPartialWindow(x, y, w, h);
  display.firstPage();
  
  do
  {
    display.fillScreen(GxEPD_WHITE);

    for (int i=0; i<3; i++) {
      // day
      display.setTextColor(GxEPD_RED);
      display.setFont(&FreeMonoBold24pt7b);
      display.getTextBounds(state.forecast[i].day, 0, 0, &tbx, &tby, &tbw, &tbh);
      if (i == 0) {
        y = initial_y;
      }
      y = y + (tbh + 30);
      display.setCursor(x, y);
      display.print(state.forecast[i].day);

      // morning temp
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeMonoBold24pt7b);
      sprintf(temp,"% 3d", state.forecast[i].morningTemp);
      display.getTextBounds(temp, 0, 0, &tbx, &tby, &tbw, &tbh);
      y = y + (tbh + 10);
      display.setCursor(x, y);
      display.print(temp);

      // morning weather
      char icon[11] = "";
      strcpy(icon, state.forecast[i].morningWeather);
      strcat(icon, "_48");
      strcat(icon, ".bmp");
      drawBitmapFromSpiffs(display, icon, x + 120, y - iconSize + 10, false);

      // afternoon temp
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeMonoBold24pt7b);
      sprintf(temp,"% 3d", state.forecast[i].afternoonTemp);
      display.getTextBounds(temp, 0, 0, &tbx, &tby, &tbw, &tbh);
      y = y + (tbh + 10);
      display.setCursor(x, y);
      display.print(temp);

      // afternoon weather
      strcpy(icon, state.forecast[i].afternoonWeather);
      strcat(icon, "_48");
      strcat(icon, ".bmp");
      drawBitmapFromSpiffs(display, icon, x + 120, y - iconSize + 10, false);
    }
  }
  while (display.nextPage());
}

void refreshDisplay(byte refresh) {
  if (refresh <= 0) {
    return;
  }

  Display display(GxEPD2_583c_Z83(/*CS=D8*/ SS, /*DC=D3*/ 0, /*RST=D4*/ 2, /*BUSY=D2*/ 4)); // GDEW0583Z83 648x480, GD7965
  display.init(115200, true, 2, false);
  clearDisplay(display);
  displayDate(display);
  displayWeather(display);
  displayForecast(display);
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
          toWeekdayStr(state.forecast[dayIndex].day, weekday(t));
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

static const uint16_t input_buffer_pixels = 800; // may affect performance

static const uint16_t max_row_width = 1872; // for up to 7.8" display 1872x1404
static const uint16_t max_palette_pixels = 256; // for depth <= 8

uint8_t input_buffer[3 * input_buffer_pixels]; // up to depth 24
uint8_t output_row_mono_buffer[max_row_width / 8]; // buffer for at least one row of b/w bits
uint8_t output_row_color_buffer[max_row_width / 8]; // buffer for at least one row of color bits
uint8_t mono_palette_buffer[max_palette_pixels / 8]; // palette buffer for depth <= 8 b/w
uint8_t color_palette_buffer[max_palette_pixels / 8]; // palette buffer for depth <= 8 c/w
uint16_t rgb_palette_buffer[max_palette_pixels]; // palette buffer for depth <= 8 for buffered graphics, needed for 7-color display

uint16_t read16(fs::File& f)
{
  // BMP data is stored little-endian, same as Arduino.
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}

uint32_t read32(fs::File& f)
{
  // BMP data is stored little-endian, same as Arduino.
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

void drawBitmapFromSpiffs(Display display, const char *filename, int16_t x, int16_t y, bool with_color)
{
  fs::File file;
  bool valid = false; // valid format to be handled
  bool flip = true; // bitmap is stored bottom-to-top
  uint32_t startTime = millis();
  if ((x >= display.epd2.WIDTH) || (y >= display.epd2.HEIGHT)) return;
  Serial.println();
  Serial.print("Loading image '");
  Serial.print(filename);
  Serial.println('\'');
#if defined(ESP32)
  file = SPIFFS.open(String("/") + filename, "r");
#else
  file = LittleFS.open(filename, "r");
#endif
  if (!file)
  {
    Serial.print("File not found");
    return;
  }
  // Parse BMP header
  if (read16(file) == 0x4D42) // BMP signature
  {
    uint32_t fileSize = read32(file);
    uint32_t creatorBytes = read32(file); (void)creatorBytes; //unused
    uint32_t imageOffset = read32(file); // Start of image data
    uint32_t headerSize = read32(file);
    uint32_t width  = read32(file);
    int32_t height = (int32_t) read32(file);
    uint16_t planes = read16(file);
    uint16_t depth = read16(file); // bits per pixel
    uint32_t format = read32(file);
    if ((planes == 1) && ((format == 0) || (format == 3))) // uncompressed is handled, 565 also
    {
      Serial.print("File size: "); Serial.println(fileSize);
      Serial.print("Image Offset: "); Serial.println(imageOffset);
      Serial.print("Header size: "); Serial.println(headerSize);
      Serial.print("Bit Depth: "); Serial.println(depth);
      Serial.print("Image size: ");
      Serial.print(width);
      Serial.print('x');
      Serial.println(height);
      // BMP rows are padded (if needed) to 4-byte boundary
      uint32_t rowSize = (width * depth / 8 + 3) & ~3;
      if (depth < 8) rowSize = ((width * depth + 8 - depth) / 8 + 3) & ~3;
      if (height < 0)
      {
        height = -height;
        flip = false;
      }
      uint16_t w = width;
      uint16_t h = height;
      if ((x + w - 1) >= display.epd2.WIDTH)  w = display.epd2.WIDTH  - x;
      if ((y + h - 1) >= display.epd2.HEIGHT) h = display.epd2.HEIGHT - y;
      if (w <= max_row_width) // handle with direct drawing
      {
        valid = true;
        uint8_t bitmask = 0xFF;
        uint8_t bitshift = 8 - depth;
        uint16_t red, green, blue;
        bool whitish = false;
        bool colored = false;
        if (depth == 1) with_color = false;
        if (depth <= 8)
        {
          if (depth < 8) bitmask >>= depth;
          //file.seek(54); //palette is always @ 54
          file.seek(imageOffset - (4 << depth)); // 54 for regular, diff for colorsimportant
          for (uint16_t pn = 0; pn < (1 << depth); pn++)
          {
            blue  = file.read();
            green = file.read();
            red   = file.read();
            file.read();
            whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
            colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0)); // reddish or yellowish?
            if (0 == pn % 8) mono_palette_buffer[pn / 8] = 0;
            mono_palette_buffer[pn / 8] |= whitish << pn % 8;
            if (0 == pn % 8) color_palette_buffer[pn / 8] = 0;
            color_palette_buffer[pn / 8] |= colored << pn % 8;
          }
        }
        uint32_t rowPosition = flip ? imageOffset + (height - h) * rowSize : imageOffset;
        for (uint16_t row = 0; row < h; row++, rowPosition += rowSize) // for each line
        {
          uint32_t in_remain = rowSize;
          uint32_t in_idx = 0;
          uint32_t in_bytes = 0;
          uint8_t in_byte = 0; // for depth <= 8
          uint8_t in_bits = 0; // for depth <= 8
          uint8_t out_byte = 0xFF; // white (for w%8!=0 border)
          uint8_t out_color_byte = 0xFF; // white (for w%8!=0 border)
          uint32_t out_idx = 0;
          file.seek(rowPosition);
          for (uint16_t col = 0; col < w; col++) // for each pixel
          {
            // Time to read more pixel data?
            if (in_idx >= in_bytes) // ok, exact match for 24bit also (size IS multiple of 3)
            {
              in_bytes = file.read(input_buffer, in_remain > sizeof(input_buffer) ? sizeof(input_buffer) : in_remain);
              in_remain -= in_bytes;
              in_idx = 0;
            }
            switch (depth)
            {
              case 24:
                blue = input_buffer[in_idx++];
                green = input_buffer[in_idx++];
                red = input_buffer[in_idx++];
                whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
                colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0)); // reddish or yellowish?
                break;
              case 16:
                {
                  uint8_t lsb = input_buffer[in_idx++];
                  uint8_t msb = input_buffer[in_idx++];
                  if (format == 0) // 555
                  {
                    blue  = (lsb & 0x1F) << 3;
                    green = ((msb & 0x03) << 6) | ((lsb & 0xE0) >> 2);
                    red   = (msb & 0x7C) << 1;
                  }
                  else // 565
                  {
                    blue  = (lsb & 0x1F) << 3;
                    green = ((msb & 0x07) << 5) | ((lsb & 0xE0) >> 3);
                    red   = (msb & 0xF8);
                  }
                  whitish = with_color ? ((red > 0x80) && (green > 0x80) && (blue > 0x80)) : ((red + green + blue) > 3 * 0x80); // whitish
                  colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0)); // reddish or yellowish?
                }
                break;
              case 1:
              case 4:
              case 8:
                {
                  if (0 == in_bits)
                  {
                    in_byte = input_buffer[in_idx++];
                    in_bits = 8;
                  }
                  uint16_t pn = (in_byte >> bitshift) & bitmask;
                  whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
                  colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
                  in_byte <<= depth;
                  in_bits -= depth;
                }
                break;
            }
            if (whitish)
            {
              // keep white
            }
            else if (colored && with_color)
            {
              out_color_byte &= ~(0x80 >> col % 8); // colored
            }
            else
            {
              out_byte &= ~(0x80 >> col % 8); // black
            }
            if ((7 == col % 8) || (col == w - 1)) // write that last byte! (for w%8!=0 border)
            {
              output_row_color_buffer[out_idx] = out_color_byte;
              output_row_mono_buffer[out_idx++] = out_byte;
              out_byte = 0xFF; // white (for w%8!=0 border)
              out_color_byte = 0xFF; // white (for w%8!=0 border)
            }
          } // end pixel
          uint16_t yrow = y + (flip ? h - row - 1 : row);
          display.writeImage(output_row_mono_buffer, output_row_color_buffer, x, yrow, w, 1);
        } // end line
        Serial.print("loaded in "); Serial.print(millis() - startTime); Serial.println(" ms");
        // display.refresh();
      }
    }
  }
  file.close();
  if (!valid)
  {
    Serial.println("bitmap format not handled.");
  }
}

void loop() {
  Serial.println("Loop");
  Serial.println(ESP.getFreeHeap(), DEC);
  // ESP.deepSleep(100*1000*1000);
  delay(100000);
}
