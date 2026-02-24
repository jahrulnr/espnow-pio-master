#pragma once

#include <Arduino.h>
#include "display_state.h"

namespace app::display {

enum class ScreenState : uint8_t {
  HomeWeather = 0,
  DeviceList = 1,
  EspNowControl = 2,
  Settings = 3,
};

class DisplayInterface {
 public:
  bool begin();
  void loop();
  void requestRender();
  bool pullFromStateStore();
  bool applyStatePayload(const String& payload);

  void setScreenState(ScreenState state);
  ScreenState getScreenState() const { return screenState; }

  void setButtonState(uint8_t index, bool pressed);
  void setAnalogValue(uint8_t index, int16_t value);

 private:
  bool started = false;
  ScreenState screenState = ScreenState::HomeWeather;

  bool buttonState[4] = {false, false, false, false};
  int16_t analogState[4] = {0, 0, 0, 0};
  bool analogScrollLatchedY = false;
  bool analogNav2LatchedX = false;
  bool analogNav2LatchedY = false;
  uint32_t lastScrollMs = 0;
  uint32_t lastActionMs = 0;
  uint8_t uiFocusIndex = 0;
  bool settingsEditMode = false;

  uint32_t lastRenderMs = 0;
  uint32_t lastClockCheckMs = 0;
  uint32_t lastEventMs = 0;
  uint32_t bootGuardUntilMs = 0;
  bool dirty = true;
  DisplayStateData stateData;

  uint16_t renderMinIntervalMs = 120;
  int16_t analogDeadzone = 3;
  int16_t analogDeltaTrigger = 5;
  int16_t analogNavThreshold = 40;
  uint16_t scrollCooldownMs = 120;
  uint16_t actionCooldownMs = 180;

  bool updateClockDmyHi();
  void nextScreen();
  void prevScreen();
  uint8_t getFocusMinIndex() const;
  uint8_t getFocusMaxIndex() const;
  void moveFocus(int8_t delta);
  void syncUiSettingsToState();
  void applySettingsDelta(int8_t delta);
  bool refreshSelectedDeviceSnapshot();
  void bindSelectedDeviceFromFocus();
  void executeEspNowControlAction();
  void handleActionButtonPress(uint8_t index);
  void render();
};

extern DisplayInterface displayInterface;

}  // namespace app::display
