#include <stdio.h>
#include <string.h>

#include "command_parser.h"
#include "event_types.h"
#include "telemetry_encoder.h"

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_command_parser(void)
{
    ParsedCommand_t cmd;

    EXPECT_TRUE(CommandParser_Parse("SET temp_threshold 42", &cmd));
    EXPECT_TRUE(cmd.commandId == COMMAND_ID_SET_CONFIG);
    EXPECT_TRUE(cmd.targetId == CONFIG_KEY_TEMP_THRESHOLD);
    EXPECT_TRUE(cmd.value == 42U);

    EXPECT_TRUE(CommandParser_Parse("ACTUATOR fan 1", &cmd));
    EXPECT_TRUE(cmd.commandId == COMMAND_ID_ACTUATOR);
    EXPECT_TRUE(cmd.targetId == 1U);
    EXPECT_TRUE(cmd.value == 1U);

    EXPECT_TRUE(!CommandParser_Parse("SET unknown 1", &cmd));
    return 0;
}

static int test_event_pack_unpack(void)
{
    Event_t evt = {0};
    SensorReadingPayload_t in = {
        .sensorId = 7,
        .value = -123,
        .unit = UNIT_CELSIUS,
        .quality = 99
    };
    SensorReadingPayload_t out = {0};

    Event_Pack(&evt, &in, sizeof(in));
    Event_Unpack(&evt, &out, sizeof(out));

    EXPECT_TRUE(out.sensorId == in.sensorId);
    EXPECT_TRUE(out.value == in.value);
    EXPECT_TRUE(out.unit == in.unit);
    EXPECT_TRUE(out.quality == in.quality);
    return 0;
}

static int test_telemetry_encoder(void)
{
    char json[192];
    Event_t evt = {
        .topic = TOPIC_SENSOR_READING,
        .timestamp = 123,
        .eventId = 9,
        .sequence = 4,
        .priority = EVENT_PRIORITY_HIGH,
        .sourceTask = 2,
        .payload = {1, 2, 3}
    };

    EXPECT_TRUE(Telemetry_EncodeJson(&evt, json, sizeof(json)) > 0);
    EXPECT_TRUE(strstr(json, "SENSOR_READING") != NULL);
    EXPECT_TRUE(strstr(json, "\"id\":9") != NULL);
    return 0;
}

int main(void)
{
    EXPECT_TRUE(test_command_parser() == 0);
    EXPECT_TRUE(test_event_pack_unpack() == 0);
    EXPECT_TRUE(test_telemetry_encoder() == 0);
    printf("middleware tests passed\n");
    return 0;
}
