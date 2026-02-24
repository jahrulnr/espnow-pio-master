#include "ui_screens.h"

#include "ui_common.h"

#include <Arduino.h>
#include <math.h>

namespace app::display::ui_component {

void renderBootAnimation(uint32_t durationMs) {
  const uint32_t startMs = millis();
  const int width = tft.width();
  const int height = tft.height();
  const int centerX = width / 2;
  const int centerY = height / 2;
  const int ringR = 32;
  const int titleY = centerY + 58;
  const int subtitleY = centerY + 78;

  while ((millis() - startMs) < durationMs) {
    const uint32_t elapsed = millis() - startMs;
    const int angle = (elapsed / 5) % 360;

    tft.fillScreen(colorBackground());

    tft.drawCircle(centerX, centerY, ringR, tft.color565(24, 62, 92));
    tft.drawCircle(centerX, centerY, ringR - 1, tft.color565(18, 46, 72));

    for (int i = 0; i < 3; ++i) {
      const float phase = (angle + (i * 120)) * 0.0174533f;
      const int dotX = centerX + static_cast<int>(ringR * cosf(phase));
      const int dotY = centerY + static_cast<int>(ringR * sinf(phase));
      const uint16_t dotColor = (i == 0) ? tft.color565(130, 220, 255) : tft.color565(0, 145, 220);
      tft.fillCircle(dotX, dotY, (i == 0) ? 5 : 4, dotColor);
    }

    tft.fillCircle(centerX, centerY, 9, tft.color565(10, 28, 44));
    tft.drawCircle(centerX, centerY, 10, tft.color565(35, 105, 165));

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, colorBackground());
    tft.drawString("ESP-NOW MASTER", centerX, titleY, 2);
    tft.setTextColor(tft.color565(170, 170, 170), colorBackground());
    tft.drawString("starting system", centerX, subtitleY, 2);

    delay(33);
  }

  tft.fillScreen(colorBackground());
}

}  // namespace app::display::ui_component
