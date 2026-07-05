#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "spi.h"
#include "epd.h"

// Pin definitions for CrowPanel ESP32-S3 with 2.9" E-Paper
#define BUTTON_1 2     // Button 1 pin
#define BUTTON_2 1     // Button 2 pin
#define LED_PIN 41     // LED pin
#define EPD_POWER_PIN 7 // E-Paper power control pin

// WiFi Access Point credentials
const char* AP_SSID = "CineTag_Config";  // WiFi hotspot name
const char* OS_VERSION = "0.1.0";

WebServer server(80);
Preferences preferences;

// Display parameters
String currentLetter = "A";
String currentFPS = "24FPS";
String currentFilter = "ND";
String currentStorage = "SSD";
String currentMainExtra = "";
String currentMainPrimaryMode = "FPS";
String currentMainPrimaryValue = "";
String currentFilterMode = "ND";
String currentFilterValue = "";
String currentStorageMode = "SSD";
String currentStorageValue = "";
String currentMainExtraMode = "NONE";
String currentMainExtraValue = "";
String currentMainZoneColor = "BLACK";
String currentFilterZoneColor = "WHITE";
String currentStorageZoneColor = "WHITE";
bool screensaverActive = false;
bool button2PendingSinglePress = false;
unsigned long lastButton2PressMs = 0;
const unsigned long BUTTON_2_DOUBLE_PRESS_MS = 350;

// Button state tracking
bool lastButton1State = HIGH;
bool lastButton2State = HIGH;

// Buffer for display
extern uint8_t ImageBW[ALLSCREEN_BYTES];

String normalizeLetter(const String& value) {
  if (value.length() == 0) return "A";
  char c = value.charAt(0);
  if (c >= 'a' && c <= 'z') c = (char)(c - 32);
  if (c < 'A' || c > 'F') return "A";
  return String(c);
}

String normalizeStorage(const String& value) {
  String v = value;
  v.trim();
  v.toUpperCase();
  if (v.length() == 0) return "SSD";
  if (v.length() > 10) v = v.substring(0, 10);
  return v;
}

String normalizeFilter(const String& value) {
  String v = value;
  v.trim();
  v.toUpperCase();
  if (v.length() == 0) return "ND";
  if (v.length() > 12) v = v.substring(0, 12);
  return v;
}

String normalizeInfoMode(const String& value) {
  String v = value;
  v.trim();
  v.toUpperCase();
  if (v == "FPS" || v == "ND" || v == "SSD" || v == "SHUTTER" || v == "SCENE" || v == "WB" || v == "CUSTOM") return v;
  return "CUSTOM";
}

String normalizeMainExtraMode(const String& value) {
  String v = value;
  v.trim();
  v.toUpperCase();
  if (v == "NONE" || v == "FPS" || v == "SHUTTER" || v == "WB" || v == "SCENE" || v == "ND" || v == "SSD" || v == "CUSTOM") return v;
  return "NONE";
}

String normalizeZoneColor(const String& value, const String& fallback) {
  String v = value;
  v.trim();
  v.toUpperCase();
  if (v == "BLACK" || v == "WHITE") return v;
  return fallback;
}

bool isZoneBlack(const String& value) {
  return normalizeZoneColor(value, "WHITE") == "BLACK";
}

bool isSupportedDisplayChar(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || c == ' ' || c == '/' || c == '.' || c == ':' || c == '_';
}

String normalizeInfoValue(const String& value, uint8_t maxLength) {
  String input = value;
  input.trim();
  input.toUpperCase();
  String out = "";
  for (uint16_t i = 0; i < input.length(); i++) {
    char c = input.charAt(i);
    if ((unsigned char)c > 127) c = ' ';
    if (c == '-') c = ' ';
    if (isSupportedDisplayChar(c)) out += c;
    if (out.length() >= maxLength) break;
  }
  out.trim();
  return out;
}

String formatFpsDisplayValue(const String& value) {
  String v = normalizeInfoValue(value, 10);
  if (v.length() == 0) return "24 FPS";
  if (v.endsWith("FPS")) {
    int fpsIndex = v.length() - 3;
    if (fpsIndex > 0 && v.charAt(fpsIndex - 1) != ' ') {
      v = v.substring(0, fpsIndex) + " FPS";
    }
  }
  return v;
}

String composeInfoText(const String& mode, const String& value, const String& fallback) {
  if (mode == "FPS") {
    return formatFpsDisplayValue(value);
  }
  if (mode == "ND") {
    String v = normalizeInfoValue(value, 12);
    if (v.length() == 0) return "ND0.6";
    return v;
  }
  if (mode == "SSD") {
    String v = normalizeInfoValue(value, 10);
    if (v == "SSD" || v == "HDD" || v == "SD") return v;
    if (v.length() == 0) return "SSD";
    return v;
  }
  if (mode == "SHUTTER") {
    String v = normalizeInfoValue(value, 8);
    if (v.length() == 0) return "180 DEG";
    return v;
  }
  if (mode == "SCENE") {
    String v = normalizeInfoValue(value, 8);
    if (v == "NIGHT" || v == "DAY") return v;
    return "DAY";
  }
  if (mode == "WB") {
    String v = normalizeInfoValue(value, 8);
    if (v.length() == 0) return "5600K";
    return v;
  }
  String v = normalizeInfoValue(value, 12);
  if (v.length() == 0) return fallback;
  return v;
}

String composeMainExtraText(const String& mode, const String& value) {
  if (mode == "NONE") return "";
  return composeInfoText(mode, value, "");
}

void refreshComputedTexts() {
  currentFPS = composeInfoText(currentMainPrimaryMode, currentMainPrimaryValue, "24FPS");
  currentFilter = composeInfoText(currentFilterMode, currentFilterValue, "ND");
  currentStorage = composeInfoText(currentStorageMode, currentStorageValue, "SSD");
  currentMainExtra = composeMainExtraText(currentMainExtraMode, currentMainExtraValue);
}

void cycleMainFpsPreset() {
  String currentPreset = formatFpsDisplayValue(currentMainPrimaryValue);
  currentMainPrimaryMode = "FPS";
  if (currentPreset == "24 FPS") {
    currentMainPrimaryValue = "30FPS";
  } else if (currentPreset == "30 FPS") {
    currentMainPrimaryValue = "50FPS";
  } else if (currentPreset == "50 FPS") {
    currentMainPrimaryValue = "60FPS";
  } else if (currentPreset == "60 FPS") {
    currentMainPrimaryValue = "120FPS";
  } else {
    currentMainPrimaryValue = "24FPS";
  }
  refreshComputedTexts();
}

