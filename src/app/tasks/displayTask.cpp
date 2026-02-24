#include "displayTask.h"

#include "app/display/display_interface.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace app::tasks {

namespace {

static constexpr const char* TAG = "DISPLAY_TASK";
static constexpr uint16_t DISPLAY_TASK_STACK = 6144;
static constexpr UBaseType_t DISPLAY_TASK_PRIORITY = 1;

TaskHandle_t displayTaskHandle = nullptr;

void displayTaskRunner(void*) {
  app::display::displayInterface.begin();
  app::display::displayInterface.setScreenState(app::display::ScreenState::HomeWeather);
  app::display::displayInterface.pullFromStateStore();
  app::display::displayInterface.requestRender();

  while (true) {
    app::display::displayInterface.loop();
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

}  // namespace

bool startDisplayTask() {
  if (displayTaskHandle != nullptr) {
    return true;
  }

  BaseType_t created = xTaskCreatePinnedToCore(
      displayTaskRunner,
      "display_task",
      DISPLAY_TASK_STACK,
      nullptr,
      DISPLAY_TASK_PRIORITY,
      &displayTaskHandle,
      tskNO_AFFINITY);

  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to start display task");
    displayTaskHandle = nullptr;
    return false;
  }

  ESP_LOGI(TAG, "Display task started");
  return true;
}

}  // namespace app::tasks
