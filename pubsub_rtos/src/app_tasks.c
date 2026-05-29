#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "event_bus.h"
#include "watchdog_task.h"
#include "app_tasks.h"
#include "payload_pool.h"
#include "runtime_config.h"
#include "event_types.h"
#include "command_parser.h"
#include "telemetry_encoder.h"
#include "fault_manager.h"
#include "storage_backend.h"

#define TASK_QUEUE_DEPTH        12
#define FRAME_BYTES             (1920UL * 1080UL * 2UL)

static QueueHandle_t s_loggerQueue;
static QueueHandle_t s_storageQueue;
static QueueHandle_t s_cloudQueue;
static QueueHandle_t s_configQueue;
static QueueHandle_t s_healthQueue;

static uint8_t s_dmaFrameA[64];
static uint8_t s_dmaFrameB[64];

static const char *topicToString(EventTopic_t topic)
{
    return Telemetry_TopicName(topic);
}

static uint32_t s_taskSequences[TASK_ID_MAX];

static Event_t makeEvent(EventTopic_t topic, TaskID_t source, EventPriority_t priority)
{
    Event_t evt = {
        .topic = topic,
        .timestamp = xTaskGetTickCount(),
        .eventId = 0,
        .sequence = ++s_taskSequences[source],
        .priority = (uint8_t)priority,
        .sourceTask = (uint8_t)source,
        .payload = {0}
    };
    return evt;
}

static void publishSimpleFrom(TaskID_t source, EventTopic_t topic,
                              EventPriority_t priority,
                              uint8_t value0, uint8_t value1)
{
    Event_t evt = makeEvent(topic, source, priority);
    evt.payload[0] = value0;
    evt.payload[1] = value1;
    Bus_Publish(&evt);
}

static void publishSimple(EventTopic_t topic, uint8_t value0, uint8_t value1)
{
    publishSimpleFrom(TASK_ID_HEALTH_MANAGER, topic, EVENT_PRIORITY_NORMAL,
                      value0, value1);
}

static inline void sendHeartbeat(TaskID_t id)
{
    publishSimpleFrom(id, TOPIC_HEARTBEAT, EVENT_PRIORITY_LOW, (uint8_t)id, 0);
}

static void publishFrameReady(uint32_t sequence)
{
    EventPayload_t *frame = Pool_Alloc();
    if (frame == NULL) {
        publishSimple(TOPIC_SYSTEM_FAULT, TASK_ID_SENSOR_MANAGER, 1);
        return;
    }

    frame->framePtr = (sequence & 1U) ? s_dmaFrameA : s_dmaFrameB;
    frame->frameSize = FRAME_BYTES;
    frame->sequence = sequence;
    frame->sourceId = 1;

    uint8_t subscriberCount = Bus_GetSubscriberCount(TOPIC_FRAME_READY);
    if (subscriberCount == 0U) {
        Pool_Free(frame);
        return;
    }

    Pool_SetRefCount(frame, subscriberCount);

    Event_t evt = makeEvent(TOPIC_FRAME_READY, TASK_ID_SENSOR_MANAGER,
                            EVENT_PRIORITY_HIGH);
    PointerPayload_t pointerPayload = { .ptr = frame };
    Event_Pack(&evt, &pointerPayload, sizeof(pointerPayload));

    uint8_t sentCount = 0;
    Bus_PublishWithCount(&evt, &sentCount);
    for (uint8_t i = sentCount; i < subscriberCount; i++) {
        Pool_Free(frame);
    }

    printf("[SENSOR_MANAGER] frame ready seq=%lu bytes=%lu refs=%u sent=%u\n",
           (unsigned long)frame->sequence,
           (unsigned long)frame->frameSize,
           subscriberCount,
           sentCount);
}

/*
 * SensorManagerTask
 * Publishes legacy TEMP_UPDATE plus richer SENSOR_READING events.
 * Payload for SENSOR_READING: [0]=sensor id, [1]=value, [2]=unit id.
 */
