# netval — build modes:
#   make        release (-O2)
#   make debug  (-g -O0)
#   make asan   (-fsanitize=address,undefined -g)
#   make tsan   (-fsanitize=thread -g)
# Each mode builds into its own directory under build/<mode>/.

CC          ?= gcc
BASE_CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -D_GNU_SOURCE
LDLIBS      := -lpthread

SRC_DIR := src
SRCS    := $(wildcard $(SRC_DIR)/*.c)

MODE      ?= release
BUILD_DIR := build/$(MODE)
BIN       := $(BUILD_DIR)/netval
OBJS      := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
DEPS      := $(OBJS:.o=.d)

release_FLAGS := -O2
debug_FLAGS   := -g -O0
asan_FLAGS    := -fsanitize=address,undefined -g -fno-omit-frame-pointer
tsan_FLAGS    := -fsanitize=thread -g

CFLAGS  := $(BASE_CFLAGS) $($(MODE)_FLAGS)
LDFLAGS := $($(MODE)_FLAGS)

.PHONY: all debug asan tsan build clean

all:
	@$(MAKE) --no-print-directory build MODE=release

debug:
	@$(MAKE) --no-print-directory build MODE=debug

asan:
	@$(MAKE) --no-print-directory build MODE=asan

tsan:
	@$(MAKE) --no-print-directory build MODE=tsan

ifeq ($(SRCS),)
build:
	@echo "netval: no C sources in $(SRC_DIR)/ yet — nothing to build."
else
build: $(BIN)
endif

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf build

-include $(DEPS)
