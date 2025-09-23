# ESP32 Alarm Clock with NeoPixels, Wi-Fi, and NVS Persistence

This project is an ESP-IDF–based firmware for ESP32 that combines:

- **Wi-Fi Manager** with captive portal for first-time setup  
- **Time Manager** using NTP + local timezone rules  
- **Alarm Manager** with NVS persistence and one-shot timers  
- **NeoPixel Driver** with RMT hardware for smooth LED output  
- **LED Animations** including breathing, pulsing, rainbow, and cross-fade to solid  
- **Button Manager** for latching or momentary button events  
- **Potentiometer Manager** to control maximum LED brightness in real time  

The system can trigger LED animations at configured alarms or timers, with brightness capped by a physical potentiometer.

---

## Features

- **Wi-Fi Manager**
  - Connects to stored credentials in NVS
  - If connection fails, starts an AP (`ESP32_Config`) with a captive portal for entering credentials

- **Time Manager**
  - Syncs via NTP
  - Handles daylight savings through POSIX TZ rules
  - Provides callbacks when time is synced

- **Alarm Manager**
  - Supports daily alarms at specified times
  - Alarms persist across reboots via NVS
  - One-shot timers (e.g., “run in 15 minutes”) supported
  - Alarms trigger LED animations or user callbacks

- **NeoPixel Driver**
  - Uses ESP32’s RMT peripheral for precise WS2812/SK6812 timing
  - Supports both GRB (WS2812B) and GRBW (SK6812) strips
  - Global brightness cap applied at transmit

- **Animations**
  - Breathing (sinusoidal brightness)
  - Pulsing (on/off cycles)
  - Rainbow (continuous cycling colors)
  - Fade-to-solid (cross-fade from current frame to a new solid color)

- **Button Manager**
  - GPIO interrupt–driven, debounced edge detection
  - Fires callback on **any** state change (works for latching switches)
  - Example: toggle a 15-minute timer with each press

- **Potentiometer Manager**
  - Reads an analog input (ADC1, e.g. GPIO34)
  - Smooths readings with IIR filter
  - Maps value to 0–255 brightness cap
  - Auto-refreshes LEDs so brightness changes apply immediately

---

## Hardware

- **ESP32-WROOM or similar**
- **NeoPixel strip** (WS2812B or SK6812)
  - Data pin: GPIO **15**
- **Button**
  - Wired to GPIO **18**
  - Configure internal pull-up if tied to GND
- **Potentiometer**
  - Wired to ADC1 channel (default: **GPIO34**)
  - 10kΩ recommended, wiper to GPIO34, ends to 3V3 and GND

---

## Getting Started

### Prerequisites
- ESP-IDF v4.4+ (recommended v5.1 or later)
- Python environment for ESP-IDF (`idf.py`)

### Build & Flash
```bash
idf.py set-target esp32
idf.py build flash monitor
```

On first boot, if no Wi-Fi credentials are found:
- The ESP32 starts an AP called `ESP32_Config`
- Connect and visit [http://192.168.4.1](http://192.168.4.1) to enter SSID & password

---

## Example Behavior

- Boot:
  - LEDs show a connecting animation
  - Device connects to Wi-Fi and syncs time
- Alarm:
  - At configured time (e.g., 12:00:00), LEDs cross-fade into a chosen animation
- Button:
  - Each press triggers a callback (e.g., start a 15-minute one-shot timer)
- Potentiometer:
  - Adjusting it immediately changes the maximum LED brightness

---

## Project Structure

```
components/
  ├── wifi_manager/        # Wi-Fi + captive portal
  ├── time_manager/        # NTP sync + TZ
  ├── storage_manager/     # NVS wrapper
  ├── alarm_manager/       # Persistent alarms + one-shot timers
  ├── neopixel_driver/     # RMT-based LED driver
  ├── neopixel_animations/ # Breathing, rainbow, fade-to-solid, etc.
  ├── button_manager/      # Edge-triggered debounced button events
  └── pot_manager/         # ADC potentiometer → brightness cap
main/
  └── main.c               # Application wiring everything together
```

---

## License
CC0 1.0 Universal

---
