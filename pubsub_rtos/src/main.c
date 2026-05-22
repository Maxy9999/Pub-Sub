#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include "event_bus.h"
#include "watchdog_task.h"
#include "app_tasks.h"

/* Required for static allocation */
StackType_t uxTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];

static void handle_sigint(int sig) { (void)sig; exit(0); }

int main(void)
{
    signal(SIGINT, handle_sigint);
    srand((unsigned)time(NULL));

    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║   FreeRTOS Pub/Sub Event Bus Demo        ║\n");
    printf("║   POSIX Simulator  (Ctrl+C to exit)      ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    Bus_Init();
    WatchdogTask_Start();
    AppTasks_Start();

    printf("[MAIN] Starting scheduler...\n\n");
    vTaskStartScheduler();

    printf("[MAIN] ERROR: scheduler exited!\n");
    return 1;
}

/* ── FreeRTOS required hooks ──────────────────────────────────────────── */

void vApplicationMallocFailedHook(void) { vAssertCalled(__FILE__, __LINE__); }
void vApplicationIdleHook(void)         { usleep(15000); }
void vApplicationTickHook(void)         { }
void vApplicationDaemonTaskStartupHook(void) { }

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName)
{
    (void)pxTask; (void)pcTaskName;
    vAssertCalled(__FILE__, __LINE__);
}

void vAssertCalled(const char *pcFileName, unsigned long ulLine)
{
    printf("[ASSERT] %s : %lu\n", pcFileName, ulLine);
    fflush(stdout);
    for(;;);
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t  uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer   = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize   = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *pulTimerTaskStackSize)
{
    static StaticTask_t xTimerTaskTCB;
    *ppxTimerTaskTCBBuffer   = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize   = configTIMER_TASK_STACK_DEPTH;
}
