
# Boardoza Smart Desk Robot Interactive Desktop Companion Project

The **Boardoza Desk Mascot Project** is an ESP32-S3-based interactive desktop companion built on the **Boardoza ESP32-S3 Display Evaluation Board**. The system features a **1.75-inch 360 × 360 AMOLED display**, animated facial expressions, a **VL53L7CX-1 Time-of-Flight (ToF) sensor**, an **SGP40 air quality sensor**, and an onboard microphone. The mascot responds to touch, movement, and sound through expressive animations and interactive functions.

The system provides timekeeping, weather information, indoor air quality monitoring, and intelligent sitting reminders. It also features a collection of interactive applications, including alarms, countdowns, music-responsive animations, breathing exercises, and a tilt-controlled maze game. Communication through **BLE, Wi-Fi, and USB** enables external control and messaging. The project is suitable for desktop use, STEM education, and interactive exhibitions.


<h2 align="center">Smart Desk Robot</h2>

<p align="center">
  <img src="./assets/Smart Desk Robot.gif" alt="Boardoza Smart Desk Robot" width="400">
</p>


---

## Key Features

- **Interactive Animated Mascot:** Features a 1.75-inch AMOLED display with 13 animated facial expressions, including blinking, eye movements, breathing animations, and customizable color themes.
- **Intelligent Sitting Reminder:** Uses a 64-zone VL53L7CX-1 ToF sensor with background calibration to detect desk occupancy and provide visual and audible reminders when sitting time exceeds a configurable limit.
- **Indoor Air Quality Monitoring:** Integrates an SGP40 gas sensor to monitor indoor air quality and provide alerts when air quality becomes poor.
- **Sound-Responsive Animations:** Uses an onboard microphone for sound detection, speech-responsive facial movements, and music-synchronized dance animations.
- **Interactive Applications:** Provides 14 applications, including clock, weather, alarm, countdown, stopwatch, calendar, breathing exercises, night light, and a tilt-controlled maze game.
- **Wireless Control and Messaging:** Supports BLE, Wi-Fi, and USB communication for device control, settings adjustment, and two-way messaging.

---

## System Architecture

The Desk Mascot system combines an ESP32-S3 Display Evaluation Board, a 1.75-inch AMOLED display, and a VL53L7CX-1 Time-of-Flight (ToF) sensor to create an interactive desktop companion with animated expressions and presence detection.

| Component | Model |
|---|---|
| Microcontroller | [Boardoza ESP32-S3 Display Evaluation Board](https://boardoza.com/tr/products/boardoza-esp32-s3-display-evaluation-board) |
| Display | [1.75-inch TDO AMOLED Display](https://www.ozdisan.com/) |
| ToF Sensor | [Boardoza VL53L7CX-1](https://github.com/Boardoza/Boardoza_VL53L7CX-1_Multizone_Time-of-Flight_Distance_Sensor_Breakout_Board) |

---

## Board Pinout

[Boardoza Smart Desk Robot Pinout Diagram](./assets/Smart%20Desk%20Robot%20Pinout%20Diagram.pdf)

---

## Step Files

[Boardoza Smart Desk Robot.step](./assets/Smart%20Desk%20Robot%20Step.step)

---

## Version History

- v1.0 – Initial release

---

## Support

- For questions or technical support, please contact **support@boardoza.com**

---

## License

This repository contains both hardware and software components:

### **Hardware Design**

[![CC BY-SA 4.0][cc-by-sa-shield]][cc-by-sa]

All hardware design files are licensed under [Creative Commons Attribution-ShareAlike 4.0 International License][cc-by-sa].

[cc-by-sa]: http://creativecommons.org/licenses/by-sa/4.0/
[cc-by-sa-shield]: https://img.shields.io/badge/License-CC%20BY--SA%204.0-lightgrey.svg

### **Software/Firmware**

[![BSD-3-Clause][bsd-shield]][bsd]

All software and firmware are licensed under [BSD 3-Clause License][bsd].

[bsd]: https://opensource.org/licenses/BSD-3-Clause
[bsd-shield]: https://img.shields.io/badge/License-BSD%203--Clause-blue.svg
