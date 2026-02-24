#pragma once

#include <Arduino.h>

namespace app::espnow::state_store {

bool upsertFromStatePayload(const String& payload);
bool getLatestValue(const String& state, const String& key, String& valueOut);
bool getLastUpdateMs(const String& state, uint32_t& lastUpdateMsOut);

}  // namespace app::espnow::state_store
