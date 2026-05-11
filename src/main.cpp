#include <Arduino.h>
#include <ArduinoJson.h>
#include <GxEPD2_3C.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <time.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#warning "Copy include/secrets.example.h to include/secrets.h before flashing."
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#define NS_API_KEY ""
#endif

namespace {

constexpr const char *kStationCode = "RTZ";
constexpr const char *kStationName = "Rotterdam Zuid";
constexpr const char *kApiUrl =
    "https://gateway.apiportal.ns.nl/reisinformatie-api/api/v2/departures"
    "?station=RTZ";
constexpr const char *kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";

constexpr uint32_t kRefreshIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr int kMaxDepartures = 12;
constexpr int kRowsOnDisplay = 8;
constexpr int kRowStartY = 94;
constexpr int kRowHeight = 24;
constexpr int kTableLeftX = 48;
constexpr int kDestinationX = 164;

constexpr int kPinBusy = 4;
constexpr int kPinCs = 5;
constexpr int kPinReset = 16;
constexpr int kPinDc = 17;
constexpr int kPinSck = 18;
constexpr int kPinMosi = 23;

#ifndef EPD_DRIVER_CLASS
#define EPD_DRIVER_CLASS GxEPD2_420c_Z21
#endif

GxEPD2_3C<EPD_DRIVER_CLASS, EPD_DRIVER_CLASS::HEIGHT> display(
    EPD_DRIVER_CLASS(kPinCs, kPinDc, kPinReset, kPinBusy));

struct Departure {
  time_t plannedEpoch = 0;
  time_t actualEpoch = 0;
  int delayMinutes = 0;
  bool cancelled = false;
  String timeText;
  String direction;
  String platform;
  String trainType;
};

Departure departures[kMaxDepartures];
int departureCount = 0;
int hiddenDepartureCount = 0;
int apiDepartureCount = 0;
int skippedParseCount = 0;
String lastError;
time_t lastSuccessfulFetch = 0;
uint32_t nextRefreshAt = 0;

String twoDigits(int value) {
  return value < 10 ? "0" + String(value) : String(value);
}

String formatClock(time_t epoch) {
  if (epoch <= 0) {
    return "--:--";
  }
  struct tm localTime;
  localtime_r(&epoch, &localTime);
  return twoDigits(localTime.tm_hour) + ":" + twoDigits(localTime.tm_min);
}

String formatDateTime(time_t epoch) {
  if (epoch <= 0) {
    return "--:--";
  }
  struct tm localTime;
  localtime_r(&epoch, &localTime);
  return twoDigits(localTime.tm_mday) + "-" + twoDigits(localTime.tm_mon + 1) +
         " " + twoDigits(localTime.tm_hour) + ":" + twoDigits(localTime.tm_min);
}

time_t parseIsoDateTime(const char *value) {
  if (value == nullptr || strlen(value) < 19) {
    return 0;
  }

  struct tm parsed = {};
  char buffer[20];
  memcpy(buffer, value, 19);
  buffer[19] = '\0';

  if (strptime(buffer, "%Y-%m-%dT%H:%M:%S", &parsed) == nullptr) {
    return 0;
  }
  parsed.tm_isdst = -1;
  return mktime(&parsed);
}

String fitText(const String &value, int maxChars) {
  if (static_cast<int>(value.length()) <= maxChars) {
    return value;
  }
  if (maxChars <= 1) {
    return value.substring(0, maxChars);
  }
  return value.substring(0, maxChars - 1) + ".";
}

void drawRightAlignedText(const String &text, int rightX, int baselineY) {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  display.getTextBounds(text, 0, baselineY, &x, &y, &width, &height);
  display.setCursor(rightX - width, baselineY);
  display.print(text);
}

void drawCenteredText(const String &text, int centerX, int baselineY) {
  int16_t x = 0;
  int16_t y = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  display.getTextBounds(text, 0, baselineY, &x, &y, &width, &height);
  display.setCursor(centerX - width / 2, baselineY);
  display.print(text);
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected: ");
    Serial.println(WiFi.localIP());
  } else {
    lastError = "Wi-Fi connection failed";
    Serial.println(lastError);
  }
}

