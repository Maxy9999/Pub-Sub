#ifndef EVENT_TYPES_H
#define EVENT_TYPES_H

#include <stdint.h>
#include <string.h>

#include "event_bus.h"

typedef enum {
    EVENT_PRIORITY_LOW = 0,
    EVENT_PRIORITY_NORMAL,
    EVENT_PRIORITY_HIGH,
    EVENT_PRIORITY_CRITICAL
} EventPriority_t;

typedef enum {
    UNIT_NONE = 0,
    UNIT_CELSIUS,
    UNIT_PERCENT_RH,
    UNIT_DECIVOLTS
} UnitId_t;

typedef struct {
    uint8_t sensorId;
    int16_t value;
    uint8_t unit;
    uint8_t quality;
} SensorReadingPayload_t;

typedef struct {
    uint8_t signalId;
    uint8_t state;
    uint16_t detail;
} StatusPayload_t;

typedef struct {
    uint8_t key;
    uint32_t value;
} ConfigUpdatePayload_t;

typedef struct {
    uint8_t commandId;
    uint8_t targetId;
    uint32_t value;
} CommandPayload_t;

typedef struct {
    void *ptr;
} PointerPayload_t;

#define CONFIG_KEY_TEMP_THRESHOLD      1U
#define CONFIG_KEY_DIAG_PERIOD_MS      2U
#define CONFIG_KEY_LOW_VOLTAGE_DV      3U
#define CONFIG_KEY_FRAME_PERIOD_MS     4U

#define COMMAND_ID_SET_CONFIG          1U
#define COMMAND_ID_ACTUATOR            2U
#define COMMAND_ID_BUTTON              3U
#define COMMAND_ID_OTA                 4U
#define COMMAND_ID_SECURITY_EVENT      5U
#define COMMAND_ID_GET_STATUS          6U

static inline void Event_Pack(Event_t *event, const void *src, uint8_t size)
{
    memset(event->payload, 0, sizeof(event->payload));
    if (size > sizeof(event->payload)) {
        size = sizeof(event->payload);
    }
    memcpy(event->payload, src, size);
}

static inline void Event_Unpack(const Event_t *event, void *dst, uint8_t size)
{
    if (size > sizeof(event->payload)) {
        size = sizeof(event->payload);
    }
    memcpy(dst, event->payload, size);
}

#endif /* EVENT_TYPES_H */
