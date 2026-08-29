/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#ifndef IGROW_PLATFORM_SHA256_H
#define IGROW_PLATFORM_SHA256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * SHA-256 (FIPS 180-4), streaming.
 *
 * Written here rather than pulled in: the STM32F405 has no hash accelerator
 * (that is the F41x/F43x crypto part), and the only other candidate was a
 * second third-party dependency for 130 lines of table-driven code.
 *
 * Because it is our own, it carries a self-test. sha256_selftest() runs the
 * FIPS one-block example and the bootloader refuses to verify an image if it
 * fails -- a hash that is subtly wrong would otherwise reject every valid
 * image with no way to tell that from a bad signature.
 */

#define SHA256_DIGEST_LEN 32u

typedef struct {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t block_len;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_LEN]);

/* One-shot over a contiguous range. */
void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN]);

/* SHA-256("abc") against the digest FIPS 180-4 publishes for it. */
bool sha256_selftest(void);

#endif /* IGROW_PLATFORM_SHA256_H */
