#include "camera_stream_buffer.h"

#include <JPEGDEC.h>
#include <LittleFS.h>
#include <esp_log.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <TJpg_Decoder.h>

namespace app::espnow::camera_stream {
namespace {

static constexpr const char* TAG = "cam_stream_buf";
static constexpr uint16_t PREVIEW_W = 160;
static constexpr uint16_t PREVIEW_H = 120;
static constexpr size_t MAX_JPEG_BYTES = 32768;
static constexpr size_t MAX_DECODE_BYTES = MAX_JPEG_BYTES + 512;
static constexpr uint16_t MAX_TRACKED_CHUNKS = static_cast<uint16_t>(MAX_JPEG_BYTES / state_binary::kCameraChunkDataBytes) + 2;
static constexpr uint8_t MAX_FAILED_DUMP_SLOTS = 4;

JPEGDEC jpeg;
// TJpg_Decoder instance is provided by the library (TJpgDec)

// Decode context holds temporary and destination buffers for a single decode.
struct DecodeContext {
  uint16_t srcW = 0;
  uint16_t srcH = 0;
  uint16_t dstW = PREVIEW_W;
  uint16_t dstH = PREVIEW_H;
  uint16_t* dstPixels = nullptr;
  uint16_t* tmpPixels = nullptr; // decoded (possibly scaled) full image
  uint16_t tmpW = 0;
  uint16_t tmpH = 0;
};

// Active decode context used by decoder callbacks.
DecodeContext* activeDecodeCtx = nullptr;

// Callback used by TJpg_Decoder to output decoded MCU blocks.
bool tjpgDrawCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t *bitmap) {
  if (activeDecodeCtx == nullptr || activeDecodeCtx->tmpPixels == nullptr) {
    return false;
  }

  // Clip to tmp buffer
  const int maxW = static_cast<int>(activeDecodeCtx->tmpW);
  const int maxH = static_cast<int>(activeDecodeCtx->tmpH);

  for (int row = 0; row < h; ++row) {
    int dstY = y + row;
    if (dstY < 0 || dstY >= maxH) continue;
    int dstX = x;
    int copyW = w;
    if (dstX < 0) {
      int skip = -dstX;
      if (skip >= copyW) continue;
      bitmap += skip;
      copyW -= skip;
      dstX = 0;
    }
    if (dstX + copyW > maxW) {
      copyW = maxW - dstX;
      if (copyW <= 0) continue;
    }

    uint16_t* dstRow = activeDecodeCtx->tmpPixels + (dstY * activeDecodeCtx->tmpW) + dstX;
    memcpy(dstRow, bitmap + (row * w), copyW * sizeof(uint16_t));
  }

  return true;
}

static const uint8_t kDefaultDhtSegment[] = {
  0xFF,0xC4,0x01,0xA2,
  0x00,0x00,0x01,0x05,0x01,0x01,0x01,0x01,0x01,0x01,0x00,0x00,
  0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
  0x09,0x0A,0x0B,
  0x10,0x00,0x02,0x01,0x03,0x03,0x02,0x04,0x03,0x05,0x05,0x04,
  0x04,0x00,0x00,0x01,0x7D,0x01,0x02,0x03,0x00,0x04,0x11,0x05,
  0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,0x22,0x71,0x14,
  0x32,0x81,0x91,0xA1,0x08,0x23,0x42,0xB1,0xC1,0x15,0x52,0xD1,
  0xF0,0x24,0x33,0x62,0x72,0x82,0x09,0x0A,0x16,0x17,0x18,0x19,
  0x1A,0x25,0x26,0x27,0x28,0x29,0x2A,0x34,0x35,0x36,0x37,0x38,
  0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x53,0x54,
  0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,0x68,
  0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x83,0x84,
  0x85,0x86,0x87,0x88,0x89,0x8A,0x92,0x93,0x94,0x95,0x96,0x97,
  0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xAA,
  0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,0xC3,0xC4,
  0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,0xD6,0xD7,
  0xD8,0xD9,0xDA,0xE1,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,
  0xEA,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA,
  0x01,0x00,0x03,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
  0x00,0x00,0x00,0x00,0x00,0x00,0x01,0x02,0x03,0x04,0x05,0x06,
  0x07,0x08,0x09,0x0A,0x0B,
  0x11,0x00,0x02,0x01,0x02,0x04,0x04,0x03,0x04,0x07,0x05,0x04,
  0x04,0x00,0x01,0x02,0x77,0x00,0x01,0x02,0x03,0x11,0x04,0x05,
  0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,0x13,0x22,0x32,
  0x81,0x08,0x14,0x42,0x91,0xA1,0xB1,0xC1,0x09,0x23,0x33,0x52,
  0xF0,0x15,0x62,0x72,0xD1,0x0A,0x16,0x24,0x34,0xE1,0x25,0xF1,
  0x17,0x18,0x19,0x1A,0x26,0x27,0x28,0x29,0x2A,0x35,0x36,0x37,
  0x38,0x39,0x3A,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0x4A,0x53,
  0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0x63,0x64,0x65,0x66,0x67,
  0x68,0x69,0x6A,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0x82,
  0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8A,0x92,0x93,0x94,0x95,
  0x96,0x97,0x98,0x99,0x9A,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,
  0xA9,0xAA,0xB2,0xB3,0xB4,0xB5,0xB6,0xB7,0xB8,0xB9,0xBA,0xC2,
  0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xCA,0xD2,0xD3,0xD4,0xD5,
  0xD6,0xD7,0xD8,0xD9,0xDA,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,
  0xE9,0xEA,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0xFA
};

uint16_t computeChecksum16(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0) {
    return 0;
  }

