#include "master.h"
#include "master_state_handler.h"
#include "master_http_proxy.h"
#include "payload_codec.h"
#include "state_binary.h"
#include "device_driver_registry.h"
#include "core/weather_sync.h"
#include <app_config.h>

#include <WiFi.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <cstdlib>

namespace app::espnow {

static const char* TAG = "espnow_master";
static constexpr uint8_t MAX_CHANNEL_SET_RETRIES = 5;
static constexpr uint32_t INTERNET_STATUS_INTERVAL_MS = 5000;
static constexpr uint32_t IDENTITY_REQ_INTERVAL_MS = 3000;
static constexpr size_t MAX_TRACKED_DEVICES = 32;
static constexpr uint32_t DEVICE_TIMEOUT_MS = 15000;
static constexpr size_t MAX_BLACKLISTED_DEVICES = 32;

struct TrackedDevice {
  bool active = false;
  uint8_t mac[6] = {0};
  uint32_t lastSeenMs = 0;
  uint32_t lastIdentityReqMs = 0;
  String lastKnownId;
  uint32_t featureBits = 0;
  String kindLabel;
  String statusLine;
  int16_t sensorTemp10 = 0;
  uint16_t sensorHum10 = 0;
  bool hasSensor = false;
  int16_t weatherCode = -1;
  String weatherTime;
  uint32_t cameraFrameId = 0;
  uint32_t cameraBytes = 0;
  uint16_t cameraChunks = 0;
};

struct BlacklistedDevice {
  bool active = false;
  uint8_t mac[6] = {0};
  uint32_t expiresAtMs = 0;
};

static TrackedDevice trackedDevices[MAX_TRACKED_DEVICES];
static BlacklistedDevice blacklistedDevices[MAX_BLACKLISTED_DEVICES];

static void macToText(const uint8_t mac[6], char out[18]) {
  snprintf(out,
           18,
           "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static int findTrackedDevice(const uint8_t mac[6]) {
  for (size_t i = 0; i < MAX_TRACKED_DEVICES; ++i) {
    if (!trackedDevices[i].active) {
      continue;
    }
    if (memcmp(trackedDevices[i].mac, mac, 6) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static int findBlacklistedDevice(const uint8_t mac[6]) {
  for (size_t i = 0; i < MAX_BLACKLISTED_DEVICES; ++i) {
    if (!blacklistedDevices[i].active) {
      continue;
    }
    if (memcmp(blacklistedDevices[i].mac, mac, 6) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

static size_t countTrackedDevices() {
  size_t count = 0;
  for (const auto& device : trackedDevices) {
    if (device.active) {
      count++;
    }
  }
  return count;
}

static bool hasIdentifiedTrackedDevice() {
  for (const auto& device : trackedDevices) {
    if (!device.active) {
      continue;
    }

    if (!device.lastKnownId.isEmpty()) {
      return true;
    }
  }
  return false;
}

static void logTrackedDevices() {
  ESP_LOGI(TAG, "Active devices: %u", static_cast<unsigned>(countTrackedDevices()));
  for (const auto& device : trackedDevices) {
    if (!device.active) {
      continue;
    }
    char macText[18] = {0};
    macToText(device.mac, macText);
    ESP_LOGI(TAG,
             " - %s id=%s",
             macText,
             device.lastKnownId.isEmpty() ? "unknown" : device.lastKnownId.c_str());
  }
}

static void touchTrackedDevice(const uint8_t mac[6], uint32_t nowMs) {
  const int existingIndex = findTrackedDevice(mac);
  if (existingIndex >= 0) {
    trackedDevices[existingIndex].lastSeenMs = nowMs;
    return;
  }

  for (size_t i = 0; i < MAX_TRACKED_DEVICES; ++i) {
    if (trackedDevices[i].active) {
      continue;
    }

    trackedDevices[i].active = true;
    memcpy(trackedDevices[i].mac, mac, 6);
    trackedDevices[i].lastSeenMs = nowMs;
    trackedDevices[i].lastIdentityReqMs = 0;
    trackedDevices[i].lastKnownId = "";
    trackedDevices[i].featureBits = 0;
    trackedDevices[i].kindLabel = "Unknown";
    trackedDevices[i].statusLine = "pending";

    char macText[18] = {0};
    macToText(mac, macText);
    ESP_LOGI(TAG, "Device connected: %s", macText);
    logTrackedDevices();
    return;
  }

  ESP_LOGW(TAG, "Tracked devices full, cannot add new device");
}

static void requestIdentityFromUnverified(MasterNode& master, uint32_t nowMs) {
  for (auto& device : trackedDevices) {
    if (!device.active || !device.lastKnownId.isEmpty()) {
      continue;
    }

    if (nowMs - device.lastIdentityReqMs < IDENTITY_REQ_INTERVAL_MS) {
      continue;
    }

    app::espnow::state_binary::IdentityReqCommand req = {};
    app::espnow::state_binary::initHeader(req.header, app::espnow::state_binary::Type::IdentityReq);

    const bool sent = master.send(device.mac, PacketType::COMMAND, &req, sizeof(req));
    device.lastIdentityReqMs = nowMs;

    if (sent) {
      char macText[18] = {0};
      macToText(device.mac, macText);
      ESP_LOGI(TAG, "Identity request sent to %s", macText);
    }
  }
}

static void refreshTrackedDeviceProfile(TrackedDevice& device) {
  const auto profile = app::espnow::device_driver::classify(device.lastKnownId, device.featureBits);
  device.kindLabel = profile.kindLabel;

  if (profile.kind == app::espnow::device_driver::DeviceKind::CameraNode) {
    if (device.cameraFrameId > 0) {
      device.statusLine = String("frame=") + String(device.cameraFrameId)
                        + " bytes=" + String(device.cameraBytes);
    } else {
      device.statusLine = "camera ready";
    }
    return;
  }

  if (profile.kind == app::espnow::device_driver::DeviceKind::WeatherNode) {
    if (device.hasSensor) {
      device.statusLine = String("temp=") + String(device.sensorTemp10 / 10.0f, 1)
                        + " hum=" + String(device.sensorHum10 / 10.0f, 1);
    } else if (!device.weatherTime.isEmpty()) {
      device.statusLine = String("weather @") + device.weatherTime;
    } else {
      device.statusLine = "weather node";
    }
    return;
  }

  device.statusLine = device.lastKnownId.isEmpty() ? "pending" : "online";
}

void updateTrackedDeviceIdentity(const uint8_t mac[6], const String& deviceId) {
  if (mac == nullptr || deviceId.isEmpty()) {
    return;
  }

  const int index = findTrackedDevice(mac);
  if (index < 0) {
    return;
  }

  if (trackedDevices[index].lastKnownId == deviceId) {
    return;
  }

  trackedDevices[index].lastKnownId = deviceId;
  trackedDevices[index].lastIdentityReqMs = 0;
  refreshTrackedDeviceProfile(trackedDevices[index]);
  char macText[18] = {0};
  macToText(mac, macText);
  ESP_LOGI(TAG, "Device identity updated: %s -> %s", macText, deviceId.c_str());
  logTrackedDevices();
}

void updateTrackedDeviceFeatures(const uint8_t mac[6], uint32_t featureBits) {
  if (mac == nullptr) {
    return;
  }

  const int index = findTrackedDevice(mac);
  if (index < 0) {
    return;
  }

  trackedDevices[index].featureBits = featureBits;
  refreshTrackedDeviceProfile(trackedDevices[index]);
}

void updateTrackedDeviceStatePayload(const uint8_t mac[6], const String& payload) {
  if (mac == nullptr || payload.isEmpty()) {
    return;
  }

  const int index = findTrackedDevice(mac);
  if (index < 0) {
    return;
  }

  auto& device = trackedDevices[index];

  String stateName;
  if (!app::espnow::codec::getField(payload, "state", stateName) || stateName.isEmpty()) {
    return;
  }

  if (stateName == "features") {
    String bits;
    if (app::espnow::codec::getField(payload, "bits", bits)) {
      device.featureBits = static_cast<uint32_t>(strtoul(bits.c_str(), nullptr, 10));
    }
    refreshTrackedDeviceProfile(device);
    return;
  }

  if (stateName == "sensor") {
    String temp;
    String hum;
    if (app::espnow::codec::getField(payload, "temp", temp)) {
      device.sensorTemp10 = static_cast<int16_t>(temp.toFloat() * 10.0f);
      device.hasSensor = true;
    }
    if (app::espnow::codec::getField(payload, "hum", hum)) {
      device.sensorHum10 = static_cast<uint16_t>(hum.toFloat() * 10.0f);
      device.hasSensor = true;
    }
    refreshTrackedDeviceProfile(device);
    return;
  }

  if (stateName == "weather") {
    String code;
    String time;
    if (app::espnow::codec::getField(payload, "code", code)) {
      device.weatherCode = static_cast<int16_t>(code.toInt());
    }
    if (app::espnow::codec::getField(payload, "time", time)) {
      device.weatherTime = time;
    }
    refreshTrackedDeviceProfile(device);
    return;
  }

  if (stateName == "camera") {
    String frameId;
    String totalBytes;
    String totalChunks;
    if (app::espnow::codec::getField(payload, "frame", frameId)) {
      device.cameraFrameId = static_cast<uint32_t>(strtoul(frameId.c_str(), nullptr, 10));
    }
    if (app::espnow::codec::getField(payload, "bytes", totalBytes)) {
      device.cameraBytes = static_cast<uint32_t>(strtoul(totalBytes.c_str(), nullptr, 10));
    }
    if (app::espnow::codec::getField(payload, "chunks", totalChunks)) {
      device.cameraChunks = static_cast<uint16_t>(strtoul(totalChunks.c_str(), nullptr, 10));
    }
    refreshTrackedDeviceProfile(device);
  }
}

size_t getTrackedDeviceSnapshotCount() {
  return countTrackedDevices();
}

static void fillSnapshotFromTracked(const TrackedDevice& device, TrackedDeviceSnapshot& out, uint32_t now) {
  out.active = true;
  out.verified = !device.lastKnownId.isEmpty();
  memcpy(out.mac, device.mac, sizeof(out.mac));
  out.deviceId = device.lastKnownId;
  out.kind = device.kindLabel;
  out.status = device.statusLine;
  out.featureBits = device.featureBits;
  out.hasSensor = device.hasSensor;
  out.sensorTemp10 = device.sensorTemp10;
  out.sensorHum10 = device.sensorHum10;
  out.weatherCode = device.weatherCode;
  out.weatherTime = device.weatherTime;
  out.cameraFrameId = device.cameraFrameId;
  out.cameraBytes = device.cameraBytes;
  out.cameraChunks = device.cameraChunks;
  out.ageMs = now - device.lastSeenMs;
}

size_t getTrackedDeviceSnapshots(TrackedDeviceSnapshot* out, size_t maxCount) {
  if (out == nullptr || maxCount == 0) {
    return 0;
  }

  size_t written = 0;
  const uint32_t now = millis();
  for (const auto& device : trackedDevices) {
    if (!device.active || written >= maxCount) {
      continue;
    }

    auto& row = out[written++];
    fillSnapshotFromTracked(device, row, now);
  }

  return written;
}

bool getTrackedDeviceSnapshotAt(size_t index, TrackedDeviceSnapshot& out) {
  const uint32_t now = millis();
  size_t current = 0;
  for (const auto& device : trackedDevices) {
    if (!device.active) {
      continue;
    }

    if (current == index) {
      fillSnapshotFromTracked(device, out, now);
      return true;
    }

    current++;
  }

  return false;
}

bool getTrackedDeviceSnapshotByMac(const uint8_t mac[6], TrackedDeviceSnapshot& out) {
  if (mac == nullptr) {
    return false;
  }

  const uint32_t now = millis();
  for (const auto& device : trackedDevices) {
    if (!device.active) {
      continue;
    }

    if (memcmp(device.mac, mac, 6) != 0) {
      continue;
    }

    fillSnapshotFromTracked(device, out, now);
    return true;
  }

  return false;
}

uint8_t getTrackedDeviceFocusMax() {
  const size_t count = countTrackedDevices();
  if (count == 0) {
    return 0;
  }

  const size_t maxIndex = count - 1;
  return maxIndex > 254 ? 254 : static_cast<uint8_t>(maxIndex);
}

bool isTrackedDeviceVerified(const uint8_t mac[6]) {
  if (mac == nullptr) {
    return false;
  }

  const int index = findTrackedDevice(mac);
  if (index < 0) {
    return false;
  }

  return !trackedDevices[index].lastKnownId.isEmpty();
}

bool getTrackedDeviceIdentity(const uint8_t mac[6], String& identityOut) {
  identityOut = "";
  if (mac == nullptr) {
    return false;
  }

  const int index = findTrackedDevice(mac);
  if (index < 0 || trackedDevices[index].lastKnownId.isEmpty()) {
    return false;
  }

  identityOut = trackedDevices[index].lastKnownId;
  return true;
}

static void pruneTrackedDevices(uint32_t nowMs) {
  bool changed = false;
  for (auto& device : trackedDevices) {
    if (!device.active) {
      continue;
    }

    if (nowMs - device.lastSeenMs <= DEVICE_TIMEOUT_MS) {
      continue;
    }

    char macText[18] = {0};
    macToText(device.mac, macText);
    ESP_LOGI(TAG, "Device disconnected (timeout): %s", macText);
    device = TrackedDevice{};
    changed = true;
  }

  if (changed) {
    logTrackedDevices();
  }
}

static void removeTrackedDevice(const uint8_t mac[6], const char* reason) {
  const int index = findTrackedDevice(mac);
  if (index < 0) {
    return;
  }

  char macText[18] = {0};
  macToText(mac, macText);
  ESP_LOGI(TAG, "Device removed: %s (%s)", macText, reason == nullptr ? "unknown" : reason);
  trackedDevices[index] = TrackedDevice{};
  logTrackedDevices();
}

static bool isBlacklisted(const uint8_t mac[6], uint32_t nowMs) {
  const int index = findBlacklistedDevice(mac);
  if (index < 0) {
    return false;
  }

  if (nowMs <= blacklistedDevices[index].expiresAtMs) {
    return true;
  }

  blacklistedDevices[index] = BlacklistedDevice{};
  return false;
}

static void pruneBlacklist(uint32_t nowMs) {
  for (auto& entry : blacklistedDevices) {
    if (!entry.active) {
      continue;
    }

    if (nowMs <= entry.expiresAtMs) {
      continue;
    }

    char macText[18] = {0};
    macToText(entry.mac, macText);
    ESP_LOGI(TAG, "Blacklist expired: %s", macText);
    entry = BlacklistedDevice{};
  }
}

void blacklistDeviceTemporarily(const uint8_t mac[6]) {
  if (mac == nullptr) {
    return;
  }

  const uint32_t nowMs = millis();
  const uint32_t expiresAt = nowMs + MASTER_BLACKLIST_DURATION_MS;
  int index = findBlacklistedDevice(mac);

  if (index < 0) {
    for (size_t i = 0; i < MAX_BLACKLISTED_DEVICES; ++i) {
      if (!blacklistedDevices[i].active) {
        index = static_cast<int>(i);
        break;
      }
    }
  }

  if (index < 0) {
    ESP_LOGW(TAG, "Blacklist full, cannot block device");
    return;
  }

  blacklistedDevices[index].active = true;
  memcpy(blacklistedDevices[index].mac, mac, 6);
  blacklistedDevices[index].expiresAtMs = expiresAt;

  char macText[18] = {0};
  macToText(mac, macText);
  ESP_LOGW(TAG,
           "Device blacklisted for %u ms: %s",
           static_cast<unsigned>(MASTER_BLACKLIST_DURATION_MS),
           macText);

  removeTrackedDevice(mac, "blacklisted");
}

static bool setWifiChannelRobust(uint8_t channel) {
  if (channel == 0) {
    return true;
  }

  esp_err_t startErr = esp_wifi_start();
  if (startErr != ESP_OK && startErr != ESP_ERR_WIFI_CONN) {
    ESP_LOGD(TAG, "esp_wifi_start before channel set returned: %s", esp_err_to_name(startErr));
  }

  for (uint8_t attempt = 1; attempt <= MAX_CHANNEL_SET_RETRIES; ++attempt) {
    const esp_err_t setErr = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    if (setErr == ESP_OK) {
      uint8_t primary = 0;
      wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
      if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary == channel) {
        return true;
      }
    } else {
      uint8_t primary = 0;
      wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
      if (esp_wifi_get_channel(&primary, &second) == ESP_OK && primary == channel) {
        return true;
      }
    }

    delay(25);
  }

  return false;
}

MasterNode* MasterNode::activeInstance = nullptr;
SlaveStateHandler MasterNode::stateHandler = defaultSlaveStateHandler;
MasterNode espnowMaster;
static uint32_t lastInternetStatusMs = 0;

bool MasterNode::isBroadcastMac(const uint8_t mac[6]) {
  if (mac == nullptr) {
    return false;
  }

  for (int i = 0; i < 6; ++i) {
    if (mac[i] != 0xFF) {
      return false;
    }
  }

  return true;
}

bool MasterNode::begin(uint8_t channel) {
  if (started) {
    return true;
  }

  const wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_NULL) {
    WiFi.mode(WIFI_STA);
  } else if (mode == WIFI_MODE_AP) {
    WiFi.mode(WIFI_AP_STA);
  }

  uint8_t resolvedChannel = channel;
  if (WiFi.status() == WL_CONNECTED) {
    resolvedChannel = WiFi.channel();
  }

  if (resolvedChannel > 0) {
    if (!setWifiChannelRobust(resolvedChannel)) {
      ESP_LOGW(TAG, "Failed to lock WiFi channel %u, continuing with current channel %u", resolvedChannel, WiFi.channel());
    }
  }

  esp_err_t initErr = esp_now_init();
  if (initErr != ESP_OK) {
    ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(initErr));
    return false;
  }

  activeInstance = this;
  esp_now_register_send_cb(MasterNode::onSendStatic);
  esp_now_register_recv_cb(MasterNode::onReceiveStatic);
  beginProxyWorker();

  esp_now_peer_info_t broadcastPeer = {};
  memcpy(broadcastPeer.peer_addr, BROADCAST_MAC, 6);
  broadcastPeer.ifidx = WIFI_IF_STA;
  broadcastPeer.channel = 0;
  broadcastPeer.encrypt = false;

  if (!esp_now_is_peer_exist(BROADCAST_MAC)) {
    esp_err_t addErr = esp_now_add_peer(&broadcastPeer);
    if (addErr != ESP_OK) {
      ESP_LOGW(TAG, "Cannot add broadcast peer: %s", esp_err_to_name(addErr));
    }
  }

  started = true;
  lastHelloMs = millis();
  lastHeartbeatMs = millis();
  ESP_LOGI(TAG, "ESP-NOW master ready on channel %u", WiFi.channel());
  return true;
}

void MasterNode::loop() {
  if (!started) {
    return;
  }

  const uint32_t now = millis();
  pruneTrackedDevices(now);
  pruneBlacklist(now);

  if (now - lastHelloMs >= 2000) {
    broadcast(PacketType::HELLO, MASTER_BEACON_ID, MASTER_BEACON_ID_LEN);
    lastHelloMs = now;
  }

  if (now - lastHeartbeatMs >= 5000) {
    broadcast(PacketType::HEARTBEAT, MASTER_BEACON_ID, MASTER_BEACON_ID_LEN);
    lastHeartbeatMs = now;
  }

  if (now - lastInternetStatusMs >= INTERNET_STATUS_INTERVAL_MS) {
    if (hasIdentifiedTrackedDevice()) {
      app::espnow::state_binary::MasterNetState internetState = {};
      app::espnow::state_binary::initHeader(internetState.header, app::espnow::state_binary::Type::MasterNet);
      internetState.online = static_cast<uint8_t>(WiFi.status() == WL_CONNECTED ? 1 : 0);
      internetState.channel = WiFi.channel();
      broadcast(PacketType::STATE, &internetState, sizeof(internetState));
    }
    lastInternetStatusMs = now;
  }

  core::weather_sync::tick(*this);

  requestIdentityFromUnverified(*this, now);

  processProxyResponses(*this);
}

bool MasterNode::addPeer(const uint8_t mac[6], uint8_t channel, bool encrypted) {
  if (!started || mac == nullptr) {
    return false;
  }

  if (esp_now_is_peer_exist(mac)) {
    return true;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.ifidx = WIFI_IF_STA;
  peer.channel = channel;
  peer.encrypt = encrypted;

  esp_err_t addErr = esp_now_add_peer(&peer);
  if (addErr != ESP_OK) {
    ESP_LOGE(TAG, "Add peer failed: %s", esp_err_to_name(addErr));
    return false;
  }

  peersCount++;
  ESP_LOGI(TAG, "Peer added: %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return true;
}

bool MasterNode::send(const uint8_t mac[6], PacketType type, const void* payload, size_t payloadSize) {
  if (!started || mac == nullptr) {
    return false;
  }

  Frame frame = {};
  frame.header.version = PROTOCOL_VERSION;
  frame.header.type = static_cast<uint8_t>(type);
  frame.header.sequence = sequence++;
  frame.header.timestampMs = millis();

  frame.payloadSize = payloadSize > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : payloadSize;
  if (frame.payloadSize > 0 && payload != nullptr) {
    memcpy(frame.payload, payload, frame.payloadSize);
  }

  const size_t frameBytes = sizeof(frame.header) + sizeof(frame.payloadSize) + frame.payloadSize;
  esp_err_t sendErr = esp_now_send(mac, reinterpret_cast<const uint8_t*>(&frame), frameBytes);
  if (sendErr != ESP_OK) {
    ESP_LOGW(TAG, "Send failed: %s", esp_err_to_name(sendErr));
    return false;
  }

  return true;
}

bool MasterNode::broadcast(PacketType type, const void* payload, size_t payloadSize) {
  return send(BROADCAST_MAC, type, payload, payloadSize);
}

void MasterNode::setStateHandler(SlaveStateHandler handler) {
  stateHandler = handler != nullptr ? handler : defaultSlaveStateHandler;
}

void MasterNode::onSendStatic(const esp_now_send_info_t* tx_info, esp_now_send_status_t status) {
  if (!activeInstance) {
    return;
  }

  if (tx_info == nullptr) {
    ESP_LOGD(TAG, "Send done -> %s", status == ESP_NOW_SEND_SUCCESS ? "ok" : "fail");
    return;
  }

  ESP_LOGD(TAG, "Send status=%s", status == ESP_NOW_SEND_SUCCESS ? "ok" : "fail");
}

void MasterNode::onReceiveStatic(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len) {
  if (!activeInstance || recv_info == nullptr || data == nullptr || len <= 0) {
    return;
  }

  if (len < static_cast<int>(sizeof(PacketHeader) + sizeof(uint8_t))) {
    ESP_LOGW(TAG, "Received frame too small: %d", len);
    return;
  }

  const auto* header = reinterpret_cast<const PacketHeader*>(data);
  const auto payloadSize = *(data + sizeof(PacketHeader));
  const auto* payload = data + sizeof(PacketHeader) + sizeof(uint8_t);
  const size_t expectedLen = sizeof(PacketHeader) + sizeof(uint8_t) + payloadSize;
  if (payloadSize > MAX_PAYLOAD_SIZE || expectedLen > static_cast<size_t>(len)) {
    ESP_LOGW(TAG, "Invalid frame size: payload=%u len=%d", payloadSize, len);
    return;
  }

  const uint32_t now = millis();
  if (!isBroadcastMac(recv_info->src_addr) && isBlacklisted(recv_info->src_addr, now)) {
    return;
  }

  if (!isBroadcastMac(recv_info->src_addr)) {
    touchTrackedDevice(recv_info->src_addr, now);
    activeInstance->addPeer(recv_info->src_addr);
  }

  ESP_LOGD(TAG,
           "RX from %02X:%02X:%02X:%02X:%02X:%02X type=%u seq=%u len=%d",
           recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
           recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5],
           header->type, header->sequence, len);

  const PacketType type = static_cast<PacketType>(header->type);
  switch (type) {
    case PacketType::HELLO:
      handleMasterHelloEvent(recv_info);
      break;
    case PacketType::STATE:
      if (payloadSize > 0) {
        handleMasterStateEvent(*activeInstance, recv_info, payload, payloadSize, stateHandler);
      } else {
        stateHandler(recv_info->src_addr, nullptr, 0);
      }
      break;
    case PacketType::COMMAND:
    case PacketType::HEARTBEAT:
    default:
      break;
  }
}

}  // namespace app::espnow
