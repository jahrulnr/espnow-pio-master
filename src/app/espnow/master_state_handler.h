#pragma once

#include <Arduino.h>
#include <esp_now.h>

#include "protocol.h"

namespace app::espnow {

class MasterNode;

using SlaveStateHandler = void (*)(const uint8_t mac[6], const char* stateText, uint8_t payloadSize);

void defaultSlaveStateHandler(const uint8_t mac[6], const char* stateText, uint8_t payloadSize);
void handleMasterHelloEvent(const esp_now_recv_info_t* recvInfo);
void handleMasterStateEvent(MasterNode& master,
							const esp_now_recv_info_t* recvInfo,
							const uint8_t* payload,
							uint8_t payloadSize,
							SlaveStateHandler stateHandler);

}  // namespace app::espnow