  uint32_t sum = 0;
  for (size_t index = 0; index < length; ++index) {
    sum += data[index];
  }
  return static_cast<uint16_t>(sum & 0xFFFF);
}

uint16_t detectSofMarker(const uint8_t* data, size_t length) {
  if (data == nullptr || length < 4) {
    return 0;
  }

  for (size_t index = 0; index + 1 < length; ++index) {
    if (data[index] != 0xFF) {
      continue;
    }

    const uint8_t marker = data[index + 1];
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      return static_cast<uint16_t>(0xFF00 | marker);
    }
  }

  return 0;
}

struct StreamState {
  bool frameOpen = false;
  bool previewReady = false;
  bool rawReady = false;
  uint8_t sourceMac[6] = {0};
  uint32_t frameId = 0;
  uint16_t srcW = 0;
  uint16_t srcH = 0;
  uint16_t expectedChunks = 0;
  uint16_t receivedChunks = 0;
  size_t expectedBytes = 0;
  size_t receivedBytes = 0;
  size_t maxWrittenOffset = 0;
  uint8_t chunkSeen[MAX_TRACKED_CHUNKS] = {0};
  uint8_t* jpegBytes = nullptr;
  uint8_t* rawJpegBytes = nullptr;
  uint8_t* decodeWorkBytes = nullptr;
  uint32_t rawJpegSize = 0;
  uint32_t rawFrameId = 0;
  uint16_t rawW = 0;
  uint16_t rawH = 0;
  uint8_t rawSourceMac[6] = {0};
  uint16_t* previewPixels = nullptr;
  // decoded (no-downscale) buffer and metadata
  uint16_t* decodedPixels = nullptr;
  uint16_t decodedW = 0;
  uint16_t decodedH = 0;
  bool decodedReady = false;
};

StreamState state;

bool legacyDumpCleanupDone = false;

void cleanupLegacyCameraDumpsOnce() {
  if (legacyDumpCleanupDone) {
    return;
  }
  legacyDumpCleanupDone = true;

  File dir = LittleFS.open("/cache");
  if (!dir || !dir.isDirectory()) {
    return;
  }

  File entry = dir.openNextFile();
  while (entry) {
    char path[96] = {0};
    const char* name = entry.name();
    if (name != nullptr) {
      strncpy(path, name, sizeof(path) - 1);
    }

    const bool isLegacyCamDump =
        strncmp(path, "/cache/cam_", strlen("/cache/cam_")) == 0 &&
        strncmp(path, "/cache/cam_fail_", strlen("/cache/cam_fail_")) != 0;

    entry.close();

    if (isLegacyCamDump) {
      LittleFS.remove(path);
    }

    entry = dir.openNextFile();
  }

  dir.close();
}

