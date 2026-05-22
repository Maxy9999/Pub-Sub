#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "FreeRTOS.h"
#include "queue.h"

/* ── Topics ───────────────────────────────────────────────────────────── */
typedef enum {
    TOPIC_TEMP_UPDATE   = 0,
    TOPIC_BUTTON_PRESS,
    TOPIC_SYSTEM_FAULT,
    TOPIC_HEARTBEAT,
    TOPIC_MAX           /* always last – used as array size */
} EventTopic_t;

/* ── Fixed-size event struct (safe to copy through queues) ────────────── */
#define EVENT_PAYLOAD_SIZE  16

typedef struct {
    EventTopic_t topic;
    TickType_t   timestamp;              /* xTaskGetTickCount() at publish */
    uint8_t      payload[EVENT_PAYLOAD_SIZE];
} Event_t;

/* Convenience: cast payload to a typed pointer                           */
#define EVENT_PAYLOAD(evt, type)   ((type *)((evt)->payload))

/* ── Bus API ──────────────────────────────────────────────────────────── */

/**
 * Bus_Init – call once before vTaskStartScheduler().
 */
void Bus_Init(void);

/**
 * Bus_Subscribe – register rxQueue to receive events on topic.
 * Returns pdPASS on success, pdFAIL if subscriber slots are full.
 * Call from task init, before the scheduler starts.
 */
BaseType_t Bus_Subscribe(EventTopic_t topic, QueueHandle_t rxQueue);

/**
 * Bus_Publish – fan-out event to all subscribers of event->topic.
 * Non-blocking: slow/full subscriber queues drop the event (logged).
 * Safe to call from tasks; use Bus_PublishFromISR from ISR context.
 */
BaseType_t Bus_Publish(const Event_t *event);

/**
 * Bus_PublishFromISR – ISR-safe variant.
 * pxHigherPriorityTaskWoken follows FreeRTOS convention.
 */
BaseType_t Bus_PublishFromISR(const Event_t *event,
                               BaseType_t *pxHigherPriorityTaskWoken);

/**
 * Bus_GetDropCount – total events dropped due to full subscriber queues.
 */
uint32_t Bus_GetDropCount(void);

#endif /* EVENT_BUS_H */
