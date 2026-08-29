/*
 * SPDX-FileCopyrightText: 2026 The IndustryGrow contributors
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "sha256.h"

#include <string.h>

/* First 32 bits of the fractional parts of the cube roots of the first 64
 * primes (FIPS 180-4 section 4.2.2). */
static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

static uint32_t ror(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void compress(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (unsigned i = 0u; i < 16u; i++) {
        w[i] = ((uint32_t)block[i * 4u] << 24) | ((uint32_t)block[i * 4u + 1u] << 16) |
               ((uint32_t)block[i * 4u + 2u] << 8) | (uint32_t)block[i * 4u + 3u];
    }
    for (unsigned i = 16u; i < 64u; i++) {
        const uint32_t s0 = ror(w[i - 15u], 7) ^ ror(w[i - 15u], 18) ^ (w[i - 15u] >> 3);
        const uint32_t s1 = ror(w[i - 2u], 17) ^ ror(w[i - 2u], 19) ^ (w[i - 2u] >> 10);
        w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (unsigned i = 0u; i < 64u; i++) {
        const uint32_t s1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
        const uint32_t ch = (e & f) ^ ((~e) & g);
        const uint32_t t1 = h + s1 + ch + K[i] + w[i];
        const uint32_t s0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
        const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sha256_init(sha256_ctx_t *ctx)
{
    /* First 32 bits of the fractional parts of the square roots of the first
     * eight primes (FIPS 180-4 section 5.3.3). */
    ctx->state[0] = 0x6a09e667u; ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u; ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu; ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu; ctx->state[7] = 0x5be0cd19u;
    ctx->bit_count = 0u;
    ctx->block_len = 0u;
}

void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    ctx->bit_count += (uint64_t)len * 8u;
    while (len > 0u) {
        const size_t take = (len < (64u - ctx->block_len)) ? len : (64u - ctx->block_len);
        memcpy(&ctx->block[ctx->block_len], p, take);
        ctx->block_len += take;
        p += take;
        len -= take;
        if (ctx->block_len == 64u) {
            compress(ctx->state, ctx->block);
            ctx->block_len = 0u;
        }
    }
}

void sha256_final(sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_LEN])
{
    const uint64_t bits = ctx->bit_count;

    /* Pad: one 1 bit, zeros, then the message length as 64 big-endian bits in
     * the last 8 bytes of the final block. A block with no room for the length
     * is compressed first and the length goes in the one after it. */
    ctx->block[ctx->block_len++] = 0x80u;
    if (ctx->block_len > 56u) {
        memset(&ctx->block[ctx->block_len], 0, 64u - ctx->block_len);
        compress(ctx->state, ctx->block);
        ctx->block_len = 0u;
    }
    memset(&ctx->block[ctx->block_len], 0, 56u - ctx->block_len);
    for (unsigned i = 0u; i < 8u; i++) {
        ctx->block[56u + i] = (uint8_t)(bits >> (56u - (8u * i)));
    }
    compress(ctx->state, ctx->block);

    for (unsigned i = 0u; i < 8u; i++) {
        out[i * 4u] = (uint8_t)(ctx->state[i] >> 24);
        out[i * 4u + 1u] = (uint8_t)(ctx->state[i] >> 16);
        out[i * 4u + 2u] = (uint8_t)(ctx->state[i] >> 8);
        out[i * 4u + 3u] = (uint8_t)ctx->state[i];
    }
}

void sha256(const void *data, size_t len, uint8_t out[SHA256_DIGEST_LEN])
{
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, out);
}

bool sha256_selftest(void)
{
    static const uint8_t expect[SHA256_DIGEST_LEN] = {
        0xbau, 0x78u, 0x16u, 0xbfu, 0x8fu, 0x01u, 0xcfu, 0xeau,
        0x41u, 0x41u, 0x40u, 0xdeu, 0x5du, 0xaeu, 0x22u, 0x23u,
        0xb0u, 0x03u, 0x61u, 0xa3u, 0x96u, 0x17u, 0x7au, 0x9cu,
        0xb4u, 0x10u, 0xffu, 0x61u, 0xf2u, 0x00u, 0x15u, 0xadu,
    };
    uint8_t got[SHA256_DIGEST_LEN];
    sha256("abc", 3u, got);
    return memcmp(got, expect, sizeof(got)) == 0;
}
