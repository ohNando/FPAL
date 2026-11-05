# FPAL (Fingerprint Auth for Linux) build. Only GNU make and a C17 compiler are required.

CC       ?= gcc
AR       ?= ar
PREFIX   ?= /usr/local
# Directory libpam loads modules from; differs between distros.
PAMDIR   ?= $(firstword $(wildcard /usr/lib64/security /usr/lib/x86_64-linux-gnu/security /usr/lib/security /lib/security))
# fpal can enrol and delete fingerprints (guarded at run time by
# --allow-chip-write and a typed confirmation). "make ENROLL=0" builds a
# read-only fpal without any of that code; it gets its own directory.
ENROLL   ?= 1
BUILD    ?= $(if $(filter 0,$(ENROLL)),build-readonly,build)
# Distributions with a merged /usr (Fedora, Arch) set SBINDIR to /usr/bin.
SBINDIR  ?= $(PREFIX)/sbin
DOCDIR   ?= $(PREFIX)/share/doc/fpal
DATADIR  ?= $(PREFIX)/share/fpal
MANDIR   ?= $(PREFIX)/share/man
LIBEXECDIR ?= $(PREFIX)/libexec
UNITDIR  ?= $(PREFIX)/lib/systemd/system
# /etc is always read by the bus; packages pass /usr/share/dbus-1/system.d.
POLICYDIR ?= /etc/dbus-1/system.d
VERSION  := $(shell sed -n 's/^\#define FPAL_VERSION "\(.*\)"/\1/p' include/fpal/config.h)

CFLAGS   ?= -O2 -g
CFLAGS   += -std=c17 -Wall -Wextra -Wpedantic -Werror \
            -Wshadow -Wstrict-prototypes -Wmissing-prototypes \
            -fstack-protector-strong -fstack-clash-protection -fPIC $(CFLAGS_EXTRA)
# Executables: position independent, GOT read-only after start-up, no
# executable stack. Distributions add the same; this keeps a plain "make" safe.
HARDEN_LD := -pie -Wl,-z,relro,-z,now,-z,noexecstack
CPPFLAGS += -D_GNU_SOURCE -Iinclude -Isrc -MMD -MP
# Added for everything except the code that runs at login (see LIB_RO below).
CPPFLAGS_WRITE := $(if $(filter 0,$(ENROLL)),,-DFPAL_ENABLE_ENROLL)

LIB_SRCS := src/core/device.c \
            src/core/err.c \
            src/core/id.c \
            src/core/lock.c \
            src/core/log.c \
            src/core/newid.c \
            src/core/registry.c \
            src/core/trace.c \
            src/dbus/conn.c \
            src/dbus/marshal.c \
            src/dbus/message.c \
            src/drivers/egis_moc/egis_moc.c \
            src/drivers/egis_moc/proto.c \
            src/storage/bindings.c \
            src/storage/labels.c

# Real hardware backend; tests link a mock instead.
USB_SRCS := src/transport/usbdevfs.c

PAM_SRCS := pam/pam_fpal.c pam/opts.c

DAEMON_SRCS := daemon/fprint_service.c

TEST_NAMES := test_proto test_egis_moc test_bindings test_pam_opts test_dbus \
              test_dbus_conn test_fprint_service test_labels test_finger test_setup test_status test_ui test_manage test_core \
              test_golden_egis test_id
ifneq ($(ENROLL),0)
TEST_NAMES += test_enroll
endif

LIB      := $(BUILD)/libfpal.a
LIB_OBJS := $(LIB_SRCS:%.c=$(BUILD)/%.o)
# The same library compiled WITHOUT the write support. pam_fpal.so and fpald
# run at login, so they link this one and cannot contain the commands that
# store or delete fingerprints, whatever fpal itself can do.
LIB_RO   := $(BUILD)/libfpal-ro.a
LIB_RO_OBJS := $(LIB_SRCS:%.c=$(BUILD)/ro/%.o)
USB_OBJS := $(USB_SRCS:%.c=$(BUILD)/%.o)
PAM_OBJS := $(PAM_SRCS:%.c=$(BUILD)/%.o)
DAEMON_OBJS := $(DAEMON_SRCS:%.c=$(BUILD)/%.o)
PAM_MOD  := $(BUILD)/pam_fpal.so
TESTS    := $(TEST_NAMES:%=$(BUILD)/tests/%)

