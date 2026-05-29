#ifndef STORAGE_BACKEND_H
#define STORAGE_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"

void StorageBackend_Init(void);
BaseType_t StorageBackend_ReadConfig(void *buffer, size_t size, size_t *bytesRead);
BaseType_t StorageBackend_WriteConfig(const void *buffer, size_t size);
BaseType_t StorageBackend_AppendLog(const char *line);

#endif /* STORAGE_BACKEND_H */
