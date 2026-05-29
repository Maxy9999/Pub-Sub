# Architecture

This project models an industrial condition-monitoring edge node built on FreeRTOS.
Application tasks communicate through a topic-based event bus instead of direct task-to-task calls.

## Core Components

- Event bus: non-blocking fanout, per-topic locks, event metadata, and per-topic metrics.
- Payload pool: fixed descriptor pool for zero-copy frame/large-payload events.
- Runtime config: versioned, CRC32-protected configuration stored through a backend abstraction.
- Fault manager: fault classification, debounce, health-state mapping, and bounded fault history.
- Telemetry encoder: converts internal events into cloud-ready JSON records.
- Command parser: parses text commands into structured command/config events.

## Application Model

Sensor, power, network, command, diagnostics, storage, cloud, config, health, and logger tasks form a realistic embedded workflow. Sensor data and frame-ready notifications flow through the bus; storage and cloud consumers react independently.
