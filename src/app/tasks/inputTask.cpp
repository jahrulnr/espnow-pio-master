#include "inputTask.h"

#include "app/display/display_interface.h"
#include "app/espnow/master_state_kv_store.h"
#include "app/espnow/payload_codec.h"
#include "app/input/battery/battery_manager.h"
#include "app/input/button/input_manager.h"
#include "app/input/joystick/joystick_manager.h"

#include <app_config.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace app::tasks {

namespace {

static constexpr const char* TAG = "INPUT_TASK";
static constexpr uint16_t INPUT_TASK_STACK = 4096;
static constexpr UBaseType_t INPUT_TASK_PRIORITY = 1;
static constexpr uint32_t INPUT_POLL_INTERVAL_MS = 20;
static constexpr uint32_t BATTERY_PUBLISH_INTERVAL_MS = 1000;

TaskHandle_t inputTaskHandle = nullptr;
InputManager inputManager;
JoystickManager joystickManager(2);
BatteryManager batteryManager;

uint32_t lastBatteryPublishMs = 0;
int lastPublishedBatteryLevel = -1;
bool l3PrevPressed = false;
bool r3PrevPressed = false;

void emitVirtualButtonPress(uint8_t index) {
  app::display::displayInterface.setButtonState(index, true);
  app::display::displayInterface.setButtonState(index, false);
}

void handleStickButtonShortcuts() {
  const bool l3Pressed = joystickManager.getJoystickCount() > 0 ? joystickManager.isSwitchPressed(0) : false;
  const bool r3Pressed = joystickManager.getJoystickCount() > 1 ? joystickManager.isSwitchPressed(1) : false;

  if (l3Pressed && !l3PrevPressed) {
    emitVirtualButtonPress(3);  // BACK
  }

  if (r3Pressed && !r3PrevPressed) {
    emitVirtualButtonPress(2);  // SELECT
  }

  l3PrevPressed = l3Pressed;
  r3PrevPressed = r3Pressed;
}

void publishBatterySnapshotToDisplay() {
  batteryManager.update();

  const uint32_t now = millis();
  if (lastBatteryPublishMs != 0 && (now - lastBatteryPublishMs) < BATTERY_PUBLISH_INTERVAL_MS) {
    return;
  }

  const int batteryLevel = batteryManager.getLevel();
  if (batteryLevel < 0 || batteryLevel > 100) {
    return;
  }

  if (batteryLevel == lastPublishedBatteryLevel && lastBatteryPublishMs != 0) {
    return;
  }

  const String payload = app::espnow::codec::buildPayload({
      {"state", "sensor"},
      {"batt", String(batteryLevel)},
  });

  app::espnow::state_store::upsertFromStatePayload(payload);
  app::display::displayInterface.applyStatePayload(payload);

  lastPublishedBatteryLevel = batteryLevel;
  lastBatteryPublishMs = now;
}

void publishInputSnapshotToDisplay() {
  app::display::displayInterface.setButtonState(0, inputManager.isPressed(BTN_UP));
  app::display::displayInterface.setButtonState(1, inputManager.isPressed(BTN_DOWN));
  app::display::displayInterface.setButtonState(2, inputManager.isPressed(BTN_SELECT));
  app::display::displayInterface.setButtonState(3, inputManager.isPressed(BTN_BACK));

  app::display::displayInterface.setAnalogValue(0, static_cast<int16_t>(joystickManager.getNormalizedX(0)));
  app::display::displayInterface.setAnalogValue(1, static_cast<int16_t>(joystickManager.getNormalizedY(0)));

  if (joystickManager.getJoystickCount() > 1) {
    app::display::displayInterface.setAnalogValue(2, static_cast<int16_t>(joystickManager.getNormalizedX(1)));
    app::display::displayInterface.setAnalogValue(3, static_cast<int16_t>(joystickManager.getNormalizedY(1)));
  }
}

void inputTaskRunner(void*) {
  inputManager.init();

  joystickManager.setupSingleJoystick(
      INPUT_JOYSTICK1_VRX_PIN,
      INPUT_JOYSTICK1_VRY_PIN,
      INPUT_JOYSTICK1_SW_PIN);
    joystickManager.addJoystick(
      INPUT_JOYSTICK2_VRX_PIN,
      INPUT_JOYSTICK2_VRY_PIN,
      INPUT_JOYSTICK2_SW_PIN);
  joystickManager.init();

  batteryManager.init(INPUT_BATTERY_ADC_PIN);
  batteryManager.setVoltage(3.3f, 4.2f, 2.0f);
  batteryManager.setUpdateInterval(5000);

  publishInputSnapshotToDisplay();
  publishBatterySnapshotToDisplay();

  while (true) {
    inputManager.update();
    joystickManager.update();

    publishInputSnapshotToDisplay();
    handleStickButtonShortcuts();
    publishBatterySnapshotToDisplay();

    vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_INTERVAL_MS));
  }
}

}  // namespace

bool startInputTask() {
  if (inputTaskHandle != nullptr) {
    return true;
  }

  BaseType_t created = xTaskCreatePinnedToCore(
      inputTaskRunner,
      "input_task",
      INPUT_TASK_STACK,
      nullptr,
      INPUT_TASK_PRIORITY,
      &inputTaskHandle,
      tskNO_AFFINITY);

  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to start input task");
    inputTaskHandle = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "Input task started");
  return true;
}

}  // namespace app::tasks
