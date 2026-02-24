#include "networkTask.h"

#include "app/espnow/master.h"
#include "WiFiManager.h"
#include <SimpleNTP.h>

#include <Arduino.h>
#include <WiFi.h>
#include <app_config.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <FTPServer.h>
#include <LittleFS.h>

namespace app::tasks {

namespace {

static constexpr uint16_t NETWORK_TASK_STACK = 8192;
static constexpr UBaseType_t NETWORK_TASK_PRIORITY = 2;
static constexpr uint32_t RADIO_MODE_LOG_INTERVAL_MS = 5000;
static constexpr uint32_t NTP_UPDATE_CHECK_INTERVAL_MS = 2000;

TaskHandle_t networkTaskHandle = nullptr;
SimpleNTP ntpClient;

void networkTaskRunner(void*) {
  wifiManager.init();
  wifiManager.setIdentity(DEVICE_NAME, WIFI_HOSTNAME);
  wifiManager.addNetwork(WIFI_SSID, WIFI_PASS);
  wifiManager.begin();
	FTPServer ftpServer(LittleFS);
	ftpServer.begin(FTP_USER, FTP_PASS);

  uint8_t channel = wifiManager.getConnectedChannel();
  if (channel == 0) {
    channel = app::espnow::DEFAULT_CHANNEL;
    ESP_LOGW("NET_TASK", "WiFi channel unknown, fallback to default channel %u", channel);
  }

  app::espnow::espnowMaster.begin(channel);

  ntpClient.setTimeZone(7);
  ntpClient.setUpdateInterval(30UL * 60UL * 1000UL);

  uint32_t lastRadioModeLogMs = millis();
  uint32_t lastNtpCheckMs = 0;
  bool ntpBeginDone = false;
  bool ntpTimeLogged = false;

  while (true) {
    wifiManager.handle();
    app::espnow::espnowMaster.loop();
    ftpServer.handleFTP();

    const uint32_t now = millis();

    if (wifiManager.isConnected()) {
      if (!ntpBeginDone) {
        ntpBeginDone = ntpClient.begin("pool.ntp.org");
        if (ntpBeginDone) {
          ESP_LOGI("NET_TASK", "NTP initialized");
        } else {
          ESP_LOGW("NET_TASK", "NTP init pending (WiFi/stack not ready)");
        }
      }

      if (ntpBeginDone && (lastNtpCheckMs == 0 || (now - lastNtpCheckMs) >= NTP_UPDATE_CHECK_INTERVAL_MS)) {
        lastNtpCheckMs = now;
        ntpClient.update();

        if (!ntpTimeLogged && ntpClient.isTimeSet()) {
          ESP_LOGI("NET_TASK", "NTP time set: %s", ntpClient.getFormattedDateTime().c_str());
          ntpTimeLogged = true;
        }
      }
    } else {
      ntpBeginDone = false;
      ntpTimeLogged = false;
    }

    if (now - lastRadioModeLogMs >= RADIO_MODE_LOG_INTERVAL_MS) {
      ESP_LOGI("NET_TASK",
               "Radio status: espnow=%s wifi=%s channel=%u ip=%s",
               app::espnow::espnowMaster.isReady() ? "ready" : "not_ready",
               wifiManager.isConnected() ? "connected" : "disconnected",
               WiFi.channel(),
               wifiManager.getIPAddress().c_str());
      lastRadioModeLogMs = now;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

}  // namespace

bool startNetworkTask() {
  if (networkTaskHandle != nullptr) {
    return true;
  }

  BaseType_t created = xTaskCreatePinnedToCore(
      networkTaskRunner,
      "network_task",
      NETWORK_TASK_STACK,
      nullptr,
      NETWORK_TASK_PRIORITY,
      &networkTaskHandle,
      tskNO_AFFINITY);

  if (created != pdPASS) {
    ESP_LOGE("NET_TASK", "Failed to start network task");
    networkTaskHandle = nullptr;
    return false;
  }

  ESP_LOGI("NET_TASK", "Network task started");
  return true;
}

}  // namespace app::tasks
