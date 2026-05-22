#ifndef WATCHDOG_TASK_H
#define WATCHDOG_TASK_H

#include "FreeRTOS.h"
#include "queue.h"

/* Task IDs - each task reports its ID in the heartbeat payload */
typedef enum {
    TASK_ID_SENSOR   = 0,
    TASK_ID_BUTTON,
    TASK_ID_DISPLAY,
    TASK_ID_LOGGER,
    TASK_ID_ALERT,
    TASK_ID_STATS,
    TASK_ID_MAX
} TaskID_t;

/* How long before a task is declared stalled (ms) */
#define WATCHDOG_TIMEOUT_MS     2000U

void WatchdogTask_Start(void);

#endif /* WATCHDOG_TASK_H */
