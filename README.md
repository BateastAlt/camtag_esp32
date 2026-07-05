# [PROJECT BANNER PLACEHOLDER]

<p align="center">
  <img src="https://via.placeholder.com/1200x320?text=Project+Banner" alt="Project banner placeholder">
</p>

<p align="center">
  <img src="https://via.placeholder.com/180x180?text=Logo" alt="Project logo placeholder">
</p>

# CineTag

Custom firmware for the Elecrow CrowPanel ESP32-S3 2.9" e-paper display, designed as a compact on-set camera info tag.

The device displays a large camera label plus configurable production metadata on a portrait e-paper screen, and exposes a built-in web interface for live configuration over Wi-Fi.

## Overview

CineTag turns an Elecrow CrowPanel ESP32 2.9" e-paper module into a configurable camera slate companion.

The screen can show:

- A large letter from `A` to `F`
- A primary information block such as `24 FPS`, `5600K`, or `180 DEG`
- An optional secondary line under the primary information
- Two secondary info zones such as `ND`, `SSD`, `SCENE`, `WHITE BALANCE`, or custom text

The firmware also includes:

- A built-in Wi-Fi access point
- A web configuration page hosted directly on the device
- Persistent settings storage
- A screensaver mode with branding and connection info
- Hardware button shortcuts for fast on-device control

## Hardware

Target hardware:

- Elecrow CrowPanel ESP32-S3 2.9" E-paper HMI Display
- Resolution: `128 x 296`
- Display orientation used by the firmware: portrait

Main firmware location:

- [epaper_display.ino](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/epaper_display.ino)

Supporting display driver files:

- [EPD.cpp](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/EPD.cpp)
- [EPD.h](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/EPD.h)
- [EPD_Init.cpp](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/EPD_Init.cpp)
- [EPD_Init.h](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/EPD_Init.h)
- [spi.cpp](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/spi.cpp)
- [spi.h](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/spi.h)

## Features

- Portrait UI optimized for on-camera readability
- Large primary letter display
- Configurable main info and secondary info fields
- User-selectable black or white zone backgrounds
- Secondary zones with bordered styling
- Web preview before pushing changes to the display
- Select lists for common values:
  - `FPS`
  - `White Balance`
  - `Shutter Angle`
  - `Storage`
  - `ND filters`
- Screensaver with:
  - `CAM / TAG` logo
  - `BY BATEAST`
  - `OS`
  - `SSID`
  - `IP`

## Web Interface

When powered on, the firmware starts its own Wi-Fi access point:

- SSID: `CineTag_Config`

Then open the device web page in a browser:

- `http://192.168.4.1/`

From the web UI, you can configure:

- Main letter
- Main zone colors
- Main primary info type and value
- Main secondary info type and value
- Secondary zone 1 type, value, and color
- Secondary zone 2 type, value, and color
- Preview layout before updating the e-paper display

## Button Controls

Current hardware button behavior:

- `Button 1`: cycles the main letter
- `Button 2`: cycles FPS presets on single press
- `Button 2`: opens screensaver on fast double press
- `Button 1` or `Button 2`: exits screensaver

## Supported Info Types

Depending on the selected zone, the firmware can display:

- `FPS`
- `Shutter Angle`
- `White Balance`
- `Scene`
- `ND`
- `SSD`
- `Custom`

Examples:

- `24 FPS`
- `180 DEG`
- `5600K`
- `DAY`
- `ND0.6`
- `SSD`

## Flashing

Typical Arduino IDE workflow:

1. Open [epaper_display.ino](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/epaper_display.ino)
2. Select the correct ESP32-S3 board in Arduino IDE
3. Check the selected COM / serial port
4. Compile the project
5. Flash the firmware to the device

If your display shows shifted lines or incorrect output, verify the pin profile in:

- [spi.h](file:///Users/baptiste/Desktop/Dev/Cinetag/src/firmware/camtag_esp32s3/spi.h)


