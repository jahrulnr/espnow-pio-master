#include "master_http_proxy.h"

#include "master.h"
#include "payload_codec.h"
#include "state_binary.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstring>

namespace app::espnow {

namespace {

static constexpr const char* TAG = "http_proxy";
static constexpr uint32_t CACHE_TTL_MS = 3600000;
static constexpr uint8_t MAX_PROXY_QUEUE = 8;
static constexpr uint32_t WIFI_WAIT_TIMEOUT_MS = 30000;
static constexpr uint32_t WIFI_WAIT_STEP_MS = 500;
static constexpr uint16_t PROXY_STACK_SIZE = 8192;
static constexpr UBaseType_t PROXY_TASK_PRIORITY = 2;
static constexpr BaseType_t PROXY_TASK_CORE = 0;
static constexpr size_t MAX_PROXY_RESPONSE_TEXT = 1024;

static uint16_t nextProxyRequestId = 1;

String cachedRequest;
String cachedResponse;
uint32_t cachedAtMs = 0;

struct ProxyRequestItem {
  uint8_t mac[6] = {0};
  char request[MAX_PAYLOAD_SIZE + 1] = {0};
};

struct ProxyResponseItem {
  uint8_t mac[6] = {0};
  char response[MAX_PROXY_RESPONSE_TEXT + 1] = {0};
};

QueueHandle_t requestQueue = nullptr;
QueueHandle_t responseQueue = nullptr;
TaskHandle_t proxyTaskHandle = nullptr;
volatile bool proxyBusy = false;

void setProxyBusy(bool busy) {
  proxyBusy = busy;
}

bool isProxyRequest(const String& request) {
  String stateName;
  return app::espnow::codec::getField(request, "state", stateName) && stateName == "proxy_req";
}

String trimResponseBody(String body) {
  body.replace('\n', ' ');
  body.replace('\r', ' ');
  while (body.indexOf("  ") >= 0) {
    body.replace("  ", " ");
  }

  return body;
}

String buildResponse(bool ok, int code, const String& bodyOrErr) {
  return app::espnow::codec::buildPayload({
      {"state", "proxy_res"},
      {"ok", ok ? String("1") : String("0")},
      {"code", String(code)},
      {"data", bodyOrErr},
  });
}

bool isAllowedMethod(const String& method) {
  return method == "GET" || method == "POST" || method == "PATCH";
}

void parseResponsePayload(const String& responsePayload, uint8_t& okOut, int16_t& codeOut, String& dataOut) {
  okOut = 0;
  codeOut = -1;
  dataOut = "";

  String ok;
  String code;
  app::espnow::codec::getField(responsePayload, "ok", ok);
  app::espnow::codec::getField(responsePayload, "code", code);
  app::espnow::codec::getField(responsePayload, "data", dataOut);

  okOut = static_cast<uint8_t>(ok == "1" || ok == "true" || ok == "ok");
  codeOut = static_cast<int16_t>(code.toInt());
}

bool waitForWifiConnected() {
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start >= WIFI_WAIT_TIMEOUT_MS) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(WIFI_WAIT_STEP_MS));
  }
  return true;
}

