/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Scripted fake transport. Bulk IN responses are served from a queue, every
 * bulk OUT frame is recorded so tests can inspect what the driver sent.
 */
#ifndef FPAL_TESTS_MOCK_TRANSPORT_H
#define FPAL_TESTS_MOCK_TRANSPORT_H

#include "transport/transport.h"
#include "fpal/config.h"

#define MOCK_MAX_FRAMES 16u

struct mock_frame {
	uint8_t data[FPAL_USB_RX_MAX];
	size_t len;
};

struct mock {
	/* Script: responses returned by bulk_in, in order. */
	struct mock_frame in[MOCK_MAX_FRAMES];
	size_t n_in, i_in;

	/* Result of wait_interrupt (FPAL_OK = finger touched). */
	int irq_rc;
	/* Result of claim. */
	int claim_rc;

	/* Recording. */
	struct mock_frame out[MOCK_MAX_FRAMES];
	size_t n_out;
	unsigned resets, claims, releases, ctrls, irqs;
};

void mock_init(struct mock *m);
struct fpal_transport mock_transport(struct mock *m);

/* Queue an Egis style response frame: SIGE header, length, body, status. */
void mock_push_egis(struct mock *m, const uint8_t *body, size_t body_len,
		    uint16_t sw);

#endif /* FPAL_TESTS_MOCK_TRANSPORT_H */
