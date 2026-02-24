#include "ui_screens.h"

#include "ui_common.h"
#include "app/espnow/state_binary.h"
#include "app/espnow/camera_stream_buffer.h"

namespace app::display::ui_component {

void renderEspNowControl(DisplayStateData& state, uint8_t focusIndex) {
  tft.fillScreen(colorBackground());

  const int margin = 12;
  const int panelY = 12;
  const int panelW = tft.width() - (margin * 2);
  const int panelH = 216;
  const uint16_t panelColor = tft.color565(28, 28, 38);
  tft.fillRoundRect(margin, panelY, panelW, panelH, 12, panelColor);

  const bool hasSelection = !state.selectedDeviceId.isEmpty();

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(tft.color565(190, 190, 210), panelColor);
  tft.drawString(hasSelection ? state.selectedDeviceId : "No device selected", margin + 12, panelY + 8, 2);

  const uint32_t cameraFeatures = static_cast<uint32_t>(app::espnow::state_binary::FeatureCameraJpeg)
                                | static_cast<uint32_t>(app::espnow::state_binary::FeatureCameraStream);
  const uint32_t weatherFeatures = static_cast<uint32_t>(app::espnow::state_binary::FeatureWeather)
                                 | static_cast<uint32_t>(app::espnow::state_binary::FeatureSensor);
  const bool isCameraSelection = hasSelection && (state.selectedDeviceKind == "Camera"
                               || (state.selectedDeviceFeatures & cameraFeatures) != 0);
  const bool isWeatherSelection = hasSelection && (state.selectedDeviceKind == "Weather"
                                || (state.selectedDeviceFeatures & weatherFeatures) != 0);

  String subtitle = "Select from Device List";
  if (hasSelection) {
    subtitle = state.selectedDeviceKind + " | " + (state.selectedDeviceStatus.isEmpty() ? String("online") : state.selectedDeviceStatus);
  }
  tft.drawString(subtitle, margin + 12, panelY + 28, 2);

  String detail = "No preview data";
  if (isWeatherSelection) {
    if (state.selectedHasSensor) {
      detail = String("S ") + String(state.selectedSensorTemp10 / 10.0f, 1)
             + "C " + String(state.selectedSensorHum10 / 10.0f, 1) + "%";
    }
    if (state.selectedWeatherCode >= 0 && !state.selectedWeatherTime.isEmpty()) {
      detail = String("W code=") + String(state.selectedWeatherCode) + " @" + state.selectedWeatherTime;
    }
  } else if (isCameraSelection) {
    detail = String(state.selectedCameraBytes/1024) + "KB/" + String(state.selectedCameraChunks) + " chunks";
  }
  tft.drawString(detail, margin + 12, panelY + 46, 2);

  if (isCameraSelection && state.selectedCameraStreamView) {
    const uint16_t previewX = margin + 12;
    const uint16_t previewY = panelY + 68;
    const uint16_t previewW = panelW - 24;
    const uint16_t previewH = 110;
    const uint16_t previewBg = tft.color565(8, 8, 12);
    tft.fillRoundRect(previewX, previewY, previewW, previewH, 8, previewBg);

    const uint16_t* previewPixels = nullptr;
    uint16_t sourceW = 0;
    uint16_t sourceH = 0;
    uint32_t frameId = 0;
    const bool hasPreview = app::espnow::camera_stream::getPreviewForMac(state.selectedDeviceMac,
                                                                          previewPixels,
                                                                          sourceW,
                                                                          sourceH,
                                                                          frameId);

    if (hasPreview && previewPixels != nullptr && sourceW > 0 && sourceH > 0) {
      const int16_t drawX = previewX + ((previewW - sourceW) / 2);
      const int16_t drawY = previewY + ((previewH - sourceH) / 2);
      // Use setWindow then push pixels per-pixel with pushColor to test
      // endianness / channel ordering differences in the driver path.
      tft.setWindow(drawX, drawY, drawX + sourceW - 1, drawY + sourceH - 1);
      tft.pushColors(const_cast<uint16_t*>(previewPixels), static_cast<uint32_t>(sourceW) * sourceH);

      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(tft.color565(150, 200, 160), previewBg);
      tft.drawString(String("#") + String(frameId), previewX + previewW - 6, previewY + 4, 2);
    } else {
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(tft.color565(160, 160, 180), previewBg);
      tft.drawString("WAITING STREAM...", previewX + (previewW / 2), previewY + (previewH / 2), 2);
    }
  }

  const char* actions[3] = {"OPEN DETAILS", "REFRESH", "BACK TO LIST"};
  bool cameraStreamView = false;
  if (hasSelection) {
    if (isCameraSelection) {
      cameraStreamView = state.selectedCameraStreamView;
      if (cameraStreamView) {
        actions[0] = "BACK TO LIST";
        actions[1] = "";
        actions[2] = "";
      } else {
        actions[0] = "OPEN CAM";
        actions[1] = "BACK TO LIST";
        actions[2] = "";
      }
    } else if (isWeatherSelection) {
      actions[0] = "VIEW WEATHER";
      actions[1] = "VIEW SENSOR";
      actions[2] = "BACK TO LIST";
    }
  }

  const int actionCount = cameraStreamView ? 1 : 3;
  for (int i = 0; i < actionCount; ++i) {
    const int baseY = cameraStreamView ? (panelY + panelH - 40) : (panelY + 72);
    const int y = baseY + (i * 44);
    const bool focused = (focusIndex % 3) == static_cast<uint8_t>(i);
    const uint16_t actionColor = focused ? tft.color565(0, 120, 215) : tft.color565(60, 60, 80);
    tft.fillRoundRect(margin + 12, y, panelW - 24, 32, 8, actionColor);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, actionColor);
    tft.drawString(actions[i], tft.width() / 2, y + 16, 2);
  }
}

}  // namespace app::display::ui_component