static void prvSensorManagerTask(void *pvParams)
{
    (void)pvParams;
    uint8_t temperature = 25;
    uint8_t humidity = 45;
    uint32_t frameSeq = 0;
    TickType_t lastFrameTick = 0;

    printf("[SENSOR_MANAGER] Started\n");

    for (;;) {
        DeviceConfig_t config;
        Config_GetSnapshot(&config);

        int tempDelta = (rand() % 5) - 2;
        int humidityDelta = (rand() % 7) - 3;

        temperature = (uint8_t)((temperature + tempDelta + 51) % 51);
        humidity = (uint8_t)((humidity + humidityDelta + 101) % 101);

        publishSimpleFrom(TASK_ID_SENSOR_MANAGER, TOPIC_TEMP_UPDATE, EVENT_PRIORITY_NORMAL, temperature, 0);

        SensorReadingPayload_t tempPayload = {
            .sensorId = 1,
            .value = temperature,
            .unit = UNIT_CELSIUS,
            .quality = 100
        };
        Event_t tempEvt = makeEvent(TOPIC_SENSOR_READING, TASK_ID_SENSOR_MANAGER,
                                    EVENT_PRIORITY_NORMAL);
        Event_Pack(&tempEvt, &tempPayload, sizeof(tempPayload));
        Bus_Publish(&tempEvt);

        SensorReadingPayload_t humidityPayload = {
            .sensorId = 2,
            .value = humidity,
            .unit = UNIT_PERCENT_RH,
            .quality = 100
        };
        Event_t humidityEvt = makeEvent(TOPIC_SENSOR_READING, TASK_ID_SENSOR_MANAGER,
                                        EVENT_PRIORITY_NORMAL);
        Event_Pack(&humidityEvt, &humidityPayload, sizeof(humidityPayload));
        Bus_Publish(&humidityEvt);

        if (temperature > config.tempThreshold) {
            publishSimpleFrom(TASK_ID_SENSOR_MANAGER, TOPIC_THRESHOLD_CROSSED, EVENT_PRIORITY_HIGH, 1, temperature);
            printf("[SENSOR_MANAGER] Threshold crossed: temp=%u limit=%u\n",
                   temperature, config.tempThreshold);
        } else {
            printf("[SENSOR_MANAGER] temp=%uC humidity=%u%%\n",
                   temperature, humidity);
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - lastFrameTick) >= pdMS_TO_TICKS(config.framePeriodMs)) {
            lastFrameTick = now;
            publishFrameReady(++frameSeq);
        }

        sendHeartbeat(TASK_ID_SENSOR_MANAGER);
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

/*
 * CommandTask
 * Simulates commands from UART/CLI/MQTT. It publishes command, config,
 * actuator, OTA, security, and legacy button events.
 */
static void prvCommandTask(void *pvParams)
{
    (void)pvParams;
    uint8_t commandIndex = 0;
    static const char *commands[] = {
        "ACTUATOR fan 1",
        "BUTTON 0",
        "OTA START",
        "SECURITY INVALID_TOKEN",
        "SET temp_threshold 38",
        "SET diag_period_ms 4000",
        "SET low_voltage_dv 34",
        "SET frame_period_ms 2000",
        "GET status"
    };

    printf("[COMMAND] Started\n");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        const char *line = commands[commandIndex++ % (sizeof(commands) / sizeof(commands[0]))];
        ParsedCommand_t parsed;

        if (CommandParser_Parse(line, &parsed)) {
            CommandPayload_t commandPayload = {
                .commandId = parsed.commandId,
                .targetId = parsed.targetId,
                .value = parsed.value
            };
            Event_t commandEvt = makeEvent(TOPIC_COMMAND_RECEIVED, TASK_ID_COMMAND,
                                           EVENT_PRIORITY_NORMAL);
            Event_Pack(&commandEvt, &commandPayload, sizeof(commandPayload));
            Bus_Publish(&commandEvt);

            switch (parsed.commandId) {
            case COMMAND_ID_SET_CONFIG: {
                ConfigUpdatePayload_t configPayload = {
                    .key = parsed.targetId,
                    .value = parsed.value
                };
                Event_t cfgEvt = makeEvent(TOPIC_CONFIG_UPDATE, TASK_ID_COMMAND,
                                           EVENT_PRIORITY_HIGH);
                Event_Pack(&cfgEvt, &configPayload, sizeof(configPayload));
                Bus_Publish(&cfgEvt);
                break;
            }
            case COMMAND_ID_ACTUATOR:
                publishSimpleFrom(TASK_ID_COMMAND, TOPIC_ACTUATOR_COMMAND,
                                  EVENT_PRIORITY_HIGH, parsed.targetId,
                                  (uint8_t)parsed.value);
                break;
            case COMMAND_ID_BUTTON:
                publishSimpleFrom(TASK_ID_COMMAND, TOPIC_BUTTON_PRESS,
                                  EVENT_PRIORITY_NORMAL, 0, 0);
                break;
            case COMMAND_ID_OTA:
                publishSimpleFrom(TASK_ID_COMMAND, TOPIC_OTA_EVENT,
                                  EVENT_PRIORITY_HIGH, 1, 0);
                break;
            case COMMAND_ID_SECURITY_EVENT:
                publishSimpleFrom(TASK_ID_COMMAND, TOPIC_SECURITY_EVENT,
                                  EVENT_PRIORITY_CRITICAL, 1, 0);
                break;
            default:
                publishSimpleFrom(TASK_ID_COMMAND, TOPIC_DEVICE_STATUS,
                                  EVENT_PRIORITY_NORMAL, 0, 0);
                break;
            }

            printf("[COMMAND] parsed: %s\n", line);
        } else {
            publishSimpleFrom(TASK_ID_COMMAND, TOPIC_SECURITY_EVENT,
                              EVENT_PRIORITY_HIGH, 2, 0);
            printf("[COMMAND] rejected: %s\n", line);
        }

        sendHeartbeat(TASK_ID_COMMAND);
    }
}

