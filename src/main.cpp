#include <Arduino.h>
#include <core/wdt.h>
#include "core/nvs.h"
#include <LittleFS.h>
#include <nvs_flash.h>
#include <User_Setups/Setup24_ST7789_ESP32.h>
#include "app/tasks/displayTask.h"
#include "app/tasks/inputTask.h"
#include "app/tasks/networkTask.h"

void init(){
	esp_panic_handler_disable_timg_wdts();
	nvs_init();
	pinMode(TFT_BL, OUTPUT);
}

void setup() {
	LittleFS.begin(true);

	#if BOARD_HAS_PSRAM
	heap_caps_malloc_extmem_enable(0);
	#endif

	if (!app::tasks::startNetworkTask()) {
		ESP_LOGE("MAIN", "Network task failed to start");
	}

	if (!app::tasks::startDisplayTask()) {
		ESP_LOGE("MAIN", "Display task failed to start");
	}

	if (!app::tasks::startInputTask()) {
		ESP_LOGE("MAIN", "Input task failed to start");
	}
}

void loop() {
	vTaskDelete(nullptr);
}