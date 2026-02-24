#pragma once

#include "../display_state.h"

namespace app::display::ui_component {

void renderBootAnimation(uint32_t durationMs);
void renderHomeWeather(DisplayStateData& state, uint8_t focusIndex);
void renderDeviceList(DisplayStateData& state, uint8_t focusIndex);
void renderEspNowControl(DisplayStateData& state, uint8_t focusIndex);
void renderSettings(DisplayStateData& state, uint8_t focusIndex);

}  // namespace app::display::ui_component
