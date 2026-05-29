#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"

#include "event_bus.h"

/* -- Config ------------------------------------------------------------- */
#define MAX_SUBSCRIBERS_PER_TOPIC   6

/* -- Internal registry -------------------------------------------------- */
typedef struct {
    QueueHandle_t    subscribers[MAX_SUBSCRIBERS_PER_TOPIC];
    uint8_t          count;
    SemaphoreHandle_t lock;
} TopicEntry_t;

static TopicEntry_t      s_registry[TOPIC_MAX];
static SemaphoreHandle_t s_dropLock;
static uint32_t          s_dropCount = 0;

static void incrementDropCount(void)
{
    xSemaphoreTake(s_dropLock, portMAX_DELAY);
    s_dropCount++;
    xSemaphoreGive(s_dropLock);
}

/* -- Bus_Init ----------------------------------------------------------- */
void Bus_Init(void)
{
    memset(s_registry, 0, sizeof(s_registry));
    s_dropCount = 0;
    s_dropLock = xSemaphoreCreateMutex();
    configASSERT(s_dropLock != NULL);

    for (uint8_t i = 0; i < TOPIC_MAX; i++) {
        s_registry[i].lock = xSemaphoreCreateMutex();
        configASSERT(s_registry[i].lock != NULL);
    }

    printf("[BUS] Initialized. %d topics registered with per-topic locks.\n",
           (int)TOPIC_MAX);
}

/* -- Bus_Subscribe ------------------------------------------------------ */
BaseType_t Bus_Subscribe(EventTopic_t topic, QueueHandle_t rxQueue)
{
    configASSERT(topic < TOPIC_MAX);
    configASSERT(rxQueue != NULL);

    TopicEntry_t *entry = &s_registry[topic];

    xSemaphoreTake(entry->lock, portMAX_DELAY);

    if (entry->count >= MAX_SUBSCRIBERS_PER_TOPIC) {
        printf("[BUS] ERROR: subscriber slots full for topic %d\n", topic);
        xSemaphoreGive(entry->lock);
        return pdFAIL;
    }

    entry->subscribers[entry->count++] = rxQueue;
    printf("[BUS] Subscribed to topic %d (slot %d)\n", topic, entry->count - 1);

    xSemaphoreGive(entry->lock);
    return pdPASS;
}

/* -- Bus_Publish -------------------------------------------------------- */
BaseType_t Bus_Publish(const Event_t *event)
{
    return Bus_PublishWithCount(event, NULL);
}

BaseType_t Bus_PublishWithCount(const Event_t *event, uint8_t *sentCount)
{
    configASSERT(event != NULL);
    configASSERT(event->topic < TOPIC_MAX);

    TopicEntry_t *entry = &s_registry[event->topic];
    BaseType_t result = pdPASS;
    uint8_t accepted = 0;

    xSemaphoreTake(entry->lock, portMAX_DELAY);

    for (uint8_t i = 0; i < entry->count; i++) {
        /* Non-blocking: never stall publisher for a slow subscriber. */
        if (xQueueSend(entry->subscribers[i], event, 0) == pdPASS) {
            accepted++;
        } else {
            incrementDropCount();
            printf("[BUS] WARN: dropped event topic=%d for subscriber %d "
                   "(total drops=%lu)\n",
                   event->topic, i, (unsigned long)Bus_GetDropCount());
            result = pdFAIL;
        }
    }

    xSemaphoreGive(entry->lock);

    if (sentCount != NULL) {
        *sentCount = accepted;
    }

    return result;
}

/* -- Bus_PublishFromISR ------------------------------------------------- */
BaseType_t Bus_PublishFromISR(const Event_t *event,
                               BaseType_t *pxHigherPriorityTaskWoken)
{
    configASSERT(event != NULL);
    configASSERT(event->topic < TOPIC_MAX);

    TopicEntry_t *entry  = &s_registry[event->topic];
    BaseType_t    result = pdPASS;

    /* ISR path intentionally does not take a mutex. Subscriptions are created
     * before the scheduler starts and remain stable at runtime. */
    for (uint8_t i = 0; i < entry->count; i++) {
        if (xQueueSendFromISR(entry->subscribers[i], event,
                              pxHigherPriorityTaskWoken) != pdPASS) {
            s_dropCount++;
            result = pdFAIL;
        }
    }

    return result;
}

/* -- Bus_GetDropCount --------------------------------------------------- */
uint32_t Bus_GetDropCount(void)
{
    uint32_t count;

    xSemaphoreTake(s_dropLock, portMAX_DELAY);
    count = s_dropCount;
    xSemaphoreGive(s_dropLock);

    return count;
}

uint8_t Bus_GetSubscriberCount(EventTopic_t topic)
{
    configASSERT(topic < TOPIC_MAX);

    TopicEntry_t *entry = &s_registry[topic];
    uint8_t count;

    xSemaphoreTake(entry->lock, portMAX_DELAY);
    count = entry->count;
    xSemaphoreGive(entry->lock);

    return count;
}
