#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "event_bus.h"

typedef enum {
    DEVICE_STATE_BOOTING = 0,
    DEVICE_STATE_NORMAL,
    DEVICE_STATE_DEGRADED,
    DEVICE_STATE_FAULT,
    DEVICE_STATE_RECOVERY
} DeviceState_t;

typedef enum {
    FAULT_SEVERITY_INFO = 0,
    FAULT_SEVERITY_WARNING,
    FAULT_SEVERITY_ERROR,
    FAULT_SEVERITY_CRITICAL
} FaultSeverity_t;

typedef struct {
    TickType_t timestamp;
    EventTopic_t topic;
    uint8_t code;
    FaultSeverity_t severity;
} FaultRecord_t;

void FaultManager_Init(void);
BaseType_t FaultManager_HandleEvent(const Event_t *event, DeviceState_t *outState);
DeviceState_t FaultManager_GetState(void);
uint8_t FaultManager_GetHistory(FaultRecord_t *outRecords, uint8_t maxRecords);

#endif /* FAULT_MANAGER_H */
