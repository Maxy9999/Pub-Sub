#ifndef APP_TASKS_H
#define APP_TASKS_H

/**
 * AppTasks_Start – create queues, subscribe to topics, and spawn all tasks.
 * Call after Bus_Init() and WatchdogTask_Start(), before vTaskStartScheduler().
 */
void AppTasks_Start(void);

#endif /* APP_TASKS_H */
