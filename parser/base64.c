#include "base64.h"

/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

static const uint8_t base64_table[65] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

ssize_t base64_encode(const uint8_t *src, size_t in_len, uint8_t* out, size_t out_len, int finalize)
{
    uint8_t *pos;
    const uint8_t *end, *in;
    const size_t olen = in_len * 4 / 3 + 4; /* 3-byte blocks to 4-byte */

    if (olen < in_len)
        return -1; /* integer overflow */
    if (out_len < olen)
        return -2;

    end = src + in_len;
    in = src;
    pos = out;
    while (end - in >= 3) {
        *pos++ = base64_table[in[0] >> 2];
        *pos++ = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        *pos++ = base64_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        *pos++ = base64_table[in[2] & 0x3f];
        in += 3;
    }

    if (end - in) {
        *pos++ = base64_table[in[0] >> 2];
        if (end - in == 1) {
            *pos++ = base64_table[(in[0] & 0x03) << 4];
            if (finalize)
                *pos++ = '=';
        } else {
            *pos++ = base64_table[((in[0] & 0x03) << 4) |
                                  (in[1] >> 4)];
            *pos++ = base64_table[(in[1] & 0x0f) << 2];
        }
        if (finalize)
            *pos++ = '=';
    }

    return pos - out;
}
