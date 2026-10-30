/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEXT_CODEC_REPLACEMENT  0xFFFD

uint32_t text_codec_gbk(uint8_t lead, uint8_t trail);
size_t text_codec_gbk_decode(const uint8_t *data, size_t len, uint32_t *cp);
size_t text_codec_utf8_decode(const uint8_t *data, size_t len, uint32_t *cp);
size_t text_codec_utf8_encode(uint32_t cp, char *out);
bool text_codec_is_utf8(const uint8_t *data, size_t len);
size_t text_codec_gbk_to_utf8(const uint8_t *data, size_t len, char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
