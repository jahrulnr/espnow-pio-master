#pragma once

#include <Arduino.h>

namespace app::espnow {

class MasterNode;

bool beginProxyWorker();
bool tryHandleProxyRequest(const char* requestText, String& responseOut);
bool enqueueProxyRequest(const uint8_t mac[6], const char* requestText);
void processProxyResponses(MasterNode& master);
bool isProxyBusy();

}  // namespace app::espnow
