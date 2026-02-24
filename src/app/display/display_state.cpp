#include "display_state.h"

#include "app/espnow/master_state_kv_store.h"
#include "app/espnow/payload_codec.h"

namespace app::display {
namespace {

String trimUnit(const String& value, const char* suffix) {
  if (suffix == nullptr) {
    return value;
  }

  String out = value;
  out.trim();

  const int suffixLen = strlen(suffix);
  const int outLen = out.length();
  if (outLen >= suffixLen && out.substring(outLen - suffixLen).equalsIgnoreCase(suffix)) {
    out = out.substring(0, outLen - suffixLen);
    out.trim();
  }

  return out;
}

bool getFirstAvailableSensorValue(const char* const* keys, size_t keyCount, String& outValue) {
  using app::espnow::state_store::getLatestValue;
  for (size_t i = 0; i < keyCount; ++i) {
    if (keys[i] == nullptr) {
      continue;
    }
    if (getLatestValue("sensor", keys[i], outValue)) {
      return true;
    }
  }
  return false;
}

String weatherCodeToText(int code) {
  switch (code) {
    case 0:
      return "CLEAR";
    case 1:
    case 2:
      return "PARTLY CLOUDY";
    case 3:
      return "OVERCAST";
    case 45:
    case 48:
      return "FOG";
    case 51:
    case 53:
    case 55:
      return "DRIZZLE";
    case 56:
    case 57:
      return "FREEZING DRIZZLE";
    case 61:
    case 63:
    case 65:
      return "RAIN";
    case 66:
    case 67:
      return "FREEZING RAIN";
    case 71:
    case 73:
    case 75:
      return "SNOW";
    case 77:
      return "SNOW GRAINS";
    case 80:
    case 81:
    case 82:
      return "RAIN SHOWERS";
    case 85:
    case 86:
      return "SNOW SHOWERS";
    case 95:
      return "THUNDER";
    case 96:
    case 99:
      return "THUNDER HAIL";
    default:
      return "UNKNOWN";
  }
}

}  // namespace

namespace state_logic {

bool pullFromStateStore(DisplayStateData& state) {
  using app::espnow::state_store::getLatestValue;

  bool changed = false;

  String sensorTempValue;
  if (getLatestValue("sensor", "temp", sensorTempValue)) {
    const String raw = trimUnit(sensorTempValue, "C");
    const String normalized = raw.isEmpty() ? "--" : raw;
    if (state.sensorTemp != normalized) {
      state.sensorTemp = normalized;
      changed = true;
    }
  }

  String sensorHumValue;
  if (getLatestValue("sensor", "hum", sensorHumValue)) {
    const String raw = trimUnit(sensorHumValue, "%");
    const String normalized = raw.isEmpty() ? "--" : raw;
    if (state.sensorHum != normalized) {
      state.sensorHum = normalized;
      changed = true;
    }
  }

  String sensorBatteryValue;
  static constexpr const char* BATTERY_KEYS[] = {"batt", "battery", "bat", "voltage", "vbat"};
  if (getFirstAvailableSensorValue(BATTERY_KEYS, sizeof(BATTERY_KEYS) / sizeof(BATTERY_KEYS[0]), sensorBatteryValue)) {
    const String normalized = sensorBatteryValue.isEmpty() ? "--" : sensorBatteryValue;
    if (state.sensorBattery != normalized) {
      state.sensorBattery = normalized;
      changed = true;
    }
  }

  String weatherCode;
  if (getLatestValue("weather", "code", weatherCode)) {
    const int code = weatherCode.toInt();
    const String nextLabel = weatherCodeToText(code);
    if (state.weatherLabel != nextLabel) {
      state.weatherLabel = nextLabel;
      changed = true;
    }
    if (state.weatherCode != code) {
      state.weatherCode = code;
      changed = true;
    }
  }

  String weatherTimeValue;
  if (getLatestValue("weather", "time", weatherTimeValue)) {
    if (state.weatherTime != weatherTimeValue) {
      state.weatherTime = weatherTimeValue;
      changed = true;
    }
  }

  return changed;
}

bool applyStatePayload(DisplayStateData& state, const String& payload) {
  if (payload.isEmpty()) {
    return false;
  }

  String stateName;
  if (!app::espnow::codec::getField(payload, "state", stateName) || stateName.isEmpty()) {
    return false;
  }

  bool changed = false;

  if (stateName == "weather") {
    String code;
    if (app::espnow::codec::getField(payload, "code", code) && !code.isEmpty()) {
      const int codeValue = code.toInt();
      const String nextLabel = weatherCodeToText(codeValue);
      if (state.weatherLabel != nextLabel) {
        state.weatherLabel = nextLabel;
        changed = true;
      }
      if (state.weatherCode != codeValue) {
        state.weatherCode = codeValue;
        changed = true;
      }
    }

    String time;
    if (app::espnow::codec::getField(payload, "time", time) && !time.isEmpty()) {
      if (state.weatherTime != time) {
        state.weatherTime = time;
        changed = true;
      }
    }
  } else if (stateName == "sensor") {
    String temp;
    if (app::espnow::codec::getField(payload, "temp", temp)) {
      const String normalized = trimUnit(temp, "C");
      if (!normalized.isEmpty() && state.sensorTemp != normalized) {
        state.sensorTemp = normalized;
        changed = true;
      }
    }

    String hum;
    if (app::espnow::codec::getField(payload, "hum", hum)) {
      const String normalized = trimUnit(hum, "%");
      if (!normalized.isEmpty() && state.sensorHum != normalized) {
        state.sensorHum = normalized;
        changed = true;
      }
    }

    String batt;
    bool hasBatt = app::espnow::codec::getField(payload, "batt", batt)
                || app::espnow::codec::getField(payload, "battery", batt)
                || app::espnow::codec::getField(payload, "bat", batt)
                || app::espnow::codec::getField(payload, "voltage", batt)
                || app::espnow::codec::getField(payload, "vbat", batt);
    if (hasBatt) {
      batt.trim();
      const String normalized = batt.isEmpty() ? "--" : batt;
      if (state.sensorBattery != normalized) {
        state.sensorBattery = normalized;
        changed = true;
      }
    }
  }

  return changed;
}

}  // namespace state_logic
}  // namespace app::display
