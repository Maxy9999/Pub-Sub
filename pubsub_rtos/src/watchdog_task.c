#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "event_bus.h"
#include "watchdog_task.h"

#define WATCHDOG_QUEUE_DEPTH    64
#define WATCHDOG_CHECK_PERIOD   pdMS_TO_TICKS(200)
#define WATCHDOG_TIMEOUT_TICKS  pdMS_TO_TICKS(WATCHDOG_TIMEOUT_MS)

static QueueHandle_t s_watchdogQueue;

/* Last tick each task was seen alive */
static TickType_t s_lastSeen[TASK_ID_MAX];

static const char *taskName[] = {
    "SensorManager", "Command", "Network", "PowerMonitor",
    "Diagnostics", "Storage", "Cloud", "ConfigManager",
    "HealthManager", "Logger"
};

/* -- Watchdog task body ------------------------------------------------- */
static void prvWatchdogTask(void *pvParams)
{
    (void)pvParams;
    Event_t evt;

    /* Initialize all timestamps to now so no false alarms at startup */
    TickType_t now = xTaskGetTickCount();
    for (int i = 0; i < TASK_ID_MAX; i++) {
        s_lastSeen[i] = now;
    }

    printf("[WATCHDOG] Started. Timeout = %u ms\n", WATCHDOG_TIMEOUT_MS);

    for (;;) {
        /* Drain all pending heartbeats */
        while (xQueueReceive(s_watchdogQueue, &evt, 0) == pdPASS) {
            uint8_t taskId = evt.payload[0];
            if (taskId < TASK_ID_MAX) {
                s_lastSeen[taskId] = evt.timestamp;
            }
        }

        /* Check for stalled tasks */
        now = xTaskGetTickCount();
        for (int i = 0; i < TASK_ID_MAX; i++) {
            TickType_t elapsed = now - s_lastSeen[i];
            if (elapsed > WATCHDOG_TIMEOUT_TICKS) {
                printf("[WATCHDOG] *** STALL DETECTED: %s has not reported "
                       "for %lu ms! ***\n",
                       taskName[i],
                       (unsigned long)(elapsed * portTICK_PERIOD_MS));

                /* Publish a system fault so all subscribers are notified */
                Event_t fault = {
                    .topic     = TOPIC_SYSTEM_FAULT,
                    .timestamp = now,
                    .payload   = {0}
                };
                fault.payload[0] = (uint8_t)i; /* which task faulted */
                Bus_Publish(&fault);

                /* Reset timer to avoid repeated fault floods */
                s_lastSeen[i] = now;
            }
        }

        vTaskDelay(WATCHDOG_CHECK_PERIOD);
    }
}

/* -- Public start function --------------------------------------------- */
void WatchdogTask_Start(void)
{
    s_watchdogQueue = xQueueCreate(WATCHDOG_QUEUE_DEPTH, sizeof(Event_t));
    configASSERT(s_watchdogQueue != NULL);

    Bus_Subscribe(TOPIC_HEARTBEAT, s_watchdogQueue);

    xTaskCreate(prvWatchdogTask,
                "Watchdog",
                configMINIMAL_STACK_SIZE * 4,
                NULL,
                configMAX_PRIORITIES - 1,   /* highest priority */
                NULL);
}
