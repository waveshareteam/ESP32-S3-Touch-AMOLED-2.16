/*
 * SPDX-FileCopyrightText: 2026 Waveshare
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TEXT_ENCODING_UTF8,
    TEXT_ENCODING_GBK,
    TEXT_ENCODING_UTF16LE,
    TEXT_ENCODING_UTF16BE,
} text_encoding_t;

/**
 * @brief  A text file read character by character from any byte offset
 *
 *         Only a small window of the file is in memory, so books of any size open at once.
 *         Positions are byte offsets in the file.
 */
typedef struct text_source text_source_t;

/**
 * @brief  Open a text file and tell its encoding from a byte order mark or from its first bytes
 *
 *         Without a mark, text that is not valid UTF-8 is read as GBK.
 *
 * @return ESP_ERR_NOT_SUPPORTED when the file does not look like text
 */
esp_err_t text_source_open(const char *path, text_source_t **ret_src);

void text_source_close(text_source_t *src);

text_encoding_t text_source_encoding(const text_source_t *src);

const char *text_encoding_name(text_encoding_t encoding);

/**
 * @brief  Offset of the first character, after any byte order mark
 */
uint32_t text_source_begin(const text_source_t *src);

/**
 * @brief  File size, the offset just past the last character
 */
uint32_t text_source_end(const text_source_t *src);

/**
 * @brief  Decode the character at *pos and move *pos past it
 *
 * @return false at the end of the file or when the card can't be read
 */
bool text_source_next(text_source_t *src, uint32_t *pos, uint32_t *cp);

/**
 * @brief  The first character boundary at or after pos, for jumping into the middle of a file
 */
uint32_t text_source_align(text_source_t *src, uint32_t pos);

/**
 * @brief  Start of the line that holds the character just before pos
 *
 * @param  max_back  How far back to look for the line break
 * @param  found     Set to false when no line start was found that close; the result is then a
 *                   character boundary max_back bytes before pos
 */
uint32_t text_source_line_start(text_source_t *src, uint32_t pos, uint32_t max_back, bool *found);

#ifdef __cplusplus
}
#endif
