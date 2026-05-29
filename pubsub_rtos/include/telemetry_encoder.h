#ifndef TELEMETRY_ENCODER_H
#define TELEMETRY_ENCODER_H

#include <stddef.h>

#include "event_bus.h"

const char *Telemetry_TopicName(EventTopic_t topic);
int Telemetry_EncodeJson(const Event_t *event, char *out, size_t outSize);

#endif /* TELEMETRY_ENCODER_H */
