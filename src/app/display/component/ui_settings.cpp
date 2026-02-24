#include "ui_screens.h"

#include "ui_common.h"

namespace app::display::ui_component {

void renderSettings(DisplayStateData& state, uint8_t focusIndex) {
  tft.fillScreen(colorBackground());

  const int width = tft.width();
  const int margin = 10;
  const int radius = 10;

  const uint16_t titleColor = tft.color565(34, 34, 44);
  tft.fillRoundRect(margin, margin, width - (margin * 2), 28, radius, titleColor);
  tft.setTextDatum(ML_DATUM);
  tft.setTextColor(TFT_WHITE, titleColor);
  tft.drawString("SETTINGS / HW TEST", margin + 10, margin + 14, 2);

  const int inputPanelY = margin + 34;
  const int inputPanelH = 116;
  const uint16_t inputPanelColor = tft.color565(20, 45, 66);
  tft.fillRoundRect(margin, inputPanelY, width - (margin * 2), inputPanelH, radius, inputPanelColor);

  const int barX = margin + 58;
  const int barW = width - barX - 16;
  const int barH = 10;

  auto drawAxisBar = [&](const char* label, int y, int16_t value, uint16_t fillColor) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, inputPanelColor);
    tft.drawString(label, margin + 10, y - 1, 2);

    const int centerX = barX + (barW / 2);
    tft.fillRoundRect(barX, y, barW, barH, 6, tft.color565(12, 28, 43));
    tft.drawFastVLine(centerX, y + 1, barH - 2, tft.color565(130, 170, 200));

    int fillPixels = (value * (barW / 2)) / 100;
    if (fillPixels > 0) {
      tft.fillRect(centerX, y + 2, fillPixels, barH - 4, fillColor);
    } else if (fillPixels < 0) {
      tft.fillRect(centerX + fillPixels, y + 2, -fillPixels, barH - 4, fillColor);
    }

    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_WHITE, inputPanelColor);
    tft.drawString(String(value), width - 14, y + (barH / 2), 2);
  };

  drawAxisBar("A1 X", inputPanelY + 8, state.inputAnalogX, tft.color565(0, 170, 255));
  drawAxisBar("A1 Y", inputPanelY + 28, state.inputAnalogY, tft.color565(80, 220, 120));
  drawAxisBar("A2 X", inputPanelY + 48, state.inputAnalog2X, tft.color565(255, 180, 0));
  drawAxisBar("A2 Y", inputPanelY + 68, state.inputAnalog2Y, tft.color565(230, 120, 255));

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, inputPanelColor);
  tft.drawString("BTN U", margin + 10, inputPanelY + 88, 2);
  tft.drawString("D", margin + 64, inputPanelY + 88, 2);
  tft.drawString("S", margin + 86, inputPanelY + 88, 2);
  tft.drawString("B", margin + 108, inputPanelY + 88, 2);

  auto drawIndicator = [&](int x, bool active) {
    tft.fillRoundRect(x, inputPanelY + 102, 14, 10, 4, active ? tft.color565(80, 220, 120) : tft.color565(70, 70, 70));
  };

  drawIndicator(margin + 42, state.inputButtonUp);
  drawIndicator(margin + 62, state.inputButtonDown);
  drawIndicator(margin + 84, state.inputButtonSelect);
  drawIndicator(margin + 106, state.inputButtonBack);

  const int setPanelY = inputPanelY + inputPanelH + 8;
  const int rowH = 22;
  const int rowW = width - (margin * 2);

  struct SettingRow {
    const char* label;
    int value;
  } rows[3] = {
      {"RENDER MIN MS", state.uiRenderMinIntervalMs},
      {"ANALOG DEADZONE", state.uiAnalogDeadzone},
      {"NAV THRESHOLD", state.uiAnalogNavThreshold},
  };

  for (int i = 0; i < 3; ++i) {
    const int y = setPanelY + (i * (rowH + 4));
    const bool focused = (focusIndex % 3) == static_cast<uint8_t>(i);
    const uint16_t rowColor = focused ? tft.color565(0, 120, 215) : tft.color565(35, 35, 35);
    tft.fillRoundRect(margin, y, rowW, rowH, 8, rowColor);

    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_WHITE, rowColor);
    tft.drawString(rows[i].label, margin + 10, y + (rowH / 2), 2);

    tft.setTextDatum(MR_DATUM);
    tft.drawString(String(rows[i].value), width - margin - 10, y + (rowH / 2), 2);
  }

  tft.setTextDatum(MC_DATUM);
  const uint16_t hintColor = tft.color565(160, 160, 160);
  tft.setTextColor(state.uiSettingsEditMode ? tft.color565(255, 220, 120) : hintColor, colorBackground());
  tft.drawString(state.uiSettingsEditMode ? "EDIT MODE: UP/DOWN or ANALOG adjust" : "SELECT to edit, BACK to home", width / 2, 232, 1);
}

}  // namespace app::display::ui_component
