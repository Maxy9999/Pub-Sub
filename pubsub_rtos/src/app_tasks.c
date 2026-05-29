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

#define TASK_QUEUE_DEPTH        12
#define FRAME_BYTES             (1920UL * 1080UL * 2UL)

static QueueHandle_t s_loggerQueue;
static QueueHandle_t s_storageQueue;
static QueueHandle_t s_cloudQueue;
static QueueHandle_t s_configQueue;
static QueueHandle_t s_healthQueue;

static uint8_t s_dmaFrameA[64];
static uint8_t s_dmaFrameB[64];

static const char *topicName[] = {
    "TEMP_UPDATE",
    "BUTTON_PRESS",
    "SYSTEM_FAULT",
    "HEARTBEAT",
    "SENSOR_READING",
    "THRESHOLD_CROSSED",
    "DEVICE_STATUS",
    "CONFIG_UPDATE",
    "COMMAND_RECEIVED",
    "ACTUATOR_COMMAND",
    "ACTUATOR_STATUS",
    "STORAGE_LOG",
    "NETWORK_STATUS",
    "DIAGNOSTIC_REPORT",
    "POWER_EVENT",
    "OTA_EVENT",
    "SECURITY_EVENT",
    "FRAME_READY"
};

static const char *topicToString(EventTopic_t topic)
{
    if (topic < TOPIC_MAX) {
        return topicName[topic];
    }
    return "UNKNOWN";
}

static void publishSimple(EventTopic_t topic, uint8_t value0, uint8_t value1)
{
    Event_t evt = {
        .topic     = topic,
        .timestamp = xTaskGetTickCount(),
        .payload   = {0}
    };

    evt.payload[0] = value0;
    evt.payload[1] = value1;
    Bus_Publish(&evt);
}

