#include "display_interface.h"

#include "display_state.h"
#include "display_ui.h"
#include "app/espnow/master.h"
#include "app/espnow/state_binary.h"

#include <app_config.h>
#include <time.h>
#include <cstdio>
#include <cstring>

namespace app::display {
namespace {

static constexpr uint32_t MIN_RENDER_INTERVAL_MS = 120;
static constexpr uint32_t CLOCK_CHECK_INTERVAL_MS = 1000;
static constexpr uint32_t BOOT_ANIMATION_MS = 2200;
static constexpr uint32_t BOOT_GUARD_EXTRA_MS = 400;

String formatClockDmyHi(const tm& timeInfo) {
  char buffer[20] = {0};
  strftime(buffer, sizeof(buffer), "%d/%m %H:%M", &timeInfo);
  return String(buffer);
}

int buildMinuteKey(const tm& timeInfo) {
  return ((timeInfo.tm_year + 1900) * 1000) + (timeInfo.tm_yday * 24 * 60) + (timeInfo.tm_hour * 60) + timeInfo.tm_min;
}

}  // namespace

DisplayInterface displayInterface;

bool DisplayInterface::begin() {
  if (started) {
    return true;
  }

  if (!ui_logic::begin(stateData)) {
    return false;
  }

  ui_logic::renderBootAnimation(BOOT_ANIMATION_MS);

  configTime(25200, 0, "pool.ntp.org", "time.google.com");

  started = true;
  lastRenderMs = 0;
  lastClockCheckMs = 0;
  lastEventMs = millis();
  lastScrollMs = 0;
  lastActionMs = 0;
  scrollCooldownMs = MASTER_UI_SCROLL_COOLDOWN_MS;
  bootGuardUntilMs = millis() + BOOT_GUARD_EXTRA_MS;
  syncUiSettingsToState();
  updateClockDmyHi();
  dirty = true;
  return true;
}

void DisplayInterface::requestRender() {
  lastEventMs = millis();
  dirty = true;
}

void DisplayInterface::setScreenState(ScreenState state) {
  if (screenState == state) {
    return;
  }

  screenState = state;
  uiFocusIndex = getFocusMinIndex();
  settingsEditMode = false;
  syncUiSettingsToState();
  requestRender();
}

uint8_t DisplayInterface::getFocusMinIndex() const {
  return static_cast<uint8_t>(MASTER_UI_FOCUS_MIN_INDEX);
}

uint8_t DisplayInterface::getFocusMaxIndex() const {
  switch (screenState) {
    case ScreenState::HomeWeather:
      return static_cast<uint8_t>(MASTER_UI_FOCUS_MAX_HOME);
    case ScreenState::DeviceList:
      return app::espnow::getTrackedDeviceFocusMax();
    case ScreenState::EspNowControl:
      return static_cast<uint8_t>(MASTER_UI_FOCUS_MAX_ESPNOW_CONTROL);
    case ScreenState::Settings:
      return static_cast<uint8_t>(MASTER_UI_FOCUS_MAX_SETTINGS);
    default:
      return static_cast<uint8_t>(MASTER_UI_FOCUS_MAX_HOME);
  }
}

void DisplayInterface::moveFocus(int8_t delta) {
  if (delta == 0) {
    return;
  }

  const int32_t minIndex = static_cast<int32_t>(getFocusMinIndex());
  const int32_t maxIndex = static_cast<int32_t>(getFocusMaxIndex());
  int32_t next = static_cast<int32_t>(uiFocusIndex) + delta;
  if (next < minIndex) {
    next = minIndex;
  } else if (next > maxIndex) {
    next = maxIndex;
  }

  if (uiFocusIndex != static_cast<uint8_t>(next)) {
    uiFocusIndex = static_cast<uint8_t>(next);
    requestRender();
  }
}

void DisplayInterface::nextScreen() {
  if (screenState == ScreenState::Settings) {
    setScreenState(ScreenState::HomeWeather);
    return;
  }
  setScreenState(static_cast<ScreenState>(static_cast<uint8_t>(screenState) + 1));
}

void DisplayInterface::prevScreen() {
  if (screenState == ScreenState::HomeWeather) {
    setScreenState(ScreenState::Settings);
    return;
  }
  setScreenState(static_cast<ScreenState>(static_cast<uint8_t>(screenState) - 1));
}

void DisplayInterface::setButtonState(uint8_t index, bool pressed) {
  if (millis() < bootGuardUntilMs) {
    return;
  }

  if (index >= 4) {
    return;
  }
  if (buttonState[index] == pressed) {
    return;
  }
  buttonState[index] = pressed;

  stateData.inputButtonUp = buttonState[0];
  stateData.inputButtonDown = buttonState[1];
  stateData.inputButtonSelect = buttonState[2];
  stateData.inputButtonBack = buttonState[3];

  if (pressed) {
    handleActionButtonPress(index);
  }

  requestRender();
}

void DisplayInterface::handleActionButtonPress(uint8_t index) {
  if (screenState == ScreenState::Settings) {
    switch (index) {
      case 0:  // UP
        if (settingsEditMode) {
          applySettingsDelta(+1);
        } else {
          moveFocus(-1);
        }
        return;
      case 1:  // DOWN
        if (settingsEditMode) {
          applySettingsDelta(-1);
        } else {
          moveFocus(+1);
        }
        return;
      case 2:  // SELECT
        settingsEditMode = !settingsEditMode;
        syncUiSettingsToState();
        requestRender();
        return;
      case 3:  // BACK
        if (settingsEditMode) {
          settingsEditMode = false;
          syncUiSettingsToState();
          requestRender();
        } else {
          setScreenState(ScreenState::HomeWeather);
        }
        return;
      default:
        break;
    }
  }

  if (screenState == ScreenState::DeviceList && index == 2) {
    bindSelectedDeviceFromFocus();
    setScreenState(ScreenState::EspNowControl);
    return;
  }

  if (screenState == ScreenState::EspNowControl) {
    const uint32_t cameraFeatures = static_cast<uint32_t>(app::espnow::state_binary::FeatureCameraJpeg)
                                  | static_cast<uint32_t>(app::espnow::state_binary::FeatureCameraStream);
    const bool isCameraSelection = (stateData.selectedDeviceKind == "Camera")
                                || ((stateData.selectedDeviceFeatures & cameraFeatures) != 0);

    if (isCameraSelection && stateData.selectedCameraStreamView) {
      if (index == 3) {
        stateData.selectedCameraStreamView = false;
        setScreenState(ScreenState::DeviceList);
      }
      return;
    }

    switch (index) {
      case 0:  // UP
        moveFocus(-1);
        return;
      case 1:  // DOWN
        moveFocus(+1);
        return;
      case 2:  // SELECT
        executeEspNowControlAction();
        requestRender();
        return;
      case 3:  // BACK
        setScreenState(ScreenState::DeviceList);
        return;
      default:
        break;
    }
  }

  switch (index) {
    case 0:  // UP
      prevScreen();
      break;
    case 1:  // DOWN
      nextScreen();
      break;
    case 2:  // SELECT
      moveFocus(+1);
      break;
    case 3:  // BACK
      setScreenState(ScreenState::HomeWeather);
      break;
    default:
      break;
  }
}

void DisplayInterface::executeEspNowControlAction() {
  if (stateData.selectedDeviceId.isEmpty()) {
    stateData.selectedDeviceStatus = "no device";
    return;
  }

  using namespace app::espnow::state_binary;

  const uint8_t action = uiFocusIndex % 3;
  const uint32_t cameraFeatures = static_cast<uint32_t>(FeatureCameraJpeg)
                                | static_cast<uint32_t>(FeatureCameraStream);
  const uint32_t weatherFeatures = static_cast<uint32_t>(FeatureWeather)
                                 | static_cast<uint32_t>(FeatureSensor);
  const bool isWeatherSelection = (stateData.selectedDeviceKind == "Weather")
                               || ((stateData.selectedDeviceFeatures & weatherFeatures) != 0);
  const bool isCameraSelection = (stateData.selectedDeviceKind == "Camera")
                              || ((stateData.selectedDeviceFeatures & cameraFeatures) != 0);

  if (isWeatherSelection) {
    stateData.selectedCameraStreamView = false;
    if (action == 2) {
      setScreenState(ScreenState::DeviceList);
      return;
    }

    if (!refreshSelectedDeviceSnapshot()) {
      stateData.selectedDeviceStatus = "weather data unavailable";
      return;
    }

    if (action == 0) {
      if (stateData.selectedWeatherCode < 0 || stateData.selectedWeatherTime.isEmpty()) {
        stateData.selectedDeviceStatus = "weather data unavailable";
      } else {
        stateData.selectedDeviceStatus = String("weather code=") + String(stateData.selectedWeatherCode)
                                       + " @" + stateData.selectedWeatherTime;
      }
      return;
    }

    if (!stateData.selectedHasSensor) {
      stateData.selectedDeviceStatus = "sensor data unavailable";
      return;
    }

    stateData.selectedDeviceStatus = String("sensor ")
                                   + String(stateData.selectedSensorTemp10 / 10.0f, 1)
                                   + "C "
                                   + String(stateData.selectedSensorHum10 / 10.0f, 1)
                                   + "%";
    return;
  }

  if (isCameraSelection) {
    if (stateData.selectedCameraStreamView) {
      if (action == 0) {
        stateData.selectedCameraStreamView = false;
        setScreenState(ScreenState::DeviceList);
      }
      return;
    }

    if (action != 0) {
      setScreenState(ScreenState::DeviceList);
      return;
    }

    CameraControlCommand command = {};
    initHeader(command.header, Type::CameraControl);
    command.action = static_cast<uint8_t>(CameraControlAction::SetStreaming);
    command.value = 1;

    const bool sent = app::espnow::espnowMaster.send(stateData.selectedDeviceMac,
                                                      app::espnow::PacketType::COMMAND,
                                                      &command,
                                                      sizeof(command));
    if (!sent) {
      stateData.selectedDeviceStatus = "camera command failed";
      return;
    }

    stateData.selectedCameraStreaming = true;
    stateData.selectedCameraStreamView = true;
    uiFocusIndex = getFocusMinIndex();
    stateData.selectedDeviceStatus = "streaming";
    return;
  }

  if (action == 2) {
    setScreenState(ScreenState::DeviceList);
    return;
  }

  stateData.selectedDeviceStatus = "unsupported control";
}

bool DisplayInterface::refreshSelectedDeviceSnapshot() {
  if (stateData.selectedDeviceId.isEmpty()) {
    return false;
  }

  app::espnow::TrackedDeviceSnapshot row;
  if (!app::espnow::getTrackedDeviceSnapshotByMac(stateData.selectedDeviceMac, row)) {
    return false;
  }

  stateData.selectedDeviceKind = row.kind;
  stateData.selectedHasSensor = row.hasSensor;
  stateData.selectedSensorTemp10 = row.sensorTemp10;
  stateData.selectedSensorHum10 = row.sensorHum10;
  stateData.selectedWeatherCode = row.weatherCode;
  stateData.selectedWeatherTime = row.weatherTime;
  stateData.selectedCameraFrameId = row.cameraFrameId;
  stateData.selectedCameraBytes = row.cameraBytes;
  stateData.selectedCameraChunks = row.cameraChunks;
  return true;
}

void DisplayInterface::bindSelectedDeviceFromFocus() {
  const size_t count = app::espnow::getTrackedDeviceSnapshotCount();

  if (count == 0) {
    stateData.selectedDeviceId = "";
    stateData.selectedDeviceKind = "";
    stateData.selectedDeviceStatus = "";
    stateData.selectedDeviceFeatures = 0;
    memset(stateData.selectedDeviceMac, 0, sizeof(stateData.selectedDeviceMac));
    stateData.selectedHasSensor = false;
    stateData.selectedSensorTemp10 = 0;
    stateData.selectedSensorHum10 = 0;
    stateData.selectedWeatherCode = -1;
    stateData.selectedWeatherTime = "";
    stateData.selectedCameraFrameId = 0;
    stateData.selectedCameraBytes = 0;
    stateData.selectedCameraChunks = 0;
    stateData.selectedCameraStreaming = false;
    stateData.selectedCameraStreamView = false;
    return;
  }

  const size_t selectedIndex = uiFocusIndex >= count ? (count - 1) : uiFocusIndex;
  app::espnow::TrackedDeviceSnapshot selected;
  if (!app::espnow::getTrackedDeviceSnapshotAt(selectedIndex, selected)) {
    return;
  }

  stateData.selectedDeviceId = selected.deviceId;
  if (stateData.selectedDeviceId.isEmpty()) {
    char macText[18] = {0};
    snprintf(macText,
             sizeof(macText),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             selected.mac[0],
             selected.mac[1],
             selected.mac[2],
             selected.mac[3],
             selected.mac[4],
             selected.mac[5]);
    stateData.selectedDeviceId = String(macText);
  }

  stateData.selectedDeviceKind = selected.kind;
  stateData.selectedDeviceStatus = selected.status;
  stateData.selectedDeviceFeatures = selected.featureBits;
  memcpy(stateData.selectedDeviceMac, selected.mac, sizeof(stateData.selectedDeviceMac));
  stateData.selectedHasSensor = selected.hasSensor;
  stateData.selectedSensorTemp10 = selected.sensorTemp10;
  stateData.selectedSensorHum10 = selected.sensorHum10;
  stateData.selectedWeatherCode = selected.weatherCode;
  stateData.selectedWeatherTime = selected.weatherTime;
  stateData.selectedCameraFrameId = selected.cameraFrameId;
  stateData.selectedCameraBytes = selected.cameraBytes;
  stateData.selectedCameraChunks = selected.cameraChunks;
  stateData.selectedCameraStreaming = false;
  stateData.selectedCameraStreamView = false;
}

void DisplayInterface::setAnalogValue(uint8_t index, int16_t value) {
  if (millis() < bootGuardUntilMs) {
    return;
  }

  if (index >= 4) {
    return;
  }

  int16_t filtered = value;
  if (index == 0 || index == 3) {
    filtered = -filtered;
  }

  if (filtered >= -analogDeadzone && filtered <= analogDeadzone) {
    filtered = 0;
  }

  const bool significantDelta = abs(filtered - analogState[index]) >= analogDeltaTrigger;
  analogState[index] = filtered;
  stateData.inputAnalogX = analogState[0];
  stateData.inputAnalogY = analogState[1];
  stateData.inputAnalog2X = analogState[2];
  stateData.inputAnalog2Y = analogState[3];

  const uint32_t now = millis();
  const int16_t analogRearmThreshold = (analogNavThreshold > 16) ? (analogNavThreshold - 12) : analogDeadzone;

  // Analog 1 (index 0/1) => scroll/focus control only.
  if (index == 1) {
    if (!analogScrollLatchedY && now - lastScrollMs >= scrollCooldownMs) {
      if (filtered >= analogNavThreshold) {
        moveFocus(-1);
        analogScrollLatchedY = true;
        lastScrollMs = now;
      } else if (filtered <= -analogNavThreshold) {
        moveFocus(+1);
        analogScrollLatchedY = true;
        lastScrollMs = now;
      }
    }

    if (abs(filtered) <= analogRearmThreshold) {
      analogScrollLatchedY = false;
    }
  }

  // Analog 2 (index 2/3) => button-like navigation.
  if (index == 3) {
    if (!analogNav2LatchedY && now - lastActionMs >= actionCooldownMs) {
      if (filtered >= analogNavThreshold) {
        handleActionButtonPress(0);  // UP
        analogNav2LatchedY = true;
        lastActionMs = now;
      } else if (filtered <= -analogNavThreshold) {
        handleActionButtonPress(1);  // DOWN
        analogNav2LatchedY = true;
        lastActionMs = now;
      }
    }

    if (abs(filtered) <= analogRearmThreshold) {
      analogNav2LatchedY = false;
    }
  }

  if (index == 2) {
    if (!analogNav2LatchedX && now - lastActionMs >= actionCooldownMs) {
      if (filtered >= analogNavThreshold) {
        handleActionButtonPress(2);  // SELECT
        analogNav2LatchedX = true;
        lastActionMs = now;
      } else if (filtered <= -analogNavThreshold) {
        handleActionButtonPress(3);  // BACK
        analogNav2LatchedX = true;
        lastActionMs = now;
      }
    }

    if (abs(filtered) <= analogRearmThreshold) {
      analogNav2LatchedX = false;
    }
  }

  if (screenState == ScreenState::Settings && significantDelta) {
    requestRender();
  }
}

bool DisplayInterface::pullFromStateStore() {
  const bool changed = state_logic::pullFromStateStore(stateData);
  if (changed) {
    requestRender();
  }

  return true;
}

bool DisplayInterface::applyStatePayload(const String& payload) {
  const bool changed = state_logic::applyStatePayload(stateData, payload);
  if (changed) {
    requestRender();
  }

  return changed;
}

void DisplayInterface::render() {
  switch (screenState) {
    case ScreenState::HomeWeather:
      ui_logic::renderHomeWeather(stateData, uiFocusIndex);
      break;
    case ScreenState::DeviceList:
      ui_logic::renderDeviceList(stateData, uiFocusIndex);
      break;
    case ScreenState::EspNowControl:
      ui_logic::renderEspNowControl(stateData, uiFocusIndex);
      break;
    case ScreenState::Settings:
      ui_logic::renderSettings(stateData, uiFocusIndex);
      break;
    default:
      ui_logic::renderHomeWeather(stateData, uiFocusIndex);
      break;
  }
}

bool DisplayInterface::updateClockDmyHi() {
  tm timeInfo = {};
  if (!getLocalTime(&timeInfo, 0)) {
    if (stateData.clockDmyHi != "--") {
      stateData.clockDmyHi = "--";
      stateData.clockMinuteKey = -1;
      return true;
    }
    return false;
  }

  const int nextMinuteKey = buildMinuteKey(timeInfo);
  if (stateData.clockMinuteKey == nextMinuteKey) {
    return false;
  }

  const String nextClock = formatClockDmyHi(timeInfo);
  const bool changed = (stateData.clockDmyHi != nextClock) || (stateData.clockMinuteKey != nextMinuteKey);
  stateData.clockDmyHi = nextClock;
  stateData.clockMinuteKey = nextMinuteKey;
  return changed;
}

void DisplayInterface::syncUiSettingsToState() {
  stateData.uiRenderMinIntervalMs = renderMinIntervalMs;
  stateData.uiAnalogDeadzone = analogDeadzone;
  stateData.uiAnalogNavThreshold = analogNavThreshold;
  stateData.uiSettingsEditMode = settingsEditMode;
}

void DisplayInterface::applySettingsDelta(int8_t delta) {
  if (delta == 0) {
    return;
  }

  switch (uiFocusIndex % 3) {
    case 0: {
      int32_t next = static_cast<int32_t>(renderMinIntervalMs) + (delta * 10);
      if (next < 60) next = 60;
      if (next > 500) next = 500;
      renderMinIntervalMs = static_cast<uint16_t>(next);
      break;
    }
    case 1: {
      int32_t next = static_cast<int32_t>(analogDeadzone) + delta;
      if (next < 1) next = 1;
      if (next > 30) next = 30;
      analogDeadzone = static_cast<int16_t>(next);
      break;
    }
    case 2: {
      int32_t next = static_cast<int32_t>(analogNavThreshold) + (delta * 2);
      if (next < 10) next = 10;
      if (next > 100) next = 100;
      analogNavThreshold = static_cast<int16_t>(next);
      break;
    }
    default:
      break;
  }

  syncUiSettingsToState();
  requestRender();
}

void DisplayInterface::loop() {
  if (!started) {
    return;
  }

  const uint32_t now = millis();

  if (lastClockCheckMs == 0 || (now - lastClockCheckMs) >= CLOCK_CHECK_INTERVAL_MS) {
    lastClockCheckMs = now;
    updateClockDmyHi();
  }

  if (!dirty) {
    return;
  }

  if (lastRenderMs != 0 && (now - lastRenderMs) < renderMinIntervalMs) {
    return;
  }

  if (screenState == ScreenState::EspNowControl) {
    refreshSelectedDeviceSnapshot();
  }

  render();
  dirty = false;
  lastRenderMs = now;
}

}  // namespace app::display
