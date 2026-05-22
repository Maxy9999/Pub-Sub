# ── Pub/Sub RTOS Demo — Makefile ──────────────────────────────────────
# Builds against the FreeRTOS POSIX/Linux simulator port.
# Usage:
#   make          – build
#   make run      – build and run
#   make clean    – remove build artifacts

# ── Paths (relative to this Makefile) ─────────────────────────────────
FREERTOS_ROOT := ../FreeRTOS/FreeRTOS
KERNEL_DIR    := $(FREERTOS_ROOT)/Source
POSIX_PORT    := $(KERNEL_DIR)/portable/ThirdParty/GCC/Posix
MEMMANG_DIR   := $(KERNEL_DIR)/portable/MemMang

# ── Project sources ────────────────────────────────────────────────────
SRC_DIR   := src
INC_DIR   := include
CFG_DIR   := freertos_config

APP_SRCS  := $(SRC_DIR)/main.c       \
             $(SRC_DIR)/event_bus.c  \
             $(SRC_DIR)/watchdog_task.c \
             $(SRC_DIR)/app_tasks.c

# ── FreeRTOS kernel sources ────────────────────────────────────────────
KERNEL_SRCS := $(KERNEL_DIR)/tasks.c        \
               $(KERNEL_DIR)/queue.c        \
               $(KERNEL_DIR)/list.c         \
               $(KERNEL_DIR)/timers.c       \
               $(KERNEL_DIR)/event_groups.c \
               $(KERNEL_DIR)/stream_buffer.c\
               $(POSIX_PORT)/port.c         \
               $(POSIX_PORT)/utils/wait_for_event.c \
               $(MEMMANG_DIR)/heap_3.c

ALL_SRCS := $(APP_SRCS) $(KERNEL_SRCS)

# ── Compiler & flags ───────────────────────────────────────────────────
CC      := gcc
TARGET  := pubsub_demo
BUILD   := build

CFLAGS  := -Wall -Wextra -g \
            -I$(INC_DIR) \
            -I$(CFG_DIR) \
            -I$(KERNEL_DIR)/include \
            -I$(POSIX_PORT) \
            -DprojCOVERAGE_TEST=0 \
            -DprojENABLE_TRACING=0

LDFLAGS := -lpthread

OBJS := $(patsubst %.c, $(BUILD)/%.o, $(ALL_SRCS))

# ── Rules ──────────────────────────────────────────────────────────────
.PHONY: all run clean

all: $(BUILD)/$(TARGET)

$(BUILD)/$(TARGET): $(OBJS)
	@echo "  LD  $@"
	@$(CC) $(OBJS) -o $@ $(LDFLAGS)
	@echo "Build successful → $@"

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "  CC  $<"
	@$(CC) $(CFLAGS) -c $< -o $@

run: all
	@echo "\nRunning $(TARGET)...\n"
	./$(BUILD)/$(TARGET)

clean:
	rm -rf $(BUILD)
	@echo "Cleaned."