/* NetworkTask publishes link/MQTT state and simulates cloud commands. */
static void prvNetworkTask(void *pvParams)
{
    (void)pvParams;
    uint8_t online = 1;

    printf("[NETWORK] Started\n");

    for (;;) {
        publishSimpleFrom(TASK_ID_NETWORK, TOPIC_NETWORK_STATUS, EVENT_PRIORITY_NORMAL, online, online ? 1 : 0);
        printf("[NETWORK] link=%s mqtt=%s\n",
               online ? "up" : "down",
               online ? "connected" : "disconnected");

        if (online) {
            publishSimpleFrom(TASK_ID_NETWORK, TOPIC_COMMAND_RECEIVED, EVENT_PRIORITY_NORMAL, 10, 0); /* cloud poll */
        }

        online = (uint8_t)!online;
        sendHeartbeat(TASK_ID_NETWORK);
        vTaskDelay(pdMS_TO_TICKS(6000));
    }
}

/* PowerMonitorTask simulates supply voltage and low-power warnings. */
static void prvPowerMonitorTask(void *pvParams)
{
    (void)pvParams;
    uint8_t voltageDecivolts = 37;

    printf("[POWER] Started\n");

    for (;;) {
        int delta = (rand() % 3) - 1;
        voltageDecivolts = (uint8_t)((voltageDecivolts + delta + 50) % 50);
        if (voltageDecivolts < 30) {
            voltageDecivolts = 37;
        }

        SensorReadingPayload_t voltagePayload = {
            .sensorId = 3,
            .value = voltageDecivolts,
            .unit = UNIT_DECIVOLTS,
            .quality = 100
        };
        Event_t reading = makeEvent(TOPIC_SENSOR_READING, TASK_ID_POWER_MONITOR,
                                    EVENT_PRIORITY_NORMAL);
        Event_Pack(&reading, &voltagePayload, sizeof(voltagePayload));
        Bus_Publish(&reading);

        DeviceConfig_t config;
        Config_GetSnapshot(&config);

        if (voltageDecivolts < config.lowVoltageDecivolts) {
            publishSimpleFrom(TASK_ID_POWER_MONITOR, TOPIC_POWER_EVENT, EVENT_PRIORITY_HIGH, 1, voltageDecivolts);
            printf("[POWER] LOW_BATTERY voltage=%u.%uV\n",
                   voltageDecivolts / 10, voltageDecivolts % 10);
        } else {
            publishSimpleFrom(TASK_ID_POWER_MONITOR, TOPIC_POWER_EVENT, EVENT_PRIORITY_NORMAL, 0, voltageDecivolts);
        }

        sendHeartbeat(TASK_ID_POWER_MONITOR);
        vTaskDelay(pdMS_TO_TICKS(2500));
    }
}

