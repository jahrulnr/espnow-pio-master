#include "master_state_handler.h"
#include "master.h"
#include "app/display/display_interface.h"
#include "master_http_proxy.h"
#include "master_state_kv_store.h"
#include "payload_codec.h"
#include "state_binary.h"
#include "camera_stream_buffer.h"

#include <esp_log.h>
#include <cstring>
#include <cstdlib>

namespace app::espnow {

static constexpr const char* TAG = "espnow_state";

static String buildTextPayloadFromBinary(const uint8_t* payload, uint8_t payloadSize) {
  if (payload == nullptr || payloadSize == 0) {
    return "";
  }

  using namespace app::espnow::state_binary;

  if (hasTypeAndSize(payload, payloadSize, Type::Identity, sizeof(IdentityState))) {
    const auto* state = reinterpret_cast<const IdentityState*>(payload);
    return app::espnow::codec::buildPayload({
        {"state", "identity"},
        {"id", String(state->id)},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::Sensor, sizeof(SensorState))) {
    const auto* state = reinterpret_cast<const SensorState*>(payload);
    return app::espnow::codec::buildPayload({
        {"state", "sensor"},
        {"temp", String(state->temperature10 / 10.0f, 1) + "C"},
        {"hum", String(state->humidity10 / 10.0f, 1) + "%"},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::ProxyReq, sizeof(ProxyReqState))) {
    const auto* state = reinterpret_cast<const ProxyReqState*>(payload);

    String method = "GET";
    if (state->method == static_cast<uint8_t>(HttpMethod::Post)) {
      method = "POST";
    } else if (state->method == static_cast<uint8_t>(HttpMethod::Patch)) {
      method = "PATCH";
    }

    return app::espnow::codec::buildPayload({
        {"state", "proxy_req"},
        {"method", method},
        {"url", String(state->url)},
        {"payload", "{}"},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::Weather, sizeof(WeatherState))) {
    const auto* state = reinterpret_cast<const WeatherState*>(payload);
    return app::espnow::codec::buildPayload({
        {"state", "weather"},
        {"ok", String(state->ok)},
        {"code", String(state->code)},
        {"time", String(state->time)},
        {"temperature", String(state->temperature10 / 10.0f, 1)},
        {"windspeed", String(state->windspeed10 / 10.0f, 1)},
        {"winddirection", String(state->winddirection)},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::SlaveAlive, sizeof(SlaveAliveState))) {
    return app::espnow::codec::buildPayload({
        {"state", "slave_alive"},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::Features, sizeof(FeaturesState))) {
    const auto* state = reinterpret_cast<const FeaturesState*>(payload);
    return app::espnow::codec::buildPayload({
        {"state", "features"},
        {"bits", String(state->featureBits)},
        {"contract", String(state->contractVersion)},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::CameraMeta, sizeof(CameraMetaState))) {
    const auto* state = reinterpret_cast<const CameraMetaState*>(payload);
    return app::espnow::codec::buildPayload({
        {"state", "camera"},
        {"frame", String(state->frameId)},
        {"bytes", String(state->totalBytes)},
        {"chunks", String(state->totalChunks)},
        {"w", String(state->width)},
        {"h", String(state->height)},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::CameraChunk, sizeof(CameraChunkState))) {
    const auto* state = reinterpret_cast<const CameraChunkState*>(payload);
    return app::espnow::codec::buildPayload({
        {"state", "camera_chunk"},
        {"frame", String(state->frameId)},
        {"idx", String(state->idx)},
        {"total", String(state->total)},
    });
  }

  if (hasTypeAndSize(payload, payloadSize, Type::CameraFrameEnd, sizeof(CameraFrameEndState))) {
    const auto* state = reinterpret_cast<const CameraFrameEndState*>(payload);
    return app::espnow::codec::buildPayload({
        {"state", "camera_end"},
        {"frame", String(state->frameId)},
        {"bytes", String(state->totalBytes)},
        {"chunks", String(state->totalChunks)},
    });
  }

  return "";
}

void defaultSlaveStateHandler(const uint8_t mac[6], const char* stateText, uint8_t payloadSize) {
  if (stateText == nullptr) {
    ESP_LOGI(TAG, "Slave state packet (empty)");
    return;
  }

  const String payload = String(stateText);
  String stateName;
  app::espnow::codec::getField(payload, "state", stateName);

  if (stateName == "camera_chunk" || stateName == "camera_end") {
    updateTrackedDeviceStatePayload(mac, payload);
    return;
  }

  updateTrackedDeviceStatePayload(mac, payload);

  if (stateName != "features") {
    app::espnow::state_store::upsertFromStatePayload(payload);
    app::display::displayInterface.applyStatePayload(payload);
  }

  String deviceId;
  if (!app::espnow::codec::getField(payload, "id", deviceId) || deviceId.isEmpty()) {
    getTrackedDeviceIdentity(mac, deviceId);
  }

  const char* idText = deviceId.isEmpty() ? "unknown" : deviceId.c_str();

  if (stateName == "identity") {
    ESP_LOGI(TAG,
             "Slave %02X:%02X:%02X:%02X:%02X:%02X identity accepted: id=%s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], idText);
    return;
  }

  if (stateName == "sensor") {
    ESP_LOGI(TAG,
             "Slave %02X:%02X:%02X:%02X:%02X:%02X id=%s sensor update: %s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], idText, stateText);
    return;
  }

  if (stateName == "proxy_req") {
    ESP_LOGI(TAG,
             "Slave %02X:%02X:%02X:%02X:%02X:%02X id=%s proxy request: %s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], idText, stateText);
    return;
  }

  if (stateName == "weather") {
    ESP_LOGI(TAG,
             "Slave %02X:%02X:%02X:%02X:%02X:%02X id=%s weather update: %s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], idText, stateText);
    return;
  }

  if (stateName == "features") {
    ESP_LOGI(TAG,
             "Slave %02X:%02X:%02X:%02X:%02X:%02X id=%s features: %s",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], idText, stateText);
    return;
  }

  if (stateName == "camera") {
    return;
  }

  ESP_LOGI(TAG,
           "Slave %02X:%02X:%02X:%02X:%02X:%02X id=%s state (%u bytes): %s",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], idText, payloadSize, stateText);
}

void handleMasterHelloEvent(const esp_now_recv_info_t* recvInfo) {
  if (recvInfo == nullptr) {
    return;
  }

  ESP_LOGI(TAG,
           "Slave hello from %02X:%02X:%02X:%02X:%02X:%02X",
           recvInfo->src_addr[0], recvInfo->src_addr[1], recvInfo->src_addr[2],
           recvInfo->src_addr[3], recvInfo->src_addr[4], recvInfo->src_addr[5]);
}

void handleMasterStateEvent(MasterNode& master,
                            const esp_now_recv_info_t* recvInfo,
                            const uint8_t* payload,
                            uint8_t payloadSize,
                            SlaveStateHandler stateHandler) {
  if (recvInfo == nullptr || payload == nullptr) {
    return;
  }

  if (app::espnow::state_binary::hasTypeAndSize(payload,
                                                 payloadSize,
                                                 app::espnow::state_binary::Type::CameraMeta,
                                                 sizeof(app::espnow::state_binary::CameraMetaState))) {
    const auto* meta = reinterpret_cast<const app::espnow::state_binary::CameraMetaState*>(payload);
    app::espnow::camera_stream::ingestMeta(recvInfo->src_addr, *meta);
    app::display::displayInterface.requestRender();
  } else if (app::espnow::state_binary::hasTypeAndSize(payload,
                                                        payloadSize,
                                                        app::espnow::state_binary::Type::CameraChunk,
                                                        sizeof(app::espnow::state_binary::CameraChunkState))) {
    const auto* chunk = reinterpret_cast<const app::espnow::state_binary::CameraChunkState*>(payload);
    app::espnow::camera_stream::ingestChunk(recvInfo->src_addr, *chunk);
  } else if (app::espnow::state_binary::hasTypeAndSize(payload,
                                                        payloadSize,
                                                        app::espnow::state_binary::Type::CameraFrameEnd,
                                                        sizeof(app::espnow::state_binary::CameraFrameEndState))) {
    const auto* frameEnd = reinterpret_cast<const app::espnow::state_binary::CameraFrameEndState*>(payload);
    app::espnow::camera_stream::ingestFrameEnd(recvInfo->src_addr, *frameEnd);
    app::display::displayInterface.requestRender();
  }

  const String payloadText = buildTextPayloadFromBinary(payload, payloadSize);
  if (payloadText.isEmpty()) {
    ESP_LOGW(TAG,
             "Ignore invalid/unknown binary state from %02X:%02X:%02X:%02X:%02X:%02X",
             recvInfo->src_addr[0], recvInfo->src_addr[1], recvInfo->src_addr[2],
             recvInfo->src_addr[3], recvInfo->src_addr[4], recvInfo->src_addr[5]);
    return;
  }

  String deviceId;
  String stateName;
  app::espnow::codec::getField(payloadText, "state", stateName);

  const bool hasDeviceId = app::espnow::codec::getField(payloadText, "id", deviceId) && !deviceId.isEmpty();
  const bool verified = isTrackedDeviceVerified(recvInfo->src_addr);
  const bool allowPreVerifiedProxyReq = (stateName == "proxy_req" || stateName == "features");

  if (!verified && !hasDeviceId && !allowPreVerifiedProxyReq) {
    return;
  }

  if (!verified && !hasDeviceId && allowPreVerifiedProxyReq) {
    ESP_LOGW(TAG,
             "Allow proxy_req from unverified slave %02X:%02X:%02X:%02X:%02X:%02X",
             recvInfo->src_addr[0], recvInfo->src_addr[1], recvInfo->src_addr[2],
             recvInfo->src_addr[3], recvInfo->src_addr[4], recvInfo->src_addr[5]);
  }

  if (hasDeviceId) {
    updateTrackedDeviceIdentity(recvInfo->src_addr, deviceId);
  }

  if (stateName == "features") {
    String bits;
    if (app::espnow::codec::getField(payloadText, "bits", bits)) {
      updateTrackedDeviceFeatures(recvInfo->src_addr, static_cast<uint32_t>(strtoul(bits.c_str(), nullptr, 10)));
    }
  }

  char stateText[MAX_PAYLOAD_SIZE + 1] = {0};
  strncpy(stateText, payloadText.c_str(), MAX_PAYLOAD_SIZE);
  stateText[MAX_PAYLOAD_SIZE] = '\0';

  if (stateHandler != nullptr) {
    stateHandler(recvInfo->src_addr, stateText, strlen(stateText));
  }

  if (enqueueProxyRequest(recvInfo->src_addr, stateText)) {
    return;
  }
}

}  // namespace app::espnow
