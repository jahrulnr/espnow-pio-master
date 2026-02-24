# ESP-NOW Master (Gateway)

Firmware master/gateway untuk jaringan ESP-NOW + internet proxy + UI display.

## Ringkasan Peran

Master bertugas untuk:
- Broadcast beacon `HELLO` dan `HEARTBEAT` agar slave bisa lock channel.
- Menjadi endpoint ESP-NOW utama untuk paket `STATE`/`COMMAND`.
- Menjalankan HTTP/HTTPS proxy asynchronous untuk request dari slave.
- Mengirim balik hasil proxy sebagai command biner `ProxyRespChunk` (chunked).
- Menyimpan nilai state terbaru ke LittleFS (`/data/state_latest.csv`).
- Menjaga daftar device aktif + identitas (`MAC -> id`) dan blacklist sementara.
- Broadcast status internet (`MasterNetState`) dan trigger sinkronisasi cuaca saat data stale.

## Arsitektur Runtime

Entrypoint `src/main.cpp` menjalankan 3 task utama:
- `network_task` (`src/app/tasks/networkTask.cpp`)
	- Inisialisasi WiFi (`WifiManager`), lock channel ESP-NOW, jalankan `espnowMaster.loop()`, update NTP.
- `display_task` (`src/app/tasks/displayTask.cpp`)
	- Menampilkan data dari state store melalui display interface.
- `input_task` (`src/app/tasks/inputTask.cpp`)
	- Membaca tombol/joystick/battery lalu push state lokal ke display/state store.

Modul inti ESP-NOW:
- `src/app/espnow/master.cpp` (node, peer management, device tracking, blacklist)
- `src/app/espnow/master_state_handler.cpp` (validasi state + konversi biner -> text internal)
- `src/app/espnow/master_http_proxy.cpp` (queue worker proxy + chunk response)
- `src/core/weather_sync.cpp` (broadcast perintah sync cuaca saat data belum ada/stale)

## Protokol Saat Ini (Biner)

`PacketType::STATE` membawa struct biner (`src/app/espnow/state_binary.h`), bukan payload text mentah dari slave.

Dokumen kontrak lengkap (ID/name, payload, response, command matrix):
- `docs/espnow_device_contract.md`

State penting dari slave:
- `IdentityState`
- `SensorState`
- `ProxyReqState`
- `WeatherState`
- `SlaveAliveState`

Command penting dari master:
- `ProxyRespChunkCommand` (hasil proxy dipecah chunk 160 byte)
- `WeatherSyncReqCommand` (paksa slave kirim ulang request cuaca)

## Aturan Verifikasi Device

- Device dianggap verified setelah mengirim `IdentityState` (`id` non-empty).
- State non-proxy dari device yang belum verified akan ditolak.
- `ProxyReq` boleh lewat sebelum verified (untuk bootstrap), tapi tetap dilog sebagai unverified.
- Device yang diblacklist dihapus dari tracked list dan ditolak sampai durasi selesai.

## Penyimpanan State

Master menyimpan latest-value store di:
- `/data/state_latest.csv`

Karakteristik:
- Model data `state,key,value` (upsert terbaru).
- Bukan audit log historis.
- Field failure tertentu dapat di-skip sesuai logic parser.

## Konfigurasi Utama

Edit `include/app_config.h`:
- Koneksi WiFi: `WIFI_SSID`, `WIFI_PASS`
- Identitas: `DEVICE_NAME`, `WIFI_HOSTNAME`
- Kontrol sync/keamanan: `MASTER_BLACKLIST_DURATION_MS`, `MASTER_WEATHER_STALE_MS`, `MASTER_WEATHER_SYNC_RETRY_MS`

## Build & Flash

```bash
platformio run -e esp32-s3-devkitc1-n16r8
platformio run -e esp32-s3-devkitc1-n16r8 -t upload --upload-port /dev/ttyACM0
platformio device monitor -e esp32-s3-devkitc1-n16r8 --port /dev/ttyACM0
```

## Catatan Operasional

- WiFi STA dan ESP-NOW berjalan bersamaan di channel yang sama.
- Proxy worker bersifat async + queue; request baru di-skip saat proxy busy.
- TLS proxy saat ini memakai `setInsecure()` (tanpa verifikasi sertifikat penuh).
