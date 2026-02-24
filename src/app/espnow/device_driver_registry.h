#pragma once

#include <Arduino.h>

namespace app::espnow::device_driver {

enum class DeviceKind : uint8_t {
  Unknown = 0,
  WeatherNode = 1,
  CameraNode = 2,
};

struct DeviceProfile {
  DeviceKind kind = DeviceKind::Unknown;
  String kindLabel = "Unknown";
};

DeviceProfile classify(const String& deviceId, uint32_t featureBits);

}  // namespace app::espnow::device_driver
