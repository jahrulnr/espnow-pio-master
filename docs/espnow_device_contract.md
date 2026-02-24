# ESP-NOW Device Contract (Master)

Dokumen ini mendefinisikan kontrak komunikasi antara `espnow-master` dan slave (`espnow-weather`, `espnow-cam`).

## Scope

- Transport: ESP-NOW frame (`PacketType`)
- Payload utama: biner (`src/app/espnow/state_binary.h`)
- Target: sinkron format antar device, parser master, dan UI control

## Versi Kontrak

| Item | Nilai |
|---|---|
| Binary magic | `0xB1` |
| Binary version | `1` |
| Features contract version | `1` |
| Frame protocol version (`PacketHeader.version`) | `1` |

## Device Identity & Feature Advertisement

Slave wajib mengirim identitas + fitur setelah lock ke master.

| Field | Source | Tipe | Wajib | Keterangan |
|---|---|---|---|---|
| `id` | `IdentityState.id` | char[24] | Ya | Device ID/name unik, contoh `weather-lolin32-a` / `cam-esp32cam-01` |
| `featureBits` | `FeaturesState.featureBits` | `uint32_t` bitmask | Ya | Kapabilitas device |
| `contractVersion` | `FeaturesState.contractVersion` | `uint16_t` | Ya | Saat ini `1` |

### Feature Bitmask

| Bit | Constant | Arti |
|---|---|---|
| 0 | `FeatureIdentity` | Mendukung identity advertisement |
| 1 | `FeatureSensor` | Mengirim data sensor |
| 2 | `FeatureWeather` | Mengirim weather state |
| 3 | `FeatureProxyClient` | Bisa kirim proxy request ke master |
| 4 | `FeatureCameraJpeg` | Mengirim frame JPEG |
| 5 | `FeatureCameraStream` | Mendukung mode streaming camera |
| 6 | `FeatureControlBasic` | Mendukung command kontrol dasar |

## Device Type Mapping (Master)

Master mengklasifikasi device berdasarkan fitur (prioritas) lalu fallback ke heuristic nama.

| Device Kind | Rule (priority) | Fallback ID/Name heuristic |
|---|---|---|
| `Camera` | `FeatureCameraStream` atau `FeatureCameraJpeg` | id mengandung `cam` atau `camera` |
| `Weather` | `FeatureWeather` atau `FeatureSensor` | id mengandung `weather` atau `slave` |
| `Unknown` | Tidak match rule | Tidak match heuristic |

## Payload Contract: Slave -> Master

| `PacketType` | Binary Type | Device sumber | Payload fields penting | Response master |
|---|---|---|---|---|
| `HELLO` | n/a (beacon text) | Semua slave | beacon lock/ack | Master track peer + add peer |
| `STATE` | `IdentityState` | Semua slave | `id` | Device di-mark verified dan identity diupdate |
| `STATE` | `FeaturesState` | Semua slave | `featureBits`, `contractVersion` | Device profile (kind/status) di-refresh |
| `STATE` | `SensorState` | Weather | `temperature10`, `humidity10` | State store + UI update |
| `STATE` | `WeatherState` | Weather | `ok`, `code`, `time`, `temperature10`, `windspeed10`, `winddirection` | State store + UI weather update |
| `STATE` | `ProxyReqState` | Weather (proxy client) | `method`, `url` | Master enqueue HTTP proxy worker |
| `STATE` | `SlaveAliveState` | Semua slave | keepalive marker | Heartbeat health update |
| `STATE` | `CameraMetaState` | Camera | `frameId`, `totalBytes`, `totalChunks`, `width`, `height`, `format`, `quality` | Tracked device status camera diupdate |
| `STATE` | `CameraChunkState` | Camera | `frameId`, `idx`, `total`, `dataLen`, `data[]` | Saat ini diproses minimal (anti flood), belum render image di UI |

## Payload Contract: Master -> Slave

| `PacketType` | Binary Type | Target device | Trigger | Ekspektasi response |
|---|---|---|---|---|
| `HELLO` | beacon text `PIO_MASTER_V1` | Semua slave | Periodik broadcast | Slave lock channel + register master |
| `HEARTBEAT` | beacon text `PIO_MASTER_V1` | Semua slave | Periodik broadcast | Slave kirim `SlaveAliveState` |
| `STATE` | `MasterNetState` | Semua slave | Periodik saat ada device verified | Opsional: slave log status internet/channel master |
| `COMMAND` | `ProxyRespChunkCommand` | Weather (proxy client) | Saat proxy HTTP selesai | Slave reassemble chunk -> proses weather pipeline |
| `COMMAND` | `WeatherSyncReqCommand` | Weather | Trigger stale weather sync | Slave kirim `ProxyReqState` baru |
| `COMMAND` | `CameraControlCommand` | Camera | UI control di screen `EspNowControl` | `CaptureOnce` => kirim `CameraMeta+Chunk`; `SetStreaming` => on/off stream |

## Command Contract Detail

### Weather Command

| Command type | Field | Nilai | Efek di slave |
|---|---|---|---|
| `WeatherSyncReqCommand` | `force` | `1` | Paksa kirim proxy request cuaca segera |
| `WeatherSyncReqCommand` | `force` | `0` | Refresh biasa (best effort) |

### Camera Command

| Command type | Field | Nilai | Efek di slave |
|---|---|---|---|
| `CameraControlCommand` | `action` | `CaptureOnce (1)` | Menandai capture satu frame segera |
| `CameraControlCommand` | `action` + `value` | `SetStreaming (2)` + `1` | Mengaktifkan stream periodik |
| `CameraControlCommand` | `action` + `value` | `SetStreaming (2)` + `0` | Menonaktifkan stream periodik |

## Validation Rules (Master)

| Rule | Status |
|---|---|
| Device dianggap verified setelah `IdentityState.id` non-empty | Aktif |
| State non-proxy dari unverified device ditolak | Aktif |
| `proxy_req` dan `features` boleh lewat sebelum verified (bootstrap) | Aktif |
| Device blacklisted di-drop dari tracked list sementara | Aktif |

## Unknown / Forward Compatibility

| Kasus | Perilaku master saat ini |
|---|---|
| `featureBits` tidak dikenal | Tetap simpan bits mentah, klasifikasi fallback ke rule yang dikenal |
| `Type` biner tidak dikenal | Di-ignore dan dilog warning |
| `contractVersion` lebih baru | Diterima, namun field yang tidak dikenal diabaikan |

## Contoh Device ID/Name

| Jenis | Contoh `id` yang disarankan |
|---|---|
| Weather node | `weather-lolin32-a` |
| Camera node | `cam-esp32cam-01` |
| Node generic | `node-<board>-<index>` |
