/* sha1.c - SHA-1 of a file, for telling the game's data files apart.
 *
 * The firmware declarations name each file by its SHA-1, so the check the core
 * makes at Init speaks the same language as the frontend. A straightforward
 * FIPS 180-1 implementation; speed is irrelevant for a few hundred kilobytes.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sha1.h"

typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; size_t fill; } sha1_ctx;

static uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void block(sha1_ctx *c, const uint8_t *p)
{
	uint32_t w[80];
	for (int i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (int i = 16; i < 80; i++)
		w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
	for (int i = 0; i < 80; i++)
	{
		uint32_t f, k;
		if (i < 20) { f = (b & cc) | (~b & d); k = 0x5A827999; }
		else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
		else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
		else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
		uint32_t t = rol(a, 5) + f + e + k + w[i];
		e = d; d = cc; cc = rol(b, 30); b = a; a = t;
	}
	c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

static void update(sha1_ctx *c, const uint8_t *p, size_t n)
{
	c->len += n;
	while (n)
	{
		size_t take = 64 - c->fill;
		if (take > n) take = n;
		memcpy(c->buf + c->fill, p, take);
		c->fill += take; p += take; n -= take;
		if (c->fill == 64) { block(c, c->buf); c->fill = 0; }
	}
}

int sha1_file(FILE *f, char hex[41], long *size)
{
	sha1_ctx c = { { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }, 0, { 0 }, 0 };
	uint8_t chunk[4096];
	size_t got;
	while ((got = fread(chunk, 1, sizeof chunk, f)) > 0)
		update(&c, chunk, got);
	if (ferror(f))
		return 0;
	uint64_t bits = c.len * 8;
	uint8_t pad = 0x80;
	update(&c, &pad, 1);
	pad = 0;
	while (c.fill != 56)
		update(&c, &pad, 1);
	uint8_t lenbe[8];
	for (int i = 0; i < 8; i++)
		lenbe[i] = (uint8_t)(bits >> (56 - 8 * i));
	update(&c, lenbe, 8);
	for (int i = 0; i < 5; i++)
		snprintf(hex + 8 * i, 9, "%08X", c.h[i]);
	if (size)
		*size = (long)(bits / 8);
	return 1;
}
