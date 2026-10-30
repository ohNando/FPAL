# OpenFinger (fpx) build. Only GNU make and a C17 compiler are required.

CC       ?= gcc
AR       ?= ar
PREFIX   ?= /usr/local
# Directory libpam loads modules from; differs between distros.
PAMDIR   ?= $(firstword $(wildcard /usr/lib64/security /usr/lib/x86_64-linux-gnu/security /usr/lib/security /lib/security))
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

PAM_SRCS := pam/pam_fpx.c pam/opts.c

TEST_NAMES := test_proto test_egis_moc test_bindings test_pam_opts

LIB      := $(BUILD)/libfpx.a
LIB_OBJS := $(LIB_SRCS:%.c=$(BUILD)/%.o)
USB_OBJS := $(USB_SRCS:%.c=$(BUILD)/%.o)
PAM_OBJS := $(PAM_SRCS:%.c=$(BUILD)/%.o)
PAM_MOD  := $(BUILD)/pam_fpx.so
TESTS    := $(TEST_NAMES:%=$(BUILD)/tests/%)

.PHONY: all pam test clean install uninstall

all: $(BUILD)/fpx $(PAM_MOD)

pam: $(PAM_MOD)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/fpx: $(BUILD)/tools/fpx.o $(USB_OBJS) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(PAM_MOD): $(PAM_OBJS) $(USB_OBJS) $(LIB) pam/pam_fpx.map
	$(CC) $(CFLAGS) -shared -Wl,--version-script=pam/pam_fpx.map \
		-Wl,-z,relro,-z,now,-z,defs \
		$(filter %.o %.a,$^) -o $@ $(LDFLAGS) -lpam

$(BUILD)/tests/%: $(BUILD)/tests/%.o $(BUILD)/tests/mock_transport.o $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# PAM option parsing is plain C and tested without libpam.
$(BUILD)/tests/test_pam_opts: $(BUILD)/pam/opts.o

test: $(TESTS)
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done

install: $(BUILD)/fpx $(PAM_MOD)
	@test -n "$(PAMDIR)" || { echo "PAMDIR not found, set PAMDIR=..."; exit 1; }
	install -D -m 0755 $(BUILD)/fpx $(DESTDIR)$(PREFIX)/sbin/fpx
	install -D -m 0755 $(PAM_MOD) $(DESTDIR)$(PAMDIR)/pam_fpx.so

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/sbin/fpx
	rm -f $(DESTDIR)$(PAMDIR)/pam_fpx.so

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