/* DiagnosticsTask publishes health counters for other tasks to consume. */
static void prvDiagnosticsTask(void *pvParams)
{
    (void)pvParams;

    printf("[DIAGNOSTICS] Started\n");

    for (;;) {
        BusTopicStats_t sensorStats;
        Bus_GetTopicStats(TOPIC_SENSOR_READING, &sensorStats);

        Event_t report = makeEvent(TOPIC_DIAGNOSTIC_REPORT, TASK_ID_DIAGNOSTICS,
                                   EVENT_PRIORITY_NORMAL);
        report.payload[0] = (uint8_t)Bus_GetDropCount();
        report.payload[1] = (uint8_t)uxQueueMessagesWaiting(s_loggerQueue);
        report.payload[2] = (uint8_t)uxQueueMessagesWaiting(s_storageQueue);
        report.payload[3] = (uint8_t)uxQueueMessagesWaiting(s_cloudQueue);
        report.payload[4] = Pool_GetFreeCount();
        report.payload[5] = (uint8_t)sensorStats.published;
        report.payload[6] = sensorStats.maxQueueDepth;
        Bus_Publish(&report);

        DeviceConfig_t config;
        Config_GetSnapshot(&config);

        printf("[DIAGNOSTICS] drops=%u loggerQ=%u storageQ=%u cloudQ=%u poolFree=%u sensorPub=%lu maxQ=%u\n",
               report.payload[0], report.payload[1],
               report.payload[2], report.payload[3], report.payload[4],
               (unsigned long)sensorStats.published, sensorStats.maxQueueDepth);

        sendHeartbeat(TASK_ID_DIAGNOSTICS);
        vTaskDelay(pdMS_TO_TICKS(config.diagnosticPeriodMs));
    }
}

/* StorageTask persists important events and emits a STORAGE_LOG summary. */
static void prvStorageTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;
    uint32_t storedCount = 0;

    printf("[STORAGE] Started\n");

    for (;;) {
        if (xQueueReceive(s_storageQueue, &evt, pdMS_TO_TICKS(1000)) == pdPASS) {
            storedCount++;
            char logLine[160];
            snprintf(logLine, sizeof(logLine),
                     "stored=%lu topic=%s event=%lu src=%u p0=%u",
                     (unsigned long)storedCount, topicToString(evt.topic),
                     (unsigned long)evt.eventId, evt.sourceTask, evt.payload[0]);
            StorageBackend_AppendLog(logLine);
            printf("[STORAGE] %s\n", logLine);

            if (evt.topic == TOPIC_FRAME_READY) {
                EventPayload_t *frame = NULL;
                memcpy(&frame, evt.payload, sizeof(frame));
                if (frame != NULL) {
                    printf("[STORAGE] indexed frame seq=%lu bytes=%lu\n",
                           (unsigned long)frame->sequence,
                           (unsigned long)frame->frameSize);
                    Pool_Free(frame);
                }
            }

            if ((storedCount % 5U) == 0U) {
                publishSimple(TOPIC_STORAGE_LOG, (uint8_t)storedCount, 0);
            }
        }
        sendHeartbeat(TASK_ID_STORAGE);
    }
}

/* CloudTask uploads telemetry/status and publishes cloud sync status. */
static void prvCloudTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;
    uint32_t uploadCount = 0;

    printf("[CLOUD] Started\n");

    for (;;) {
        if (xQueueReceive(s_cloudQueue, &evt, pdMS_TO_TICKS(1200)) == pdPASS) {
            uploadCount++;
            char json[192];
            Telemetry_EncodeJson(&evt, json, sizeof(json));
            printf("[CLOUD] upload #%lu %s\n",
                   (unsigned long)uploadCount, json);

            if (evt.topic == TOPIC_ACTUATOR_COMMAND) {
                publishSimple(TOPIC_ACTUATOR_STATUS, evt.payload[0], 1);
            } else if (evt.topic == TOPIC_FRAME_READY) {
                EventPayload_t *frame = NULL;
                memcpy(&frame, evt.payload, sizeof(frame));
                if (frame != NULL) {
                    printf("[CLOUD] streamed frame seq=%lu bytes=%lu\n",
                           (unsigned long)frame->sequence,
                           (unsigned long)frame->frameSize);
                    Pool_Free(frame);
                }
            }
        }
        sendHeartbeat(TASK_ID_CLOUD);
    }
}

