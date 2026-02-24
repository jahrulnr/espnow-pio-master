#include "ui_common.h"

namespace app::display::ui_component {

TFT_eSPI tft = TFT_eSPI();

uint16_t colorTileBlue() {
  return tft.color565(0, 120, 215);
}

uint16_t colorTileCyan() {
  return tft.color565(0, 153, 188);
}

uint16_t colorTileGreen() {
  return tft.color565(16, 124, 16);
}

uint16_t colorBackground() {
  return TFT_BLACK;
}

}  // namespace app::display::ui_component
