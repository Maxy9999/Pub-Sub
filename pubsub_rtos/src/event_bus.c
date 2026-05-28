#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"

#include "event_bus.h"

/* ── Config ───────────────────────────────────────────────────────────── */
#define MAX_SUBSCRIBERS_PER_TOPIC   6

/* ── Internal registry ────────────────────────────────────────────────── */
typedef struct {
    QueueHandle_t subscribers[MAX_SUBSCRIBERS_PER_TOPIC];
    uint8_t       count;
} TopicEntry_t;

static TopicEntry_t      s_registry[TOPIC_MAX];
static SemaphoreHandle_t s_mutex;
static uint32_t          s_dropCount = 0;

/* ── Bus_Init ─────────────────────────────────────────────────────────── */
void Bus_Init(void)
{
    memset(s_registry, 0, sizeof(s_registry));
    s_dropCount = 0;
    s_mutex = xSemaphoreCreateMutex();
    configASSERT(s_mutex != NULL);
    printf("[BUS] Initialized. %d topics registered.\n", (int)TOPIC_MAX);
}

/* ── Bus_Subscribe ────────────────────────────────────────────────────── */
BaseType_t Bus_Subscribe(EventTopic_t topic, QueueHandle_t rxQueue)
{
    configASSERT(topic < TOPIC_MAX);
    configASSERT(rxQueue != NULL);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    TopicEntry_t *entry = &s_registry[topic];
    if (entry->count >= MAX_SUBSCRIBERS_PER_TOPIC) {
        printf("[BUS] ERROR: subscriber slots full for topic %d\n", topic);
        xSemaphoreGive(s_mutex);
        return pdFAIL;
    }

    entry->subscribers[entry->count++] = rxQueue;
    printf("[BUS] Subscribed to topic %d (slot %d)\n", topic, entry->count - 1);

    xSemaphoreGive(s_mutex);
    return pdPASS;
}

/* ── Bus_Publish ──────────────────────────────────────────────.────────── */
BaseType_t Bus_Publish(const Event_t *event)
{
    configASSERT(event != NULL);
    configASSERT(event->topic < TOPIC_MAX);

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    TopicEntry_t *entry = &s_registry[event->topic];
    BaseType_t    result = pdPASS;

    for (uint8_t i = 0; i < entry->count; i++) {
        /* Non-blocking: never stall publisher for a slow subscriber */
        if (xQueueSend(entry->subscribers[i], event, 0) != pdPASS) {
            s_dropCount++; 
            // printf("[BUS] WARN: dropped event topic=%d for subscriber %d "
            //        "(total drops=%lu)\n",
            //        event->topic, i, (unsigned long)s_dropCount);
            result = pdFAIL;
        }
    }

    xSemaphoreGive(s_mutex);
    return result;
}

/* ── Bus_PublishFromISR ───────────────────────────────────────────────── */
BaseType_t Bus_PublishFromISR(const Event_t *event,
                               BaseType_t *pxHigherPriorityTaskWoken)
{
    configASSERT(event != NULL);
    configASSERT(event->topic < TOPIC_MAX);

    TopicEntry_t *entry  = &s_registry[event->topic];
    BaseType_t    result = pdPASS;

    /* NOTE: no mutex here – ISR context. Registry must be stable at ISR time.
     * This is safe because subscriptions are set up before the scheduler
     * starts and never modified at runtime.                                */
    for (uint8_t i = 0; i < entry->count; i++) {
        if (xQueueSendFromISR(entry->subscribers[i], event,
                              pxHigherPriorityTaskWoken) != pdPASS) {
            s_dropCount++;
            result = pdFAIL;
        }
    }

    return result;
}

/* ── Bus_GetDropCount ─────────────────────────────────────────────────── */
uint32_t Bus_GetDropCount(void)
{
    return s_dropCount;
}