void proxyWorkerTask(void*) {
  ProxyRequestItem requestItem;

  while (true) {
    if (xQueueReceive(requestQueue, &requestItem, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    setProxyBusy(true);
    String response;
    if (!waitForWifiConnected()) {
      response = buildResponse(false, -10, "wifi_offline_timeout");
    } else if (!tryHandleProxyRequest(requestItem.request, response)) {
      response = buildResponse(false, -11, "invalid_proxy_request");
    }

    if (response.isEmpty()) {
      setProxyBusy(false);
      continue;
    }

    ProxyResponseItem responseItem;
    memcpy(responseItem.mac, requestItem.mac, 6);
    strncpy(responseItem.response, response.c_str(), MAX_PROXY_RESPONSE_TEXT);
    responseItem.response[MAX_PROXY_RESPONSE_TEXT] = '\0';

    if (xQueueSend(responseQueue, &responseItem, 0) != pdTRUE) {
      ESP_LOGW(TAG, "Response queue full, dropping proxy response");
      setProxyBusy(false);
    }
  }
}

}  // namespace

bool beginProxyWorker() {
  if (requestQueue != nullptr && responseQueue != nullptr && proxyTaskHandle != nullptr) {
    return true;
  }

  requestQueue = xQueueCreate(MAX_PROXY_QUEUE, sizeof(ProxyRequestItem));
  responseQueue = xQueueCreate(MAX_PROXY_QUEUE, sizeof(ProxyResponseItem));
  if (requestQueue == nullptr || responseQueue == nullptr) {
    ESP_LOGE(TAG, "Failed to create proxy queues");
    return false;
  }

  BaseType_t created = xTaskCreatePinnedToCore(
      proxyWorkerTask,
      "proxy_worker",
      PROXY_STACK_SIZE,
      nullptr,
      PROXY_TASK_PRIORITY,
      &proxyTaskHandle,
      PROXY_TASK_CORE);

  if (created != pdPASS) {
    ESP_LOGE(TAG, "Failed to create proxy worker task");
    return false;
  }

  ESP_LOGI(TAG, "Proxy worker started on core %d", PROXY_TASK_CORE);
  return true;
}

bool tryHandleProxyRequest(const char* requestText, String& responseOut) {
  responseOut = "";
  if (requestText == nullptr) {
    return false;
  }

  const String request = String(requestText);
  String method;
  String url;
  String payload;

  if (!isProxyRequest(request)) {
    return false;
  }

  if (!app::espnow::codec::getField(request, "method", method) ||
      !app::espnow::codec::getField(request, "url", url)) {
    return false;
  }
  if (!app::espnow::codec::getField(request, "payload", payload)) {
    payload = "{}";
  }

  method.toUpperCase();
  if (!isAllowedMethod(method)) {
    responseOut = buildResponse(false, -1, "invalid_method");
    return true;
  }

  if (cachedRequest == request && !cachedResponse.isEmpty() && (millis() - cachedAtMs) < CACHE_TTL_MS) {
    responseOut = cachedResponse;
    return true;
  }

  const bool isHttps = url.startsWith("https://");
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  bool beginOk = false;
  if (isHttps) {
    beginOk = http.begin(secureClient, url);
  } else {
    beginOk = http.begin(plainClient, url);
  }

  if (!beginOk) {
    responseOut = buildResponse(false, -2, "http_begin_failed");
    return true;
  }

  http.setTimeout(7000);
  if (method != "GET") {
    http.addHeader("Content-Type", "application/json");
  }

  int code = -3;
  if (method == "GET") {
    code = http.GET();
  } else if (method == "POST") {
    code = http.POST(payload);
  } else if (method == "PATCH") {
    code = http.sendRequest("PATCH", payload);
  }

  if (code <= 0) {
    http.end();
    responseOut = buildResponse(false, code, "http_error");
    return true;
  }

  const String body = trimResponseBody(http.getString());
  http.end();

  responseOut = buildResponse(true, code, body);
  cachedRequest = request;
  cachedResponse = responseOut;
  cachedAtMs = millis();

  ESP_LOGI(TAG, "Proxy success method=%s code=%d", method.c_str(), code);
  return true;
}

bool enqueueProxyRequest(const uint8_t mac[6], const char* requestText) {
  if (requestText == nullptr || mac == nullptr) {
    return false;
  }

  if (!beginProxyWorker()) {
    return false;
  }

  const String request = String(requestText);
  if (!isProxyRequest(request)) {
    return false;
  }

  if (proxyBusy) {
    ESP_LOGW(TAG, "Proxy busy, skipping new request");
    return true;
  }

  ProxyRequestItem item;
  memcpy(item.mac, mac, 6);
  strncpy(item.request, request.c_str(), MAX_PAYLOAD_SIZE);
  item.request[MAX_PAYLOAD_SIZE] = '\0';

  if (xQueueSend(requestQueue, &item, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Proxy queue full, dropping request");
    return true;
  }

  setProxyBusy(true);

  ESP_LOGI(TAG, "Queued proxy request");
  return true;
}

void processProxyResponses(MasterNode& master) {
  if (responseQueue == nullptr) {
    return;
  }

  ProxyResponseItem item;
  while (xQueueReceive(responseQueue, &item, 0) == pdTRUE) {
    const String response = String(item.response);

    uint8_t ok = 0;
    int16_t code = -1;
    String responseBody;
    parseResponsePayload(response, ok, code, responseBody);

    const size_t chunkSize = app::espnow::state_binary::kProxyChunkDataBytes;
    const size_t totalChunks = responseBody.isEmpty()
                                   ? 1
                                   : (responseBody.length() + chunkSize - 1) / chunkSize;
    const uint16_t requestId = nextProxyRequestId++;

    ESP_LOGI(TAG,
             "Sending proxy response as binary chunks id=%u chunks=%u",
             requestId,
             static_cast<unsigned>(totalChunks));

    for (size_t index = 0; index < totalChunks; ++index) {
      app::espnow::state_binary::ProxyRespChunkCommand command = {};
      app::espnow::state_binary::initHeader(command.header, app::espnow::state_binary::Type::ProxyRespChunk);
      command.requestId = requestId;
      command.idx = static_cast<uint16_t>(index + 1);
      command.total = static_cast<uint16_t>(totalChunks);
      command.ok = ok;
      command.code = code;

      if (!responseBody.isEmpty()) {
        const size_t offset = index * chunkSize;
        const String chunkData = responseBody.substring(offset, offset + chunkSize);
        command.dataLen = static_cast<uint8_t>(chunkData.length());
        memcpy(command.data, chunkData.c_str(), command.dataLen);
      } else {
        command.dataLen = 0;
      }

      master.send(item.mac,
                  PacketType::COMMAND,
                  &command,
                  sizeof(command));
      vTaskDelay(pdMS_TO_TICKS(12));
    }

    setProxyBusy(false);
  }
}

bool isProxyBusy() {
  return proxyBusy;
}

}  // namespace app::espnow
