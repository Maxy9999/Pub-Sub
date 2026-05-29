#ifndef COMMAND_PARSER_H
#define COMMAND_PARSER_H

#include <stdint.h>

#include "event_types.h"

typedef struct {
    uint8_t commandId;
    uint8_t targetId;
    uint32_t value;
} ParsedCommand_t;

int CommandParser_Parse(const char *line, ParsedCommand_t *outCommand);

#endif /* COMMAND_PARSER_H */
