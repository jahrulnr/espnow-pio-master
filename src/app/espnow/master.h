#pragma once

#include <Arduino.h>
#include <esp_now.h>

#include "protocol.h"
#include "master_state_handler.h"

namespace app::espnow {

struct TrackedDeviceSnapshot {
  bool active = false;
  bool verified = false;
  uint8_t mac[6] = {0};
  String deviceId;
  String kind;
  String status;
  uint32_t featureBits = 0;
  bool hasSensor = false;
  int16_t sensorTemp10 = 0;
  uint16_t sensorHum10 = 0;
  int16_t weatherCode = -1;
  String weatherTime;
  uint32_t cameraFrameId = 0;
  uint32_t cameraBytes = 0;
  uint16_t cameraChunks = 0;
  uint32_t ageMs = 0;
};

class MasterNode {
 public:
  MasterNode() = default;

  bool begin(uint8_t channel = 1);
  void loop();

  bool addPeer(const uint8_t mac[6], uint8_t channel = 0, bool encrypted = false);
  bool send(const uint8_t mac[6], PacketType type, const void* payload, size_t payloadSize);
  bool broadcast(PacketType type, const void* payload, size_t payloadSize);
  void setStateHandler(SlaveStateHandler handler);

  bool isReady() const { return started; }
  size_t peerCount() const { return peersCount; }

 private:
  static constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  static bool isBroadcastMac(const uint8_t mac[6]);

  static void onSendStatic(const esp_now_send_info_t* tx_info, esp_now_send_status_t status);
  static void onReceiveStatic(const esp_now_recv_info_t* recv_info, const uint8_t* data, int len);

  static MasterNode* activeInstance;
  static SlaveStateHandler stateHandler;

  uint16_t sequence = 0;
  bool started = false;
  uint32_t lastHelloMs = 0;
  uint32_t lastHeartbeatMs = 0;
  size_t peersCount = 0;
};

extern MasterNode espnowMaster;

void updateTrackedDeviceIdentity(const uint8_t mac[6], const String& deviceId);
void updateTrackedDeviceFeatures(const uint8_t mac[6], uint32_t featureBits);
void updateTrackedDeviceStatePayload(const uint8_t mac[6], const String& payload);
bool isTrackedDeviceVerified(const uint8_t mac[6]);
bool getTrackedDeviceIdentity(const uint8_t mac[6], String& identityOut);
size_t getTrackedDeviceSnapshotCount();
size_t getTrackedDeviceSnapshots(TrackedDeviceSnapshot* out, size_t maxCount);
bool getTrackedDeviceSnapshotAt(size_t index, TrackedDeviceSnapshot& out);
bool getTrackedDeviceSnapshotByMac(const uint8_t mac[6], TrackedDeviceSnapshot& out);
uint8_t getTrackedDeviceFocusMax();
void blacklistDeviceTemporarily(const uint8_t mac[6]);

}  // namespace app::espnow
