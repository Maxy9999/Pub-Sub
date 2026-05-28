#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "event_bus.h"
#include "watchdog_task.h"
#include "app_tasks.h"

/* ── Shared queue depth ───────────────────────────────────────────────── */
#define TASK_QUEUE_DEPTH    8

/* ── Helper: send heartbeat ───────────────────────────────────────────── */
static inline void sendHeartbeat(TaskID_t id)
{
    Event_t hb = {
        .topic     = TOPIC_HEARTBEAT,
        .timestamp = xTaskGetTickCount(),
        .payload   = {0}
    };
    hb.payload[0] = (uint8_t)id;
    Bus_Publish(&hb);
}

/* ════════════════════════════════════════════════════════════════════════
 * SENSOR TASK  – publishes TOPIC_TEMP_UPDATE every 500 ms
 * Payload: payload[0] = temperature (simulated 0–50 °C)
 * ════════════════════════════════════════════════════════════════════════ */
static void prvSensorTask(void *pvParams)
{
    (void)pvParams;
    uint8_t temperature = 22;

    printf("[SENSOR] Started\n");

    for (;;) {
        /* Simulate temperature drift */
        int delta = (rand() % 3) - 1;   /* -1, 0, or +1 */
        temperature = (uint8_t)((temperature + delta + 51) % 51);

        Event_t evt = {
            .topic     = TOPIC_TEMP_UPDATE,
            .timestamp = xTaskGetTickCount(),
            .payload   = {0}
        };
        evt.payload[0] = temperature;
        Bus_Publish(&evt);

        //printf("[SENSOR] Published temp = %d°C\n", temperature);
        sendHeartbeat(TASK_ID_SENSOR);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * BUTTON TASK  – stub: simulates a button press every 3 s
 * On real Pi, replace vTaskDelay loop with GPIO interrupt + semaphore.
 * Payload: payload[0] = button ID (always 0 for now)
 * ════════════════════════════════════════════════════════════════════════ */
static void prvButtonTask(void *pvParams)
{
    (void)pvParams;
    printf("[BUTTON] Started (STUB – simulating press every 3s)\n");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(300));

        Event_t evt = {
            .topic     = TOPIC_BUTTON_PRESS,
            .timestamp = xTaskGetTickCount(),
            .payload   = {0}
        };
        evt.payload[0] = 0; /* button 0 */
        Bus_Publish(&evt);

        //printf("[BUTTON] Button press published\n");
        sendHeartbeat(TASK_ID_BUTTON);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * DISPLAY TASK  – subscribes to TOPIC_TEMP_UPDATE
 * ════════════════════════════════════════════════════════════════════════ */
static QueueHandle_t s_displayQueue;

static void prvDisplayTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;

    printf("[DISPLAY] Started\n");

    for (;;) {
        if (xQueueReceive(s_displayQueue, &evt, pdMS_TO_TICKS(600)) == pdPASS) {
            if (evt.topic == TOPIC_TEMP_UPDATE) {
                // printf("[DISPLAY] ┌─────────────────────┐\n");
                // printf("[DISPLAY] │  Temperature: %3d°C │\n", evt.payload[0]);
                // printf("[DISPLAY] └─────────────────────┘\n");
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
        }
        sendHeartbeat(TASK_ID_DISPLAY);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * LOGGER TASK  – subscribes to ALL topics, writes log lines
 * ════════════════════════════════════════════════════════════════════════ */
static QueueHandle_t s_loggerQueue;

static const char *topicName[] = {
    "TEMP_UPDATE", "BUTTON_PRESS", "SYSTEM_FAULT", "HEARTBEAT"
};

static void prvLoggerTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;
    uint32_t logCount = 0;

    printf("[LOGGER] Started\n");

    for (;;) {
        if (xQueueReceive(s_loggerQueue, &evt, pdMS_TO_TICKS(1000)) == pdPASS) {
            /* Skip heartbeats to reduce log noise */
            if (evt.topic != TOPIC_HEARTBEAT) {
                // printf("[LOGGER] #%04lu t=%lu topic=%-14s payload[0]=%d\n",
                //        (unsigned long)++logCount,
                //        (unsigned long)evt.timestamp,
                //        topicName[evt.topic],
                //        evt.payload[0]);
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
        }
        sendHeartbeat(TASK_ID_LOGGER);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * ALERT TASK  – subscribes to TOPIC_TEMP_UPDATE & TOPIC_SYSTEM_FAULT
 * Fires alert if temp > 35°C
 * ════════════════════════════════════════════════════════════════════════ */
#define TEMP_ALERT_THRESHOLD    35

static QueueHandle_t s_alertQueue;

static void prvAlertTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;

    printf("[ALERT] Started. Threshold = %d°C\n", TEMP_ALERT_THRESHOLD);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        if (xQueueReceive(s_alertQueue, &evt, pdMS_TO_TICKS(600)) == pdPASS) {
            if (evt.topic == TOPIC_TEMP_UPDATE) {
                if (evt.payload[0] > TEMP_ALERT_THRESHOLD) {
                    // printf("[ALERT] *** HIGH TEMP ALERT: %d°C > %d°C ***\n",
                    //        evt.payload[0], TEMP_ALERT_THRESHOLD);
                }
            } else if (evt.topic == TOPIC_SYSTEM_FAULT) {
                // printf("[ALERT] *** SYSTEM FAULT: task_id=%d stalled ***\n",
                //        evt.payload[0]);
            }
        }
        sendHeartbeat(TASK_ID_ALERT);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * STATS TASK  – prints bus & queue stats every 5 s
 * ════════════════════════════════════════════════════════════════════════ */
static void prvStatsTask(void *pvParams)
{
    (void)pvParams;
    printf("[STATS] Started\n");

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        printf("\n[STATS] ── Bus Statistics ─────────────────────────\n");
        printf("[STATS]   Total dropped events : %lu\n",
               (unsigned long)Bus_GetDropCount());
        printf("[STATS]   Display queue waiting: %lu\n",
               (unsigned long)uxQueueMessagesWaiting(s_displayQueue));
        printf("[STATS]   Logger queue waiting : %lu\n",
               (unsigned long)uxQueueMessagesWaiting(s_loggerQueue));
        printf("[STATS]   Alert queue waiting  : %lu\n",
               (unsigned long)uxQueueMessagesWaiting(s_alertQueue));
        printf("[STATS] ──────────────────────────────────────────────\n\n");

        sendHeartbeat(TASK_ID_STATS);
    }
}

/* ════════════════════════════════════════════════════════════════════════
 * AppTasks_Start  – create all queues, subscribe, spawn tasks
 * ════════════════════════════════════════════════════════════════════════ */
void AppTasks_Start(void)
{
    /* Create subscriber queues */
    s_displayQueue = xQueueCreate(TASK_QUEUE_DEPTH*10, sizeof(Event_t));
    s_loggerQueue  = xQueueCreate(TASK_QUEUE_DEPTH * 2, sizeof(Event_t));
    s_alertQueue   = xQueueCreate(TASK_QUEUE_DEPTH*5, sizeof(Event_t));

    configASSERT(s_displayQueue && s_loggerQueue && s_alertQueue);

    /* Subscribe to topics */
    Bus_Subscribe(TOPIC_TEMP_UPDATE,  s_displayQueue);

    Bus_Subscribe(TOPIC_TEMP_UPDATE,  s_loggerQueue);
    Bus_Subscribe(TOPIC_BUTTON_PRESS, s_loggerQueue);
    Bus_Subscribe(TOPIC_SYSTEM_FAULT, s_loggerQueue);

    Bus_Subscribe(TOPIC_TEMP_UPDATE,  s_alertQueue);
    Bus_Subscribe(TOPIC_SYSTEM_FAULT, s_alertQueue);

    /* Spawn tasks (priority 1=low, higher = more urgent) */
    xTaskCreate(prvSensorTask,  "Sensor",  configMINIMAL_STACK_SIZE * 4, NULL, 3, NULL);
    xTaskCreate(prvButtonTask,  "Button",  configMINIMAL_STACK_SIZE * 4, NULL, 4, NULL);
    xTaskCreate(prvDisplayTask, "Display", configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(prvLoggerTask,  "Logger",  configMINIMAL_STACK_SIZE * 4, NULL, 2, NULL);
    xTaskCreate(prvAlertTask,   "Alert",   configMINIMAL_STACK_SIZE * 4, NULL, 3, NULL);
    xTaskCreate(prvStatsTask,   "Stats",   configMINIMAL_STACK_SIZE * 4, NULL, 1, NULL);
}
