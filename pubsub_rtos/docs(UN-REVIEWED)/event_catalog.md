# Event Catalog

| Topic | Purpose |
|---|---|
| TEMP_UPDATE | Legacy compact temperature update. |
| SENSOR_READING | Typed sensor reading payload. |
| THRESHOLD_CROSSED | Sensor value crossed runtime threshold. |
| DEVICE_STATUS | Health/config/device state update. |
| CONFIG_UPDATE | Runtime config mutation request. |
| COMMAND_RECEIVED | Parsed external command. |
| ACTUATOR_COMMAND | Command for fan/relay/actuator. |
| ACTUATOR_STATUS | Actuator acknowledgement/status. |
| STORAGE_LOG | Storage task summary. |
| NETWORK_STATUS | Link and cloud/MQTT status. |
| DIAGNOSTIC_REPORT | Queue, drop, pool, and bus metrics. |
| POWER_EVENT | Battery/supply state. |
| OTA_EVENT | Firmware update lifecycle. |
| SECURITY_EVENT | Invalid command/auth/tamper signal. |
| FRAME_READY | Zero-copy pointer to frame descriptor. |
| SYSTEM_FAULT | Watchdog or internal fault. |
| HEARTBEAT | Task liveness signal consumed by watchdog. |

Each `Event_t` carries metadata: event id, sequence, source task, priority, timestamp, topic, and fixed-size payload.
