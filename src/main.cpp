#include "main.h"

#define FileClass fs::File

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_data_cert_x509_crt_bundle_bin_start");

const char* openWeatherApi = "https://api.openweathermap.org/data/2.5/%s?q=%s&units=metric&APPID=%s%s";
const char* weatherEndpoint = "weather";
const char* forecastEndpoint = "forecast";

State state;

Display display(GxEPD2_583c_Z83(16, 4, 22, 17)); // GDEW0583Z83 648x480, GD7965

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

  delay(1000);
  Serial.println(F("memory before display: "));
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
  Serial.print(F("later time: "));
  Serial.println(state.laterTime);
  Serial.print(F("later temp: "));
  Serial.println(state.laterTemp);
  Serial.print(F("later weather: "));
  Serial.println(state.laterWeather);
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

  WiFiClientSecure *client = new WiFiClientSecure();
  client->setCACertBundle(rootca_crt_bundle_start);

  if (refresh & 1) refreshWeather(&settings, client);
  Serial.println(ESP.getFreeHeap(), DEC);
  if (refresh >> 1 & 1) refreshForecast(&settings, client);

  delete client;
  client = NULL;

  disconnectWifi();
}

void clearDisplay() {
  Serial.println(F("clear display"));
  display.setFullWindow();
  display.firstPage();

  do
  {
    display.fillScreen(GxEPD_WHITE);
  }
  while (display.nextPage());
}

void displayDate() {
  uint16_t w = 240;
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
  snprintf(dayStr, 3, "%02d", day(now_t));

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
  delay(1000);
}

void displayWeather()
{
  const uint16_t initial_x = 240;
  const uint16_t initial_y = 30;
  uint16_t x = initial_x;
  uint16_t y = initial_y;
  uint16_t w = 210;
  uint16_t h = 410;

  int16_t tbx, tby; uint16_t tbw, tbh;
  
  char temp[4] = "";
  snprintf(temp, 4, "%2d", state.currentTemp);

  char laterTemp[4] = "";
  snprintf(laterTemp, 4, "%2d", state.laterTemp);

  const int iconSize = 128;
  const int laterIconSize = 64;

  char icon[12] = "";
  snprintf(icon, 12, "%s_%d.bmp", state.currentWeather, iconSize);

  char laterIcon[12] = "";
  snprintf(laterIcon, 12, "%s_%d.bmp", state.laterWeather, laterIconSize);

  char laterTimeStr[6];
  snprintf(laterTimeStr, 6, "%02d:%02d", hour(state.laterTime), minute(state.laterTime));

  display.setRotation(0);
  display.setPartialWindow(x, y, w, h);
  display.firstPage();
  
  do
  {
    display.fillScreen(GxEPD_WHITE);

    // weather
    x = initial_x + (w - iconSize) / 2;
    y = initial_y;
    drawBitmapFromSpiffs(icon, x, y, false);

    // temp
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold48pt7b);
    display.getTextBounds(temp, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = initial_x + (w - tbw) / 2 - 10;
    y = y + iconSize + tbh + 20;
    display.setCursor(x, y);
    display.print(temp);
    // degree symbol (the letter "o")
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(x + tbw + 15, y-tbh+9);
    display.print("o");

    // later time
    display.setTextColor(GxEPD_RED);
    display.setFont(&FreeMonoBold18pt7b);
    display.getTextBounds(laterTimeStr, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = initial_x;
    y = y + tbh + 60;
    display.setCursor(x, y);
    display.print(laterTimeStr);
    
    // later weather
    x = initial_x + 10;
    y = y + 15;
    drawBitmapFromSpiffs(laterIcon, x, y, false);

    // later temp
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold24pt7b);
    display.getTextBounds(laterTemp, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = initial_x + 20 + laterIconSize;
    y = y + tbh + 15;
    display.setCursor(x, y);
    display.print(laterTemp);
    // degree symbol (the letter "o")
    display.setFont(&FreeMonoBold9pt7b);
    display.setCursor(x + tbw + 10, y-tbh+9);
    display.print("o");
  }
  while (display.nextPage());
  delay(1000);
}

void displaySunset() {
  const uint16_t initial_x = 10;
  const uint16_t initial_y = 330;
  uint16_t x = initial_x;
  uint16_t y = initial_y;
  const uint16_t w = 200;
  const uint16_t h = 100;

  int16_t tbx, tby; uint16_t tbw, tbh;

  display.setRotation(0);
  display.setPartialWindow(x, y, w, h);
  display.firstPage();

  char * sunriseIcon = "sun-rise_36.bmp";
  char * sunsetIcon = "sun-set_36.bmp";
  const int iconSize = 36;

  do
  {
    display.fillScreen(GxEPD_WHITE);
    // sunrise
    x = initial_x + 20;
    y = initial_y;
    drawBitmapFromSpiffs(sunriseIcon, x, y, false);

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeMonoBold12pt7b);
    display.getTextBounds(state.todaySunrise, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = x + iconSize + 10;
    y = y + tbh + 10;
    display.setCursor(x, y);
    display.print(state.todaySunrise);

    // sunset
    x = initial_x + 20;
    y = y + 10;
    drawBitmapFromSpiffs(sunsetIcon, x, y, false);

    display.getTextBounds(state.todaySunset, 0, 0, &tbx, &tby, &tbw, &tbh);
    x = x + iconSize + 10;
    y = y + tbh + 10;
    display.setCursor(x, y);
    display.print(state.todaySunset);
  }
  while (display.nextPage());
  delay(1000);
}

void displayForecast() {
  const uint16_t initial_x = 470;
  const uint16_t initial_y = 10;
  uint16_t x = initial_x;
  uint16_t y = initial_y;
  const uint16_t w = 648 - x - 1;
  const uint16_t h = 640;

  int16_t tbx, tby; uint16_t tbw, tbh;

  char temp[5] = "";
  char icon[11] = "";

  const int iconSize = 36;
  
  display.setRotation(0);
  display.setPartialWindow(x, y, w, h);
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    for (int i=0; i<3; i++) {
      if (i == 0) {
        y = initial_y;
      }
      // day
      display.setTextColor(GxEPD_RED);
      display.setFont(&FreeMonoBold24pt7b);
      display.getTextBounds(state.forecast[i].day, 0, 0, &tbx, &tby, &tbw, &tbh);
      y = y + (tbh + 20);
      display.setCursor(x, y);
      display.print(state.forecast[i].day);

      // morning temp
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeMonoBold18pt7b);
      snprintf(temp, 5, "% 3d", state.forecast[i].morningTemp);
      display.getTextBounds(temp, 0, 0, &tbx, &tby, &tbw, &tbh);
      y = y + (tbh + 12);
      display.setCursor(x+10, y);
      display.print(temp);
      // degree symbol (the letter "o")
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(x+13+tbw, y-tbh+6);
      display.print("o");

      // morning weather
      snprintf(icon, 11, "%s_%d.bmp", state.forecast[i].morningWeather, iconSize);
      drawBitmapFromSpiffs(icon, x + 110, y - iconSize + 10, false);

      // afternoon temp
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeMonoBold18pt7b);
      snprintf(temp, 5, "% 3d", state.forecast[i].afternoonTemp);
      display.getTextBounds(temp, 0, 0, &tbx, &tby, &tbw, &tbh);
      y = y + (tbh + 20);
      display.setCursor(x+10, y);
      display.print(temp);
      // degree symbol (the letter "o")
      display.setFont(&FreeMonoBold9pt7b);
      display.setCursor(x+13+tbw, y-tbh+6);
      display.print("o");

      // afternoon weather
      snprintf(icon, 11, "%s_%d.bmp", state.forecast[i].afternoonWeather, iconSize);
      drawBitmapFromSpiffs(icon, x + 110, y - iconSize + 10, false);
    }
  }
  while (display.nextPage());
  delay(1000);
}

