#pragma once

// Copy this file to include/secrets.h and fill in local values.
#define WIFI_SSID "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#define NS_API_KEY "your-ns-api-key"

// Station shown on the display and queried from the NS departures API.
// Common Rotterdam examples:
//   RTZ  - Rotterdam Zuid
//   RTB  - Rotterdam Blaak
//   RTA  - Rotterdam Alexander
//   RLN  - Rotterdam Lombardijen
//   RDAM - Rotterdam Centraal
// Use the NS station-list service for the full list of possible station codes.
#define STATION_CODE "RTZ"
#define STATION_NAME "Rotterdam Zuid"
