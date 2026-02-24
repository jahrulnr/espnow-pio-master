#include "ui_screens.h"

#include "ui_common.h"
#include "ui_weather_icon.h"

namespace app::display::ui_component {

void renderHomeWeather(DisplayStateData& state, uint8_t focusIndex) {
  tft.fillScreen(colorBackground());

  const int width = tft.width();
  const int height = tft.height();
  const int margin = 10;
  const int gutter = 8;
  const int radius = 12;

  const int heroX = margin;
  const int heroY = margin;
  const int heroW = width - (margin * 2);
  const int heroH = 136;

  const int metricsY = heroY + heroH + gutter;
  const int metricsH = height - metricsY - margin;
  const int metricsW = (heroW - (gutter * 2)) / 3;
  const int tempX = heroX;
  const int humX = heroX + metricsW + gutter;
  const int battX = humX + metricsW + gutter;

  const uint16_t heroColor = colorTileBlue();
  const uint16_t tempColor = colorTileCyan();
  const uint16_t humColor = colorTileGreen();
  const uint16_t battColor = tft.color565(198, 134, 0);

  tft.fillRoundRect(heroX, heroY, heroW, heroH, radius, heroColor);
  tft.fillRoundRect(tempX, metricsY, metricsW, metricsH, radius, tempColor);
  tft.fillRoundRect(humX, metricsY, metricsW, metricsH, radius, humColor);
  tft.fillRoundRect(battX, metricsY, metricsW, metricsH, radius, battColor);

  const int iconSize = 32;
  const int iconX = heroX + heroW - iconSize - 14;
  const int iconY = heroY + 16;

  tft.setTextColor(TFT_WHITE, heroColor);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.drawString("WEATHER", heroX + 14, heroY + 12, 2);
  tft.drawString(state.clockDmyHi, heroX + 14, heroY + 30, 2);

  if (ensureWeatherIconLoaded(state) && state.weatherIconPixels != nullptr) {
    for (int y = 0; y < iconSize; ++y) {
      const uint16_t* srcRow = state.weatherIconPixels + (y * 32);
      tft.pushImage(iconX, iconY + y, iconSize, 1, srcRow);
    }
  }

  String weatherLine1 = state.weatherLabel;
  String weatherLine2;
  if (state.weatherLabel.length() > 14) {
    int splitPos = state.weatherLabel.lastIndexOf(' ', 14);
    if (splitPos < 0) {
      splitPos = 14;
    }
    weatherLine1 = state.weatherLabel.substring(0, splitPos);
    weatherLine2 = state.weatherLabel.substring(splitPos);
    weatherLine1.trim();
    weatherLine2.trim();
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, heroColor);
  if (weatherLine2.isEmpty()) {
    tft.drawString(weatherLine1, heroX + 14, heroY + 66, 4);
  } else {
    tft.drawString(weatherLine1, heroX + 14, heroY + 64, 2);
    tft.drawString(weatherLine2, heroX + 14, heroY + 84, 2);
  }

  tft.setTextDatum(TL_DATUM);
  const uint16_t valueHighlight = tft.color565(255, 255, 220);
  tft.setTextColor((focusIndex % 3 == 0) ? valueHighlight : TFT_WHITE, tempColor);
  tft.drawString("TEMP", tempX + 12, metricsY + 10, 2);

  String tempValue = state.sensorTemp + "C";
  tft.setTextDatum(MC_DATUM);
  tft.drawString(tempValue, tempX + (metricsW / 2), metricsY + (metricsH / 2) + 8, 2);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor((focusIndex % 3 == 1) ? valueHighlight : TFT_WHITE, humColor);
  tft.drawString("HUM", humX + 12, metricsY + 10, 2);

  String humValue = state.sensorHum + "%";
  tft.setTextDatum(MC_DATUM);
  tft.drawString(humValue, humX + (metricsW / 2), metricsY + (metricsH / 2) + 8, 2);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor((focusIndex % 3 == 2) ? valueHighlight : TFT_WHITE, battColor);
  tft.drawString("BATT", battX + 12, metricsY + 10, 2);

  String battValue = state.sensorBattery;
  if (battValue != "--") {
    String upperValue = battValue;
    upperValue.toUpperCase();
    if (upperValue.indexOf('%') < 0 && upperValue.indexOf('V') < 0) {
      battValue += "%";
    }
  }
  tft.setTextDatum(MC_DATUM);
  tft.drawString(battValue, battX + (metricsW / 2), metricsY + (metricsH / 2) + 8, 2);
}

}  // namespace app::display::ui_component
