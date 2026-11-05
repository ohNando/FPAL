// SPDX-License-Identifier: Apache-2.0
/*
 * Small core pieces: the environment switches that privileged code can turn
 * off, and cleaning up strings that a USB device chooses.
 */
#include <stdlib.h>
#include <string.h>

#include "core/driver.h"
#include "core/log.h"
#include "core/trace.h"
#include "t.h"

static void test_trace_follows_the_environment_until_told_otherwise(void) {
	fpal_trace_set(-1);
	unsetenv("FPAL_TRACE");
	T_CHECK(!fpal_trace_enabled());
	setenv("FPAL_TRACE", "1", 1);
	T_CHECK(fpal_trace_enabled());
	setenv("FPAL_TRACE", "0", 1);
	T_CHECK(!fpal_trace_enabled());

	/* pam_fpal: the caller's environment must not switch tracing on */
	setenv("FPAL_TRACE", "1", 1);
	fpal_trace_set(0);
	T_CHECK(!fpal_trace_enabled());
	fpal_trace_set(1); /* but code can still ask for it */
	T_CHECK(fpal_trace_enabled());

	fpal_trace_set(-1);
	unsetenv("FPAL_TRACE");
}

static unsigned debug_lines;

static void count_sink(enum fpal_log_level level, const char *msg) {
	(void)msg;
	if (level == FPAL_LOG_DEBUG)
		debug_lines++;
}

static void test_debug_logging_can_ignore_the_environment(void) {
	fpal_log_set_sink(count_sink);
	fpal_log_set_debug(false);
	fpal_log_use_env(true);
	setenv("FPAL_DEBUG", "1", 1);
	debug_lines = 0;
	fpal_dbg("with the environment");
	T_EQ_INT(debug_lines, 1);

	fpal_log_use_env(false); /* what pam_fpal does */
	fpal_dbg("without the environment");
	T_EQ_INT(debug_lines, 1); /* nothing more */

	fpal_log_set_debug(true); /* the "debug" PAM option */
	fpal_dbg("asked for in code");
	T_EQ_INT(debug_lines, 2);

	fpal_log_set_debug(false);
	fpal_log_use_env(true);
	fpal_log_set_sink(NULL);
	unsetenv("FPAL_DEBUG");
}

static void test_device_strings_are_made_printable(void) {
	char s[] = "ETU905A88-E";
	char evil[] = "ok\x1b[2Jbad\x07\x7f\xc2\x9b" "end";
	char empty[] = "";

	fpal_text_sanitize(s);
	T_CHECK(!strcmp(s, "ETU905A88-E")); /* ordinary text is left alone */
	fpal_text_sanitize(evil);
	for (const char *p = evil; *p; p++)
		T_CHECK((unsigned char)*p >= 0x20 && (unsigned char)*p <= 0x7e);
	T_CHECK(!strncmp(evil, "ok?[2J", 6)); /* the escape is gone, the text stays */
	T_EQ_INT(strlen(evil), strlen("ok\x1b[2Jbad\x07\x7f\xc2\x9b" "end"));
	fpal_text_sanitize(empty);
	T_EQ_INT(empty[0], 0);
}

/* Rules every driver in the table has to follow, now and in the future. */
static void test_every_driver_follows_the_rules(void) {
	size_t count = 0;

	for (const struct fpal_driver *const *d = fpal_drivers; *d; d++) {
		const struct fpal_driver *drv = *d;

		count++;
		T_CHECK(drv->name && drv->name[0]);
		T_CHECK(drv->ids && drv->ids[0].vid);
		T_CHECK(drv->open && drv->close && drv->list && drv->identify);
		T_CHECK(drv->max_templates >= 1 &&
			drv->max_templates <= FPAL_MAX_TEMPLATES);
		/* enough random data that a stale binding cannot match another finger */
		T_CHECK(drv->new_id_len >= sizeof(FPAL_NEW_ID_PREFIX) - 1 + 16 &&
			drv->new_id_len <= FPAL_ID_MAX);
		for (const struct fpal_driver *const *o = d + 1; *o; o++)
			T_CHECK(strcmp(drv->name, (*o)->name) != 0);
		for (const struct fpal_usb_id *id = drv->ids; id->vid; id++)
			for (const struct fpal_driver *const *o = fpal_drivers; *o; o++)
				for (const struct fpal_usb_id *oid = (*o)->ids; oid->vid; oid++)
					T_CHECK(oid == id || oid->vid != id->vid ||
						oid->pid != id->pid);
	}
	T_CHECK(count > 0);
}

int main(void) {
	T_RUN(test_trace_follows_the_environment_until_told_otherwise);
	T_RUN(test_debug_logging_can_ignore_the_environment);
	T_RUN(test_device_strings_are_made_printable);
	T_RUN(test_every_driver_follows_the_rules);
	return T_DONE();
}