static inline void sendHeartbeat(TaskID_t id)
{
    publishSimple(TOPIC_HEARTBEAT, (uint8_t)id, 0);
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

    Event_t evt = {
        .topic     = TOPIC_FRAME_READY,
        .timestamp = xTaskGetTickCount(),
        .payload   = {0}
    };
    memcpy(evt.payload, &frame, sizeof(frame));

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

        publishSimple(TOPIC_TEMP_UPDATE, temperature, 0);

        Event_t tempEvt = {
            .topic     = TOPIC_SENSOR_READING,
            .timestamp = xTaskGetTickCount(),
            .payload   = {0}
        };
        tempEvt.payload[0] = 1; /* temperature sensor */
        tempEvt.payload[1] = temperature;
        tempEvt.payload[2] = 1; /* degree C */
        Bus_Publish(&tempEvt);

        Event_t humidityEvt = tempEvt;
        humidityEvt.timestamp = xTaskGetTickCount();
        humidityEvt.payload[0] = 2; /* humidity sensor */
        humidityEvt.payload[1] = humidity;
        humidityEvt.payload[2] = 2; /* percent RH */
        Bus_Publish(&humidityEvt);

        if (temperature > config.tempThreshold) {
            publishSimple(TOPIC_THRESHOLD_CROSSED, 1, temperature);
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
    uint8_t commandId = 0;

    printf("[COMMAND] Started\n");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        commandId = (uint8_t)((commandId + 1) % 8);

        publishSimple(TOPIC_COMMAND_RECEIVED, commandId, 0);

        switch (commandId) {
        case 0:
            publishSimple(TOPIC_CONFIG_UPDATE, 1, 38); /* temp threshold */
            printf("[COMMAND] SET_TEMP_THRESHOLD 38\n");
            break;
        case 1:
            publishSimple(TOPIC_ACTUATOR_COMMAND, 1, 1); /* fan on */
            printf("[COMMAND] FAN_ON\n");
            break;
        case 2:
            publishSimple(TOPIC_BUTTON_PRESS, 0, 0);
            printf("[COMMAND] Simulated local button press\n");
            break;
        case 3:
            publishSimple(TOPIC_OTA_EVENT, 1, 0); /* OTA start */
            printf("[COMMAND] OTA_START\n");
            break;
        case 4:
            publishSimple(TOPIC_SECURITY_EVENT, 1, 0); /* invalid token */
            printf("[COMMAND] Invalid command token detected\n");
            break;
        case 5:
            publishSimple(TOPIC_CONFIG_UPDATE, 2, 4); /* diagnostics period seconds */
            printf("[COMMAND] SET_DIAGNOSTIC_PERIOD 4s\n");
            break;
        case 6:
            publishSimple(TOPIC_CONFIG_UPDATE, 3, 34); /* low voltage threshold */
            printf("[COMMAND] SET_LOW_VOLTAGE 3.4V\n");
            break;
        default:
            publishSimple(TOPIC_CONFIG_UPDATE, 4, 2); /* frame period seconds */
            printf("[COMMAND] SET_FRAME_PERIOD 2s\n");
            break;
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
        publishSimple(TOPIC_NETWORK_STATUS, online, online ? 1 : 0);
        printf("[NETWORK] link=%s mqtt=%s\n",
               online ? "up" : "down",
               online ? "connected" : "disconnected");

        if (online) {
            publishSimple(TOPIC_COMMAND_RECEIVED, 10, 0); /* cloud poll */
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

        Event_t reading = {
            .topic     = TOPIC_SENSOR_READING,
            .timestamp = xTaskGetTickCount(),
            .payload   = {0}
        };
        reading.payload[0] = 3; /* supply voltage */
        reading.payload[1] = voltageDecivolts;
        reading.payload[2] = 3; /* decivolts */
        Bus_Publish(&reading);

        DeviceConfig_t config;
        Config_GetSnapshot(&config);

        if (voltageDecivolts < config.lowVoltageDecivolts) {
            publishSimple(TOPIC_POWER_EVENT, 1, voltageDecivolts);
            printf("[POWER] LOW_BATTERY voltage=%u.%uV\n",
                   voltageDecivolts / 10, voltageDecivolts % 10);
        } else {
            publishSimple(TOPIC_POWER_EVENT, 0, voltageDecivolts);
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
        Event_t report = {
            .topic     = TOPIC_DIAGNOSTIC_REPORT,
            .timestamp = xTaskGetTickCount(),
            .payload   = {0}
        };
        report.payload[0] = (uint8_t)Bus_GetDropCount();
        report.payload[1] = (uint8_t)uxQueueMessagesWaiting(s_loggerQueue);
        report.payload[2] = (uint8_t)uxQueueMessagesWaiting(s_storageQueue);
        report.payload[3] = (uint8_t)uxQueueMessagesWaiting(s_cloudQueue);
        report.payload[4] = Pool_GetFreeCount();
        Bus_Publish(&report);

        DeviceConfig_t config;
        Config_GetSnapshot(&config);

        printf("[DIAGNOSTICS] drops=%u loggerQ=%u storageQ=%u cloudQ=%u poolFree=%u\n",
               report.payload[0], report.payload[1],
               report.payload[2], report.payload[3], report.payload[4]);

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
            printf("[STORAGE] stored #%lu topic=%s value=%u\n",
                   (unsigned long)storedCount,
                   topicToString(evt.topic),
                   evt.payload[0]);

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
            printf("[CLOUD] upload #%lu topic=%s value=%u\n",
                   (unsigned long)uploadCount,
                   topicToString(evt.topic),
                   evt.payload[0]);

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

                switch (evt.payload[0]) {
                case 1:
                    result = Config_SetTempThreshold(evt.payload[1]);
                    printf("[CONFIG] Applied temp threshold=%u result=%s\n",
                           evt.payload[1], result == pdPASS ? "ok" : "fail");
                    break;
                case 2:
                    result = Config_SetDiagnosticPeriodMs((uint32_t)evt.payload[1] * 1000UL);
                    printf("[CONFIG] Applied diagnostic period=%us result=%s\n",
                           evt.payload[1], result == pdPASS ? "ok" : "fail");
                    break;
                case 3:
                    result = Config_SetLowVoltageThreshold(evt.payload[1]);
                    printf("[CONFIG] Applied low voltage=%u.%uV result=%s\n",
                           evt.payload[1] / 10, evt.payload[1] % 10,
                           result == pdPASS ? "ok" : "fail");
                    break;
                case 4:
                    result = Config_SetFramePeriodMs((uint32_t)evt.payload[1] * 1000UL);
                    printf("[CONFIG] Applied frame period=%us result=%s\n",
                           evt.payload[1], result == pdPASS ? "ok" : "fail");
                    break;
                default:
                    break;
                }

                Config_GetSnapshot(&config);
                publishSimple(TOPIC_DEVICE_STATUS,
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
    uint8_t deviceState = 0; /* 0=OK, 1=degraded, 2=fault */

    printf("[HEALTH] Started\n");

    for (;;) {
        if (xQueueReceive(s_healthQueue, &evt, pdMS_TO_TICKS(1000)) == pdPASS) {
            switch (evt.topic) {
            case TOPIC_SYSTEM_FAULT:
            case TOPIC_SECURITY_EVENT:
                deviceState = 2;
                break;
            case TOPIC_POWER_EVENT:
            case TOPIC_NETWORK_STATUS:
            case TOPIC_THRESHOLD_CROSSED:
                deviceState = evt.payload[0] ? 1 : 0;
                break;
            default:
                break;
            }

            publishSimple(TOPIC_DEVICE_STATUS, deviceState, evt.payload[0]);
            printf("[HEALTH] state=%u reason=%s value=%u\n",
                   deviceState, topicToString(evt.topic), evt.payload[0]);
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
                printf("[LOGGER] #%04lu t=%lu topic=%-19s p0=%u p1=%u p2=%u\n",
                       (unsigned long)++logCount,
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
