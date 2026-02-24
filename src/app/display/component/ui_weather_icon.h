#pragma once

#include "../display_state.h"

namespace app::display::ui_component {

size_t weatherIconBytes();
bool ensureWeatherIconLoaded(DisplayStateData& state);

}  // namespace app::display::ui_component
