#include "ui_weather_icon.h"

#include "ui_common.h"

#include <LittleFS.h>
#include <PNGdec.h>
#include <esp_log.h>

namespace app::display::ui_component {
namespace {

static constexpr const char* TAG = "display_if";
static constexpr const char* WEATHER_ICON_BASE = "/assets/weather-icons-v2-png/";
static constexpr int WEATHER_ICON_SIZE = 32;
static constexpr int PNG_MAX_DYNAMIC_LINE_PIXELS = 1024;
static constexpr size_t WEATHER_ICON_PIXELS = WEATHER_ICON_SIZE * WEATHER_ICON_SIZE;
static constexpr size_t WEATHER_ICON_BYTES = WEATHER_ICON_PIXELS * sizeof(uint16_t);

PNG pngDecoder;

struct PngDecodeContext {
  uint16_t* outPixels;
  uint16_t* lineBuffer;
  int lineBufferPixels;
  uint32_t alphaFill;
  int width;
  int height;
  bool ok;
};

PngDecodeContext* activePngDecode = nullptr;

uint32_t rgb565ToRgb888(uint16_t color565) {
  const uint8_t r5 = (color565 >> 11) & 0x1F;
  const uint8_t g6 = (color565 >> 5) & 0x3F;
  const uint8_t b5 = color565 & 0x1F;
  const uint8_t r8 = (r5 << 3) | (r5 >> 2);
  const uint8_t g8 = (g6 << 2) | (g6 >> 4);
  const uint8_t b8 = (b5 << 3) | (b5 >> 2);
  return (static_cast<uint32_t>(b8) << 16) | (static_cast<uint32_t>(g8) << 8) | r8;
}

uint32_t colorTileBlueRgb888() {
  return rgb565ToRgb888(colorTileBlue());
}

void* pngOpen(const char* fileName, int32_t* size) {
  if (fileName == nullptr || size == nullptr) {
    return nullptr;
  }

  fs::File* file = new fs::File(LittleFS.open(fileName, "r"));
  if (!(*file)) {
    delete file;
    return nullptr;
  }

  *size = static_cast<int32_t>(file->size());
  return file;
}

void pngClose(void* handle) {
  auto* file = static_cast<fs::File*>(handle);
  if (file == nullptr) {
    return;
  }

  file->close();
  delete file;
}

int32_t pngRead(PNGFILE* pngFile, uint8_t* buffer, int32_t length) {
  if (pngFile == nullptr || pngFile->fHandle == nullptr) {
    return 0;
  }

  auto* file = static_cast<fs::File*>(pngFile->fHandle);
  return static_cast<int32_t>(file->read(buffer, static_cast<size_t>(length)));
}

int32_t pngSeek(PNGFILE* pngFile, int32_t position) {
  if (pngFile == nullptr || pngFile->fHandle == nullptr) {
    return 0;
  }

  auto* file = static_cast<fs::File*>(pngFile->fHandle);
  return file->seek(static_cast<size_t>(position), fs::SeekSet) ? position : -1;
}

int pngDraw(PNGDRAW* draw) {
  if (draw == nullptr || activePngDecode == nullptr || activePngDecode->outPixels == nullptr) {
    return 0;
  }

  if (activePngDecode->lineBuffer == nullptr || activePngDecode->lineBufferPixels <= 0) {
    activePngDecode->ok = false;
    return 0;
  }

  if (draw->iWidth <= 0 || draw->iWidth > activePngDecode->lineBufferPixels) {
    activePngDecode->ok = false;
    return 0;
  }

  if (draw->y < 0 || draw->y >= activePngDecode->height) {
    return 1;
  }

  pngDecoder.getLineAsRGB565(draw, activePngDecode->lineBuffer, PNG_RGB565_BIG_ENDIAN, activePngDecode->alphaFill);

  const int copyWidth = (draw->iWidth < activePngDecode->width) ? draw->iWidth : activePngDecode->width;
  uint16_t* dst = activePngDecode->outPixels + (draw->y * activePngDecode->width);
  for (int index = 0; index < copyWidth; ++index) {
    dst[index] = activePngDecode->lineBuffer[index];
  }

  return 1;
}

const char* weatherCodeToIconFile(int code) {
  switch (code) {
    case 0: return "sunny.png";
    case 1: return "mostly_sunny.png";
    case 2: return "partly_cloudy.png";
    case 3: return "cloudy.png";
    case 45:
    case 48: return "haze_fog_dust_smoke.png";
    case 51:
    case 53: return "drizzle.png";
    case 55: return "showers_rain.png";
    case 56:
    case 57: return "wintry_mix_rain_snow.png";
    case 61:
    case 63:
    case 80:
    case 81: return "showers_rain.png";
    case 65:
    case 82: return "heavy_rain.png";
    case 66:
    case 67: return "wintry_mix_rain_snow.png";
    case 71: return "flurries.png";
    case 73:
    case 77:
    case 85: return "snow_showers_snow.png";
    case 75:
    case 86: return "heavy_snow.png";
    case 95: return "strong_tstorms.png";
    case 96:
    case 99: return "sleet_hail.png";
    default: return "cloudy.png";
  }
}

bool loadWeatherIconPixels(const char* fileName, uint16_t* outPixels) {
  if (fileName == nullptr || outPixels == nullptr) {
    return false;
  }

  String path = WEATHER_ICON_BASE;
  path += fileName;

  const uint16_t iconBgColor = colorTileBlue();
  for (size_t i = 0; i < WEATHER_ICON_PIXELS; ++i) {
    outPixels[i] = iconBgColor;
  }

  PngDecodeContext context = {
      .outPixels = outPixels,
      .lineBuffer = nullptr,
      .lineBufferPixels = 0,
      .alphaFill = colorTileBlueRgb888(),
      .width = WEATHER_ICON_SIZE,
      .height = WEATHER_ICON_SIZE,
      .ok = true,
  };

  const int16_t openResult = pngDecoder.open(path.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  if (openResult != PNG_SUCCESS) {
    ESP_LOGW(TAG, "Icon PNG open failed: %s (rc=%d)", path.c_str(), openResult);
    return false;
  }

  const int pngWidth = pngDecoder.getWidth();
  if (pngWidth <= 0 || pngWidth > PNG_MAX_DYNAMIC_LINE_PIXELS) {
    pngDecoder.close();
    ESP_LOGW(TAG, "Icon PNG width unsupported: %s (w=%d)", path.c_str(), pngWidth);
    return false;
  }

  context.lineBuffer = static_cast<uint16_t*>(calloc(static_cast<size_t>(pngWidth), sizeof(uint16_t)));
  if (context.lineBuffer == nullptr) {
    pngDecoder.close();
    ESP_LOGW(TAG, "Icon PNG line buffer alloc failed: %s", path.c_str());
    return false;
  }
  context.lineBufferPixels = pngWidth;

  activePngDecode = &context;

  const int16_t decodeResult = pngDecoder.decode(nullptr, 0);
  pngDecoder.close();
  activePngDecode = nullptr;
  free(context.lineBuffer);
  context.lineBuffer = nullptr;

  if (decodeResult != PNG_SUCCESS) {
    ESP_LOGW(TAG, "Icon PNG decode failed: %s (rc=%d)", path.c_str(), decodeResult);
    return false;
  }

  if (!context.ok) {
    ESP_LOGW(TAG, "Icon PNG draw callback failed: %s", path.c_str());
    return false;
  }

  return true;
}

}  // namespace

size_t weatherIconBytes() {
  return WEATHER_ICON_BYTES;
}

bool ensureWeatherIconLoaded(DisplayStateData& state) {
  if (state.weatherIconPixels == nullptr) {
    return false;
  }

  if (state.loadedWeatherCode == state.weatherCode) {
    return state.weatherIconLoaded;
  }

  state.loadedWeatherCode = state.weatherCode;
  state.weatherIconLoaded = loadWeatherIconPixels(weatherCodeToIconFile(state.weatherCode), state.weatherIconPixels);
  return state.weatherIconLoaded;
}

}  // namespace app::display::ui_component
