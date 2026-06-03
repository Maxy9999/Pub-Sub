# Porting Guide

## Storage

Replace `src/storage_backend_posix.c` with a flash/NVS implementation that provides:

- `StorageBackend_ReadConfig`
- `StorageBackend_WriteConfig`
- `StorageBackend_AppendLog`

## Frame Payloads

`FRAME_READY` events carry an `EventPayload_t *` from the fixed payload pool. On hardware, set `framePtr` to a DMA buffer and keep `frameSize` as the actual byte count.

## Interrupts

For GPIO or DMA ISR publishers, use `Bus_PublishFromISR`. Runtime subscriptions should remain frozen after startup.

## Networking

Replace the simulated `CloudTask` print path with MQTT/HTTP publish using the JSON string from `Telemetry_EncodeJson`.
