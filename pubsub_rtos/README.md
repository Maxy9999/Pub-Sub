# FreeRTOS Pub/Sub Event Bus — POSIX Simulator

A lightweight **publish-subscribe inter-task communication bus** built on
FreeRTOS, running on the POSIX Linux simulator. Designed to run identically
on a Raspberry Pi Pico with minimal porting effort.

## Architecture

```
┌─────────────┐  publish(TEMP_UPDATE)   ┌──────────────────────────┐
│ SensorTask  │ ─────────────────────►  │                          │
└─────────────┘                         │       Event Bus          │
                                        │    (Central Broker)      │
┌─────────────┐  publish(BTN_PRESS)     │                          │
│ ButtonTask  │ ─────────────────────►  │  - Topic registry        │
└─────────────┘                         │  - Subscriber queues     │
                                        │  - Mutex-protected       │
┌─────────────┐  subscribe(TEMP)        │  - Non-blocking fanout   │
│ DisplayTask │ ◄─────────────────────  │                          │
└─────────────┘                         └──────────────────────────┘
┌─────────────┐  subscribe(ALL)
│ LoggerTask  │ ◄─────────────────────
└─────────────┘
┌─────────────┐  subscribe(TEMP+FAULT)
│  AlertTask  │ ◄─────────────────────
└─────────────┘
┌─────────────┐  subscribe(HEARTBEAT)
│  Watchdog   │ ◄─────────────────────
└─────────────┘
```

## Tasks

| Task        | Priority | Role |
|-------------|----------|------|
| Watchdog    | Highest  | Monitors all task heartbeats, publishes SYSTEM_FAULT on stall |
| Button      | 4        | Simulates GPIO button press every 3s (stub — replace with ISR on Pi) |
| Sensor      | 3        | Publishes temperature update every 500ms |
| Alert       | 3        | Fires alert if temp > 35°C or SYSTEM_FAULT received |
| Display     | 2        | Renders current temperature |
| Logger      | 2        | Logs all non-heartbeat events with timestamps |
| Stats       | 1        | Prints queue depths and drop counts every 5s |

## Key Design Decisions

- **Fixed-size event struct**: `Event_t` is always the same size, making it
  safe to copy through FreeRTOS queues without dynamic allocation.
- **Non-blocking publish**: `xQueueSend(..., 0)` — a slow subscriber never
  stalls a publisher. Dropped events are counted and logged.
- **No mutex in ISR path**: `Bus_PublishFromISR` skips the mutex since
  subscriptions are frozen before the scheduler starts.
- **Heartbeat watchdog**: Each task sends a heartbeat event periodically. The
  watchdog detects stalls and publishes a `SYSTEM_FAULT` event.

## Quick Start

### Prerequisites

```bash
sudo apt install gcc make git
```

### Clone & Build

```bash
# Clone FreeRTOS (if not already present)
git clone --depth=1 https://github.com/FreeRTOS/FreeRTOS.git ../FreeRTOS
cd ../FreeRTOS && git submodule update --init FreeRTOS/Source && cd -

# Build and run
make run
```

### Controls

- `Ctrl+C` — exit

## Porting to Raspberry Pi Pico

1. Replace `FreeRTOS/Source/portable/ThirdParty/GCC/Posix` with
   `FreeRTOS/Source/portable/ThirdParty/GCC/RP2040` in your CMakeLists.
2. In `app_tasks.c`, replace `prvButtonTask`'s `vTaskDelay` loop with a
   GPIO interrupt handler that calls `Bus_PublishFromISR`.
3. Replace `printf` in `DisplayTask` with your LCD/OLED driver calls.
4. That's it — `event_bus.c`, `watchdog_task.c`, and all task logic are
   portable as-is.

## Project Structure

```
pubsub_rtos/
├── include/
│   ├── event_bus.h       # Pub/sub API & event types
│   ├── watchdog_task.h   # Watchdog task interface
│   └── app_tasks.h       # Application task launcher
├── src/
│   ├── main.c            # Entry point + FreeRTOS hooks
│   ├── event_bus.c       # Broker implementation
│   ├── watchdog_task.c   # Watchdog implementation
│   └── app_tasks.c       # All 6 application tasks
├── freertos_config/
│   └── FreeRTOSConfig.h  # FreeRTOS configuration
└── Makefile
```

## Resume Bullet

> Designed and implemented a publish-subscribe event bus on FreeRTOS,
> enabling decoupled inter-task communication across 6 concurrent tasks.
> Built a central broker using fixed-size queues and a mutex-protected topic
> registry; implemented a watchdog task for runtime stall detection and fault
> propagation. Validated on POSIX simulator; architected for direct
> deployment to Raspberry Pi Pico with minimal porting effort.
