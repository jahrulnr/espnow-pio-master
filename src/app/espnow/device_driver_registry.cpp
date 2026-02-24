#include "device_driver_registry.h"

#include "state_binary.h"

namespace app::espnow::device_driver {

namespace {

bool hasFeature(uint32_t featureBits, app::espnow::state_binary::Feature feature) {
  return (featureBits & static_cast<uint32_t>(feature)) != 0;
}

String toLowerCopy(const String& input) {
  String out = input;
  out.toLowerCase();
  return out;
}

}  // namespace

DeviceProfile classify(const String& deviceId, uint32_t featureBits) {
  DeviceProfile profile;

  if (hasFeature(featureBits, app::espnow::state_binary::FeatureCameraStream) ||
      hasFeature(featureBits, app::espnow::state_binary::FeatureCameraJpeg)) {
    profile.kind = DeviceKind::CameraNode;
    profile.kindLabel = "Camera";
    return profile;
  }

  if (hasFeature(featureBits, app::espnow::state_binary::FeatureWeather) ||
      hasFeature(featureBits, app::espnow::state_binary::FeatureSensor)) {
    profile.kind = DeviceKind::WeatherNode;
    profile.kindLabel = "Weather";
    return profile;
  }

  const String loweredId = toLowerCopy(deviceId);
  if (loweredId.indexOf("cam") >= 0 || loweredId.indexOf("camera") >= 0) {
    profile.kind = DeviceKind::CameraNode;
    profile.kindLabel = "Camera";
    return profile;
  }

  if (loweredId.indexOf("weather") >= 0 || loweredId.indexOf("slave") >= 0) {
    profile.kind = DeviceKind::WeatherNode;
    profile.kindLabel = "Weather";
    return profile;
  }

  return profile;
}

}  // namespace app::espnow::device_driver
