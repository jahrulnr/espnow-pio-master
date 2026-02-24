#pragma once

#include <TFT_eSPI.h>

namespace app::display::ui_component {

extern TFT_eSPI tft;

uint16_t colorTileBlue();
uint16_t colorTileCyan();
uint16_t colorTileGreen();
uint16_t colorBackground();

}  // namespace app::display::ui_component
