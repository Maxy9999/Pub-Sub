#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

#include "FreeRTOS.h"
#include "queue.h"

/* Task IDs - each task reports its ID in the heartbeat payload */
typedef enum {
    TASK_ID_SENSOR_MANAGER = 0,
    TASK_ID_COMMAND,
    TASK_ID_NETWORK,
    TASK_ID_POWER_MONITOR,
    TASK_ID_DIAGNOSTICS,
    TASK_ID_STORAGE,
    TASK_ID_CLOUD,
    TASK_ID_CONFIG_MANAGER,
    TASK_ID_HEALTH_MANAGER,
    TASK_ID_LOGGER,
    TASK_ID_MAX
} TaskID_t;

/* How long before a task is declared stalled (ms) */
#define WATCHDOG_TIMEOUT_MS     8000U

void WatchdogTask_Start(void);

#endif /* WATCHDOG_TASK_H */
