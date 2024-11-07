#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>   // For date and time calculations
#include <HTTPClient.h>  // For HTTP requests

// Wi-Fi credentials
const char* ssid = "Tech";
const char* password = "12345678";

// Initialize the LCD (20x4)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// DHT11 Sensor
#define DHTPIN 4  // DHT11 data pin connected to GPIO 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// RSS Feed URL for TechCrunch
const char* rssFeed = "https://techcrunch.com/feed/";

// Mutex for LCD access control using Priority Inheritance Protocol (PIP)
SemaphoreHandle_t lcdMutex;

// Task handles
TaskHandle_t tempTaskHandle;
TaskHandle_t displayTaskHandle;

// Variables to hold previous values for comparison
int prevHours = -1, prevMinutes = -1, prevSeconds = -1;
float prevTemp = NAN;

// NTP Client setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 19800, 60000); // UTC timezone, update every 60 seconds

// Time and date variables (renamed to avoid conflict with TimeLib functions)
int currentHours, currentMinutes, currentSeconds;
int currentDay, currentMonth, currentYear;

// RSS Headline and News Content variables
String rssHeadline = "";  
String rssContent = "";  
int currentNewsIndex = 0;  // Index to track the current news item

void setup() {
  Serial.begin(115200);

  lcd.begin(20, 4);
  lcd.init();
  lcd.backlight();

  dht.begin();

  connectToWiFi();

  timeClient.begin();

  lcdMutex = xSemaphoreCreateMutex();

  xTaskCreate(readTemperature, "TempTask", 2048, NULL, 2, &tempTaskHandle);
  xTaskCreate(displayData, "DisplayTask", 4096, NULL, 1, &displayTaskHandle);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}

void fetchRSSFeed() {
  HTTPClient http;
  http.begin(rssFeed);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    
    // Extract the title and a shortened description
    int itemStartIdx = payload.indexOf("<item>", currentNewsIndex);
    if (itemStartIdx != -1) {
      int titleStartIdx = payload.indexOf("<title>", itemStartIdx) + 7;
      int titleEndIdx = payload.indexOf("</title>", titleStartIdx);
      rssHeadline = payload.substring(titleStartIdx, titleEndIdx);

      int descStartIdx = payload.indexOf("<description>", itemStartIdx) + 13;
      int descEndIdx = payload.indexOf("</description>", descStartIdx);
      rssContent = payload.substring(descStartIdx, descEndIdx);

      // Move to the next item for the next fetch
      currentNewsIndex = payload.indexOf("</item>", itemStartIdx) + 7;
      if (currentNewsIndex >= payload.length()) {
        currentNewsIndex = 0;  // Reset to the first news item if we've reached the end
      }
    } else {
      rssHeadline = "No news found";
      rssContent = "";
    }
  } else {
    rssHeadline = "Failed to fetch RSS";
    rssContent = "";
  }
  
  http.end();
}

void readTemperature(void *parameter) {
  while (true) {
    float temp = dht.readTemperature();
    if (!isnan(temp) && temp != prevTemp) {
      prevTemp = temp;

      if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
        lcd.setCursor(6, 2);
        lcd.print(temp);
        lcd.print(" C  ");
        xSemaphoreGive(lcdMutex);
      }
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void displayData(void *parameter) {
  while (true) {
    timeClient.update();

    unsigned long epochTime = timeClient.getEpochTime();

    currentHours = hour(epochTime);
    currentMinutes = minute(epochTime);
    currentSeconds = second(epochTime);
    currentDay = day(epochTime);
    currentMonth = month(epochTime);
    currentYear = year(epochTime);

    if (currentSeconds != prevSeconds || currentMinutes != prevMinutes || currentHours != prevHours) {
      prevSeconds = currentSeconds;
      prevMinutes = currentMinutes;
      prevHours = currentHours;

      if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
        lcd.setCursor(6, 0);
        if (currentHours < 10) lcd.print('0');
        lcd.print(currentHours);
        lcd.print(":");
        if (currentMinutes < 10) lcd.print('0');
        lcd.print(currentMinutes);
        lcd.print(":");
        if (currentSeconds < 10) lcd.print('0');
        lcd.print(currentSeconds);
        xSemaphoreGive(lcdMutex);
      }
    }

    if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
      lcd.setCursor(6, 1);
      lcd.printf("%02d/%02d/%04d", currentDay, currentMonth, currentYear);
      xSemaphoreGive(lcdMutex);
    }

    static unsigned long lastFetchTime = 0;
    if (millis() - lastFetchTime > 4000) {  // Switch feed every 4 seconds
      fetchRSSFeed();
      lastFetchTime = millis();
    }

    String displayText = rssHeadline + ": " + rssContent.substring(0, 100) + "...";  // Limit content length
    scrollText(displayText, 3);

    vTaskDelay(1000 / portTICK_PERIOD_MS);  // Slow down scrolling for readability
  }
}

void scrollText(String message, int row) {
  int len = message.length();
  int displayWidth = 20;  // LCD display width

  if (len <= displayWidth) {
    if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
      lcd.setCursor(0, row);
      lcd.print(message);
      xSemaphoreGive(lcdMutex);
    }
  } else {
    // Scroll by full display width (smooth transition)
    for (int i = 0; i < len - displayWidth; i += displayWidth) {
      if (xSemaphoreTake(lcdMutex, portMAX_DELAY) == pdTRUE) {
        lcd.setCursor(0, row);
        lcd.print(message.substring(i, i + displayWidth));
        xSemaphoreGive(lcdMutex);
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);  // Pause for a second for each page
    }
  }
}

void loop() {
  // Empty, as everything is managed by FreeRTOS tasks and timers
}