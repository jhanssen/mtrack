#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t base64_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len, int finalize);

#ifdef __cplusplus
}
#endif
