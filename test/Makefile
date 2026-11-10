# Host-side tests for the Lua core in ../src/lua. Not part of the firmware
# build: PlatformIO compiles only src/.
#
#   make -C test                     build and run
#   make -C test CFLAGS='-O1 -g -fsanitize=address,undefined'

LUA_DIR = ../src/lua
LUA_SRC = $(filter-out $(LUA_DIR)/lua.c $(LUA_DIR)/luac.c, \
            $(wildcard $(LUA_DIR)/*.c))

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
ALL_CFLAGS = $(CFLAGS) -std=gnu99 -I$(LUA_DIR)

BUILD = build

.PHONY: all test clean
all: test

$(BUILD)/hook_delivery: hook_delivery.c $(LUA_SRC) $(wildcard $(LUA_DIR)/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(ALL_CFLAGS) -o $@ hook_delivery.c $(LUA_SRC) -lm

test: $(BUILD)/hook_delivery
	./$(BUILD)/hook_delivery

clean:
	rm -rf $(BUILD)