void displayNextBus() {
  
}

void displayLastUpdate() {
  
}

void refreshDisplay(byte refresh) {
  if (refresh <= 0) {
    return;
  }

  Serial.println("Create display");
  delay(500);
  Serial.println("Init display");
  display.init(115200, true, 2, false);
  clearDisplay();
  displaySunset();
  displayDate();
  displayWeather();
  displayForecast();
  displayNextBus();
  displayLastUpdate();
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
}

void disconnectWifi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

void refreshWeather(Settings *settings, WiFiClientSecure *client) {
  char url[128];
  snprintf(url, 128, openWeatherApi, weatherEndpoint, settings->OWLocation, settings->OWApiKey, "");
  Serial.println(url);

  HTTPClient http;
  http.begin(dynamic_cast<WiFiClient&>(*client), url);
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

  snprintf(state.todaySunrise, 6, "%02d:%02d", hour(sunrise), minute(sunrise));
  snprintf(state.todaySunset, 6, "%02d:%02d", hour(sunset), minute(sunset));
}

void refreshForecast(Settings *settings, WiFiClientSecure *client) {
  char url[132];
  snprintf(url, 132, openWeatherApi, forecastEndpoint, settings->OWLocation, settings->OWApiKey, "&cnt=30");
  Serial.println(url);

  HTTPClient http;
  http.begin(dynamic_cast<WiFiClient&>(*client), url);
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

  int offset = doc["city"]["timezone"];
  unsigned int currentTime = state.dt + state.offset;

  state.laterTime = (int)doc["list"][1]["dt"] + offset;
  state.laterTemp = doc["list"][1]["main"]["temp"];
  strcpy(state.laterWeather, doc["list"][1]["weather"][0]["icon"]);

  for (JsonObject list_item : doc["list"].as<JsonArray>()) {
    unsigned long t = ((int)list_item["dt"]) + offset;
    if ((hour(t) >= 7 && hour(t) < 10) || (hour(t) >= 16 && hour(t) < 19)) {
      if (day(t) != day(currentTime)) {
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

void drawBitmapFromSpiffs(const char *filename, int16_t x, int16_t y, bool with_color)
{
  fs::File file;
  bool valid = false; // valid format to be handled
  bool flip = true; // bitmap is stored bottom-to-top
  uint32_t startTime = millis();
  if ((x >= display.epd2.WIDTH) || (y >= display.epd2.HEIGHT)) return;
  Serial.println();
  Serial.print(F("Loading image '"));
  Serial.print(filename);
  Serial.println('\'');
  file = LittleFS.open(String("/") + filename, "r");
  if (!file)
  {
    Serial.print(F("File not found"));
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
      Serial.print(F("File size: ")); Serial.println(fileSize);
      Serial.print(F("Image Offset: ")); Serial.println(imageOffset);
      Serial.print(F("Header size: ")); Serial.println(headerSize);
      Serial.print(F("Bit Depth: ")); Serial.println(depth);
      Serial.print(F("Image size: "));
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
        Serial.print(F("loaded in ")); Serial.print(millis() - startTime); Serial.println(F(" ms"));
        // display.refresh();
      }
    }
  }
  file.close();
  if (!valid)
  {
    Serial.println(F("bitmap format not handled."));
  }
}

void loop() {
  Serial.println("Loop");
  Serial.println(ESP.getFreeHeap(), DEC);
  // ESP.deepSleep(100*1000*1000);
  delay(100000);
}
