#include <stdio.h>

#include "telemetry_encoder.h"

const char *Telemetry_TopicName(EventTopic_t topic)
{
    static const char *names[] = {
        "TEMP_UPDATE", "BUTTON_PRESS", "SYSTEM_FAULT", "HEARTBEAT",
        "SENSOR_READING", "THRESHOLD_CROSSED", "DEVICE_STATUS",
        "CONFIG_UPDATE", "COMMAND_RECEIVED", "ACTUATOR_COMMAND",
        "ACTUATOR_STATUS", "STORAGE_LOG", "NETWORK_STATUS",
        "DIAGNOSTIC_REPORT", "POWER_EVENT", "OTA_EVENT",
        "SECURITY_EVENT", "FRAME_READY"
    };

    if (topic < TOPIC_MAX) {
        return names[topic];
    }
    return "UNKNOWN";
}

int Telemetry_EncodeJson(const Event_t *event, char *out, size_t outSize)
{
    if ((event == NULL) || (out == NULL) || (outSize == 0U)) {
        return -1;
    }

    return snprintf(out, outSize,
                    "{\"id\":%lu,\"seq\":%lu,\"ts\":%lu,\"topic\":\"%s\","
                    "\"src\":%u,\"prio\":%u,\"p0\":%u,\"p1\":%u,\"p2\":%u}",
                    (unsigned long)event->eventId,
                    (unsigned long)event->sequence,
                    (unsigned long)event->timestamp,
                    Telemetry_TopicName(event->topic),
                    event->sourceTask,
                    event->priority,
                    event->payload[0], event->payload[1], event->payload[2]);
}
