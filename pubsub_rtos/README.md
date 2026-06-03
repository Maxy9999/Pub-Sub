# Pub/Sub RTOS Demo

A FreeRTOS-based simulation of an embedded IoT/video-surveillance system demonstrating **Publish/Subscribe communication**, **task isolation**, **event-driven design**, **watchdog monitoring**, **runtime configuration**, and **shared resource management**.

---

# 1. Architectural Decisions

This project was intentionally designed to mimic the architecture of a scalable embedded product where multiple software modules operate independently and communicate through events rather than direct function calls.

## 1.1 Publish/Subscribe Event Bus

### Decision

All inter-task communication is performed through a centralized Event Bus.

### Why?

Traditional approaches often create tight coupling:

```text
Sensor Task
    ↓
calls
    ↓
Cloud Task
    ↓
calls
    ↓
Logger Task
```

This quickly becomes difficult to maintain as the system grows.

Instead:

```text
Producer
    ↓
 Publish Event
    ↓
  Event Bus
    ↓
Subscribers
```

### Benefits

* Loose coupling between modules
* Easy addition/removal of features
* Better maintainability
* Improved testability
* Scales to large embedded systems

A producer never knows who consumes its data.

---

## 1.2 Per-Task Message Queues

### Decision

Each task owns its own FreeRTOS queue.

```text
Logger Queue
Cloud Queue
Storage Queue
Health Queue
...
```

### Why?

Avoids shared queue contention and prevents one slow consumer from blocking others.

### Benefits

* Task isolation
* Predictable behavior
* Reduced synchronization complexity
* Independent queue sizing

---

## 1.3 Topic-Based Routing

### Decision

Events are categorized into topics.

Examples:

```text
TOPIC_SENSOR_READING
TOPIC_TEMP_UPDATE
TOPIC_POWER_EVENT
TOPIC_FRAME_READY
TOPIC_CONFIG_UPDATE
TOPIC_HEARTBEAT
TOPIC_SYSTEM_FAULT
```

### Why?

Subscribers only receive events they care about.

### Benefits

* Reduced processing overhead
* Cleaner modular design
* Easy extensibility

---

## 1.4 Runtime Subscription Model

### Decision

Tasks subscribe to topics during initialization.

Example:

```text
Logger      → SENSOR_READING
Cloud       → FRAME_READY
Storage     → FRAME_READY
Health      → SYSTEM_FAULT
Watchdog    → HEARTBEAT
```

### Why?

Eliminates compile-time dependencies.

### Benefits

* Dynamic architecture
* Easy feature addition
* Supports plugin-like behavior

---

## 1.5 Heartbeat-Based Watchdog

### Decision

Each task periodically publishes a heartbeat.

```text
Task
  ↓
TOPIC_HEARTBEAT
  ↓
Watchdog
```

### Why?

Direct monitoring of task execution is difficult in complex RTOS systems.

### Benefits

* Detects stalled tasks
* Detects deadlocks
* Detects starvation scenarios
* Centralized health monitoring

---

## 1.6 Event-Driven Fault Management

### Decision

Watchdog does not directly reset tasks.

Instead:

```text
Watchdog
    ↓
SYSTEM_FAULT Event
    ↓
Health Manager
```

### Why?

Keeps watchdog logic simple and reusable.

### Benefits

* Separation of concerns
* Easier fault policy customization
* Better scalability

---

## 1.7 Runtime Configuration Updates

### Decision

Configuration changes are propagated through events.

```text
SET temp_threshold 38
        ↓
CONFIG_UPDATE
        ↓
ConfigManager
```

### Why?

Avoids task restarts.

### Benefits

* Dynamic reconfiguration
* Zero downtime updates
* Centralized configuration ownership

---

## 1.8 Shared Frame Descriptor Pool

### Decision

Large frame buffers are not copied between tasks.

Instead:

```text
Frame Descriptor
      +
Reference Count
```

is shared.

### Why?

Copying multi-megabyte video frames is expensive.

### Benefits

* Reduced memory usage
* Better performance
* Deterministic resource management

---

## 1.9 Reference Counting for Ownership

### Decision

Frame descriptors use reference counting.

```text
Cloud    → Release
Storage  → Release

RefCount:
2 → 1 → 0
```

### Why?

Multiple consumers may process the same frame.

### Benefits

* Prevents memory leaks
* Prevents premature frees
* Safe shared ownership

---

## 1.10 Diagnostics Through Bus Statistics

### Decision

The Event Bus maintains runtime statistics.

Examples:

```text
Published Events
Delivered Events
Dropped Events
Queue Utilization
```

### Why?

Provides observability without modifying application tasks.

### Benefits

