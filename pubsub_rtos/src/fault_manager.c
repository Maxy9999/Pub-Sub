#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "fault_manager.h"

#define FAULT_HISTORY_DEPTH       8U
#define FAULT_DEBOUNCE_TICKS      pdMS_TO_TICKS(2000)

static SemaphoreHandle_t s_faultLock;
static FaultRecord_t s_history[FAULT_HISTORY_DEPTH];
static uint8_t s_next;
static DeviceState_t s_state;
static EventTopic_t s_lastTopic;
static uint8_t s_lastCode;
static TickType_t s_lastTick;

static FaultSeverity_t classifyFault(const Event_t *event)
{
    switch (event->topic) {
    case TOPIC_SYSTEM_FAULT:
        return FAULT_SEVERITY_CRITICAL;
    case TOPIC_SECURITY_EVENT:
        return FAULT_SEVERITY_ERROR;
    case TOPIC_POWER_EVENT:
    case TOPIC_NETWORK_STATUS:
    case TOPIC_THRESHOLD_CROSSED:
        return event->payload[0] ? FAULT_SEVERITY_WARNING : FAULT_SEVERITY_INFO;
    default:
        return FAULT_SEVERITY_INFO;
    }
}

static DeviceState_t stateForSeverity(FaultSeverity_t severity)
{
    switch (severity) {
    case FAULT_SEVERITY_CRITICAL:
    case FAULT_SEVERITY_ERROR:
        return DEVICE_STATE_FAULT;
    case FAULT_SEVERITY_WARNING:
        return DEVICE_STATE_DEGRADED;
    default:
        return DEVICE_STATE_NORMAL;
    }
}

void FaultManager_Init(void)
{
    s_faultLock = xSemaphoreCreateMutex();
    configASSERT(s_faultLock != NULL);
    memset(s_history, 0, sizeof(s_history));
    s_next = 0;
    s_state = DEVICE_STATE_BOOTING;
    s_lastTopic = TOPIC_MAX;
    s_lastCode = 0;
    s_lastTick = 0;
}

BaseType_t FaultManager_HandleEvent(const Event_t *event, DeviceState_t *outState)
{
    configASSERT(event != NULL);

    TickType_t now = xTaskGetTickCount();
    FaultSeverity_t severity = classifyFault(event);
    BaseType_t accepted = pdTRUE;

    xSemaphoreTake(s_faultLock, portMAX_DELAY);

    if ((event->topic == s_lastTopic) &&
        (event->payload[0] == s_lastCode) &&
        ((now - s_lastTick) < FAULT_DEBOUNCE_TICKS)) {
        accepted = pdFALSE;
    } else {
        s_lastTopic = event->topic;
        s_lastCode = event->payload[0];
        s_lastTick = now;
        s_state = stateForSeverity(severity);

        if (severity != FAULT_SEVERITY_INFO) {
            s_history[s_next].timestamp = now;
            s_history[s_next].topic = event->topic;
            s_history[s_next].code = event->payload[0];
            s_history[s_next].severity = severity;
            s_next = (uint8_t)((s_next + 1U) % FAULT_HISTORY_DEPTH);
        }
    }

    if (outState != NULL) {
        *outState = s_state;
    }

    xSemaphoreGive(s_faultLock);
    return accepted;
}

DeviceState_t FaultManager_GetState(void)
{
    DeviceState_t state;
    xSemaphoreTake(s_faultLock, portMAX_DELAY);
    state = s_state;
    xSemaphoreGive(s_faultLock);
    return state;
}

uint8_t FaultManager_GetHistory(FaultRecord_t *outRecords, uint8_t maxRecords)
{
    uint8_t count = (maxRecords < FAULT_HISTORY_DEPTH) ? maxRecords : FAULT_HISTORY_DEPTH;
    if (outRecords == NULL) {
        return 0;
    }

    xSemaphoreTake(s_faultLock, portMAX_DELAY);
    memcpy(outRecords, s_history, count * sizeof(FaultRecord_t));
    xSemaphoreGive(s_faultLock);
    return count;
}
