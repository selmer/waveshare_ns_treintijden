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
//   RTD - Rotterdam Centraal
// Use the NS station-list service for the full list of possible station codes.
#define STATION_CODE "RTZ"
#define STATION_NAME "Rotterdam Zuid"

// Use compact rows for large stations to show more departures.
// false: 8 rows with larger 12pt text.
// true: 10 rows with smaller 9pt text.
#define COMPACT_ROWS false

// Hide the station title and footer to use the screen for more departures.
// false: show station title, footer timestamp, and "+N meer vertrekken".
// true: show only the table header and departure rows.
#define FULLSCREEN_ROWS false
