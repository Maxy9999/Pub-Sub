#ifndef PAYLOAD_POOL_H
#define PAYLOAD_POOL_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"

#define PAYLOAD_POOL_BLOCKS    8U

/* Descriptor for high-throughput payloads. The large frame/data buffer stays
 * outside Event_t; events carry only a pointer to one of these descriptors. */
typedef struct {
    void    *framePtr;
    size_t   frameSize;
    uint32_t sequence;
    uint8_t  sourceId;
} EventPayload_t;

void Pool_Init(void);
EventPayload_t *Pool_Alloc(void);
void Pool_AddRef(EventPayload_t *payload);
void Pool_SetRefCount(EventPayload_t *payload, uint8_t refCount);
void Pool_Free(EventPayload_t *payload);
uint8_t Pool_GetFreeCount(void);

#endif /* PAYLOAD_POOL_H */
