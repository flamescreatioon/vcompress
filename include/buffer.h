#ifndef VCOMPRESS_BUFFER_H
#define VCOMPRESS_BUFFER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUF_OK = 0,
    BUF_ERR_NOMEM = -1,
    BUF_ERR_BOUNDS = -2,
    BUF_ERR_PARAM = -3
} BufferError;

typedef struct Buffer {
    uint8_t *data;
    size_t size;
    size_t capacity;
} Buffer;

/* Initialize an empty buffer */
void buffer_init(Buffer *buf);

/* Allocate an empty buffer on the heap */
Buffer *buffer_create(size_t initial_capacity);

/* Reserve capacity to hold at least `min_capacity` bytes */
BufferError buffer_reserve(Buffer *buf, size_t min_capacity);

/* Append raw data to buffer, growing capacity dynamically */
BufferError buffer_append(Buffer *buf, const void *src, size_t len);

/* Append single byte */
BufferError buffer_append_u8(Buffer *buf, uint8_t byte);

/* Append 16-bit big-endian integer */
BufferError buffer_append_u16_be(Buffer *buf, uint16_t val);

/* Append 32-bit big-endian integer */
BufferError buffer_append_u32_be(Buffer *buf, uint32_t val);

/* Append 64-bit big-endian integer */
BufferError buffer_append_u64_be(Buffer *buf, uint64_t val);

/* Read bytes from offset in buffer */
BufferError buffer_read(const Buffer *buf, size_t offset, void *dst, size_t len);

/* Write bytes at offset in buffer (must already be within size) */
BufferError buffer_write(Buffer *buf, size_t offset, const void *src, size_t len);

/* Clear buffer contents without releasing allocated memory */
void buffer_clear(Buffer *buf);

/* Free buffer internal memory */
void buffer_free(Buffer *buf);

/* Destroy heap-allocated buffer */
void buffer_destroy(Buffer *buf);

/* Detach internal data pointer (transfers ownership to caller, clears Buffer) */
uint8_t *buffer_detach(Buffer *buf, size_t *out_size);

/* -------------------------------------------------------------
 * BitWriter and BitReader for entropy bitstream manipulation
 * ------------------------------------------------------------- */

typedef struct BitWriter {
    Buffer *buf;
    uint64_t bit_buffer;
    uint8_t bits_in_buffer;
} BitWriter;

void bit_writer_init(BitWriter *bw, Buffer *target);
BufferError bit_writer_put_bits(BitWriter *bw, uint32_t val, uint8_t num_bits);
BufferError bit_writer_flush(BitWriter *bw);

typedef struct BitReader {
    const uint8_t *data;
    size_t size;
    size_t byte_pos;
    uint64_t bit_buffer;
    uint8_t bits_in_buffer;
} BitReader;

void bit_reader_init(BitReader *br, const uint8_t *data, size_t size);
bool bit_reader_get_bits(BitReader *br, uint8_t num_bits, uint32_t *out_val);
bool bit_reader_get_bit(BitReader *br, uint8_t *out_val);
void bit_reader_align_byte(BitReader *br);
size_t bit_reader_bytes_consumed(const BitReader *br);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_BUFFER_H */
