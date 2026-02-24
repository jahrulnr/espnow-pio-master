# ESP-NOW Master (Gateway)

Firmware for the ESP-NOW master/gateway. Responsibilities include peer management, acting as an HTTP proxy for slaves, and providing a local UI/display.

Summary
-------

- Broadcast discovery beacons (`HELLO` / `HEARTBEAT`) for slave devices to detect and lock channels.
- Serve as the central ESP-NOW endpoint for `STATE` and `COMMAND` packets.
- Execute asynchronous HTTP/HTTPS proxy requests on behalf of slaves and return responses in chunked binary commands.
- Persist latest state values to LittleFS (`/data/state_latest.csv`).
- Maintain device registry (MAC → id) and temporary blacklist.

Runtime architecture
--------------------

The application entrypoint is `src/main.cpp`. It starts several FreeRTOS tasks:

- `network_task` (`src/app/tasks/networkTask.cpp`): WiFi initialization (`WifiManager`), ESP-NOW channel lock, running `espnowMaster.loop()`, and NTP sync.
- `display_task` (`src/app/tasks/displayTask.cpp`): render information from the state store to the attached display.
- `input_task` (`src/app/tasks/inputTask.cpp`): read user input (buttons/joystick/battery) and push local state updates.

Core modules
------------

- `src/app/espnow/master.cpp` — node logic, peer management, device tracking, blacklist
- `src/app/espnow/master_state_handler.cpp` — validate incoming states and convert binary → internal representation
- `src/app/espnow/master_http_proxy.cpp` — proxy worker queue and chunked response handling
- `src/core/weather_sync.cpp` — broadcast weather sync commands when necessary

Wire protocol
-------------

Binary structs are defined in `src/app/espnow/state_binary.h`.

Outbound (`PacketType::STATE`): `IdentityState`, `SensorState`, `WeatherState`, `SlaveAliveState`, `ProxyReqState`.

Inbound (`PacketType::COMMAND`): `ProxyRespChunkCommand`, `WeatherSyncReqCommand`.

Refer to `docs/espnow_device_contract.md` for full contract details (IDs, payloads, responses).

Device verification
-------------------

- A device is considered verified after sending `IdentityState` with a non-empty `id`.
- Non-proxy state updates from unverified devices are rejected.
- `ProxyReq` messages may be permitted before verification for bootstrap purposes but are logged as unverified.
- Blacklisted devices are removed from tracking and rejected until the blacklist expires.

Storage
-------

Latest values stored at: `/data/state_latest.csv` (model: `state,key,value` — upsert of latest values). This storage is not an audit log.

Configuration
-------------

Edit `include/app_config.h` to configure WiFi and runtime parameters:

- `WIFI_SSID`, `WIFI_PASS`
- `DEVICE_NAME`, `WIFI_HOSTNAME`
- `MASTER_BLACKLIST_DURATION_MS`, `MASTER_WEATHER_STALE_MS`, `MASTER_WEATHER_SYNC_RETRY_MS`

Build & flash
-------------

Example commands using PlatformIO:

```bash
platformio run -e esp32-s3-devkitc1-n16r8
platformio run -e esp32-s3-devkitc1-n16r8 -t upload --upload-port /dev/ttyACM0
platformio device monitor -e esp32-s3-devkitc1-n16r8 --port /dev/ttyACM0
```

Operational notes
-----------------

- WiFi station (STA) and ESP-NOW operate on the same radio channel.
- The proxy worker is asynchronous and queue-based; requests may be deferred while the proxy is busy.
- TLS proxy currently uses `setInsecure()` (no certificate verification).

Related repositories
--------------------

- ESP-NOW Master (this repo): https://github.com/jahrulnr/espnow-pio-master.git
- ESP-NOW Weather (slave/proxy client): https://github.com/jahrulnr/espnow-pio-weather.git
- ESP-NOW Cam (camera slave): https://github.com/jahrulnr/espnow-pio-camera.git