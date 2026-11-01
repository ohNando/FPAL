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
            -fstack-protector-strong -fPIC $(CFLAGS_EXTRA)
CPPFLAGS += -D_GNU_SOURCE -Iinclude -Isrc -MMD -MP
# Added for everything except the code that runs at login (see LIB_RO below).
CPPFLAGS_WRITE := $(if $(filter 0,$(ENROLL)),,-DFPAL_ENABLE_ENROLL)

LIB_SRCS := src/core/device.c \
            src/core/err.c \
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
              test_dbus_conn test_fprint_service test_labels test_finger test_setup test_status test_ui
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

.PHONY: all pam test check-login-code clean install install-fpald uninstall uninstall-fpald dist

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
               $(USB_OBJS) $(LIB)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/fpald: $(BUILD)/daemon/fpald.o $(DAEMON_OBJS) $(USB_OBJS) $(LIB_RO)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(PAM_MOD): $(PAM_OBJS) $(USB_OBJS) $(LIB_RO) pam/pam_fpal.map
	$(CC) $(CFLAGS) -shared -Wl,--version-script=pam/pam_fpal.map \
		-Wl,-z,relro,-z,now,-z,defs \
		$(filter %.o %.a,$^) -o $@ $(LDFLAGS) -lpam

# Objects first, archives last: a static library only supplies what is still missing.
$(BUILD)/tests/%: $(BUILD)/tests/%.o $(BUILD)/tests/mock_transport.o $(LIB)
	$(CC) $(CFLAGS) $(filter-out %.a,$^) $(filter %.a,$^) -o $@ $(LDFLAGS)

# The finger picking of the command line tool is tested on its own.
$(BUILD)/tests/test_finger: $(BUILD)/tools/finger.o

# Pieces of the command line tool that are tested on their own.
$(BUILD)/tests/test_setup: $(BUILD)/tools/setup.o
$(BUILD)/tests/test_status: $(BUILD)/tools/status.o $(BUILD)/tools/ui.o
$(BUILD)/tests/test_ui: $(BUILD)/tools/ui.o

# The service logic is linked in as an object; the tests need no bus.
$(BUILD)/tests/test_fprint_service: $(BUILD)/daemon/fprint_service.o

# PAM option parsing is plain C and tested without libpam.
$(BUILD)/tests/test_pam_opts: $(BUILD)/pam/opts.o

test: $(TESTS) check-login-code
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done

# The code that runs at login must not contain the commands that write to the
# sensor; a read-only fpal must not either.
check-login-code: $(PAM_MOD) $(BUILD)/fpald $(BUILD)/fpal
	sh scripts/check-readonly.sh $(PAM_MOD) $(BUILD)/fpald $(if $(filter 0,$(ENROLL)),$(BUILD)/fpal)

install: $(BUILD)/fpal $(PAM_MOD) install-fpald
	@test -n "$(PAMDIR)" || { echo "PAMDIR not found, set PAMDIR=..."; exit 1; }
	install -D -m 0755 $(BUILD)/fpal $(DESTDIR)$(SBINDIR)/fpal
	install -D -m 0755 $(PAM_MOD) $(DESTDIR)$(PAMDIR)/pam_fpal.so
	install -D -m 0755 scripts/authselect-fpal.sh $(DESTDIR)$(DATADIR)/authselect-fpal.sh
	install -D -m 0644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -D -m 0644 docs/usage.md $(DESTDIR)$(DOCDIR)/usage.md
	install -D -m 0644 docs/pam.md $(DESTDIR)$(DOCDIR)/pam.md
	install -D -m 0644 docs/gnome.md $(DESTDIR)$(DOCDIR)/gnome.md
	install -D -m 0644 docs/protocol-egis-moc.md $(DESTDIR)$(DOCDIR)/protocol-egis-moc.md
	for p in fpal pam_fpal fpald; do \
		install -d $(DESTDIR)$(MANDIR)/man8; \
		sed 's|@VERSION@|$(VERSION)|' man/$$p.8.in > $(DESTDIR)$(MANDIR)/man8/$$p.8; \
		chmod 0644 $(DESTDIR)$(MANDIR)/man8/$$p.8; \
	done

# The GNOME presence service alone (daemon, systemd unit, bus policy).
install-fpald: $(BUILD)/fpald
	install -D -m 0755 $(BUILD)/fpald $(DESTDIR)$(LIBEXECDIR)/fpald
	install -d $(DESTDIR)$(UNITDIR)
	sed 's|@LIBEXECDIR@|$(LIBEXECDIR)|' data/systemd/fpald.service.in \
		> $(DESTDIR)$(UNITDIR)/fpald.service
	chmod 0644 $(DESTDIR)$(UNITDIR)/fpald.service
	install -D -m 0644 data/dbus/fpald.conf $(DESTDIR)$(POLICYDIR)/fpald.conf

uninstall: uninstall-fpald
	rm -f $(DESTDIR)$(SBINDIR)/fpal
	rm -f $(DESTDIR)$(PAMDIR)/pam_fpal.so
	rm -rf $(DESTDIR)$(DATADIR) $(DESTDIR)$(DOCDIR)
	rm -f $(DESTDIR)$(MANDIR)/man8/fpal.8 $(DESTDIR)$(MANDIR)/man8/pam_fpal.8 \
		$(DESTDIR)$(MANDIR)/man8/fpald.8

uninstall-fpald:
	rm -f $(DESTDIR)$(LIBEXECDIR)/fpald $(DESTDIR)$(UNITDIR)/fpald.service
	rm -f $(DESTDIR)$(POLICYDIR)/fpald.conf

# Source tarball of the committed tree: build/fpal-<version>.tar.gz
dist:
	@mkdir -p $(BUILD)
	git archive --format=tar.gz --prefix=fpal-$(VERSION)/ \
		-o $(BUILD)/fpal-$(VERSION).tar.gz HEAD
	@echo $(BUILD)/fpal-$(VERSION).tar.gz

clean:
	rm -rf build build-readonly build-enroll

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