void syncClock() {
  configTzTime(kTimezone, "pool.ntp.org", "time.nist.gov");

  Serial.print("Syncing time");
  for (int i = 0; i < 40; ++i) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      Serial.println();
      Serial.print("Time synced: ");
      Serial.println(formatDateTime(now));
      return;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Time sync timed out");
}

bool fetchDepartures() {
  if (strlen(NS_API_KEY) == 0) {
    lastError = "NS API key missing";
    Serial.println(lastError);
    return false;
  }

  connectWifi();
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, kApiUrl)) {
    lastError = "HTTP setup failed";
    Serial.println(lastError);
    return false;
  }

  http.addHeader("Ocp-Apim-Subscription-Key", NS_API_KEY);
  http.addHeader("Accept", "application/json");

  const int status = http.GET();
  Serial.printf("NS API status: %d\n", status);
  if (status != HTTP_CODE_OK) {
    lastError = "NS API HTTP " + String(status);
    Serial.println(lastError);
    http.end();
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();

  if (error) {
    lastError = "JSON parse failed";
    Serial.print(lastError);
    Serial.print(": ");
    Serial.println(error.c_str());
    return false;
  }

  JsonArray list = doc["payload"]["departures"].as<JsonArray>();
  if (list.isNull()) {
    list = doc["departures"].as<JsonArray>();
  }
  if (list.isNull()) {
    lastError = "No departures in response";
    Serial.println(lastError);
    return false;
  }

  const time_t now = time(nullptr);
  const bool clockReady = now > 1700000000;
  departureCount = 0;
  hiddenDepartureCount = 0;
  apiDepartureCount = list.size();
  skippedParseCount = 0;

  Serial.print("Clock now: ");
  Serial.print(formatDateTime(now));
  Serial.print(" (");
  Serial.print(clockReady ? "synced" : "not synced");
  Serial.println(")");
  Serial.printf("NS API returned %d departures\n", apiDepartureCount);

  for (JsonObject item : list) {
    const char *plannedText = item["plannedDateTime"] | "";
    const char *actualText = item["actualDateTime"] | plannedText;
    const time_t planned = parseIsoDateTime(plannedText);
    const time_t actual = parseIsoDateTime(actualText);
    const time_t sortTime = actual > 0 ? actual : planned;

    if (sortTime <= 0) {
      skippedParseCount++;
      continue;
    }

    if (departureCount >= kMaxDepartures) {
      hiddenDepartureCount++;
      continue;
    }

    Departure &departure = departures[departureCount++];
    departure.plannedEpoch = planned;
    departure.actualEpoch = actual;
    departure.delayMinutes =
        planned > 0 && actual > planned ? static_cast<int>((actual - planned) / 60) : 0;
    departure.cancelled = item["cancelled"] | false;
    departure.timeText = formatClock(sortTime);
    const char *direction = item["direction"] | "Onbekend";
    const char *actualTrack = item["actualTrack"] | "";
    const char *plannedTrack = item["plannedTrack"] | "";
    const char *shortCategory = item["product"]["shortCategoryName"] | "";
    const char *categoryCode = item["product"]["categoryCode"] | "";
    const char *name = item["name"] | "";

    departure.direction = direction;
    departure.platform = strlen(actualTrack) > 0 ? actualTrack
                         : strlen(plannedTrack) > 0 ? plannedTrack
                                                    : "-";
    departure.trainType = strlen(shortCategory) > 0 ? shortCategory
                          : strlen(categoryCode) > 0 ? categoryCode
                          : strlen(name) > 0         ? name
                                                     : "Trein";
  }

  for (int i = 0; i < departureCount - 1; ++i) {
    for (int j = i + 1; j < departureCount; ++j) {
      const time_t left =
          departures[i].actualEpoch > 0 ? departures[i].actualEpoch : departures[i].plannedEpoch;
      const time_t right =
          departures[j].actualEpoch > 0 ? departures[j].actualEpoch : departures[j].plannedEpoch;
      if (right < left) {
        Departure tmp = departures[i];
        departures[i] = departures[j];
        departures[j] = tmp;
      }
    }
  }

  lastError = "";
  lastSuccessfulFetch = clockReady ? now : time(nullptr);
  Serial.printf("Fetched %d departures for %s, skipped parse=%d, hidden=%d\n",
                departureCount,
                kStationCode,
                skippedParseCount,
                hiddenDepartureCount);
  return true;
}