bool ensureBuffers() {
  if (state.jpegBytes == nullptr) {
    state.jpegBytes = static_cast<uint8_t*>(malloc(MAX_JPEG_BYTES));
    if (state.jpegBytes == nullptr) {
      ESP_LOGE(TAG, "Alloc jpeg buffer failed");
      return false;
    }
  }

  if (state.previewPixels == nullptr) {
    state.previewPixels = static_cast<uint16_t*>(malloc(PREVIEW_W * PREVIEW_H * sizeof(uint16_t)));
    if (state.previewPixels == nullptr) {
      ESP_LOGE(TAG, "Alloc preview buffer failed");
      return false;
    }
  }

  if (state.rawJpegBytes == nullptr) {
    state.rawJpegBytes = static_cast<uint8_t*>(malloc(MAX_JPEG_BYTES));
    if (state.rawJpegBytes == nullptr) {
      ESP_LOGE(TAG, "Alloc raw jpeg buffer failed");
      return false;
    }
  }

  if (state.decodeWorkBytes == nullptr) {
    state.decodeWorkBytes = static_cast<uint8_t*>(malloc(MAX_DECODE_BYTES));
    if (state.decodeWorkBytes == nullptr) {
      ESP_LOGE(TAG, "Alloc decode work buffer failed");
      return false;
    }
  }

  return true;
}

bool hasMarker(const uint8_t* data, size_t length, uint8_t marker) {
  if (data == nullptr || length < 2) {
    return false;
  }

  for (size_t index = 0; index + 1 < length; ++index) {
    if (data[index] == 0xFF && data[index + 1] == marker) {
      return true;
    }
  }

  return false;
}

void resetCurrentFrame() {
  state.frameOpen = false;
  state.expectedChunks = 0;
  state.receivedChunks = 0;
  state.expectedBytes = 0;
  state.receivedBytes = 0;
  state.maxWrittenOffset = 0;
  memset(state.chunkSeen, 0, sizeof(state.chunkSeen));
}

int jpegDrawCallback(JPEGDRAW* draw) {
  if (draw == nullptr || activeDecodeCtx == nullptr || activeDecodeCtx->tmpPixels == nullptr) {
    return 0;
  }

  auto* ctx = activeDecodeCtx;
  if (ctx->tmpW == 0 || ctx->tmpH == 0) {
    return 1;
  }

  auto* src = static_cast<const uint16_t*>(draw->pPixels);
  if (src == nullptr) {
    return 1;
  }

  // write decoded (scaled) pixels into temporary full-frame buffer
  for (int y = 0; y < draw->iHeight; ++y) {
    const int dstY = draw->y + y;
    if (dstY < 0 || dstY >= ctx->tmpH) {
      continue;
    }

    for (int x = 0; x < draw->iWidth; ++x) {
      const int dstX = draw->x + x;
      if (dstX < 0 || dstX >= ctx->tmpW) {
        continue;
      }

      ctx->tmpPixels[(dstY * ctx->tmpW) + dstX] = src[(y * draw->iWidth) + x];
    }
  }

  return 1;
}

