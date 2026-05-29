#include <stdio.h>
#include <string.h>

#include "command_parser.h"

int CommandParser_Parse(const char *line, ParsedCommand_t *outCommand)
{
    char key[32] = {0};
    unsigned value = 0;

    if ((line == NULL) || (outCommand == NULL)) {
        return 0;
    }

    memset(outCommand, 0, sizeof(*outCommand));

    if (sscanf(line, "SET %31s %u", key, &value) == 2) {
        outCommand->commandId = COMMAND_ID_SET_CONFIG;
        outCommand->value = value;

        if (strcmp(key, "temp_threshold") == 0) {
            outCommand->targetId = CONFIG_KEY_TEMP_THRESHOLD;
        } else if (strcmp(key, "diag_period_ms") == 0) {
            outCommand->targetId = CONFIG_KEY_DIAG_PERIOD_MS;
        } else if (strcmp(key, "low_voltage_dv") == 0) {
            outCommand->targetId = CONFIG_KEY_LOW_VOLTAGE_DV;
        } else if (strcmp(key, "frame_period_ms") == 0) {
            outCommand->targetId = CONFIG_KEY_FRAME_PERIOD_MS;
        } else {
            return 0;
        }
        return 1;
    }

    if (sscanf(line, "ACTUATOR %31s %u", key, &value) == 2) {
        outCommand->commandId = COMMAND_ID_ACTUATOR;
        outCommand->targetId = (strcmp(key, "fan") == 0) ? 1U : 0U;
        outCommand->value = value;
        return outCommand->targetId != 0U;
    }

    if (strcmp(line, "BUTTON 0") == 0) {
        outCommand->commandId = COMMAND_ID_BUTTON;
        return 1;
    }

    if (strcmp(line, "OTA START") == 0) {
        outCommand->commandId = COMMAND_ID_OTA;
        outCommand->value = 1U;
        return 1;
    }

    if (strcmp(line, "SECURITY INVALID_TOKEN") == 0) {
        outCommand->commandId = COMMAND_ID_SECURITY_EVENT;
        outCommand->value = 1U;
        return 1;
    }

    if (strcmp(line, "GET status") == 0) {
        outCommand->commandId = COMMAND_ID_GET_STATUS;
        return 1;
    }

    return 0;
}
