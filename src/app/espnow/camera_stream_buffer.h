#pragma once

#include <Arduino.h>

#include "state_binary.h"

namespace app::espnow::camera_stream {

void ingestMeta(const uint8_t mac[6], const state_binary::CameraMetaState& meta);
void ingestChunk(const uint8_t mac[6], const state_binary::CameraChunkState& chunk);
void ingestFrameEnd(const uint8_t mac[6], const state_binary::CameraFrameEndState& frameEnd);

bool getPreviewForMac(const uint8_t mac[6],
                      const uint16_t*& pixels,
                      uint16_t& width,
                      uint16_t& height,
                      uint32_t& frameId);
                      
bool getDecodedForMac(const uint8_t mac[6],
                      const uint16_t*& pixels,
                      uint16_t& width,
                      uint16_t& height,
                      uint32_t& frameId);

bool getRawJpegForMac(const uint8_t mac[6],
                      const uint8_t*& jpeg,
                      uint32_t& jpegBytes,
                      uint16_t& width,
                      uint16_t& height,
                      uint32_t& frameId);

}  // namespace app::espnow::camera_stream