void loadSettings() {
  preferences.begin("cinetag", true);
  currentLetter = normalizeLetter(preferences.getString("letter", currentLetter));
  currentMainPrimaryMode = normalizeInfoMode(preferences.getString("mainPrimaryMode", currentMainPrimaryMode));
  currentMainPrimaryValue = normalizeInfoValue(preferences.getString("mainPrimaryValue", currentMainPrimaryValue), 12);
  currentFilterMode = normalizeInfoMode(preferences.getString("filterMode", currentFilterMode));
  currentFilterValue = normalizeInfoValue(preferences.getString("filterValue", currentFilterValue), 12);
  currentStorageMode = normalizeInfoMode(preferences.getString("storageMode", currentStorageMode));
  currentStorageValue = normalizeInfoValue(preferences.getString("storageValue", currentStorageValue), 12);
  currentMainExtraMode = normalizeMainExtraMode(preferences.getString("mainExtraMode", currentMainExtraMode));
  currentMainExtraValue = normalizeInfoValue(preferences.getString("mainExtraValue", currentMainExtraValue), 12);
  currentMainZoneColor = normalizeZoneColor(preferences.getString("mainZoneColor", currentMainZoneColor), "BLACK");
  currentFilterZoneColor = normalizeZoneColor(preferences.getString("filterZoneColor", currentFilterZoneColor), "WHITE");
  currentStorageZoneColor = normalizeZoneColor(preferences.getString("storageZoneColor", currentStorageZoneColor), "WHITE");
  preferences.end();
  refreshComputedTexts();
}

void saveSettings() {
  preferences.begin("cinetag", false);
  preferences.putString("letter", currentLetter);
  preferences.putString("mainPrimaryMode", currentMainPrimaryMode);
  preferences.putString("mainPrimaryValue", currentMainPrimaryValue);
  preferences.putString("filterMode", currentFilterMode);
  preferences.putString("filterValue", currentFilterValue);
  preferences.putString("storageMode", currentStorageMode);
  preferences.putString("storageValue", currentStorageValue);
  preferences.putString("mainExtraMode", currentMainExtraMode);
  preferences.putString("mainExtraValue", currentMainExtraValue);
  preferences.putString("mainZoneColor", currentMainZoneColor);
  preferences.putString("filterZoneColor", currentFilterZoneColor);
  preferences.putString("storageZoneColor", currentStorageZoneColor);
  preferences.end();
}

