/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Scripted fake transport. Bulk IN responses are served from a queue, every
 * bulk OUT frame is recorded so tests can inspect what the driver sent.
 */
#ifndef FPAL_TESTS_MOCK_TRANSPORT_H
#define FPAL_TESTS_MOCK_TRANSPORT_H

#include "transport/transport.h"
#include "fpal/config.h"

/* An enrol run exchanges about 70 frames. */
#define MOCK_MAX_FRAMES 192u
#define MOCK_FRAME_MAX 512u

struct mock_frame {
	uint8_t data[MOCK_FRAME_MAX];
	size_t len;
};

/* One control-out transfer the driver sent. */
#define MOCK_MAX_CTRL 16u
#define MOCK_CTRL_DATA 64u
struct mock_ctrl {
	uint8_t req_type, req;
	uint16_t value, index;
	uint8_t data[MOCK_CTRL_DATA];
	uint16_t len;
};

struct mock {
	/* Script: responses returned by bulk_in, in order. */
	struct mock_frame in[MOCK_MAX_FRAMES];
	size_t n_in, i_in;

	/* Result of wait_interrupt (FPAL_OK = finger touched). */
	int irq_rc;
	/* Make wait_interrupt number irq_fail_at (1-based, 0 = never) fail
	 * with irq_fail_rc. */
	unsigned irq_fail_at;
	int irq_fail_rc;
	/* Control transfers: control_in answers come from ctrl_in (zeros once
	 * it is used up), control_out is recorded in ctrl_out. */
	struct mock_frame ctrl_in[MOCK_MAX_CTRL];
	size_t n_ctrl_in, i_ctrl_in;
	struct mock_ctrl ctrl_out[MOCK_MAX_CTRL];
	size_t n_ctrl_out;
	int ctrl_out_rc;

	/* wait_bulk_in: frames served in order; when none is left the wait
	 * returns bulk_wait_rc (FPAL_E_TIMEOUT by default). cancel() makes the
	 * next wait return FPAL_E_CANCELLED. */
	struct mock_frame wait_in[MOCK_MAX_CTRL];
	size_t n_wait_in, i_wait_in;
	int bulk_wait_rc;
	unsigned bulk_waits;

	/* Result of claim. */
	int claim_rc;

	/* Recording. */
	struct mock_frame out[MOCK_MAX_FRAMES];
	size_t n_out;
	unsigned resets, claims, releases, ctrls, irqs;
};

void mock_init(struct mock *m);

/* Script the next control_in answer / bulk wait result. */
void mock_push_ctrl_in(struct mock *m, const void *data, size_t len);
void mock_push_wait_in(struct mock *m, const void *data, size_t len);
struct fpal_transport mock_transport(struct mock *m);

/* Queue an Egis style response frame: SIGE header, length, body, status. */
void mock_push_egis(struct mock *m, const uint8_t *body, size_t body_len,
		    uint16_t sw);

#endif /* FPAL_TESTS_MOCK_TRANSPORT_H */
