#ifndef RUNTIME_CONFIG_H
#define RUNTIME_CONFIG_H

#include <stdint.h>

#include "FreeRTOS.h"

#define CONFIG_DEFAULT_TEMP_THRESHOLD       35U
#define CONFIG_DEFAULT_LOW_VOLTAGE_DV       33U
#define CONFIG_DEFAULT_DIAG_PERIOD_MS       5000U
#define CONFIG_DEFAULT_FRAME_PERIOD_MS      2800U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  tempThreshold;
    uint8_t  lowVoltageDecivolts;
    uint32_t diagnosticPeriodMs;
    uint32_t framePeriodMs;
    uint32_t bootCount;
    uint32_t checksum;
} DeviceConfig_t;

void Config_Init(void);
void Config_GetSnapshot(DeviceConfig_t *outConfig);
BaseType_t Config_SetTempThreshold(uint8_t threshold);
BaseType_t Config_SetLowVoltageThreshold(uint8_t decivolts);
BaseType_t Config_SetDiagnosticPeriodMs(uint32_t periodMs);
BaseType_t Config_SetFramePeriodMs(uint32_t periodMs);
BaseType_t Config_Save(void);

#endif /* RUNTIME_CONFIG_H */
