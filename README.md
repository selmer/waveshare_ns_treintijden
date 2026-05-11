# Rotterdam Zuid e-Paper Departures

ESP32 firmware for a Waveshare Pico-ePaper-4.2-B red/black/white display. It shows train departures from Rotterdam Zuid (`RTZ`) for the next hour using the NS Reisinformatie API.

## Hardware

- ESP32 development board, USB powered.
- Waveshare Pico-ePaper-4.2-B, 400x300, red/black/white SPI panel.
- Suggested wiring:
  - `VCC` -> `3V3`
  - `GND` -> `GND`
  - `DIN` -> `GPIO23`
  - `CLK` -> `GPIO18`
  - `CS` -> `GPIO5`
  - `DC` -> `GPIO17`
  - `RST` -> `GPIO16`
  - `BUSY` -> `GPIO4`

## Setup

1. Install PlatformIO.
2. Copy `include/secrets.example.h` to `include/secrets.h`.
3. Fill in Wi-Fi credentials and your NS API key.
4. Build and upload:

```sh
pio run
pio run --target upload
pio device monitor
```

## Behavior

The device connects to Wi-Fi, synchronizes local time for Europe/Amsterdam, and fetches `RTZ` departures from the NS API. It refreshes the full tri-color e-paper display every 5 minutes, using red for delays, cancellations, stale data, and API errors.

If a refresh fails, the last successful departure list remains visible and the footer marks the data as stale.

The default GxEPD2 driver is `GxEPD2_420c_Z21`, matching the newer 4.2-inch red/black/white Waveshare panel. If your panel is an older revision, override `EPD_DRIVER_CLASS` in `platformio.ini`.
