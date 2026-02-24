#pragma once

#include <Arduino.h>

namespace app::display {

struct DisplayStateData {
  String weatherLabel = "--";
  int weatherCode = -1;
  String weatherTime = "--";
  String clockDmyHi = "--";
  int clockMinuteKey = -1;
  String sensorTemp = "--";
  String sensorHum = "--";
  String sensorBattery = "--";
  int16_t inputAnalogX = 0;
  int16_t inputAnalogY = 0;
  int16_t inputAnalog2X = 0;
  int16_t inputAnalog2Y = 0;
  bool inputButtonUp = false;
  bool inputButtonDown = false;
  bool inputButtonSelect = false;
  bool inputButtonBack = false;
  uint16_t uiRenderMinIntervalMs = 120;
  int16_t uiAnalogDeadzone = 3;
  int16_t uiAnalogNavThreshold = 40;
  bool uiSettingsEditMode = false;
  String selectedDeviceId = "";
  String selectedDeviceKind = "";
  String selectedDeviceStatus = "";
  uint32_t selectedDeviceFeatures = 0;
  uint8_t selectedDeviceMac[6] = {0};
  bool selectedHasSensor = false;
  int16_t selectedSensorTemp10 = 0;
  uint16_t selectedSensorHum10 = 0;
  int16_t selectedWeatherCode = -1;
  String selectedWeatherTime = "";
  uint32_t selectedCameraFrameId = 0;
  uint32_t selectedCameraBytes = 0;
  uint16_t selectedCameraChunks = 0;
  bool selectedCameraStreaming = false;
  bool selectedCameraStreamView = false;
  int loadedWeatherCode = -9999;
  bool weatherIconLoaded = false;
  uint16_t* weatherIconPixels = nullptr;
};

namespace state_logic {

bool pullFromStateStore(DisplayStateData& state);
bool applyStatePayload(DisplayStateData& state, const String& payload);

}  // namespace state_logic

}  // namespace app::display
