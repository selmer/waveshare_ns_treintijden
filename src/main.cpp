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

#ifndef STATION_CODE
#define STATION_CODE "RTZ"
#endif

#ifndef STATION_NAME
#define STATION_NAME "Rotterdam Zuid"
#endif

#ifndef COMPACT_ROWS
#define COMPACT_ROWS false
#endif

#ifndef FULLSCREEN_ROWS
#define FULLSCREEN_ROWS false
#endif

namespace {

constexpr const char *kStationCode = STATION_CODE;
constexpr const char *kStationName = STATION_NAME;
constexpr const char *kApiUrlBase =
    "https://gateway.apiportal.ns.nl/reisinformatie-api/api/v2/departures?station=";
constexpr const char *kTimezone = "CET-1CEST,M3.5.0,M10.5.0/3";

constexpr uint32_t kRefreshIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr int kMaxDepartures = 16;
constexpr bool kCompactRows = COMPACT_ROWS;
constexpr bool kFullscreenRows = FULLSCREEN_ROWS;
constexpr int kRowsOnDisplay =
    kFullscreenRows ? (kCompactRows ? 14 : 11) : (kCompactRows ? 10 : 8);
constexpr int kHeaderBaselineY = kFullscreenRows ? 22 : 64;
constexpr int kHeaderUnderlineY = kFullscreenRows ? 28 : 70;
constexpr int kRowStartY = kFullscreenRows ? 50 : (kCompactRows ? 88 : 94);
constexpr int kRowHeight = kCompactRows ? 18 : 24;
constexpr int kTableLeftX = 18;
constexpr int kDestinationX = 116;
constexpr int kTrackX = 334;
constexpr int kTrackRightX = 382;

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

  Serial.print("Verbinden met Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Verbonden: ");
    Serial.println(WiFi.localIP());
  } else {
    lastError = "Wi-Fi verbinding mislukt";
    Serial.println(lastError);
  }
}

void syncClock() {
  configTzTime(kTimezone, "pool.ntp.org", "time.nist.gov");

  Serial.print("Tijd synchroniseren");
  for (int i = 0; i < 40; ++i) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      Serial.println();
      Serial.print("Tijd gesynchroniseerd: ");
      Serial.println(formatDateTime(now));
      return;
    }
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Tijd synchroniseren verlopen");
}

bool fetchDepartures() {
  if (strlen(NS_API_KEY) == 0) {
    lastError = "NS API-sleutel ontbreekt";
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
  const String apiUrl = String(kApiUrlBase) + kStationCode;
  if (!http.begin(client, apiUrl)) {
    lastError = "HTTP setup mislukt";
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
    lastError = "JSON verwerken mislukt";
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
    lastError = "Geen vertrekken in antwoord";
    Serial.println(lastError);
    return false;
  }

  const time_t now = time(nullptr);
  const bool clockReady = now > 1700000000;
  departureCount = 0;
  hiddenDepartureCount = 0;
  apiDepartureCount = list.size();
  skippedParseCount = 0;

  Serial.print("Klok nu: ");
  Serial.print(formatDateTime(now));
  Serial.print(" (");
  Serial.print(clockReady ? "gesynchroniseerd" : "niet gesynchroniseerd");
  Serial.println(")");
  Serial.printf("NS API gaf %d vertrekken terug\n", apiDepartureCount);

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
  Serial.printf("Opgehaald: %d vertrekken voor %s, parse overgeslagen=%d, verborgen=%d\n",
                departureCount,
                kStationCode,
                skippedParseCount,
                hiddenDepartureCount);
  return true;
}

void drawRow(const Departure &departure, int y) {
  const bool alert = departure.cancelled || departure.delayMinutes > 0;
  display.setFont(kCompactRows ? &FreeSans9pt7b : &FreeSans12pt7b);
  display.setTextColor(alert ? GxEPD_RED : GxEPD_BLACK);
  display.setCursor(kTableLeftX, y);
  display.print(departure.timeText);

  if (departure.cancelled) {
    display.print(" X ");
  } else if (departure.delayMinutes > 0) {
    display.print(" +");
    display.print(min(departure.delayMinutes, 99));
    display.print(" ");
  } else {
    display.print("   ");
  }

  display.setTextColor(departure.cancelled ? GxEPD_RED : GxEPD_BLACK);
  display.setCursor(kDestinationX, y);
  display.print(fitText(departure.direction, kCompactRows ? 22 : 20));

  display.setTextColor(departure.cancelled ? GxEPD_RED : GxEPD_BLACK);
  drawRightAlignedText(fitText(departure.platform, 4), kTrackRightX, y);
}

void drawScreen(bool fetchOk) {
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    if (!kFullscreenRows) {
      display.setTextColor(GxEPD_BLACK);
      display.setFont(&FreeSansBold12pt7b);
      drawCenteredText(kStationName, 200, 28);
      display.drawFastHLine(0, 42, 400, GxEPD_RED);
    }

    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(kTableLeftX, kHeaderBaselineY);
    display.print("Tijd");
    display.setCursor(kDestinationX, kHeaderBaselineY);
    display.print("Bestemming");
    display.setCursor(kTrackX, kHeaderBaselineY);
    display.print("Spoor");
    display.drawFastHLine(kTableLeftX, kHeaderUnderlineY,
                          kTrackRightX - kTableLeftX, GxEPD_RED);

    if (!fetchOk && departureCount == 0) {
      display.setFont(&FreeSans12pt7b);
      display.setTextColor(GxEPD_RED);
      display.setCursor(45, 135);
      display.print("Ophalen mislukt");
      display.setFont(&FreeSans9pt7b);
      display.setCursor(35, 165);
      display.print(fitText(lastError, 44));
    } else if (departureCount == 0) {
      display.setFont(&FreeSans12pt7b);
      display.setCursor(35, 145);
      display.print("Geen vertrekken");
    } else {
      const int rows = min(departureCount, kRowsOnDisplay);
      for (int i = 0; i < rows; ++i) {
        drawRow(departures[i], kRowStartY + i * kRowHeight);
      }

      if (!kFullscreenRows &&
          (departureCount > kRowsOnDisplay || hiddenDepartureCount > 0)) {
        display.setFont(&FreeSans9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(4, 284);
        display.print("+");
        display.print(departureCount - kRowsOnDisplay + hiddenDepartureCount);
        display.print(" meer vertrekken");
      }
    }

    if (!kFullscreenRows) {
      display.drawFastHLine(0, 270, 400, GxEPD_RED);
      display.setFont(&FreeSans9pt7b);

      if (fetchOk) {
        display.setTextColor(GxEPD_BLACK);
        drawRightAlignedText("Bij " + formatDateTime(lastSuccessfulFetch), 396, 292);
      } else {
        display.setTextColor(GxEPD_RED);
        drawRightAlignedText("Fout " + formatDateTime(lastSuccessfulFetch), 396, 292);
      }
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
  Serial.print(kStationName);
  Serial.println(" e-paper vertrekken");

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
