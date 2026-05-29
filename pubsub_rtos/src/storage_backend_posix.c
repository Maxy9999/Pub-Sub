#include <stdio.h>

#include "storage_backend.h"

#define CONFIG_NVM_PATH   "device_config.nvm"
#define EVENT_LOG_PATH    "event_log.txt"

void StorageBackend_Init(void)
{
    /* POSIX simulator backend. Real ports replace this file with flash/NVS. */
}

BaseType_t StorageBackend_ReadConfig(void *buffer, size_t size, size_t *bytesRead)
{
    FILE *file = fopen(CONFIG_NVM_PATH, "rb");
    if (bytesRead != NULL) {
        *bytesRead = 0U;
    }

    if (file == NULL) {
        return pdFAIL;
    }

    size_t readCount = fread(buffer, 1U, size, file);
    fclose(file);

    if (bytesRead != NULL) {
        *bytesRead = readCount;
    }

    return (readCount > 0U) ? pdPASS : pdFAIL;
}

BaseType_t StorageBackend_WriteConfig(const void *buffer, size_t size)
{
    FILE *file = fopen(CONFIG_NVM_PATH, "wb");
    if (file == NULL) {
        return pdFAIL;
    }

    size_t wrote = fwrite(buffer, 1U, size, file);
    fclose(file);
    return (wrote == size) ? pdPASS : pdFAIL;
}

BaseType_t StorageBackend_AppendLog(const char *line)
{
    FILE *file = fopen(EVENT_LOG_PATH, "a");
    if (file == NULL) {
        return pdFAIL;
    }

    fputs(line, file);
    fputc('\n', file);
    fclose(file);
    return pdPASS;
}
