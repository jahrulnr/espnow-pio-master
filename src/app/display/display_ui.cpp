#include "display_ui.h"

#include "component/ui_common.h"
#include "component/ui_screens.h"
#include "component/ui_weather_icon.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

namespace app::display::ui_logic {
namespace {

static constexpr const char* TAG = "display_if";

}  // namespace

bool begin(DisplayStateData& state) {
#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
#ifdef TFT_BACKLIGHT_ON
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#else
  digitalWrite(TFT_BL, HIGH);
#endif
#endif

  ui_component::tft.init();
  ui_component::tft.setRotation(3);
  ui_component::tft.fillScreen(ui_component::colorBackground());

  if (state.weatherIconPixels == nullptr) {
    const size_t iconBytes = ui_component::weatherIconBytes();
    state.weatherIconPixels = static_cast<uint16_t*>(heap_caps_malloc(iconBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (state.weatherIconPixels == nullptr) {
      state.weatherIconPixels = static_cast<uint16_t*>(malloc(iconBytes));
    }
    if (state.weatherIconPixels == nullptr) {
      ESP_LOGW(TAG, "Failed allocating weather icon buffer");
    }
  }

  ESP_LOGI(TAG, "Display initialized (%dx%d)", ui_component::tft.width(), ui_component::tft.height());
  return true;
}

void renderBootAnimation(uint32_t durationMs) {
  ui_component::renderBootAnimation(durationMs);
}

void renderHomeWeather(DisplayStateData& state, uint8_t focusIndex) {
  ui_component::renderHomeWeather(state, focusIndex);
}

void renderDeviceList(DisplayStateData& state, uint8_t focusIndex) {
  ui_component::renderDeviceList(state, focusIndex);
}

void renderEspNowControl(DisplayStateData& state, uint8_t focusIndex) {
  ui_component::renderEspNowControl(state, focusIndex);
}

void renderSettings(DisplayStateData& state, uint8_t focusIndex) {
  ui_component::renderSettings(state, focusIndex);
}

}  // namespace app::display::ui_logic