.PHONY: all pam test fuzz check-login-code clean install install-fpald uninstall uninstall-fpald dist

all: $(BUILD)/fpal $(BUILD)/fpald $(PAM_MOD)

pam: $(PAM_MOD)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CPPFLAGS_WRITE) $(CFLAGS) -c $< -o $@

$(BUILD)/ro/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(LIB_RO): $(LIB_RO_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/fpal: $(BUILD)/tools/fpal.o $(BUILD)/tools/finger.o \
               $(BUILD)/tools/setup.o $(BUILD)/tools/status.o $(BUILD)/tools/ui.o \
               $(BUILD)/tools/manage.o \
               $(USB_OBJS) $(LIB)
	$(CC) $(CFLAGS) $(HARDEN_LD) $^ -o $@ $(LDFLAGS)

$(BUILD)/fpald: $(BUILD)/daemon/fpald.o $(DAEMON_OBJS) $(USB_OBJS) $(LIB_RO)
	$(CC) $(CFLAGS) $(HARDEN_LD) $^ -o $@ $(LDFLAGS)

$(PAM_MOD): $(PAM_OBJS) $(USB_OBJS) $(LIB_RO) pam/pam_fpal.map
	$(CC) $(CFLAGS) -shared -Wl,--version-script=pam/pam_fpal.map \
		-Wl,-z,relro,-z,now,-z,defs \
		$(filter %.o %.a,$^) -o $@ $(LDFLAGS) -lpam

# Objects first, archives last: a static library only supplies what is still missing.
$(BUILD)/tests/%: $(BUILD)/tests/%.o $(BUILD)/tests/mock_transport.o $(LIB)
	$(CC) $(CFLAGS) $(filter-out %.a,$^) $(filter %.a,$^) -o $@ $(LDFLAGS)

# The finger picking of the command line tool is tested on its own.
$(BUILD)/tests/test_finger: $(BUILD)/tools/finger.o

# The fuzzer links the same objects as the tests of the pieces it exercises.
$(BUILD)/tests/fuzz: $(BUILD)/daemon/fprint_service.o $(BUILD)/tools/manage.o \
                     $(BUILD)/tools/status.o $(BUILD)/tools/finger.o $(BUILD)/tools/ui.o

# Pieces of the command line tool that are tested on their own.
$(BUILD)/tests/test_setup: $(BUILD)/tools/setup.o
$(BUILD)/tests/test_status: $(BUILD)/tools/status.o $(BUILD)/tools/ui.o
$(BUILD)/tests/test_ui: $(BUILD)/tools/ui.o
$(BUILD)/tests/test_manage: $(BUILD)/tools/manage.o $(BUILD)/tools/finger.o $(BUILD)/tools/ui.o

# The service logic is linked in as an object; the tests need no bus.
$(BUILD)/tests/test_fprint_service: $(BUILD)/daemon/fprint_service.o

# PAM option parsing is plain C and tested without libpam.
$(BUILD)/tests/test_pam_opts: $(BUILD)/pam/opts.o

test: $(TESTS) check-login-code
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done

# The code that runs at login must not contain the commands that write to the
# sensor; a read-only fpal must not either.
# Mutation fuzzing of the parsers (a few seconds). FUZZ_PERCENT=5 for a quick run.
FUZZ_PERCENT ?= 100
fuzz: $(BUILD)/tests/fuzz
	$(BUILD)/tests/fuzz $(FUZZ_PERCENT)

check-login-code: $(PAM_MOD) $(BUILD)/fpald $(BUILD)/fpal $(LIB) $(LIB_RO)
	sh scripts/check-readonly.sh --symbols $(LIB_RO) $(PAM_MOD) $(BUILD)/fpald
	sh scripts/check-readonly.sh $(PAM_MOD) $(BUILD)/fpald $(if $(filter 0,$(ENROLL)),$(BUILD)/fpal)
ifeq ($(ENROLL),0)
	sh scripts/check-readonly.sh --symbols $(LIB) $(BUILD)/fpal
else
	sh scripts/check-readonly.sh --symbols --present $(LIB)
endif

clean:
	rm -rf build build-readonly build-enroll

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
