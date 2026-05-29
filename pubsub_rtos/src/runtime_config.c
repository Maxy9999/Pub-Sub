#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include "runtime_config.h"

#define CONFIG_MAGIC      0x43464731UL  /* CFG1 */
#define CONFIG_VERSION    1U
#define CONFIG_NVM_PATH   "device_config.nvm"

static DeviceConfig_t s_config;
static SemaphoreHandle_t s_configLock;

static uint32_t configChecksum(const DeviceConfig_t *config)
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
    config->version = CONFIG_VERSION;
    config->tempThreshold = CONFIG_DEFAULT_TEMP_THRESHOLD;
    config->lowVoltageDecivolts = CONFIG_DEFAULT_LOW_VOLTAGE_DV;
    config->diagnosticPeriodMs = CONFIG_DEFAULT_DIAG_PERIOD_MS;
    config->framePeriodMs = CONFIG_DEFAULT_FRAME_PERIOD_MS;
    config->bootCount = 0;
    config->checksum = configChecksum(config);
}

static BaseType_t configIsValid(const DeviceConfig_t *config)
{
    if ((config->magic != CONFIG_MAGIC) || (config->version != CONFIG_VERSION)) {
        return pdFALSE;
    }

    return (config->checksum == configChecksum(config)) ? pdTRUE : pdFALSE;
}

void Config_Init(void)
{
    DeviceConfig_t loaded;
    FILE *file;

    s_configLock = xSemaphoreCreateMutex();
    configASSERT(s_configLock != NULL);

    file = fopen(CONFIG_NVM_PATH, "rb");
    if ((file != NULL) &&
        (fread(&loaded, sizeof(loaded), 1, file) == 1U) &&
        (configIsValid(&loaded) == pdTRUE)) {
        s_config = loaded;
        printf("[CONFIG_STORE] Loaded runtime config from %s\n", CONFIG_NVM_PATH);
    } else {
        configLoadDefaults(&s_config);
        printf("[CONFIG_STORE] Using default runtime config\n");
    }

    if (file != NULL) {
        fclose(file);
    }

    s_config.bootCount++;
    s_config.checksum = configChecksum(&s_config);
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
    s_config.checksum = configChecksum(&s_config);
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
    s_config.checksum = configChecksum(&s_config);
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
    s_config.checksum = configChecksum(&s_config);
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
    s_config.checksum = configChecksum(&s_config);
    xSemaphoreGive(s_configLock);
    return Config_Save();
}

BaseType_t Config_Save(void)
{
    DeviceConfig_t snapshot;
    FILE *file;
    BaseType_t result = pdFAIL;

    xSemaphoreTake(s_configLock, portMAX_DELAY);
    snapshot = s_config;
    xSemaphoreGive(s_configLock);

    file = fopen(CONFIG_NVM_PATH, "wb");
    if (file != NULL) {
        result = (fwrite(&snapshot, sizeof(snapshot), 1, file) == 1U) ? pdPASS : pdFAIL;
        fclose(file);
    }

    if (result != pdPASS) {
        printf("[CONFIG_STORE] WARN: failed to save %s\n", CONFIG_NVM_PATH);
    }

    return result;
}
