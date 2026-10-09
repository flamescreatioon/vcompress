#include "buffer.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

#define BUFFER_INITIAL_CAPACITY 256

void buffer_init(Buffer *buf) {
    if (!buf) return;
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
}

Buffer *buffer_create(size_t initial_capacity) {
    Buffer *buf = (Buffer *)malloc(sizeof(Buffer));
    if (!buf) return NULL;
    buffer_init(buf);
    if (initial_capacity > 0) {
        if (buffer_reserve(buf, initial_capacity) != BUF_OK) {
            free(buf);
            return NULL;
        }
    }
    return buf;
}

BufferError buffer_reserve(Buffer *buf, size_t min_capacity) {
    if (!buf) return BUF_ERR_PARAM;
    if (buf->capacity >= min_capacity) return BUF_OK;

    size_t new_cap = buf->capacity == 0 ? BUFFER_INITIAL_CAPACITY : buf->capacity;
    while (new_cap < min_capacity) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = min_capacity;
            break;
        }
        new_cap = new_cap + (new_cap / 2); /* 1.5x growth */
    }

    uint8_t *new_data = (uint8_t *)realloc(buf->data, new_cap);
    if (!new_data) return BUF_ERR_NOMEM;

    buf->data = new_data;
    buf->capacity = new_cap;
    return BUF_OK;
}

BufferError buffer_append(Buffer *buf, const void *src, size_t len) {
    if (!buf) return BUF_ERR_PARAM;
    if (len == 0) return BUF_OK;
    if (!src) return BUF_ERR_PARAM;

    if (buf->size + len < buf->size) return BUF_ERR_BOUNDS; /* overflow */

    BufferError err = buffer_reserve(buf, buf->size + len);
    if (err != BUF_OK) return err;

    memcpy(buf->data + buf->size, src, len);
    buf->size += len;
    return BUF_OK;
}

BufferError buffer_append_u8(Buffer *buf, uint8_t byte) {
    return buffer_append(buf, &byte, 1);
}

BufferError buffer_append_u16_be(Buffer *buf, uint16_t val) {
    uint8_t tmp[2];
    write_u16_be(tmp, val);
    return buffer_append(buf, tmp, 2);
}

BufferError buffer_append_u32_be(Buffer *buf, uint32_t val) {
    uint8_t tmp[4];
    write_u32_be(tmp, val);
    return buffer_append(buf, tmp, 4);
}

BufferError buffer_append_u64_be(Buffer *buf, uint64_t val) {
    uint8_t tmp[8];
    write_u64_be(tmp, val);
    return buffer_append(buf, tmp, 8);
}

BufferError buffer_read(const Buffer *buf, size_t offset, void *dst, size_t len) {
    if (!buf || !dst) return BUF_ERR_PARAM;
    if (len == 0) return BUF_OK;
    if (offset + len > buf->size || offset + len < offset) return BUF_ERR_BOUNDS;

    memcpy(dst, buf->data + offset, len);
    return BUF_OK;
}

BufferError buffer_write(Buffer *buf, size_t offset, const void *src, size_t len) {
    if (!buf || !src) return BUF_ERR_PARAM;
    if (len == 0) return BUF_OK;
    if (offset + len > buf->size || offset + len < offset) return BUF_ERR_BOUNDS;

    memcpy(buf->data + offset, src, len);
    return BUF_OK;
}

void buffer_clear(Buffer *buf) {
    if (buf) {
        buf->size = 0;
    }
}

void buffer_free(Buffer *buf) {
    if (buf) {
        if (buf->data) {
            free(buf->data);
            buf->data = NULL;
        }
        buf->size = 0;
        buf->capacity = 0;
    }
}

void buffer_destroy(Buffer *buf) {
    if (buf) {
        buffer_free(buf);
        free(buf);
    }
}

uint8_t *buffer_detach(Buffer *buf, size_t *out_size) {
    if (!buf) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    uint8_t *d = buf->data;
    if (out_size) *out_size = buf->size;
    buf->data = NULL;
    buf->size = 0;
    buf->capacity = 0;
    return d;
}

/* -------------------------------------------------------------
 * BitWriter and BitReader implementation
 * ------------------------------------------------------------- */

void bit_writer_init(BitWriter *bw, Buffer *target) {
    if (!bw) return;
    bw->buf = target;
    bw->bit_buffer = 0;
    bw->bits_in_buffer = 0;
}

BufferError bit_writer_put_bits(BitWriter *bw, uint32_t val, uint8_t num_bits) {
    if (!bw || !bw->buf || num_bits > 32) return BUF_ERR_PARAM;
    if (num_bits == 0) return BUF_OK;

    /* Mask to desired bit count */
    if (num_bits < 32) {
        val &= ((1U << num_bits) - 1U);
    }

    bw->bit_buffer = (bw->bit_buffer << num_bits) | (uint64_t)val;
    bw->bits_in_buffer = (uint8_t)(bw->bits_in_buffer + num_bits);

    while (bw->bits_in_buffer >= 8) {
        bw->bits_in_buffer = (uint8_t)(bw->bits_in_buffer - 8);
        uint8_t byte = (uint8_t)((bw->bit_buffer >> bw->bits_in_buffer) & 0xFF);
        BufferError err = buffer_append_u8(bw->buf, byte);
        if (err != BUF_OK) return err;
    }
    return BUF_OK;
}

BufferError bit_writer_flush(BitWriter *bw) {
    if (!bw || !bw->buf) return BUF_ERR_PARAM;
    if (bw->bits_in_buffer > 0) {
        /* Pad with zeros on the right to make full byte */
        uint8_t byte = (uint8_t)((bw->bit_buffer << (8 - bw->bits_in_buffer)) & 0xFF);
        BufferError err = buffer_append_u8(bw->buf, byte);
        if (err != BUF_OK) return err;
        bw->bits_in_buffer = 0;
        bw->bit_buffer = 0;
    }
    return BUF_OK;
}

void bit_reader_init(BitReader *br, const uint8_t *data, size_t size) {
    if (!br) return;
    br->data = data;
    br->size = size;
    br->byte_pos = 0;
    br->bit_buffer = 0;
    br->bits_in_buffer = 0;
}

bool bit_reader_get_bits(BitReader *br, uint8_t num_bits, uint32_t *out_val) {
    if (!br || !out_val || num_bits > 32) return false;
    if (num_bits == 0) {
        *out_val = 0;
        return true;
    }

    while (br->bits_in_buffer < num_bits) {
        if (br->byte_pos >= br->size) {
            return false; /* Unexpected EOF */
        }
        uint8_t next_byte = br->data[br->byte_pos++];
        br->bit_buffer = (br->bit_buffer << 8) | (uint64_t)next_byte;
        br->bits_in_buffer = (uint8_t)(br->bits_in_buffer + 8);
    }

    br->bits_in_buffer = (uint8_t)(br->bits_in_buffer - num_bits);
    uint64_t mask = (num_bits == 32) ? 0xFFFFFFFFULL : ((1ULL << num_bits) - 1ULL);
    *out_val = (uint32_t)((br->bit_buffer >> br->bits_in_buffer) & mask);
    return true;
}

bool bit_reader_get_bit(BitReader *br, uint8_t *out_val) {
    uint32_t v = 0;
    if (!bit_reader_get_bits(br, 1, &v)) return false;
    *out_val = (uint8_t)v;
    return true;
}

void bit_reader_align_byte(BitReader *br) {
    if (!br) return;
    br->bits_in_buffer = 0;
    br->bit_buffer = 0;
}

size_t bit_reader_bytes_consumed(const BitReader *br) {
    if (!br) return 0;
    return br->byte_pos;
}
