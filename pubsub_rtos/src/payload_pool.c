#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "payload_pool.h"

typedef struct {
    EventPayload_t payload;
    uint8_t inUse;
    uint8_t refCount;
} PayloadBlock_t;

static PayloadBlock_t s_blocks[PAYLOAD_POOL_BLOCKS];
static SemaphoreHandle_t s_poolLock;

static int blockIndexFromPayload(const EventPayload_t *payload)
{
    for (uint8_t i = 0; i < PAYLOAD_POOL_BLOCKS; i++) {
        if (&s_blocks[i].payload == payload) {
            return i;
        }
    }
    return -1;
}

void Pool_Init(void)
{
    memset(s_blocks, 0, sizeof(s_blocks));
    s_poolLock = xSemaphoreCreateMutex();
    configASSERT(s_poolLock != NULL);
    printf("[POOL] Initialized with %u payload descriptors\n",
           (unsigned)PAYLOAD_POOL_BLOCKS);
}

EventPayload_t *Pool_Alloc(void)
{
    EventPayload_t *result = NULL;

    xSemaphoreTake(s_poolLock, portMAX_DELAY);
    for (uint8_t i = 0; i < PAYLOAD_POOL_BLOCKS; i++) {
        if (!s_blocks[i].inUse) {
            memset(&s_blocks[i].payload, 0, sizeof(s_blocks[i].payload));
            s_blocks[i].inUse = 1;
            s_blocks[i].refCount = 1;
            result = &s_blocks[i].payload;
            break;
        }
    }
    xSemaphoreGive(s_poolLock);

    if (result == NULL) {
        printf("[POOL] WARN: payload pool exhausted\n");
    }

    return result;
}

void Pool_AddRef(EventPayload_t *payload)
{
    if (payload == NULL) {
        return;
    }

    xSemaphoreTake(s_poolLock, portMAX_DELAY);
    int index = blockIndexFromPayload(payload);
    if ((index >= 0) && s_blocks[index].inUse) {
        s_blocks[index].refCount++;
    }
    xSemaphoreGive(s_poolLock);
}

void Pool_SetRefCount(EventPayload_t *payload, uint8_t refCount)
{
    if ((payload == NULL) || (refCount == 0U)) {
        return;
    }

    xSemaphoreTake(s_poolLock, portMAX_DELAY);
    int index = blockIndexFromPayload(payload);
    if ((index >= 0) && s_blocks[index].inUse) {
        s_blocks[index].refCount = refCount;
    }
    xSemaphoreGive(s_poolLock);
}

void Pool_Free(EventPayload_t *payload)
{
    if (payload == NULL) {
        return;
    }

    xSemaphoreTake(s_poolLock, portMAX_DELAY);
    int index = blockIndexFromPayload(payload);
    if ((index >= 0) && s_blocks[index].inUse) {
        if (s_blocks[index].refCount > 1U) {
            s_blocks[index].refCount--;
        } else {
            memset(&s_blocks[index], 0, sizeof(s_blocks[index]));
        }
    } else {
        printf("[POOL] WARN: ignored free for foreign payload %p\n", (void *)payload);
    }
    xSemaphoreGive(s_poolLock);
}

uint8_t Pool_GetFreeCount(void)
{
    uint8_t freeCount = 0;

    xSemaphoreTake(s_poolLock, portMAX_DELAY);
    for (uint8_t i = 0; i < PAYLOAD_POOL_BLOCKS; i++) {
        if (!s_blocks[i].inUse) {
            freeCount++;
        }
    }
    xSemaphoreGive(s_poolLock);

    return freeCount;
}
