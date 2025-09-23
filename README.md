# ESP32 Color Alarm Lamp

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
  - Breathing (si