bool decodeLatestFrameToPreview() {
  if (!ensureBuffers()) {
    return false;
  }

  if (state.receivedBytes == 0 || state.srcW == 0 || state.srcH == 0) {
    return false;
  }

  if (!(state.jpegBytes[0] == 0xFF && state.jpegBytes[1] == 0xD8)) {
    ESP_LOGW(TAG,
             "invalid SOI for frame=%lu bytes=%u",
             static_cast<unsigned long>(state.frameId),
             static_cast<unsigned>(state.receivedBytes));
    return false;
  }

  size_t decodeBytes = 0;
  for (size_t index = state.receivedBytes; index >= 2; --index) {
    if (state.jpegBytes[index - 2] == 0xFF && state.jpegBytes[index - 1] == 0xD9) {
      decodeBytes = index;
      break;
    }
    if (index == 2) {
      break;
    }
  }

  if (decodeBytes == 0) {
    ESP_LOGW(TAG,
             "missing EOI for frame=%lu bytes=%u",
             static_cast<unsigned long>(state.frameId),
             static_cast<unsigned>(state.receivedBytes));
    return false;
  }

  memset(state.previewPixels, 0, PREVIEW_W * PREVIEW_H * sizeof(uint16_t));

  DecodeContext ctx;
  // choose decoder scale level. Prefer decoding to a larger intermediate
  // (helps later upscaling in UI). Target a decode size of 240x180 when possible.
  const uint16_t DECODE_TARGET_W = 240;
  const uint16_t DECODE_TARGET_H = 180;
  uint16_t targetW = DECODE_TARGET_W;
  uint16_t targetH = DECODE_TARGET_H;
  if (targetW == 0) targetW = 1;
  if (targetH == 0) targetH = 1;

  // Heuristic: prefer a full (scale=0) decode when the source image is
  // small enough to decode fully into PSRAM to avoid decoder fast-scale
  // artifacts. Otherwise select the smallest decoder scale that still
  // yields at least PREVIEW size.
  int chosenScale = 0;
  uint16_t decW = state.srcW;
  uint16_t decH = state.srcH;

  const size_t MAX_FULL_DECODE_PIXELS = static_cast<size_t>(240) * static_cast<size_t>(180); // 240x180
  const size_t srcPixels = static_cast<size_t>(state.srcW) * static_cast<size_t>(state.srcH);

  if (srcPixels <= MAX_FULL_DECODE_PIXELS) {
    // prefer full decode (scale=0)
    chosenScale = 0;
    decW = state.srcW;
    decH = state.srcH;
  } else {
    // pick smallest decoder scale that yields >= PREVIEW size
    chosenScale = 3; // default to highest reduction
    decW = static_cast<uint16_t>(state.srcW >> chosenScale);
    decH = static_cast<uint16_t>(state.srcH >> chosenScale);
    for (int s = 0; s <= 3; ++s) {
      uint16_t w = static_cast<uint16_t>(state.srcW >> s);
      uint16_t h = static_cast<uint16_t>(state.srcH >> s);
      if (w == 0) w = 1;
      if (h == 0) h = 1;
      if (w >= PREVIEW_W && h >= PREVIEW_H) {
        chosenScale = s;
        decW = w;
        decH = h;
        break;
      }
    }
  }

  ctx.srcW = decW; // scaled decode width
  ctx.srcH = decH; // scaled decode height
  ctx.dstW = PREVIEW_W;
  ctx.dstH = PREVIEW_H;
  ctx.dstPixels = state.previewPixels;
  ctx.tmpW = decW;
  ctx.tmpH = decH;

  // allocate temporary decoded buffer
  const size_t tmpCount = static_cast<size_t>(decW) * static_cast<size_t>(decH);
  // allocate temporary decoded buffer in PSRAM if available to reduce OOM
  ctx.tmpPixels = static_cast<uint16_t*>(heap_caps_malloc(tmpCount * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (ctx.tmpPixels == nullptr) {
    // fallback to regular malloc
    ctx.tmpPixels = static_cast<uint16_t*>(malloc(tmpCount * sizeof(uint16_t)));
  }
  if (ctx.tmpPixels == nullptr) {
    ESP_LOGE(TAG, "alloc tmp decode buffer failed %u x %u", decW, decH);
    return false;
  }
  // zero to avoid holes
  memset(ctx.tmpPixels, 0, tmpCount * sizeof(uint16_t));

  activeDecodeCtx = &ctx;

  uint8_t* decodePtr = state.jpegBytes;
  size_t decodeLen = decodeBytes;
  bool dhtInjected = false;

  if (!hasMarker(state.jpegBytes, decodeBytes, 0xC4) && state.decodeWorkBytes != nullptr) {
    const size_t injectedLen = decodeBytes + sizeof(kDefaultDhtSegment);
    if (decodeBytes > 2 && injectedLen <= MAX_DECODE_BYTES) {
      state.decodeWorkBytes[0] = state.jpegBytes[0];
      state.decodeWorkBytes[1] = state.jpegBytes[1];
      memcpy(state.decodeWorkBytes + 2, kDefaultDhtSegment, sizeof(kDefaultDhtSegment));
      memcpy(state.decodeWorkBytes + 2 + sizeof(kDefaultDhtSegment), state.jpegBytes + 2, decodeBytes - 2);
      decodePtr = state.decodeWorkBytes;
      decodeLen = injectedLen;
      dhtInjected = true;
    }
  }

  // Try TJpg_Decoder first; fallback to JPEGDEC if TJpg_Decoder fails.
  bool usedTjpg = false;
  int decodeRc = 0;

  // Configure TJpg_Decoder
  TJpgDec.setCallback(tjpgDrawCallback);
  // map chosenScale (0..3) to scale factor (1,2,4,8)
  uint8_t jpgScaleFactor = 1;
  switch (chosenScale) {
    case 0: jpgScaleFactor = 1; break;
    case 1: jpgScaleFactor = 2; break;
    case 2: jpgScaleFactor = 4; break;
    case 3: jpgScaleFactor = 8; break;
    default: jpgScaleFactor = 1; break;
  }
  TJpgDec.setJpgScale(jpgScaleFactor);
  TJpgDec.setSwapBytes(false);

  JRESULT tjres = TJpgDec.drawJpg(0, 0, decodePtr, static_cast<uint32_t>(decodeLen));
  if (tjres == JDR_OK) {
    usedTjpg = true;
    decodeRc = 1;
  } else {
    // fallback to previous JPEGDEC open/decode path
    const bool openRamOk = jpeg.openRAM(decodePtr, static_cast<int>(decodeLen), jpegDrawCallback) != 0;
    int openRamErr = openRamOk ? JPEG_SUCCESS : jpeg.getLastError();

    bool openFlashOk = false;
    int openFlashErr = JPEG_INVALID_FILE;
    bool openFileOk = false;
    int openFileErr = JPEG_INVALID_FILE;
    bool usedFallbackOpen = false;
    bool usedFileOpen = false;
    char dumpPath[48] = {0};
    File decodeFile;

    if (!openRamOk) {
      openFlashOk = jpeg.openFLASH(decodePtr, static_cast<int>(decodeLen), jpegDrawCallback) != 0;
      openFlashErr = openFlashOk ? JPEG_SUCCESS : jpeg.getLastError();
      usedFallbackOpen = openFlashOk;
    }

    if (!openRamOk && !openFlashOk) {
      cleanupLegacyCameraDumpsOnce();

      const unsigned dumpSlot = static_cast<unsigned>(state.frameId % MAX_FAILED_DUMP_SLOTS);
      snprintf(dumpPath, sizeof(dumpPath), "/cache/cam_fail_%u.jpg", dumpSlot);

      File dumpFile = LittleFS.open(dumpPath, "w");
      if (dumpFile) {
        dumpFile.write(state.jpegBytes, decodeBytes);
        dumpFile.close();
      }

      decodeFile = LittleFS.open(dumpPath, "r");
      if (decodeFile) {
        openFileOk = jpeg.open(decodeFile, jpegDrawCallback) != 0;
        openFileErr = openFileOk ? JPEG_SUCCESS : jpeg.getLastError();
        usedFallbackOpen = openFileOk;
        usedFileOpen = openFileOk;
      }
    }

    if (!openRamOk && !openFlashOk && !openFileOk) {
      const uint8_t h0 = decodeBytes > 0 ? state.jpegBytes[0] : 0;
      const uint8_t h1 = decodeBytes > 1 ? state.jpegBytes[1] : 0;
      const uint8_t t0 = decodeBytes > 1 ? state.jpegBytes[decodeBytes - 2] : 0;
      const uint8_t t1 = decodeBytes > 0 ? state.jpegBytes[decodeBytes - 1] : 0;
      const uint16_t sof = detectSofMarker(state.jpegBytes, decodeBytes);

      activeDecodeCtx = nullptr;
      ESP_LOGW(TAG, "jpeg open failed frame=%lu bytes=%u used=%u hdr=%02X%02X tail=%02X%02X sof=0x%04X dht=%u openErr(ram=%d flash=%d file=%d) file=%s",
               static_cast<unsigned long>(state.frameId),
               static_cast<unsigned>(state.receivedBytes),
               static_cast<unsigned>(decodeBytes),
               static_cast<unsigned>(h0),
               static_cast<unsigned>(h1),
               static_cast<unsigned>(t0),
               static_cast<unsigned>(t1),
               static_cast<unsigned>(sof),
               static_cast<unsigned>(dhtInjected ? 1 : 0),
               openRamErr,
               openFlashErr,
               openFileErr,
               dumpPath[0] != '\0' ? dumpPath : "-");
      return false;
    }

    const int jdecRc = jpeg.decode(0, 0, chosenScale);
    jpeg.close();
    activeDecodeCtx = nullptr;

    if (jdecRc == 0) {
      ESP_LOGW(TAG, "decode failed frame=%lu rc=%d err=%d",
               static_cast<unsigned long>(state.frameId),
               jdecRc,
               jpeg.getLastError());
      free(ctx.tmpPixels);
      return false;
    }
    decodeRc = 1;
  }

  // Downscale from tmpPixels (decW x decH) -> previewPixels (dstW x dstH)
  if (ctx.tmpPixels != nullptr && ctx.dstPixels != nullptr) {
    // fast path: identical size
    if (ctx.tmpW == ctx.dstW && ctx.tmpH == ctx.dstH) {
      // fast path: identical size, copy directly (no R/B swap)
      for (size_t i = 0; i < static_cast<size_t>(ctx.dstW) * ctx.dstH; ++i) {
        ctx.dstPixels[i] = ctx.tmpPixels[i];
      }
    } else {
      // bilinear resampling in RGB888 space to avoid color banding
      auto unpack565 = [](uint16_t p, uint8_t &r, uint8_t &g, uint8_t &b) {
        uint8_t r5 = static_cast<uint8_t>((p >> 11) & 0x1F);
        uint8_t g6 = static_cast<uint8_t>((p >> 5) & 0x3F);
        uint8_t b5 = static_cast<uint8_t>(p & 0x1F);
        // expand to 8-bit (no channel swap)
        r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
        g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
        b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
      };

      // Pack to RGB565
      auto pack565 = [](uint8_t r8, uint8_t g8, uint8_t b8) -> uint16_t {
        // swap R and B: treat input r8 as blue and b8 as red for packing
        uint16_t r5 = static_cast<uint16_t>((r8 * 31 + 127) / 255);
        uint16_t g6 = static_cast<uint16_t>((g8 * 63 + 127) / 255);
        uint16_t b5 = static_cast<uint16_t>((b8 * 31 + 127) / 255);
        return static_cast<uint16_t>((r5 << 11) | (g6 << 5) | b5);
      };

      const float scaleX = static_cast<float>(ctx.tmpW) / static_cast<float>(ctx.dstW);
      const float scaleY = static_cast<float>(ctx.tmpH) / static_cast<float>(ctx.dstH);

      for (uint16_t yy = 0; yy < ctx.dstH; ++yy) {
        const float srcY = (yy + 0.5f) * scaleY - 0.5f;
        int y0 = static_cast<int>(floorf(srcY));
        int y1 = y0 + 1;
        float wy = srcY - static_cast<float>(y0);
        if (y0 < 0) { y0 = 0; }
        if (y1 < 0) { y1 = 0; }
        if (y0 >= static_cast<int>(ctx.tmpH)) { y0 = ctx.tmpH - 1; }
        if (y1 >= static_cast<int>(ctx.tmpH)) { y1 = ctx.tmpH - 1; }

        for (uint16_t xx = 0; xx < ctx.dstW; ++xx) {
          const float srcX = (xx + 0.5f) * scaleX - 0.5f;
          int x0 = static_cast<int>(floorf(srcX));
          int x1 = x0 + 1;
          float wx = srcX - static_cast<float>(x0);
          if (x0 < 0) { x0 = 0; }
          if (x1 < 0) { x1 = 0; }
          if (x0 >= static_cast<int>(ctx.tmpW)) { x0 = ctx.tmpW - 1; }
          if (x1 >= static_cast<int>(ctx.tmpW)) { x1 = ctx.tmpW - 1; }

          // sample four neighbors
          uint16_t p00 = ctx.tmpPixels[y0 * ctx.tmpW + x0];
          uint16_t p10 = ctx.tmpPixels[y0 * ctx.tmpW + x1];
          uint16_t p01 = ctx.tmpPixels[y1 * ctx.tmpW + x0];
          uint16_t p11 = ctx.tmpPixels[y1 * ctx.tmpW + x1];

          uint8_t r00,g00,b00; unpack565(p00,r00,g00,b00);
          uint8_t r10,g10,b10; unpack565(p10,r10,g10,b10);
          uint8_t r01,g01,b01; unpack565(p01,r01,g01,b01);
          uint8_t r11,g11,b11; unpack565(p11,r11,g11,b11);

          // bilinear interpolation
          float r0 = r00 + (r10 - r00) * wx;
          float r1 = r01 + (r11 - r01) * wx;
          float r = r0 + (r1 - r0) * wy;

          float g0 = g00 + (g10 - g00) * wx;
          float g1 = g01 + (g11 - g01) * wx;
          float g = g0 + (g1 - g0) * wy;

          float b0 = b00 + (b10 - b00) * wx;
          float b1 = b01 + (b11 - b01) * wx;
          float b = b0 + (b1 - b0) * wy;

          uint8_t rf = static_cast<uint8_t>(fminf(fmaxf(r, 0.0f), 255.0f));
          uint8_t gf = static_cast<uint8_t>(fminf(fmaxf(g, 0.0f), 255.0f));
          uint8_t bf = static_cast<uint8_t>(fminf(fmaxf(b, 0.0f), 255.0f));

          ctx.dstPixels[yy * ctx.dstW + xx] = pack565(rf, gf, bf);
        }
      }
    }
  }
  // copy tmp (decoded, possibly scaled by decoder) into persistent decoded buffer
  if (ctx.tmpPixels != nullptr) {
    const size_t decodedCount = static_cast<size_t>(ctx.tmpW) * static_cast<size_t>(ctx.tmpH);
    if (state.decodedPixels == nullptr || state.decodedW != ctx.tmpW || state.decodedH != ctx.tmpH) {
      if (state.decodedPixels != nullptr) {
        free(state.decodedPixels);
        state.decodedPixels = nullptr;
      }
      state.decodedPixels = static_cast<uint16_t*>(malloc(decodedCount * sizeof(uint16_t)));
      if (state.decodedPixels == nullptr) {
        ESP_LOGW(TAG, "alloc decoded persistent buffer failed %u x %u", ctx.tmpW, ctx.tmpH);
      }
    }

    if (state.decodedPixels != nullptr) {
      memcpy(state.decodedPixels, ctx.tmpPixels, decodedCount * sizeof(uint16_t));
      state.decodedW = ctx.tmpW;
      state.decodedH = ctx.tmpH;
      state.decodedReady = true;

      // No filesystem dumps in production; dumping removed per request.
    }

    free(ctx.tmpPixels);
  }

  state.previewReady = true;
  return true;
}

}  // namespace

void ingestMeta(const uint8_t mac[6], const state_binary::CameraMetaState& meta) {
  if (mac == nullptr) {
    return;
  }

  if (!ensureBuffers()) {
    return;
  }

  memcpy(state.sourceMac, mac, sizeof(state.sourceMac));
  state.frameId = meta.frameId;
  state.srcW = meta.width;
  state.srcH = meta.height;
  state.expectedChunks = meta.totalChunks;
  state.receivedChunks = 0;
  state.expectedBytes = meta.totalBytes;
  state.receivedBytes = 0;
  state.maxWrittenOffset = 0;
  memset(state.chunkSeen, 0, sizeof(state.chunkSeen));
  state.frameOpen = true;
}

void ingestChunk(const uint8_t mac[6], const state_binary::CameraChunkState& chunk) {
  if (mac == nullptr || !state.frameOpen) {
    return;
  }

  if (memcmp(mac, state.sourceMac, sizeof(state.sourceMac)) != 0) {
    return;
  }

  if (chunk.frameId != state.frameId || chunk.dataLen == 0) {
    return;
  }

  if (chunk.idx == 0 || chunk.idx >= MAX_TRACKED_CHUNKS) {
    return;
  }

  if (state.expectedChunks > 0 && chunk.idx > state.expectedChunks) {
    return;
  }

  const size_t chunkOffset = static_cast<size_t>(chunk.idx - 1) * state_binary::kCameraChunkDataBytes;
  const size_t chunkEnd = chunkOffset + chunk.dataLen;

  if (chunkEnd > MAX_JPEG_BYTES) {
    ESP_LOGW(TAG, "Frame exceeds local buffer, frame=%lu", static_cast<unsigned long>(state.frameId));
    resetCurrentFrame();
    return;
  }

  memcpy(state.jpegBytes + chunkOffset, chunk.data, chunk.dataLen);

  if (state.chunkSeen[chunk.idx] == 0) {
    state.chunkSeen[chunk.idx] = 1;
    state.receivedChunks++;
  }

  if (chunkEnd > state.maxWrittenOffset) {
    state.maxWrittenOffset = chunkEnd;
  }

}

void ingestFrameEnd(const uint8_t mac[6], const state_binary::CameraFrameEndState& frameEnd) {
  if (mac == nullptr || !state.frameOpen) {
    return;
  }

  if (memcmp(mac, state.sourceMac, sizeof(state.sourceMac)) != 0) {
    return;
  }

  if (frameEnd.frameId != state.frameId) {
    return;
  }

  if (frameEnd.totalChunks > 0) {
    state.expectedChunks = frameEnd.totalChunks;
  }
  if (frameEnd.totalBytes > 0) {
    state.expectedBytes = frameEnd.totalBytes;
  }

  if (state.expectedBytes > 0 && state.expectedBytes < state.maxWrittenOffset) {
    state.receivedBytes = state.expectedBytes;
  } else {
    state.receivedBytes = state.maxWrittenOffset;
  }

  if (frameEnd.reserved != 0 && state.receivedBytes > 0) {
    const uint16_t actualChecksum = computeChecksum16(state.jpegBytes, state.receivedBytes);
    if (actualChecksum != frameEnd.reserved) {
      ESP_LOGW(TAG,
               "Frame checksum mismatch frame=%lu expected=0x%04X actual=0x%04X bytes=%u",
               static_cast<unsigned long>(state.frameId),
               static_cast<unsigned>(frameEnd.reserved),
               static_cast<unsigned>(actualChecksum),
               static_cast<unsigned>(state.receivedBytes));
      resetCurrentFrame();
      return;
    }
  }

  if (state.expectedChunks > 0 && state.receivedChunks < state.expectedChunks) {
    ESP_LOGW(TAG,
             "Frame incomplete frame=%lu chunks=%u/%u bytes=%u, skip decode",
             static_cast<unsigned long>(state.frameId),
             static_cast<unsigned>(state.receivedChunks),
             static_cast<unsigned>(state.expectedChunks),
             static_cast<unsigned>(state.receivedBytes));
    resetCurrentFrame();
    return;
  }

  if (state.receivedBytes > 0 && state.receivedBytes <= MAX_JPEG_BYTES && state.rawJpegBytes != nullptr) {
    memcpy(state.rawJpegBytes, state.jpegBytes, state.receivedBytes);
    state.rawJpegSize = static_cast<uint32_t>(state.receivedBytes);
    state.rawFrameId = state.frameId;
    state.rawW = state.srcW;
    state.rawH = state.srcH;
    memcpy(state.rawSourceMac, state.sourceMac, sizeof(state.rawSourceMac));
    state.rawReady = true;
  }

  if (state.receivedBytes > 4 && state.jpegBytes[0] == 0xFF && state.jpegBytes[1] == 0xD8) {
    const bool decodeOk = decodeLatestFrameToPreview();
  } else {
    ESP_LOGW(TAG,
             "Frame invalid jpeg header frame=%lu bytes=%u, skip decode",
             static_cast<unsigned long>(state.frameId),
             static_cast<unsigned>(state.receivedBytes));
  }

  resetCurrentFrame();
}

bool getPreviewForMac(const uint8_t mac[6],
                      const uint16_t*& pixels,
                      uint16_t& width,
                      uint16_t& height,
                      uint32_t& frameId) {
  pixels = nullptr;
  width = 0;
  height = 0;
  frameId = 0;

  if (mac == nullptr || !state.previewReady || state.previewPixels == nullptr) {
    return false;
  }

  if (memcmp(mac, state.sourceMac, sizeof(state.sourceMac)) != 0) {
    return false;
  }

  pixels = state.previewPixels;
  width = PREVIEW_W;
  height = PREVIEW_H;
  frameId = state.frameId;
  return true;
}

// Return the decoded (no-downscale) image if available for the given MAC.
bool getDecodedForMac(const uint8_t mac[6],
                      const uint16_t*& pixels,
                      uint16_t& width,
                      uint16_t& height,
                      uint32_t& frameId) {
  pixels = nullptr;
  width = 0;
  height = 0;
  frameId = 0;

  if (mac == nullptr || !state.decodedReady || state.decodedPixels == nullptr) {
    return false;
  }

  if (memcmp(mac, state.sourceMac, sizeof(state.sourceMac)) != 0) {
    return false;
  }

  pixels = state.decodedPixels;
  width = state.decodedW;
  height = state.decodedH;
  frameId = state.frameId;
  return true;
}

bool getRawJpegForMac(const uint8_t mac[6],
                      const uint8_t*& jpeg,
                      uint32_t& jpegBytes,
                      uint16_t& width,
                      uint16_t& height,
                      uint32_t& frameId) {
  jpeg = nullptr;
  jpegBytes = 0;
  width = 0;
  height = 0;
  frameId = 0;

  if (mac == nullptr || !state.rawReady || state.rawJpegBytes == nullptr || state.rawJpegSize == 0) {
    return false;
  }

  if (memcmp(mac, state.rawSourceMac, sizeof(state.rawSourceMac)) != 0) {
    return false;
  }

  jpeg = state.rawJpegBytes;
  jpegBytes = state.rawJpegSize;
  width = state.rawW;
  height = state.rawH;
  frameId = state.rawFrameId;
  return true;
}

}  // namespace app::espnow::camera_stream
