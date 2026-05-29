#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "runtime_config.h"
#include "storage_backend.h"

#define CONFIG_MAGIC      0x43464732UL  /* CFG2 */
#define CONFIG_V1_MAGIC   0x43464731UL  /* CFG1 */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  tempThreshold;
    uint8_t  lowVoltageDecivolts;
    uint32_t diagnosticPeriodMs;
    uint32_t framePeriodMs;
    uint32_t bootCount;
    uint32_t checksum;
} DeviceConfigV1_t;

static DeviceConfig_t s_config;
static SemaphoreHandle_t s_configLock;

uint32_t Config_Crc32ForTest(const void *data, uint32_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= bytes[i];
        for (uint8_t bit = 0; bit < 8U; bit++) {
            uint32_t mask = (uint32_t)(0U - (crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

static uint32_t configCrc32(const DeviceConfig_t *config)
{
    return Config_Crc32ForTest(config, sizeof(*config) - sizeof(config->crc32));
}

static uint32_t configV1Checksum(const DeviceConfigV1_t *config)
{
    const uint8_t *bytes = (const uint8_t *)config;
    size_t len = sizeof(*config) - sizeof(config->checksum);
    uint32_t sum = 0;

    for (size_t i = 0; i < len; i++) {
        sum = (sum * 33U) ^ bytes[i];
    }

    return sum;
}

static void configLoadDefaults(DeviceConfig_t *config)
{
    memset(config, 0, sizeof(*config));
    config->magic = CONFIG_MAGIC;
    config->version = CONFIG_SCHEMA_VERSION;
    config->size = sizeof(*config);
    config->tempThreshold = CONFIG_DEFAULT_TEMP_THRESHOLD;
    config->lowVoltageDecivolts = CONFIG_DEFAULT_LOW_VOLTAGE_DV;
    config->diagnosticPeriodMs = CONFIG_DEFAULT_DIAG_PERIOD_MS;
    config->framePeriodMs = CONFIG_DEFAULT_FRAME_PERIOD_MS;
    config->bootCount = 0;
    config->crc32 = configCrc32(config);
}

static BaseType_t configIsValid(const DeviceConfig_t *config)
{
    if ((config->magic != CONFIG_MAGIC) ||
        (config->version != CONFIG_SCHEMA_VERSION) ||
        (config->size != sizeof(*config))) {
        return pdFALSE;
    }

    return (config->crc32 == configCrc32(config)) ? pdTRUE : pdFALSE;
}

static BaseType_t configMigrateV1(const DeviceConfigV1_t *oldConfig,
                                  DeviceConfig_t *newConfig)
{
    if ((oldConfig->magic != CONFIG_V1_MAGIC) ||
        (oldConfig->version != 1U) ||
        (oldConfig->checksum != configV1Checksum(oldConfig))) {
        return pdFAIL;
    }

    configLoadDefaults(newConfig);
    newConfig->tempThreshold = oldConfig->tempThreshold;
    newConfig->lowVoltageDecivolts = oldConfig->lowVoltageDecivolts;
    newConfig->diagnosticPeriodMs = oldConfig->diagnosticPeriodMs;
    newConfig->framePeriodMs = oldConfig->framePeriodMs;
    newConfig->bootCount = oldConfig->bootCount;
    newConfig->crc32 = configCrc32(newConfig);
    return pdPASS;
}

void Config_Init(void)
{
    uint8_t raw[sizeof(DeviceConfig_t)] = {0};
    size_t bytesRead = 0;
    BaseType_t loaded = pdFAIL;

    s_configLock = xSemaphoreCreateMutex();
    configASSERT(s_configLock != NULL);
    StorageBackend_Init();

    if (StorageBackend_ReadConfig(raw, sizeof(raw), &bytesRead) == pdPASS) {
        if (bytesRead == sizeof(DeviceConfig_t)) {
            DeviceConfig_t candidate;
            memcpy(&candidate, raw, sizeof(candidate));
            if (configIsValid(&candidate) == pdTRUE) {
                s_config = candidate;
                loaded = pdPASS;
                printf("[CONFIG_STORE] Loaded v%u runtime config\n", s_config.version);
            }
        } else if (bytesRead == sizeof(DeviceConfigV1_t)) {
            DeviceConfigV1_t oldConfig;
            memcpy(&oldConfig, raw, sizeof(oldConfig));
            if (configMigrateV1(&oldConfig, &s_config) == pdPASS) {
                loaded = pdPASS;
                printf("[CONFIG_STORE] Migrated v1 config to v%u\n", CONFIG_SCHEMA_VERSION);
            }
        }
    }

    if (loaded != pdPASS) {
        configLoadDefaults(&s_config);
        printf("[CONFIG_STORE] Using default runtime config\n");
    }

    s_config.bootCount++;
    s_config.crc32 = configCrc32(&s_config);
    Config_Save();
}

void Config_GetSnapshot(DeviceConfig_t *outConfig)
{
    configASSERT(outConfig != NULL);

    xSemaphoreTake(s_configLock, portMAX_DELAY);
    *outConfig = s_config;
    xSemaphoreGive(s_configLock);
}

BaseType_t Config_SetTempThreshold(uint8_t threshold)
{
    if ((threshold < 5U) || (threshold > 80U)) {
        return pdFAIL;
    }

    xSemaphoreTake(s_configLock, portMAX_DELAY);
    s_config.tempThreshold = threshold;
    s_config.crc32 = configCrc32(&s_config);
    xSemaphoreGive(s_configLock);
    return Config_Save();
}

BaseType_t Config_SetLowVoltageThreshold(uint8_t decivolts)
{
    if ((decivolts < 20U) || (decivolts > 50U)) {
        return pdFAIL;
    }

    xSemaphoreTake(s_configLock, portMAX_DELAY);
    s_config.lowVoltageDecivolts = decivolts;
    s_config.crc32 = configCrc32(&s_config);
    xSemaphoreGive(s_configLock);
    return Config_Save();
}

BaseType_t Config_SetDiagnosticPeriodMs(uint32_t periodMs)
{
    if ((periodMs < 1000UL) || (periodMs > 60000UL)) {
        return pdFAIL;
    }

    xSemaphoreTake(s_configLock, portMAX_DELAY);
    s_config.diagnosticPeriodMs = periodMs;
    s_config.crc32 = configCrc32(&s_config);
    xSemaphoreGive(s_configLock);
    return Config_Save();
}

BaseType_t Config_SetFramePeriodMs(uint32_t periodMs)
{
    if ((periodMs < 500UL) || (periodMs > 30000UL)) {
        return pdFAIL;
    }

    xSemaphoreTake(s_configLock, portMAX_DELAY);
    s_config.framePeriodMs = periodMs;
    s_config.crc32 = configCrc32(&s_config);
    xSemaphoreGive(s_configLock);
    return Config_Save();
}

BaseType_t Config_Save(void)
{
    DeviceConfig_t snapshot;
    BaseType_t result;

    xSemaphoreTake(s_configLock, portMAX_DELAY);
    s_config.size = sizeof(s_config);
    s_config.version = CONFIG_SCHEMA_VERSION;
    s_config.magic = CONFIG_MAGIC;
    s_config.crc32 = configCrc32(&s_config);
    snapshot = s_config;
    xSemaphoreGive(s_configLock);

    result = StorageBackend_WriteConfig(&snapshot, sizeof(snapshot));
    if (result != pdPASS) {
        printf("[CONFIG_STORE] WARN: failed to save runtime config\n");
    }

    return result;
}
