# OpenFinger (fpx) build. Only GNU make and a C17 compiler are required.

CC       ?= gcc
AR       ?= ar
PREFIX   ?= /usr/local
BUILD    := build

CFLAGS   ?= -O2 -g
CFLAGS   += -std=c17 -Wall -Wextra -Wpedantic -Werror \
            -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
            -fstack-protector-strong -fPIC $(CFLAGS_EXTRA)
CPPFLAGS += -D_GNU_SOURCE -Iinclude -Isrc -MMD -MP

LIB_SRCS := src/core/device.c \
            src/core/err.c \
            src/core/lock.c \
            src/core/log.c \
            src/core/registry.c \
            src/core/trace.c \
            src/drivers/egis_moc/egis_moc.c \
            src/drivers/egis_moc/proto.c \
            src/storage/bindings.c

# Real hardware backend; tests link a mock instead.
USB_SRCS := src/transport/usbdevfs.c

TEST_NAMES := test_proto test_egis_moc test_bindings

LIB      := $(BUILD)/libfpx.a
LIB_OBJS := $(LIB_SRCS:%.c=$(BUILD)/%.o)
USB_OBJS := $(USB_SRCS:%.c=$(BUILD)/%.o)
TESTS    := $(TEST_NAMES:%=$(BUILD)/tests/%)

.PHONY: all test clean install uninstall

all: $(BUILD)/fpx

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/fpx: $(BUILD)/tools/fpx.o $(USB_OBJS) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/tests/%: $(BUILD)/tests/%.o $(BUILD)/tests/mock_transport.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done

install: $(BUILD)/fpx
	install -D -m 0755 $(BUILD)/fpx $(DESTDIR)$(PREFIX)/sbin/fpx

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/sbin/fpx

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