void drawRow(const Departure &departure, int y) {
  const bool alert = departure.cancelled || departure.delayMinutes > 0;
  display.setFont(&FreeSans12pt7b);
  display.setTextColor(alert ? GxEPD_RED : GxEPD_BLACK);
  display.setCursor(kTableLeftX, y);
  display.print(departure.timeText);

  if (departure.cancelled) {
    display.print(" X ");
  } else if (departure.delayMinutes > 0) {
    display.print(" +");
    display.print(departure.delayMinutes);
    display.print(" ");
  } else {
    display.print("   ");
  }

  display.setTextColor(departure.cancelled ? GxEPD_RED : GxEPD_BLACK);
  display.setCursor(kDestinationX, y);
  display.print(fitText(departure.direction, 24));
}

void drawScreen(bool fetchOk) {
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    display.setTextColor(GxEPD_BLACK);
    display.setFont(&FreeSansBold12pt7b);
    drawCenteredText(kStationName, 200, 28);

    display.drawFastHLine(0, 42, 400, GxEPD_RED);

    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(kTableLeftX, 64);
    display.print("Time");
    display.setCursor(kDestinationX, 64);
    display.print("Destination");
    display.drawFastHLine(kTableLeftX, 70, 210, GxEPD_RED);

    if (!fetchOk && departureCount == 0) {
      display.setFont(&FreeSans12pt7b);
      display.setTextColor(GxEPD_RED);
      display.setCursor(45, 135);
      display.print("Fetch failed");
      display.setFont(&FreeSans9pt7b);
      display.setCursor(35, 165);
      display.print(fitText(lastError, 44));
    } else if (departureCount == 0) {
      display.setFont(&FreeSans12pt7b);
      display.setCursor(35, 145);
      display.print("No departures found");
    } else {
      const int rows = min(departureCount, kRowsOnDisplay);
      for (int i = 0; i < rows; ++i) {
        drawRow(departures[i], kRowStartY + i * kRowHeight);
      }

      if (departureCount > kRowsOnDisplay || hiddenDepartureCount > 0) {
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(4, 284);
        display.print("+");
        display.print(departureCount - kRowsOnDisplay + hiddenDepartureCount);
        display.print(" more departures");
      }
    }

    display.drawFastHLine(0, 270, 400, GxEPD_RED);
    display.setFont(&FreeSans9pt7b);

    if (fetchOk) {
      display.setTextColor(GxEPD_BLACK);
      drawRightAlignedText("Upd " + formatDateTime(lastSuccessfulFetch), 396, 292);
    } else {
      display.setTextColor(GxEPD_RED);
      drawRightAlignedText("Err " + formatDateTime(lastSuccessfulFetch), 396, 292);
    }
  } while (display.nextPage());

  display.hibernate();
}

void refreshDepartures() {
  const bool fetchOk = fetchDepartures();
  drawScreen(fetchOk);
  nextRefreshAt = millis() + kRefreshIntervalMs;
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("Rotterdam Zuid e-paper departures");

  SPI.begin(kPinSck, -1, kPinMosi, kPinCs);
  display.init(115200, true, 2, false);

  connectWifi();
  syncClock();
  refreshDepartures();
}

void loop() {
  if (static_cast<int32_t>(millis() - nextRefreshAt) >= 0) {
    refreshDepartures();
  }
  delay(1000);
}