/* ConfigManagerTask applies runtime configuration changes. */
static void prvConfigManagerTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;

    printf("[CONFIG] Started\n");

    for (;;) {
        if (xQueueReceive(s_configQueue, &evt, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (evt.topic == TOPIC_CONFIG_UPDATE) {
                BaseType_t result = pdFAIL;
                DeviceConfig_t config;

                ConfigUpdatePayload_t update;
                Event_Unpack(&evt, &update, sizeof(update));

                switch (update.key) {
                case CONFIG_KEY_TEMP_THRESHOLD:
                    result = Config_SetTempThreshold((uint8_t)update.value);
                    printf("[CONFIG] Applied temp threshold=%lu result=%s\n",
                           (unsigned long)update.value, result == pdPASS ? "ok" : "fail");
                    break;
                case CONFIG_KEY_DIAG_PERIOD_MS:
                    result = Config_SetDiagnosticPeriodMs(update.value);
                    printf("[CONFIG] Applied diagnostic period=%lums result=%s\n",
                           (unsigned long)update.value, result == pdPASS ? "ok" : "fail");
                    break;
                case CONFIG_KEY_LOW_VOLTAGE_DV:
                    result = Config_SetLowVoltageThreshold((uint8_t)update.value);
                    printf("[CONFIG] Applied low voltage=%lu.%luV result=%s\n",
                           (unsigned long)(update.value / 10UL),
                           (unsigned long)(update.value % 10UL),
                           result == pdPASS ? "ok" : "fail");
                    break;
                case CONFIG_KEY_FRAME_PERIOD_MS:
                    result = Config_SetFramePeriodMs(update.value);
                    printf("[CONFIG] Applied frame period=%lums result=%s\n",
                           (unsigned long)update.value, result == pdPASS ? "ok" : "fail");
                    break;
                default:
                    break;
                }

                Config_GetSnapshot(&config);
                publishSimpleFrom(TASK_ID_CONFIG_MANAGER, TOPIC_DEVICE_STATUS,
                                  result == pdPASS ? EVENT_PRIORITY_NORMAL : EVENT_PRIORITY_HIGH,
                                  result == pdPASS ? 1U : 2U,
                                  config.tempThreshold);
            }
        }
        sendHeartbeat(TASK_ID_CONFIG_MANAGER);
    }
}

/* HealthManagerTask converts raw health/fault signals into device state. */
static void prvHealthManagerTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;
    DeviceState_t deviceState = DEVICE_STATE_BOOTING;

    printf("[HEALTH] Started\n");

    for (;;) {
        if (xQueueReceive(s_healthQueue, &evt, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (FaultManager_HandleEvent(&evt, &deviceState) == pdTRUE) {
                publishSimpleFrom(TASK_ID_HEALTH_MANAGER, TOPIC_DEVICE_STATUS,
                                  deviceState >= DEVICE_STATE_FAULT ? EVENT_PRIORITY_HIGH : EVENT_PRIORITY_NORMAL,
                                  (uint8_t)deviceState, evt.payload[0]);
                printf("[HEALTH] state=%u reason=%s value=%u\n",
                       (unsigned)deviceState, topicToString(evt.topic), evt.payload[0]);
            }
        }
        sendHeartbeat(TASK_ID_HEALTH_MANAGER);
    }
}

/* LoggerTask subscribes to every topic and prints compact trace lines. */
static void prvLoggerTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;
    uint32_t logCount = 0;

    printf("[LOGGER] Started\n");

    for (;;) {
        if (xQueueReceive(s_loggerQueue, &evt, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (evt.topic != TOPIC_HEARTBEAT) {
                printf("[LOGGER] #%04lu id=%lu seq=%lu src=%u prio=%u t=%lu topic=%-19s p0=%u p1=%u p2=%u\n",
                       (unsigned long)++logCount,
                       (unsigned long)evt.eventId,
                       (unsigned long)evt.sequence,
                       evt.sourceTask, evt.priority,
                       (unsigned long)evt.timestamp,
                       topicToString(evt.topic),
                       evt.payload[0], evt.payload[1], evt.payload[2]);

                if (evt.topic == TOPIC_FRAME_READY) {
                    EventPayload_t *frame = NULL;
                    memcpy(&frame, evt.payload, sizeof(frame));
                    if (frame != NULL) {
                        printf("[LOGGER]       frame seq=%lu bytes=%lu ptr=%p\n",
                               (unsigned long)frame->sequence,
                               (unsigned long)frame->frameSize,
                               frame->framePtr);
                        Pool_Free(frame);
                    }
                }
            }
        }
        sendHeartbeat(TASK_ID_LOGGER);
    }
}