const uint8_t GLYPH_SPACE[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const uint8_t GLYPH_DOT[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06};
const uint8_t GLYPH_COLON[7] = {0x00, 0x06, 0x06, 0x00, 0x06, 0x06, 0x00};
const uint8_t GLYPH_SLASH[7] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
const uint8_t GLYPH_UNDERSCORE[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
const uint8_t GLYPH_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
const uint8_t GLYPH_1[7] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
const uint8_t GLYPH_2[7] = {0x1E, 0x01, 0x01, 0x1E, 0x10, 0x10, 0x1F};
const uint8_t GLYPH_3[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
const uint8_t GLYPH_4[7] = {0x12, 0x12, 0x12, 0x1F, 0x02, 0x02, 0x02};
const uint8_t GLYPH_5[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
const uint8_t GLYPH_6[7] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
const uint8_t GLYPH_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
const uint8_t GLYPH_8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
const uint8_t GLYPH_9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C};
const uint8_t GLYPH_A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
const uint8_t GLYPH_B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
const uint8_t GLYPH_C[7] = {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F};
const uint8_t GLYPH_D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
const uint8_t GLYPH_E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
const uint8_t GLYPH_F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
const uint8_t GLYPH_G[7] = {0x0F, 0x10, 0x10, 0x13, 0x11, 0x11, 0x0F};
const uint8_t GLYPH_H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
const uint8_t GLYPH_I[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
const uint8_t GLYPH_K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
const uint8_t GLYPH_L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
const uint8_t GLYPH_M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
const uint8_t GLYPH_N[7] = {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11};
const uint8_t GLYPH_O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
const uint8_t GLYPH_P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
const uint8_t GLYPH_R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
const uint8_t GLYPH_S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
const uint8_t GLYPH_T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
const uint8_t GLYPH_U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
const uint8_t GLYPH_W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
const uint8_t GLYPH_X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
const uint8_t GLYPH_Y[7] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};

const uint8_t* glyphFor(char ch) {
  switch (ch) {
    case '0': return GLYPH_0;
    case '1': return GLYPH_1;
    case '2': return GLYPH_2;
    case '3': return GLYPH_3;
    case '4': return GLYPH_4;
    case '5': return GLYPH_5;
    case '6': return GLYPH_6;
    case '7': return GLYPH_7;
    case '8': return GLYPH_8;
    case '9': return GLYPH_9;
    case 'A': return GLYPH_A;
    case 'B': return GLYPH_B;
    case 'C': return GLYPH_C;
    case 'D': return GLYPH_D;
    case 'E': return GLYPH_E;
    case 'F': return GLYPH_F;
    case 'G': return GLYPH_G;
    case 'H': return GLYPH_H;
    case 'I': return GLYPH_I;
    case 'K': return GLYPH_K;
    case 'L': return GLYPH_L;
    case 'M': return GLYPH_M;
    case 'N': return GLYPH_N;
    case 'O': return GLYPH_O;
    case 'P': return GLYPH_P;
    case 'R': return GLYPH_R;
    case 'S': return GLYPH_S;
    case 'T': return GLYPH_T;
    case 'U': return GLYPH_U;
    case 'W': return GLYPH_W;
    case 'X': return GLYPH_X;
    case 'Y': return GLYPH_Y;
    case '.': return GLYPH_DOT;
    case ':': return GLYPH_COLON;
    case '/': return GLYPH_SLASH;
    case '_': return GLYPH_UNDERSCORE;
    case ' ': return GLYPH_SPACE;
    default: return GLYPH_SPACE;
  }
}

String displayText(String text) {
  text.toUpperCase();
  return text;
}

void drawGlyphPixelCW(uint16_t baseX, uint16_t baseY, uint8_t glyphX, uint8_t glyphY, uint8_t color, uint8_t scale) {
  uint16_t rx = (uint16_t)((6 - glyphY) * scale);
  uint16_t ry = (uint16_t)(glyphX * scale);
  for (uint8_t sy = 0; sy < scale; sy++) {
    for (uint8_t sx = 0; sx < scale; sx++) {
      EPD_DrawPoint((uint16_t)(baseX + rx + sx), (uint16_t)(baseY + ry + sy), color);
    }
  }
}

void drawGlyphPixelCWScaled(uint16_t baseX, uint16_t baseY, uint8_t glyphX, uint8_t glyphY, uint8_t color, uint8_t scaleX, uint8_t scaleY) {
  uint16_t rx = (uint16_t)((6 - glyphY) * scaleX);
  uint16_t ry = (uint16_t)(glyphX * scaleY);
  for (uint8_t sy = 0; sy < scaleY; sy++) {
    for (uint8_t sx = 0; sx < scaleX; sx++) {
      EPD_DrawPoint((uint16_t)(baseX + rx + sx), (uint16_t)(baseY + ry + sy), color);
    }
  }
}

void drawCharCW(uint16_t baseX, uint16_t baseY, char ch, uint8_t color, uint8_t scale) {
  const uint8_t* glyph = glyphFor(ch);
  for (uint8_t row = 0; row < 7; row++) {
    for (uint8_t col = 0; col < 5; col++) {
      if (glyph[row] & (1 << (4 - col))) {
        drawGlyphPixelCW(baseX, baseY, col, row, color, scale);
      }
    }
  }
}

void drawCharCWScaled(uint16_t baseX, uint16_t baseY, char ch, uint8_t color, uint8_t scaleX, uint8_t scaleY) {
  const uint8_t* glyph = glyphFor(ch);
  for (uint8_t row = 0; row < 7; row++) {
    for (uint8_t col = 0; col < 5; col++) {
      if (glyph[row] & (1 << (4 - col))) {
        drawGlyphPixelCWScaled(baseX, baseY, col, row, color, scaleX, scaleY);
      }
    }
  }
}

void drawCenteredStringCW(uint16_t xStart, uint16_t xEnd, uint16_t yStart, uint16_t yEnd, const String& textValue, uint8_t color, uint8_t scale, bool bold) {
  String text = displayText(textValue);
  uint16_t charAdvance = (uint16_t)(6 * scale);
  uint16_t stringWidth = text.length() ? (uint16_t)(text.length() * charAdvance - scale) : 0;
  uint16_t stringHeight = (uint16_t)(7 * scale);

  int baseX = (int)(xStart + ((xEnd - xStart) - stringHeight) / 2);
  int baseY = (int)(yStart + ((yEnd - yStart) - stringWidth) / 2);
  if (baseX < 0) baseX = 0;
  if (baseY < 0) baseY = 0;

  for (uint16_t i = 0; i < (uint16_t)text.length(); i++) {
    uint16_t charY = (uint16_t)(baseY + i * charAdvance);
    drawCharCW((uint16_t)baseX, charY, text.charAt(i), color, scale);
    if (bold) drawCharCW((uint16_t)(baseX + 1), charY, text.charAt(i), color, scale);
  }
}

uint8_t fittedScaleForTextCW(uint16_t xStart, uint16_t xEnd, uint16_t yStart, uint16_t yEnd, const String& textValue, uint8_t preferredScale) {
  String text = displayText(textValue);
  if (text.length() == 0) return preferredScale;

  uint16_t areaWidth = (xEnd > xStart) ? (uint16_t)(xEnd - xStart) : 0;
  uint16_t areaHeight = (yEnd > yStart) ? (uint16_t)(yEnd - yStart) : 0;
  if (areaWidth < 8 || areaHeight < 8) return 1;

  uint8_t scale = preferredScale;
  while (scale > 1) {
    uint16_t charAdvance = (uint16_t)(6 * scale);
    uint16_t stringWidth = (uint16_t)(text.length() * charAdvance - scale);
    uint16_t stringHeight = (uint16_t)(7 * scale);
    if (stringWidth <= areaHeight - 4 && stringHeight <= areaWidth - 4) break;
    scale--;
  }
  return scale;
}

void drawFittedCenteredStringCW(uint16_t xStart, uint16_t xEnd, uint16_t yStart, uint16_t yEnd, const String& textValue, uint8_t color, uint8_t preferredScale, bool bold) {
  uint8_t fittedScale = fittedScaleForTextCW(xStart, xEnd, yStart, yEnd, textValue, preferredScale);
  drawCenteredStringCW(xStart, xEnd, yStart, yEnd, textValue, color, fittedScale, bold);
}

void drawFittedCenteredSingleCharCWStretched(uint16_t xStart, uint16_t xEnd, uint16_t yStart, uint16_t yEnd, char ch, uint8_t color, uint8_t preferredScale, bool bold) {
  uint16_t areaWidth = (xEnd > xStart) ? (uint16_t)(xEnd - xStart) : 0;
  uint16_t areaHeight = (yEnd > yStart) ? (uint16_t)(yEnd - yStart) : 0;
  uint8_t scaleY = preferredScale;
  uint8_t scaleX = (uint8_t)((preferredScale * 5 + 3) / 4);

  while ((scaleX > 1 || scaleY > 1) && ((uint16_t)(7 * scaleX) > areaWidth - 4 || (uint16_t)(5 * scaleY) > areaHeight - 4)) {
    if (scaleY > 1) scaleY--;
    scaleX = (uint8_t)((scaleY * 5 + 3) / 4);
    if (scaleX < 1) scaleX = 1;
  }

  uint16_t charWidth = (uint16_t)(7 * scaleX);
  uint16_t charHeight = (uint16_t)(5 * scaleY);
  uint16_t baseX = (uint16_t)(xStart + ((areaWidth - charWidth) / 2));
  uint16_t baseY = (uint16_t)(yStart + ((areaHeight - charHeight) / 2));
  drawCharCWScaled(baseX, baseY, ch, color, scaleX, scaleY);
  if (bold) drawCharCWScaled((uint16_t)(baseX + 1), baseY, ch, color, scaleX, scaleY);
}

void drawThickRectangle(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint8_t color, uint8_t thickness) {
  for (uint8_t i = 0; i < thickness; i++) {
    if (xe <= xs + i * 2 || ye <= ys + i * 2) return;
    EPD_DrawRectangle((uint16_t)(xs + i), (uint16_t)(ys + i), (uint16_t)(xe - i), (uint16_t)(ye - i), color);
  }
}

void drawQuarterCirclePoints(uint16_t cx, uint16_t cy, uint16_t x, uint16_t y, uint8_t quadrant, uint8_t color) {
  if (quadrant == 0) {
    EPD_DrawPoint((uint16_t)(cx - x), (uint16_t)(cy - y), color);
    EPD_DrawPoint((uint16_t)(cx - y), (uint16_t)(cy - x), color);
  } else if (quadrant == 1) {
    EPD_DrawPoint((uint16_t)(cx + x), (uint16_t)(cy - y), color);
    EPD_DrawPoint((uint16_t)(cx + y), (uint16_t)(cy - x), color);
  } else if (quadrant == 2) {
    EPD_DrawPoint((uint16_t)(cx - x), (uint16_t)(cy + y), color);
    EPD_DrawPoint((uint16_t)(cx - y), (uint16_t)(cy + x), color);
  } else {
    EPD_DrawPoint((uint16_t)(cx + x), (uint16_t)(cy + y), color);
    EPD_DrawPoint((uint16_t)(cx + y), (uint16_t)(cy + x), color);
  }
}

void drawRoundedRectangle(uint16_t xs, uint16_t ys, uint16_t xe, uint16_t ye, uint8_t color, uint8_t thickness, uint8_t radius) {
  if (xe <= xs || ye <= ys) return;
  for (uint8_t i = 0; i < thickness; i++) {
    uint16_t x0 = (uint16_t)(xs + i);
    uint16_t y0 = (uint16_t)(ys + i);
    uint16_t x1 = (uint16_t)(xe - i);
    uint16_t y1 = (uint16_t)(ye - i);
    if (x1 <= x0 || y1 <= y0) return;

    uint8_t r = radius > i ? (uint8_t)(radius - i) : 1;
    uint16_t maxR = (uint16_t)(((x1 - x0) < (y1 - y0) ? (x1 - x0) : (y1 - y0)) / 2);
    if (r > maxR) r = (uint8_t)maxR;
    if (r < 1) {
      EPD_DrawRectangle(x0, y0, x1, y1, color);
      continue;
    }

    EPD_DrawLine((uint16_t)(x0 + r), y0, (uint16_t)(x1 - r), y0, color);
    EPD_DrawLine((uint16_t)(x0 + r), y1, (uint16_t)(x1 - r), y1, color);
    EPD_DrawLine(x0, (uint16_t)(y0 + r), x0, (uint16_t)(y1 - r), color);
    EPD_DrawLine(x1, (uint16_t)(y0 + r), x1, (uint16_t)(y1 - r), color);

    int x = 0;
    int y = r;
    int d = 3 - 2 * r;
    while (x <= y) {
      drawQuarterCirclePoints((uint16_t)(x0 + r), (uint16_t)(y0 + r), (uint16_t)x, (uint16_t)y, 0, color);
      drawQuarterCirclePoints((uint16_t)(x1 - r), (uint16_t)(y0 + r), (uint16_t)x, (uint16_t)y, 1, color);
      drawQuarterCirclePoints((uint16_t)(x0 + r), (uint16_t)(y1 - r), (uint16_t)x, (uint16_t)y, 2, color);
      drawQuarterCirclePoints((uint16_t)(x1 - r), (uint16_t)(y1 - r), (uint16_t)x, (uint16_t)y, 3, color);
      if (d < 0) {
        d += 4 * x + 6;
      } else {
        d += 4 * (x - y) + 10;
        y--;
      }
      x++;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Demarrage...");
  
  // Initialize buttons and LED
  pinMode(BUTTON_1, INPUT);
  pinMode(BUTTON_2, INPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // Initialize E-Paper power control
  pinMode(EPD_POWER_PIN, OUTPUT);
  digitalWrite(EPD_POWER_PIN, HIGH);  // Turn on E-Paper power
  delay(200);
  
  // Blink LED to indicate startup
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }

  loadSettings();
  
  Serial.println("Initialisation de l'ecran...");
  
  // Initialize display using the EPD functions
  EPD_GPIOInit();
  EPD_HW_RESET();
  delay(100);
  EPD_Init();
  EPD_ALL_Fill(WHITE);
  EPD_Update();
  EPD_Clear_R26H();
  delay(500);
  
  // Setup WiFi in AP mode
  Serial.println("Configuration du point d'acces WiFi...");
  WiFi.mode(WIFI_AP);
  bool result = WiFi.softAP(AP_SSID, "");
  if (result) {
    IPAddress myIP = WiFi.softAPIP();
    Serial.print("Adresse IP du point d'acces: ");
    Serial.println(myIP);
    Serial.println("Adresse MAC: " + WiFi.softAPmacAddress());
  } else {
    Serial.println("Echec de la creation du point d'acces WiFi");
    delay(3000);
  }
  
  // Setup web server routes
  setupServer();
  
  // Initial display update
  updateDisplay();
}

void setupServer() {
  // Serve the main page
  server.on("/", HTTP_GET, []() {
    String html = "<html><head><title>CineTag Display Control</title>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>body{font-family:Arial,sans-serif;margin:20px;background:#f6f6f6;color:#111;} .form-group{margin-bottom:15px;} label{display:block;margin-bottom:5px;font-weight:600;} input,select{width:100%;padding:10px;font-size:16px;box-sizing:border-box;border:1px solid #bbb;border-radius:8px;background:#fff;} .row{display:grid;grid-template-columns:1fr;gap:12px;} .hint{font-size:12px;color:#555;margin-top:4px;} .card{background:#fff;border-radius:14px;padding:16px;box-shadow:0 1px 4px rgba(0,0,0,0.08);margin-bottom:18px;} .preview-shell{display:flex;justify-content:center;margin-top:8px;} .screen-preview{width:160px;height:370px;border:8px solid #222;border-radius:18px;background:#fff;overflow:hidden;} .preview-main{background:#111;color:#fff;height:54%;display:flex;flex-direction:column;align-items:center;justify-content:center;border-bottom:2px solid #111;padding:14px 10px;gap:10px;text-align:center;box-sizing:border-box;} .preview-main-letter{font-size:54px;font-weight:700;line-height:1;} .preview-main-primary{font-size:22px;font-weight:600;line-height:1.1;} .preview-main-extra{font-size:18px;opacity:0.9;line-height:1.1;} .preview-secondary{height:23%;background:#fff;color:#111;display:flex;align-items:center;justify-content:center;font-size:12px;font-weight:600;position:relative;text-align:center;padding:5px;line-height:1.05;box-sizing:border-box;border:0 solid transparent;border-radius:0;} .preview-secondary span{position:relative;z-index:1;max-width:82px;word-break:break-word;overflow-wrap:anywhere;} .zone-color-row{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px;}</style>";
    html += "</head><body>";
    html += "<h2>CineTag Display</h2>";
    html += "<div class='card'><form action='/update' method='get'>";
    html += "<div class='row'>";
    html += "<div class='form-group'><label>Lettre (A-F)</label><select name='letter'>";
    html += "<option" + String(currentLetter == "A" ? " selected" : "") + ">A</option>";
    html += "<option" + String(currentLetter == "B" ? " selected" : "") + ">B</option>";
    html += "<option" + String(currentLetter == "C" ? " selected" : "") + ">C</option>";
    html += "<option" + String(currentLetter == "D" ? " selected" : "") + ">D</option>";
    html += "<option" + String(currentLetter == "E" ? " selected" : "") + ">E</option>";
    html += "<option" + String(currentLetter == "F" ? " selected" : "") + ">F</option>";
    html += "</select></div>";
    html += "<div class='form-group'><label>Zone principale haute</label><select name='mainPrimaryMode'>";
    html += "<option value='FPS'" + String(currentMainPrimaryMode == "FPS" ? " selected" : "") + ">FPS</option>";
    html += "<option value='SHUTTER'" + String(currentMainPrimaryMode == "SHUTTER" ? " selected" : "") + ">Shutter Angle</option>";
    html += "<option value='WB'" + String(currentMainPrimaryMode == "WB" ? " selected" : "") + ">White Balance</option>";
    html += "<option value='SCENE'" + String(currentMainPrimaryMode == "SCENE" ? " selected" : "") + ">Scene</option>";
    html += "<option value='ND'" + String(currentMainPrimaryMode == "ND" ? " selected" : "") + ">ND</option>";
    html += "<option value='SSD'" + String(currentMainPrimaryMode == "SSD" ? " selected" : "") + ">SSD</option>";
    html += "<option value='CUSTOM'" + String(currentMainPrimaryMode == "CUSTOM" ? " selected" : "") + ">Custom</option>";
    html += "</select><input id='mainPrimaryValue' type='text' name='mainPrimaryValue' maxlength='12' value='" + currentMainPrimaryValue + "' placeholder='24FPS / 180 / 5600K'><select id='mainPrimaryPreset' style='display:none;margin-top:8px;'></select><div id='mainPrimaryHint' class='hint'></div></div>";
    html += "<div class='form-group'><label>Info principale complementaire</label><select name='mainExtraMode'>";
    html += "<option value='NONE'" + String(currentMainExtraMode == "NONE" ? " selected" : "") + ">Aucune</option>";
    html += "<option value='SHUTTER'" + String(currentMainExtraMode == "SHUTTER" ? " selected" : "") + ">Shutter Angle</option>";
    html += "<option value='FPS'" + String(currentMainExtraMode == "FPS" ? " selected" : "") + ">FPS</option>";
    html += "<option value='WB'" + String(currentMainExtraMode == "WB" ? " selected" : "") + ">White Balance</option>";
    html += "<option value='SCENE'" + String(currentMainExtraMode == "SCENE" ? " selected" : "") + ">Scene</option>";
    html += "<option value='ND'" + String(currentMainExtraMode == "ND" ? " selected" : "") + ">ND</option>";
    html += "<option value='SSD'" + String(currentMainExtraMode == "SSD" ? " selected" : "") + ">SSD</option>";
    html += "<option value='CUSTOM'" + String(currentMainExtraMode == "CUSTOM" ? " selected" : "") + ">Custom</option>";
    html += "</select><input id='mainExtraValue' type='text' name='mainExtraValue' maxlength='12' value='" + currentMainExtraValue + "' placeholder='180'><select id='mainExtraPreset' style='display:none;margin-top:8px;'></select><div id='mainExtraHint' class='hint'></div></div>";
    html += "<div class='form-group'><label>Zone secondaire 1</label><select name='filterMode'>";
    html += "<option value='ND'" + String(currentFilterMode == "ND" ? " selected" : "") + ">ND</option>";
    html += "<option value='SSD'" + String(currentFilterMode == "SSD" ? " selected" : "") + ">SSD</option>";
    html += "<option value='SHUTTER'" + String(currentFilterMode == "SHUTTER" ? " selected" : "") + ">Shutter Angle</option>";
    html += "<option value='SCENE'" + String(currentFilterMode == "SCENE" ? " selected" : "") + ">Scene</option>";
    html += "<option value='WB'" + String(currentFilterMode == "WB" ? " selected" : "") + ">White Balance</option>";
    html += "<option value='FPS'" + String(currentFilterMode == "FPS" ? " selected" : "") + ">FPS</option>";
    html += "<option value='CUSTOM'" + String(currentFilterMode == "CUSTOM" ? " selected" : "") + ">Custom</option>";
    html += "</select><input id='filterValue' type='text' name='filterValue' maxlength='12' value='" + currentFilterValue + "' placeholder='180 / DAY / 5600K'><select id='filterPreset' style='display:none;margin-top:8px;'></select><div id='filterHint' class='hint'></div></div>";
    html += "<div class='form-group'><label>Zone secondaire 2</label><select name='storageMode'>";
    html += "<option value='SSD'" + String(currentStorageMode == "SSD" ? " selected" : "") + ">SSD</option>";
    html += "<option value='ND'" + String(currentStorageMode == "ND" ? " selected" : "") + ">ND</option>";
    html += "<option value='SHUTTER'" + String(currentStorageMode == "SHUTTER" ? " selected" : "") + ">Shutter Angle</option>";
    html += "<option value='SCENE'" + String(currentStorageMode == "SCENE" ? " selected" : "") + ">Scene</option>";
    html += "<option value='WB'" + String(currentStorageMode == "WB" ? " selected" : "") + ">White Balance</option>";
    html += "<option value='FPS'" + String(currentStorageMode == "FPS" ? " selected" : "") + ">FPS</option>";
    html += "<option value='CUSTOM'" + String(currentStorageMode == "CUSTOM" ? " selected" : "") + ">Custom</option>";
    html += "</select><input id='storageValue' type='text' name='storageValue' maxlength='12' value='" + currentStorageValue + "' placeholder='SSD / NIGHT / 3200K'><select id='storagePreset' style='display:none;margin-top:8px;'></select><div id='storageHint' class='hint'></div></div>";
    html += "</div>";
    html += "<div class='zone-color-row'>";
    html += "<div class='form-group'><label>Couleur zone principale</label><select name='mainZoneColor'>";
    html += "<option value='BLACK'" + String(currentMainZoneColor == "BLACK" ? " selected" : "") + ">Noir</option>";
    html += "<option value='WHITE'" + String(currentMainZoneColor == "WHITE" ? " selected" : "") + ">Blanc</option>";
    html += "</select></div>";
    html += "<div class='form-group'><label>Couleur zone secondaire 1</label><select name='filterZoneColor'>";
    html += "<option value='WHITE'" + String(currentFilterZoneColor == "WHITE" ? " selected" : "") + ">Blanc</option>";
    html += "<option value='BLACK'" + String(currentFilterZoneColor == "BLACK" ? " selected" : "") + ">Noir</option>";
    html += "</select></div>";
    html += "<div class='form-group'><label>Couleur zone secondaire 2</label><select name='storageZoneColor'>";
    html += "<option value='WHITE'" + String(currentStorageZoneColor == "WHITE" ? " selected" : "") + ">Blanc</option>";
    html += "<option value='BLACK'" + String(currentStorageZoneColor == "BLACK" ? " selected" : "") + ">Noir</option>";
    html += "</select></div>";
    html += "</div>";
    html += "<input type='submit' value='Update' style='background:#111;color:#fff;border:0;border-radius:10px;font-weight:700;cursor:pointer;'>";
    html += "</form></div>";
    html += "<div class='card'><h3 style='margin-top:0'>Apercu</h3><div class='preview-shell'><div class='screen-preview'><div class='preview-main'><div class='preview-main-letter' id='previewLetter'>A</div><div class='preview-main-primary' id='previewMainPrimary'>24FPS</div><div class='preview-main-extra' id='previewMainExtra'></div></div><div class='preview-secondary' id='previewFilterZone'><span id='previewFilter'>ND</span></div><div class='preview-secondary' id='previewStorageZone'><span id='previewStorage'>SSD</span></div></div></div></div>";
    html += "<script>";
    html += "const configs={NONE:{placeholder:'',hint:'Aucune valeur necessaire',disabled:true},FPS:{placeholder:'24FPS',hint:'Choix: 24FPS, 30FPS, 50FPS, 60FPS, 120FPS',disabled:false},SHUTTER:{placeholder:'180 DEG',hint:'Choix: 45 DEG, 90 DEG, 180 DEG',disabled:false},WB:{placeholder:'5600K',hint:'Choisissez une balance des blancs en Kelvin',disabled:false},SCENE:{placeholder:'DAY',hint:'Valeurs conseillees: DAY ou NIGHT',disabled:false},ND:{placeholder:'ND0.6',hint:'Choisissez un filtre ND courant',disabled:false},SSD:{placeholder:'SSD',hint:'Choisissez le type de stockage',disabled:false},CUSTOM:{placeholder:'Texte libre',hint:'Texte personnalise',disabled:false}};";
    html += "const presets={FPS:['24FPS','30FPS','50FPS','60FPS','120FPS'],SHUTTER:['45 DEG','90 DEG','180 DEG'],WB:['2500K','3200K','4600K','5600K'],SSD:['SSD','HDD','SD'],ND:['ND0.3','ND0.6','ND0.9','ND1.2','ND1.5','ND1.8','ND2.1'],SCENE:['DAY','NIGHT']};";
    html += "function composePreview(mode,value,fallback){value=(value||'').trim().toUpperCase();if(mode==='NONE')return '';if(mode==='FPS')return value||'24FPS';if(mode==='ND')return value||'ND0.6';if(mode==='SSD')return value||'SSD';if(mode==='SHUTTER')return value||'180 DEG';if(mode==='SCENE')return value||'DAY';if(mode==='WB')return value||'5600K';if(mode==='CUSTOM')return value||fallback||'';return value||fallback||'';}";
    html += "function syncPresetValue(mode,input,preset){if(!presets[mode])return;const current=(input.value||'').trim().toUpperCase();const normalized=presets[mode].map(function(v){return v.toUpperCase();});const index=normalized.indexOf(current);preset.value=index>=0?presets[mode][index]:presets[mode][0];if(!current&&presets[mode].length){input.value=presets[mode][0];}}";
    html += "function fillPreset(mode,preset){preset.innerHTML='';(presets[mode]||[]).forEach(function(value){const option=document.createElement('option');option.value=value;option.textContent=value;preset.appendChild(option);});}";
    html += "function applyField(selectName,inputId,presetId,hintId){const mode=document.querySelector('[name=\"'+selectName+'\"]').value;const input=document.getElementById(inputId);const preset=document.getElementById(presetId);const hint=document.getElementById(hintId);const cfg=configs[mode]||configs.CUSTOM;input.placeholder=cfg.placeholder;input.disabled=cfg.disabled;hint.textContent=cfg.hint;if(cfg.disabled){input.value='';}if(presets[mode]){fillPreset(mode,preset);input.style.display='none';preset.style.display='block';syncPresetValue(mode,input,preset);}else{input.style.display=cfg.disabled?'none':'block';preset.style.display='none';preset.innerHTML='';}}";
    html += "function applyZonePreviewColors(){const mainBg=document.querySelector('[name=\"mainZoneColor\"]').value;const filterBg=document.querySelector('[name=\"filterZoneColor\"]').value;const storageBg=document.querySelector('[name=\"storageZoneColor\"]').value;const main=document.querySelector('.preview-main');const filter=document.getElementById('previewFilterZone');const storage=document.getElementById('previewStorageZone');main.style.background=(mainBg==='BLACK')?'#111':'#fff';main.style.color=(mainBg==='BLACK')?'#fff':'#111';main.style.border='0';main.style.borderRadius='0';filter.style.background=(filterBg==='BLACK')?'#111':'#fff';filter.style.color=(filterBg==='BLACK')?'#fff':'#111';storage.style.background=(storageBg==='BLACK')?'#111':'#fff';storage.style.color=(storageBg==='BLACK')?'#fff':'#111';filter.style.border='0';storage.style.border='0';filter.style.borderRadius='0';storage.style.borderRadius='0';filter.style.margin='0';storage.style.margin='0';if(filterBg==='WHITE'&&storageBg==='WHITE'){filter.style.borderTop='3px solid #111';filter.style.borderLeft='3px solid #111';filter.style.borderRight='3px solid #111';filter.style.borderBottom='0';storage.style.borderTop='3px solid #111';storage.style.borderLeft='3px solid #111';storage.style.borderRight='3px solid #111';storage.style.borderBottom='3px solid #111';}else if(filterBg==='WHITE'){filter.style.border='3px solid #111';}else if(storageBg==='WHITE'){storage.style.border='3px solid #111';}}";
    html += "function updatePreview(){const letter=document.querySelector('[name=\"letter\"]').value;const mainPrimary=composePreview(document.querySelector('[name=\"mainPrimaryMode\"]').value,document.getElementById('mainPrimaryValue').value,'24FPS');const mainExtra=composePreview(document.querySelector('[name=\"mainExtraMode\"]').value,document.getElementById('mainExtraValue').value,'');const filter=composePreview(document.querySelector('[name=\"filterMode\"]').value,document.getElementById('filterValue').value,'ND0.6');const storage=composePreview(document.querySelector('[name=\"storageMode\"]').value,document.getElementById('storageValue').value,'SSD');document.getElementById('previewLetter').textContent=letter;document.getElementById('previewMainPrimary').textContent=mainPrimary;document.getElementById('previewMainExtra').textContent=mainExtra;document.getElementById('previewMainExtra').style.display=mainExtra?'block':'none';document.getElementById('previewFilter').textContent=filter;document.getElementById('previewStorage').textContent=storage;applyZonePreviewColors();}";
    html += "const fieldMap=[['mainPrimaryMode','mainPrimaryValue','mainPrimaryPreset','mainPrimaryHint'],['mainExtraMode','mainExtraValue','mainExtraPreset','mainExtraHint'],['filterMode','filterValue','filterPreset','filterHint'],['storageMode','storageValue','storagePreset','storageHint']];";
    html += "fieldMap.forEach(function(cfg){document.querySelector('[name=\"'+cfg[0]+'\"]').addEventListener('change',function(){applyField(cfg[0],cfg[1],cfg[2],cfg[3]);updatePreview();});document.getElementById(cfg[1]).addEventListener('input',function(){const mode=document.querySelector('[name=\"'+cfg[0]+'\"]').value;syncPresetValue(mode,document.getElementById(cfg[1]),document.getElementById(cfg[2]));updatePreview();});document.getElementById(cfg[2]).addEventListener('change',function(){document.getElementById(cfg[1]).value=this.value;updatePreview();});});";
    html += "document.querySelector('[name=\"letter\"]').addEventListener('change',updatePreview);document.querySelector('[name=\"mainZoneColor\"]').addEventListener('change',updatePreview);document.querySelector('[name=\"filterZoneColor\"]').addEventListener('change',updatePreview);document.querySelector('[name=\"storageZoneColor\"]').addEventListener('change',updatePreview);";
    html += "fieldMap.forEach(function(cfg){applyField(cfg[0],cfg[1],cfg[2],cfg[3]);});updatePreview();";
    html += "</script></body></html>";
    server.send(200, "text/html", html);
  });
  
  // Handle updates
  server.on("/update", HTTP_GET, []() {
    if (server.hasArg("letter")) currentLetter = normalizeLetter(server.arg("letter"));
    if (server.hasArg("mainPrimaryMode")) currentMainPrimaryMode = normalizeInfoMode(server.arg("mainPrimaryMode"));
    if (server.hasArg("mainPrimaryValue")) currentMainPrimaryValue = normalizeInfoValue(server.arg("mainPrimaryValue"), 12);
    if (server.hasArg("mainExtraMode")) currentMainExtraMode = normalizeMainExtraMode(server.arg("mainExtraMode"));
    if (server.hasArg("mainExtraValue")) currentMainExtraValue = normalizeInfoValue(server.arg("mainExtraValue"), 12);
    if (server.hasArg("filterMode")) currentFilterMode = normalizeInfoMode(server.arg("filterMode"));
    if (server.hasArg("filterValue")) currentFilterValue = normalizeInfoValue(server.arg("filterValue"), 12);
    if (server.hasArg("storageMode")) currentStorageMode = normalizeInfoMode(server.arg("storageMode"));
    if (server.hasArg("storageValue")) currentStorageValue = normalizeInfoValue(server.arg("storageValue"), 12);
    if (server.hasArg("mainZoneColor")) currentMainZoneColor = normalizeZoneColor(server.arg("mainZoneColor"), "BLACK");
    if (server.hasArg("filterZoneColor")) currentFilterZoneColor = normalizeZoneColor(server.arg("filterZoneColor"), "WHITE");
    if (server.hasArg("storageZoneColor")) currentStorageZoneColor = normalizeZoneColor(server.arg("storageZoneColor"), "WHITE");
    refreshComputedTexts();
    
    // Blink LED to indicate update
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    
    saveSettings();
    updateDisplay();
    server.sendHeader("Location", "/");
    server.send(303);
  });
  
  server.begin();
  Serial.println("Serveur HTTP demarre");
}

void updateDisplay() {
  Serial.println("Mise a jour de l'affichage...");
  Serial.println("Lettre: " + currentLetter);
  Serial.println("FPS: " + currentFPS);
  Serial.println("Filtre: " + currentFilter);
  Serial.println("Stockage: " + currentStorage);
  
  // Reinitialisation complete de l'ecran
  EPD_HW_RESET();
  delay(300);
  EPD_Init();
  delay(200);
  
  // Initialiser le buffer en blanc
  for (int i = 0; i < ALLSCREEN_BYTES; i++) {
    ImageBW[i] = 0x00;
  }
  
  int screenWidth = EPD_W;
  int screenHeight = EPD_H;

  int storageWidth = 50;
  int filterWidth = 54;
  int fpsWidth = 72;
  int letterWidth = screenWidth - storageWidth - filterWidth - fpsWidth;

  uint16_t x0 = 0;
  uint16_t x1 = (uint16_t)storageWidth;
  uint16_t x2 = (uint16_t)(storageWidth + filterWidth);
  uint16_t x3 = (uint16_t)(storageWidth + filterWidth + fpsWidth);
  uint16_t x4 = (uint16_t)screenWidth;

  uint16_t y0 = 0;
  uint16_t y1 = (uint16_t)screenHeight;

  if (screensaverActive) {
    EPD_Clear(0, y0, (uint16_t)(screenWidth - 1), y1, BLACK);
    String ssidText = "SSID: " + String(AP_SSID);
    String ipText = "IP: " + WiFi.softAPIP().toString();
    String osText = "OS " + String(OS_VERSION);
    uint16_t osBandHeight = 10;
    uint16_t ipBandHeight = 10;
    uint16_t ssidBandHeight = 10;
    uint16_t byBandHeight = 12;
    uint16_t gapMeta = 2;
    uint16_t gapBelowBy = 8;
    uint16_t gapLogoToBy = 3;
    uint16_t wordBandHeight = 38;
    uint16_t wordGap = 1;
    uint16_t groupHeight = (uint16_t)(wordBandHeight + wordGap + wordBandHeight + gapLogoToBy + byBandHeight + gapBelowBy + osBandHeight + gapMeta + ssidBandHeight + gapMeta + ipBandHeight);
    uint16_t groupTop = (uint16_t)((screenWidth - groupHeight) / 2);
    uint16_t ipStart = groupTop;
    uint16_t ipEnd = (uint16_t)(ipStart + ipBandHeight);
    uint16_t ssidStart = (uint16_t)(ipEnd + gapMeta);
    uint16_t ssidEnd = (uint16_t)(ssidStart + ssidBandHeight);
    uint16_t osStart = (uint16_t)(ssidEnd + gapMeta);
    uint16_t osEnd = (uint16_t)(osStart + osBandHeight);
    uint16_t byStart = (uint16_t)(osEnd + gapBelowBy);
    uint16_t byEnd = (uint16_t)(byStart + byBandHeight);
    uint16_t tagStart = (uint16_t)(byEnd + gapLogoToBy);
    uint16_t tagEnd = (uint16_t)(tagStart + wordBandHeight);
    uint16_t camStart = (uint16_t)(tagEnd + wordGap);
    uint16_t camEnd = (uint16_t)(camStart + wordBandHeight);
    uint16_t boxInsetY = 18;
    drawRoundedRectangle((uint16_t)(tagStart - 1), (uint16_t)(y0 + boxInsetY), (uint16_t)(camEnd + 1), (uint16_t)(y1 - boxInsetY - 1), WHITE, 2, 8);
    drawFittedCenteredStringCW(ipStart, ipEnd, y0, y1, ipText, WHITE, 1, false);
    drawFittedCenteredStringCW(ssidStart, ssidEnd, y0, y1, ssidText, WHITE, 1, false);
    drawFittedCenteredStringCW(osStart, osEnd, y0, y1, osText, WHITE, 1, false);
    drawFittedCenteredStringCW(byStart, byEnd, y0, y1, "BY BATEAST", WHITE, 1, false);
    drawFittedCenteredStringCW(tagStart, tagEnd, y0, y1, "TAG", WHITE, 5, true);
    drawFittedCenteredStringCW(camStart, camEnd, y0, y1, "CAM", WHITE, 5, true);
    EPD_DisplayImage(ImageBW);
    EPD_Update();
    EPD_Clear_R26H();
    delay(1000);
    Serial.println("Mise a jour terminee");
    return;
  }

  uint8_t mainBg = isZoneBlack(currentMainZoneColor) ? BLACK : WHITE;
  uint8_t mainText = (mainBg == BLACK) ? WHITE : BLACK;
  uint8_t filterBg = isZoneBlack(currentFilterZoneColor) ? BLACK : WHITE;
  uint8_t filterText = (filterBg == BLACK) ? WHITE : BLACK;
  uint8_t storageBg = isZoneBlack(currentStorageZoneColor) ? BLACK : WHITE;
  uint8_t storageText = (storageBg == BLACK) ? WHITE : BLACK;

  EPD_Clear(x0, y0, x1, y1, storageBg);
  if (storageBg == WHITE && filterBg != WHITE) {
    drawThickRectangle(x0, y0, (uint16_t)(x1 - 1), (uint16_t)(y1 - 1), storageText, 3);
  }

  EPD_Clear(x1, y0, x2, y1, filterBg);
  if (filterBg == WHITE && storageBg != WHITE) {
    drawThickRectangle(x1, y0, (uint16_t)(x2 - 1), (uint16_t)(y1 - 1), filterText, 3);
  }
  if (storageBg == WHITE && filterBg == WHITE) {
    drawThickRectangle(x0, y0, (uint16_t)(x2 - 1), (uint16_t)(y1 - 1), BLACK, 3);
    EPD_DrawLine((uint16_t)(x1 - 1), y0, (uint16_t)(x1 - 1), (uint16_t)(y1 - 1), BLACK);
    EPD_DrawLine(x1, y0, x1, (uint16_t)(y1 - 1), BLACK);
    EPD_DrawLine((uint16_t)(x1 + 1), y0, (uint16_t)(x1 + 1), (uint16_t)(y1 - 1), BLACK);
  }
  drawCenteredStringCW(x0, x1, y0, y1, currentStorage, storageText, 2, false);
  drawCenteredStringCW(x1, x2, y0, y1, currentFilter, filterText, 2, false);

  EPD_Clear(x2, y0, x4, y1, mainBg);
  if (currentMainExtra.length() > 0) {
    uint16_t splitGap = 6;
    uint16_t midX = (uint16_t)(x2 + (x3 - x2) / 2);
    uint16_t topStart = (uint16_t)(midX + splitGap / 2);
    uint16_t bottomEnd = (uint16_t)(midX - splitGap / 2);
    // In portrait view, a split on X becomes a vertical stack under the letter.
    drawFittedCenteredStringCW(topStart, x3, y0, y1, currentFPS, mainText, 2, false);
    drawFittedCenteredStringCW(x2, bottomEnd, y0, y1, currentMainExtra, mainText, 2, false);
  } else {
    drawFittedCenteredStringCW(x2, x3, y0, y1, currentFPS, mainText, 3, false);
  }
  drawFittedCenteredSingleCharCWStretched(x3, x4, y0, y1, currentLetter.length() ? currentLetter.charAt(0) : 'A', mainText, 8, true);
  
  // Mise a jour de l'affichage - utiliser les deux fonctions dans le bon ordre
  EPD_DisplayImage(ImageBW);
  EPD_Update();
  EPD_Clear_R26H();
  delay(1000);
  
  Serial.println("Mise a jour terminee");
}

void loop() {
  server.handleClient();
  
  // Check button 1 (cycle through letters)
  bool currentButton1State = digitalRead(BUTTON_1);
  if (currentButton1State == LOW && lastButton1State == HIGH) {
    if (screensaverActive) {
      screensaverActive = false;
      button2PendingSinglePress = false;
      digitalWrite(LED_PIN, HIGH);
      updateDisplay();
      delay(100);
      digitalWrite(LED_PIN, LOW);
      lastButton1State = currentButton1State;
      delay(50);
      return;
    }
    // Button 1 pressed - cycle letter
    char c = currentLetter.length() ? currentLetter.charAt(0) : 'A';
    if (c < 'A' || c > 'F') c = 'A';
    c = (c == 'F') ? 'A' : (char)(c + 1);
    currentLetter = String(c);
    digitalWrite(LED_PIN, HIGH);
    saveSettings();
    updateDisplay();
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }
  lastButton1State = currentButton1State;
  
  // Check button 2 (cycle through FPS)
  bool currentButton2State = digitalRead(BUTTON_2);
  if (currentButton2State == LOW && lastButton2State == HIGH) {
    unsigned long now = millis();
    if (screensaverActive) {
      screensaverActive = false;
      button2PendingSinglePress = false;
      digitalWrite(LED_PIN, HIGH);
      updateDisplay();
      delay(100);
      digitalWrite(LED_PIN, LOW);
    } else if (button2PendingSinglePress && (now - lastButton2PressMs) <= BUTTON_2_DOUBLE_PRESS_MS) {
      button2PendingSinglePress = false;
      screensaverActive = true;
      digitalWrite(LED_PIN, HIGH);
      updateDisplay();
      delay(100);
      digitalWrite(LED_PIN, LOW);
    } else {
      button2PendingSinglePress = true;
      lastButton2PressMs = now;
    }
  }

  if (!screensaverActive && button2PendingSinglePress && (millis() - lastButton2PressMs) > BUTTON_2_DOUBLE_PRESS_MS) {
    button2PendingSinglePress = false;
    cycleMainFpsPreset();
    digitalWrite(LED_PIN, HIGH);
    saveSettings();
    updateDisplay();
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }
  lastButton2State = currentButton2State;
  
  delay(50); // Small delay to prevent button bounce
}
