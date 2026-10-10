#ifndef VCOMPRESS_COMPRESSOR_H
#define VCOMPRESS_COMPRESSOR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "buffer.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLOCK_SIZE 8
#define BLOCK_AREA (BLOCK_SIZE * BLOCK_SIZE)
#define MAX_BLOCK_SYMBOLS (BLOCK_AREA + 16)

/* Zigzag order table (64 entries mapping 2D 8x8 index to 1D zigzag index) */
extern const uint8_t g_zigzag_order[BLOCK_AREA];
extern const uint8_t g_unzigzag_order[BLOCK_AREA];

/* Standard luminance & chrominance base quantization matrices */
extern const uint8_t g_default_luma_qmatrix[BLOCK_AREA];
extern const uint8_t g_default_chroma_qmatrix[BLOCK_AREA];

/* Forward and inverse 8x8 2D Discrete Cosine Transform */
void dct_8x8(const int16_t input[BLOCK_AREA], double output[BLOCK_AREA]);
void idct_8x8(const double input[BLOCK_AREA], int16_t output[BLOCK_AREA]);

/* Integer/fixed-point scaled DCT for performance */
void dct_8x8_fast(const int16_t input[BLOCK_AREA], int32_t output[BLOCK_AREA]);
void idct_8x8_fast(const int32_t input[BLOCK_AREA], int16_t output[BLOCK_AREA]);

/* Generate scaled quantization matrix for given quality (1-100) */
void quant_matrix_init(uint16_t qmatrix[BLOCK_AREA], const uint8_t base_matrix[BLOCK_AREA], int quality);

/* Calculate spatial variance of an 8x8 pixel block */
uint32_t calc_block_variance_u8(const uint8_t *pixels, uint32_t stride, uint32_t width, uint32_t height, uint32_t px0, uint32_t py0);

/* Quantize and dequantize 8x8 transform coefficients */
void quantize_block(const int32_t in_coeff[BLOCK_AREA], const uint16_t qmatrix[BLOCK_AREA], int16_t out_quant[BLOCK_AREA]);
void quantize_block_aq(const int32_t in_coeff[BLOCK_AREA], const uint16_t qmatrix[BLOCK_AREA], uint32_t variance, int16_t out_quant[BLOCK_AREA]);
void dequantize_block(const int16_t in_quant[BLOCK_AREA], const uint16_t qmatrix[BLOCK_AREA], int32_t out_coeff[BLOCK_AREA]);

/* Zigzag reordering */
void zigzag_reorder(const int16_t in_block[BLOCK_AREA], int16_t out_zigzag[BLOCK_AREA]);
void unzigzag_reorder(const int16_t in_zigzag[BLOCK_AREA], int16_t out_block[BLOCK_AREA]);

/* -------------------------------------------------------------
 * RLE (Run-Length Encoding)
 * ------------------------------------------------------------- */

typedef struct RLESymbol {
    uint8_t run;    /* Run length of preceding zeros (0-15, or 0 for EOB) */
    int16_t value;  /* Non-zero coefficient value, or 0 for EOB */
} RLESymbol;

/* Convert zigzag-ordered 64 coefficients into RLE symbols.
 * Returns number of symbols generated (appends EOB if block ends before 64). */
size_t rle_encode_block(const int16_t zigzag[BLOCK_AREA], RLESymbol *symbols, size_t max_symbols);

/* Reconstruct zigzag-ordered 64 coefficients from RLE symbols.
 * Returns number of symbols consumed. */
size_t rle_decode_block(const RLESymbol *symbols, size_t num_symbols, int16_t zigzag[BLOCK_AREA]);

/* -------------------------------------------------------------
 * Huffman Coding & Entropy Stage
 * ------------------------------------------------------------- */

#define HUFFMAN_MAX_SYMBOLS 512

typedef struct HuffmanCode {
    uint32_t code;
    uint8_t length;
} HuffmanCode;

typedef struct HuffmanTable {
    uint16_t num_symbols;
    uint16_t symbols[HUFFMAN_MAX_SYMBOLS];
    HuffmanCode codes[HUFFMAN_MAX_SYMBOLS];
} HuffmanTable;

/* Build canonical Huffman table from symbol frequency histogram */
bool huffman_build_table(const uint32_t *freqs, const uint16_t *symbols, size_t num_symbols, HuffmanTable *out_table);

/* Serialize Huffman table into bitstream/buffer */
BufferError huffman_table_write(const HuffmanTable *table, Buffer *target);

/* Deserialize Huffman table from buffer/bitstream */
bool huffman_table_read(BitReader *reader, HuffmanTable *out_table);

/* Encode symbol using Huffman table */
BufferError huffman_encode_symbol(BitWriter *writer, const HuffmanTable *table, uint16_t symbol);

/* Decode symbol from bitstream using Huffman table */
bool huffman_decode_symbol(BitReader *reader, const HuffmanTable *table, uint16_t *out_symbol);

/* -------------------------------------------------------------
 * In-Loop Deblocking Filter
 * ------------------------------------------------------------- */

/* Apply in-loop deblocking filter on a single planar component across 8x8 block boundaries */
void deblock_filter_plane(uint8_t *plane, uint32_t stride, uint32_t width, uint32_t height, int quality);

/* Apply in-loop deblocking filter across all planes of a reconstructed frame */
void deblock_filter_frame(Frame *frame, int quality);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_COMPRESSOR_H */
