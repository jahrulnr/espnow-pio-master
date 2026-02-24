#include "weather_sync.h"

#include "app/espnow/master.h"
#include "app/espnow/master_state_kv_store.h"
#include "app/espnow/protocol.h"
#include "app/espnow/state_binary.h"

#include <app_config.h>
#include <esp_log.h>

namespace core::weather_sync {

namespace {

static constexpr const char* TAG = "weather_sync";
uint32_t lastSyncRequestMs = 0;

bool hasWeatherData() {
  String weatherCode;
  String weatherTime;
  const bool hasCode = app::espnow::state_store::getLatestValue("weather", "code", weatherCode) && !weatherCode.isEmpty();
  const bool hasTime = app::espnow::state_store::getLatestValue("weather", "time", weatherTime) && !weatherTime.isEmpty();
  return hasCode && hasTime;
}

bool isWeatherStale(uint32_t nowMs) {
  uint32_t lastWeatherUpdateMs = 0;
  if (!app::espnow::state_store::getLastUpdateMs("weather", lastWeatherUpdateMs)) {
    return true;
  }

  if (lastWeatherUpdateMs == 0) {
    return true;
  }

  return (nowMs - lastWeatherUpdateMs) >= MASTER_WEATHER_STALE_MS;
}

}  // namespace

void tick(app::espnow::MasterNode& master) {
  const uint32_t nowMs = millis();
  if ((nowMs - lastSyncRequestMs) < MASTER_WEATHER_SYNC_RETRY_MS) {
    return;
  }

  const bool missingWeather = !hasWeatherData();
  const bool staleWeather = isWeatherStale(nowMs);
  if (!missingWeather && !staleWeather) {
    return;
  }

  app::espnow::state_binary::WeatherSyncReqCommand command = {};
  app::espnow::state_binary::initHeader(command.header, app::espnow::state_binary::Type::WeatherSyncReq);
  command.force = 1;

  if (master.broadcast(app::espnow::PacketType::COMMAND, &command, sizeof(command))) {
    ESP_LOGW(TAG,
             "Broadcast weather sync request (missing=%u stale=%u)",
             static_cast<unsigned>(missingWeather),
             static_cast<unsigned>(staleWeather));
    lastSyncRequestMs = nowMs;
  }
}

}  // namespace core::weather_sync
