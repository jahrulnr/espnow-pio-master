#include "ui_screens.h"

#include "ui_common.h"
#include "app/espnow/master.h"

namespace app::display::ui_component {

void renderDeviceList(DisplayStateData& state, uint8_t focusIndex) {
  (void)state;

  tft.fillScreen(colorBackground());

  const int margin = 12;
  const int cardW = tft.width() - (margin * 2);
  const int cardH = 56;
  const int gap = 10;
  const int startY = 12;

  app::espnow::TrackedDeviceSnapshot snapshots[32];
  const size_t totalDevices = app::espnow::getTrackedDeviceSnapshots(snapshots, 32);

  if (totalDevices == 0) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(tft.color565(180, 180, 180), colorBackground());
    tft.drawString("No slave connected", tft.width() / 2, tft.height() / 2, 2);
    tft.drawString("Waiting for identity/features", tft.width() / 2, (tft.height() / 2) + 20, 2);
    return;
  }

  const size_t clampedFocus = (focusIndex >= totalDevices) ? (totalDevices - 1) : focusIndex;
  const size_t pageStart = (clampedFocus / 3) * 3;

  for (int i = 0; i < 3; ++i) {
    const size_t deviceIndex = pageStart + static_cast<size_t>(i);
    if (deviceIndex >= totalDevices) {
      break;
    }

    const auto& device = snapshots[deviceIndex];
    const int y = startY + (i * (cardH + gap));
    const bool focused = deviceIndex == clampedFocus;
    const uint16_t cardColor = focused ? tft.color565(23, 86, 163) : tft.color565(33, 33, 33);
    tft.fillRoundRect(margin, y, cardW, cardH, 10, cardColor);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, cardColor);

    String id = device.deviceId;
    if (id.isEmpty()) {
      char macText[18] = {0};
      snprintf(macText,
               sizeof(macText),
               "%02X:%02X:%02X:%02X:%02X:%02X",
               device.mac[0],
               device.mac[1],
               device.mac[2],
               device.mac[3],
               device.mac[4],
               device.mac[5]);
      id = String(macText);
    }

    String status = device.status;
    if (status.isEmpty()) {
      status = device.verified ? "online" : "pending identity";
    }

    tft.drawString(id, margin + 12, y + 8, 2);
    tft.drawString(device.kind + " | " + status, margin + 12, y + 30, 2);
  }
}

}  // namespace app::display::ui_component