* Runtime debugging
* Performance analysis
* Capacity planning

---

# 2. System Architecture

```text
                     +------------------+
                     |   Event Bus      |
                     +------------------+
                               |
      ---------------------------------------------------
      |         |          |         |         |         |
      v         v          v         v         v         v

 Sensor      Logger     Cloud     Storage   Health   Watchdog
 Manager

      |
      +--> SENSOR_READING
      +--> TEMP_UPDATE
      +--> FRAME_READY
      +--> HEARTBEAT

 PowerMonitor
      |
      +--> POWER_EVENT
      +--> SENSOR_READING

 Command Task
      |
      +--> COMMAND_RECEIVED
      +--> CONFIG_UPDATE
      +--> BUTTON_PRESS
      +--> OTA_EVENT

 Watchdog
      |
      +--> SYSTEM_FAULT
```

---

# 3. Task Overview

| Task            | Purpose                           |
| --------------- | --------------------------------- |
| SensorManager   | Generates sensor and frame events |
| CommandTask     | Simulates user commands           |
| PowerMonitor    | Monitors supply voltage           |
| Logger          | Records system activity           |
| CloudTask       | Processes cloud-bound data        |
| StorageTask     | Handles frame persistence         |
| ConfigManager   | Maintains runtime configuration   |
| HealthManager   | Handles system faults             |
| DiagnosticsTask | Reports bus statistics            |
| WatchdogTask    | Monitors task liveness            |

---

# 4. Runtime Dry Run

## Step 1: System Initialization

```text
Bus_Init()
WatchdogTask_Start()
AppTasks_Start()
vTaskStartScheduler()
```

The Event Bus, task queues, subscriptions, and watchdog are initialized.

---

## Step 2: SensorManager Starts

Publishes:

```text
TOPIC_TEMP_UPDATE
TOPIC_SENSOR_READING
TOPIC_HEARTBEAT
```

Example:

```text
Temperature = 25°C
Humidity = 45%
```

These events are routed to all interested subscribers.

---

## Step 3: Event Bus Fan-Out

A single event may be delivered to multiple consumers.

```text
SENSOR_READING
       ↓
    Event Bus
       ↓
 ┌─────┼─────┐
 ↓     ↓     ↓
Cloud Logger Diagnostics
```

This is the core Pub/Sub mechanism.

---

## Step 4: Watchdog Monitoring

Tasks periodically publish:

```text
TOPIC_HEARTBEAT
```

The watchdog updates:

```text
lastSeen[task]
```

to track liveness.

---

## Step 5: Power Monitoring

PowerMonitor periodically publishes:

```text
TOPIC_SENSOR_READING
TOPIC_POWER_EVENT
```

Example:

```text
Voltage = 3.7V
```

Thresholds are obtained from the runtime configuration.

---

## Step 6: Command Processing

The Command Task simulates commands such as:

```text
ACTUATOR fan 1
BUTTON 0
OTA START
SET temp_threshold 38
```

Each command generates corresponding events.

---

## Step 7: Runtime Configuration Update

When:

```text
SET temp_threshold 38
```

is received:

```text
CONFIG_UPDATE
      ↓
ConfigManager
      ↓
Configuration Updated
```

All future reads automatically use the new value.

No restart is required.

---

## Step 8: Frame Processing

SensorManager publishes:

```text
TOPIC_FRAME_READY
```

A frame descriptor is allocated from the pool.

Subscribers:

```text
CloudTask
StorageTask
```

share the descriptor using reference counting.

```text
RefCount = 2
```

After both consumers release it:

```text
RefCount = 0
```

the frame is returned to the pool.

---

## Step 9: Diagnostics Reporting

DiagnosticsTask periodically reads Event Bus statistics.

Example:

```text
Published = 200
Delivered = 195
Dropped = 5
```

and publishes:

```text
TOPIC_DIAGNOSTIC_REPORT
```

---

## Step 10: Fault Scenario

Suppose SensorManager hangs.

Heartbeats stop:

```text
TOPIC_HEARTBEAT
```

Watchdog detects timeout:

```text
STALL DETECTED
```

and publishes:

```text
TOPIC_SYSTEM_FAULT
```

HealthManager receives the event and performs recovery actions.

---

# 5. Key Learning Outcomes

This project demonstrates:

* FreeRTOS task management
* Publish/Subscribe architecture
* Event-driven embedded design
* Inter-task communication
* Watchdog monitoring
* Runtime configuration management
* Reference-counted resource ownership
* Diagnostics and observability
* Scalable embedded software architecture

---

# 6. Build & Run

```bash
make
make run
```

Expected output will show task initialization, event publications, watchdog heartbeats, command processing, diagnostics reports, and fault handling activity.

