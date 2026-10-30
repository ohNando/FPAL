/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Explicit byte order helpers for wire formats. Wire structures are never
 * mapped onto packed C structs; every field is read and written through
 * these helpers.
 */
#ifndef FPX_CORE_BYTES_H
#define FPX_CORE_BYTES_H

#include <stdint.h>

static inline void fpx_put_be16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static inline void fpx_put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static inline uint16_t fpx_get_be16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static inline uint32_t fpx_get_be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
	       (uint32_t)p[2] << 8 | p[3];
}

#endif /* FPX_CORE_BYTES_H */