static void subscribeLoggerToAllTopics(void)
{
    for (EventTopic_t topic = TOPIC_TEMP_UPDATE; topic < TOPIC_MAX; topic++) {
        if (topic != TOPIC_HEARTBEAT) {
            Bus_Subscribe(topic, s_loggerQueue);
        }
    }
}

void AppTasks_Start(void)
{
    Pool_Init();
    Config_Init();
    FaultManager_Init();

    s_loggerQueue = xQueueCreate(TASK_QUEUE_DEPTH * 3, sizeof(Event_t));
    s_storageQueue = xQueueCreate(TASK_QUEUE_DEPTH * 2, sizeof(Event_t));
    s_cloudQueue = xQueueCreate(TASK_QUEUE_DEPTH * 2, sizeof(Event_t));
    s_configQueue = xQueueCreate(TASK_QUEUE_DEPTH, sizeof(Event_t));
    s_healthQueue = xQueueCreate(TASK_QUEUE_DEPTH * 2, sizeof(Event_t));

    configASSERT(s_loggerQueue && s_storageQueue && s_cloudQueue &&
                 s_configQueue && s_healthQueue);

    subscribeLoggerToAllTopics();

    Bus_Subscribe(TOPIC_SENSOR_READING,      s_storageQueue);
    Bus_Subscribe(TOPIC_THRESHOLD_CROSSED,   s_storageQueue);
    Bus_Subscribe(TOPIC_SYSTEM_FAULT,        s_storageQueue);
    Bus_Subscribe(TOPIC_SECURITY_EVENT,      s_storageQueue);
    Bus_Subscribe(TOPIC_ACTUATOR_STATUS,     s_storageQueue);
    Bus_Subscribe(TOPIC_DIAGNOSTIC_REPORT,   s_storageQueue);
    Bus_Subscribe(TOPIC_FRAME_READY,         s_storageQueue);

    Bus_Subscribe(TOPIC_SENSOR_READING,      s_cloudQueue);
    Bus_Subscribe(TOPIC_DEVICE_STATUS,       s_cloudQueue);
    Bus_Subscribe(TOPIC_SYSTEM_FAULT,        s_cloudQueue);
    Bus_Subscribe(TOPIC_DIAGNOSTIC_REPORT,   s_cloudQueue);
    Bus_Subscribe(TOPIC_ACTUATOR_STATUS,     s_cloudQueue);
    Bus_Subscribe(TOPIC_ACTUATOR_COMMAND,    s_cloudQueue);
    Bus_Subscribe(TOPIC_NETWORK_STATUS,      s_cloudQueue);
    Bus_Subscribe(TOPIC_FRAME_READY,         s_cloudQueue);

    Bus_Subscribe(TOPIC_CONFIG_UPDATE,       s_configQueue);

    Bus_Subscribe(TOPIC_SYSTEM_FAULT,        s_healthQueue);
    Bus_Subscribe(TOPIC_THRESHOLD_CROSSED,   s_healthQueue);
    Bus_Subscribe(TOPIC_NETWORK_STATUS,      s_healthQueue);
    Bus_Subscribe(TOPIC_POWER_EVENT,         s_healthQueue);
    Bus_Subscribe(TOPIC_DIAGNOSTIC_REPORT,   s_healthQueue);
    Bus_Subscribe(TOPIC_SECURITY_EVENT,      s_healthQueue);

    xTaskCreate(prvSensorManagerTask, "SensorMgr", configMINIMAL_STACK_SIZE * 4, NULL, 3, NULL);
    xTaskCreate(prvCommandTask,       "Command",   configMINIMAL_STACK_SIZE * 4, NULL, 4, NULL);
    xTaskCreate(prvNetworkTask,       "Network",   configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(prvPowerMonitorTask,  "Power",     configMINIMAL_STACK_SIZE * 4, NULL, 3, NULL);
    xTaskCreate(prvDiagnosticsTask,   "Diag",      configMINIMAL_STACK_SIZE * 4, NULL, 1, NULL);
    xTaskCreate(prvStorageTask,       "Storage",   configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(prvCloudTask,         "Cloud",     configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(prvConfigManagerTask, "Config",    configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(prvHealthManagerTask, "Health",    configMINIMAL_STACK_SIZE * 4, NULL, 3, NULL);
    xTaskCreate(prvLoggerTask,        "Logger",    configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
}
