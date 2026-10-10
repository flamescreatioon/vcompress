#include "compressor.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 8x8 Zigzag table */
const uint8_t g_zigzag_order[BLOCK_AREA] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

const uint8_t g_unzigzag_order[BLOCK_AREA] = {
     0,  1,  5,  6, 14, 15, 27, 28,
     2,  4,  7, 13, 16, 26, 29, 42,
     3,  8, 12, 17, 25, 30, 41, 43,
     9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63
};

/* Standard JPEG base luminance matrix */
const uint8_t g_default_luma_qmatrix[BLOCK_AREA] = {
    16, 11, 10, 16,  24,  40,  51,  61,
    12, 12, 14, 19,  26,  58,  60,  55,
    14, 13, 16, 24,  40,  57,  69,  56,
    14, 17, 22, 29,  51,  87,  80,  62,
    18, 22, 37, 56,  68, 109, 103,  77,
    24, 35, 55, 64,  81, 104, 113,  92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103,  99
};

/* Standard JPEG base chrominance matrix */
const uint8_t g_default_chroma_qmatrix[BLOCK_AREA] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

/* Precomputed 1D DCT coefficients table: C[u][x] = alpha(u) * cos((2x+1)*u*pi / 16) */
static double s_dct_coeff[BLOCK_SIZE][BLOCK_SIZE];
static bool s_dct_coeff_inited = false;

#define DCT_SHIFT 13
#define DCT_ROUND (1 << (DCT_SHIFT - 1))
static int32_t s_dct_fixed[BLOCK_SIZE][BLOCK_SIZE];

static void init_dct_tables(void) {
    if (s_dct_coeff_inited) return;
    const double inv_sqrt2 = 1.0 / sqrt(2.0);
    for (int u = 0; u < BLOCK_SIZE; u++) {
        double alpha = (u == 0) ? inv_sqrt2 : 1.0;
        for (int x = 0; x < BLOCK_SIZE; x++) {
            double c = 0.5 * alpha * cos(((2.0 * (double)x + 1.0) * (double)u * M_PI) / 16.0);
            s_dct_coeff[u][x] = c;
            s_dct_fixed[u][x] = (int32_t)round(c * (double)(1 << DCT_SHIFT));
        }
    }
    s_dct_coeff_inited = true;
}

void dct_8x8(const int16_t input[BLOCK_AREA], double output[BLOCK_AREA]) {
    init_dct_tables();
    double temp[BLOCK_AREA];

    /* 1D DCT on rows: temp[y][u] = sum_x (input[y][x] * s_dct_coeff[u][x]) */
    for (int y = 0; y < BLOCK_SIZE; y++) {
        for (int u = 0; u < BLOCK_SIZE; u++) {
            double sum = 0.0;
            for (int x = 0; x < BLOCK_SIZE; x++) {
                sum += (double)input[y * BLOCK_SIZE + x] * s_dct_coeff[u][x];
            }
            temp[y * BLOCK_SIZE + u] = sum;
        }
    }

    /* 1D DCT on columns: output[v][u] = sum_y (temp[y][u] * s_dct_coeff[v][y]) */
    for (int u = 0; u < BLOCK_SIZE; u++) {
        for (int v = 0; v < BLOCK_SIZE; v++) {
            double sum = 0.0;
            for (int y = 0; y < BLOCK_SIZE; y++) {
                sum += temp[y * BLOCK_SIZE + u] * s_dct_coeff[v][y];
            }
            output[v * BLOCK_SIZE + u] = sum;
        }
    }
}

void idct_8x8(const double input[BLOCK_AREA], int16_t output[BLOCK_AREA]) {
    init_dct_tables();
    double temp[BLOCK_AREA];

    /* 1D IDCT on columns: temp[y][u] = sum_v (input[v][u] * s_dct_coeff[v][y]) */
    for (int u = 0; u < BLOCK_SIZE; u++) {
        for (int y = 0; y < BLOCK_SIZE; y++) {
            double sum = 0.0;
            for (int v = 0; v < BLOCK_SIZE; v++) {
                sum += input[v * BLOCK_SIZE + u] * s_dct_coeff[v][y];
            }
            temp[y * BLOCK_SIZE + u] = sum;
        }
    }

    /* 1D IDCT on rows: output[y][x] = sum_u (temp[y][u] * s_dct_coeff[u][x]) */
    for (int y = 0; y < BLOCK_SIZE; y++) {
        for (int x = 0; x < BLOCK_SIZE; x++) {
            double sum = 0.0;
            for (int u = 0; u < BLOCK_SIZE; u++) {
                sum += temp[y * BLOCK_SIZE + u] * s_dct_coeff[u][x];
            }
            int32_t val = (int32_t)floor(sum + 0.5);
            output[y * BLOCK_SIZE + x] = (int16_t)utils_clamp_i32(val, -2048, 2047);
        }
    }
}

void dct_8x8_fast(const int16_t input[BLOCK_AREA], int32_t output[BLOCK_AREA]) {
    init_dct_tables();
    int32_t temp[BLOCK_AREA];

    /* Fast 1D fixed-point DCT on rows */
    for (int y = 0; y < BLOCK_SIZE; y++) {
        const int16_t *in_row = &input[y * BLOCK_SIZE];
        int32_t *tmp_row = &temp[y * BLOCK_SIZE];
        for (int u = 0; u < BLOCK_SIZE; u++) {
            int32_t sum = 0;
            const int32_t *coeff = s_dct_fixed[u];
            for (int x = 0; x < BLOCK_SIZE; x++) {
                sum += (int32_t)in_row[x] * coeff[x];
            }
            tmp_row[u] = (sum + DCT_ROUND) >> DCT_SHIFT;
        }
    }

    /* Fast 1D fixed-point DCT on columns */
    for (int u = 0; u < BLOCK_SIZE; u++) {
        for (int v = 0; v < BLOCK_SIZE; v++) {
            int32_t sum = 0;
            const int32_t *coeff = s_dct_fixed[v];
            for (int y = 0; y < BLOCK_SIZE; y++) {
                sum += temp[y * BLOCK_SIZE + u] * coeff[y];
            }
            output[v * BLOCK_SIZE + u] = (sum + DCT_ROUND) >> DCT_SHIFT;
        }
    }
}

void idct_8x8_fast(const int32_t input[BLOCK_AREA], int16_t output[BLOCK_AREA]) {
    init_dct_tables();
    int32_t temp[BLOCK_AREA];

    /* Fast 1D fixed-point IDCT on columns */
    for (int u = 0; u < BLOCK_SIZE; u++) {
        for (int y = 0; y < BLOCK_SIZE; y++) {
            int32_t sum = 0;
            for (int v = 0; v < BLOCK_SIZE; v++) {
                sum += input[v * BLOCK_SIZE + u] * s_dct_fixed[v][y];
            }
            temp[y * BLOCK_SIZE + u] = (sum + DCT_ROUND) >> DCT_SHIFT;
        }
    }

    /* Fast 1D fixed-point IDCT on rows */
    for (int y = 0; y < BLOCK_SIZE; y++) {
        int16_t *out_row = &output[y * BLOCK_SIZE];
        for (int x = 0; x < BLOCK_SIZE; x++) {
            int32_t sum = 0;
            for (int u = 0; u < BLOCK_SIZE; u++) {
                sum += temp[y * BLOCK_SIZE + u] * s_dct_fixed[u][x];
            }
            int32_t val = (sum + DCT_ROUND) >> DCT_SHIFT;
            out_row[x] = (int16_t)utils_clamp_i32(val, -2048, 2047);
        }
    }
}

void quant_matrix_init(uint16_t qmatrix[BLOCK_AREA], const uint8_t base_matrix[BLOCK_AREA], int quality) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    int scale = (quality < 50) ? (5000 / quality) : (200 - 2 * quality);

    for (int i = 0; i < BLOCK_AREA; i++) {
        int32_t q = ((int32_t)base_matrix[i] * scale + 50) / 100;
        if (q < 1) q = 1;
        if (q > 255) q = 255;
        qmatrix[i] = (uint16_t)q;
    }
}

uint32_t calc_block_variance_u8(const uint8_t *pixels, uint32_t stride, uint32_t width, uint32_t height, uint32_t px0, uint32_t py0) {
    if (!pixels) return 0;

    int32_t sum = 0;
    for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
        uint32_t py = (py0 + y < height) ? (py0 + y) : (height - 1);
        const uint8_t *row = pixels + (size_t)py * stride;
        for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
            uint32_t px = (px0 + x < width) ? (px0 + x) : (width - 1);
            sum += (int32_t)row[px];
        }
    }

    int32_t mean = (sum + (BLOCK_AREA / 2)) / BLOCK_AREA;
    int64_t diff_sq = 0;

    for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
        uint32_t py = (py0 + y < height) ? (py0 + y) : (height - 1);
        const uint8_t *row = pixels + (size_t)py * stride;
        for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
            uint32_t px = (px0 + x < width) ? (px0 + x) : (width - 1);
            int32_t d = (int32_t)row[px] - mean;
            diff_sq += (int64_t)d * d;
        }
    }

    return (uint32_t)(diff_sq / BLOCK_AREA);
}

void quantize_block_aq(const int32_t in_coeff[BLOCK_AREA], const uint16_t qmatrix[BLOCK_AREA],
                       uint32_t variance, int16_t out_quant[BLOCK_AREA]) {
    /* DC coefficient: standard mid-tread rounding to preserve mean luminance */
    int32_t c0 = in_coeff[0];
    int32_t q0 = (int32_t)qmatrix[0];
    out_quant[0] = (int16_t)((c0 >= 0) ? ((c0 + (q0 / 2)) / q0) : ((c0 - (q0 / 2)) / q0));

    /* AC coefficients: Adaptive Quantization (AQ) dead-zone thresholding.
     * Modulates dead-zone threshold and rounding offset based on local spatial variance:
     * - Flat/smooth gradient blocks (variance < 25): narrow dead-zone (3/8 * q) to retain
     *   subtle gradient harmonics and eliminate false contouring (color banding).
     * - Textured/complex blocks (variance > 140): wide dead-zone (6/8 * q) to eliminate
     *   imperceptible noise masked by high-frequency energy, boosting compression ratio.
     * - Standard blocks: balanced dead-zone (5/8 * q). */
    int32_t thresh_num;
    int32_t offset_num;

    if (variance < 25) {
        thresh_num = 3; /* 3/8 * q: narrower dead-zone preserves gradient continuity */
        offset_num = 4; /* 4/8 * q: mid-tread rounding */
    } else if (variance > 140) {
        thresh_num = 6; /* 6/8 * q: wider dead-zone for texture masking */
        offset_num = 2; /* 2/8 * q: aggressive noise truncation */
    } else {
        thresh_num = 5; /* 5/8 * q: balanced default dead-zone */
        offset_num = 3; /* 3/8 * q */
    }

    for (int i = 1; i < BLOCK_AREA; i++) {
        int32_t c = in_coeff[i];
        int32_t q = (int32_t)qmatrix[i];
        int32_t abs_c = (c < 0) ? -c : c;
        int32_t thresh = (q * thresh_num) / 8;

        if (abs_c < thresh) {
            out_quant[i] = 0;
        } else {
            int32_t val = (abs_c + (q * offset_num) / 8) / q;
            out_quant[i] = (int16_t)((c < 0) ? -val : val);
        }
    }
}

void quantize_block(const int32_t in_coeff[BLOCK_AREA], const uint16_t qmatrix[BLOCK_AREA], int16_t out_quant[BLOCK_AREA]) {
    quantize_block_aq(in_coeff, qmatrix, 64, out_quant);
}

void dequantize_block(const int16_t in_quant[BLOCK_AREA], const uint16_t qmatrix[BLOCK_AREA], int32_t out_coeff[BLOCK_AREA]) {
    for (int i = 0; i < BLOCK_AREA; i++) {
        out_coeff[i] = (int32_t)in_quant[i] * (int32_t)qmatrix[i];
    }
}

void zigzag_reorder(const int16_t in_block[BLOCK_AREA], int16_t out_zigzag[BLOCK_AREA]) {
    for (int i = 0; i < BLOCK_AREA; i++) {
        out_zigzag[i] = in_block[g_zigzag_order[i]];
    }
}

void unzigzag_reorder(const int16_t in_zigzag[BLOCK_AREA], int16_t out_block[BLOCK_AREA]) {
    for (int i = 0; i < BLOCK_AREA; i++) {
        out_block[g_zigzag_order[i]] = in_zigzag[i];
    }
}

/* -------------------------------------------------------------
 * RLE Implementation
 * ------------------------------------------------------------- */

size_t rle_encode_block(const int16_t zigzag[BLOCK_AREA], RLESymbol *symbols, size_t max_symbols) {
    if (!symbols || max_symbols == 0) return 0;
    size_t count = 0;

    /* DC coefficient */
    symbols[count].run = 0;
    symbols[count].value = zigzag[0];
    count++;

    /* Find last non-zero AC coefficient */
    int last_non_zero = 0;
    for (int i = BLOCK_AREA - 1; i >= 1; i--) {
        if (zigzag[i] != 0) {
            last_non_zero = i;
            break;
        }
    }

    if (last_non_zero == 0) {
        /* All AC coefficients are zero: just write EOB */
        if (count < max_symbols) {
            symbols[count].run = 0;
            symbols[count].value = 0; /* EOB */
            count++;
        }
        return count;
    }

    uint8_t zero_run = 0;
    for (int i = 1; i <= last_non_zero; i++) {
        if (count >= max_symbols - 1) break;

        if (zigzag[i] == 0) {
            zero_run++;
            if (zero_run == 16) {
                /* ZRL symbol (16 zeros) */
                symbols[count].run = 15;
                symbols[count].value = 0;
                count++;
                zero_run = 0;
            }
        } else {
            symbols[count].run = zero_run;
            symbols[count].value = zigzag[i];
            count++;
            zero_run = 0;
        }
    }

    /* EOB if block didn't end at 63 */
    if (last_non_zero < BLOCK_AREA - 1 && count < max_symbols) {
        symbols[count].run = 0;
        symbols[count].value = 0; /* EOB */
        count++;
    }

    return count;
}

size_t rle_decode_block(const RLESymbol *symbols, size_t num_symbols, int16_t zigzag[BLOCK_AREA]) {
    memset(zigzag, 0, sizeof(int16_t) * BLOCK_AREA);
    if (!symbols || num_symbols == 0) return 0;

    zigzag[0] = symbols[0].value;
    int idx = 1;
    size_t sym_idx = 1;

    while (sym_idx < num_symbols && idx < BLOCK_AREA) {
        uint8_t run = symbols[sym_idx].run;
        int16_t val = symbols[sym_idx].value;
        sym_idx++;

        if (run == 0 && val == 0) {
            /* EOB: remaining are zero */
            break;
        }

        idx += run;
        if (idx < BLOCK_AREA) {
            zigzag[idx] = val;
            idx++;
        }
    }

    return sym_idx;
}

/* -------------------------------------------------------------
 * Huffman Coding Implementation
 * ------------------------------------------------------------- */

typedef struct HeapNode {
    uint32_t freq;
    uint16_t symbol;
    struct HeapNode *left;
    struct HeapNode *right;
} HeapNode;

static void generate_codes(const HeapNode *node, uint32_t current_code, uint8_t depth, HuffmanTable *table) {
    if (!node) return;
    if (!node->left && !node->right) {
        /* Leaf node */
        for (uint16_t i = 0; i < table->num_symbols; i++) {
            if (table->symbols[i] == node->symbol) {
                table->codes[i].code = current_code;
                table->codes[i].length = (depth == 0) ? 1 : depth;
                break;
            }
        }
        return;
    }
    generate_codes(node->left, (current_code << 1) | 0, depth + 1, table);
    generate_codes(node->right, (current_code << 1) | 1, depth + 1, table);
}

bool huffman_build_table(const uint32_t *freqs, const uint16_t *symbols, size_t num_symbols, HuffmanTable *out_table) {
    if (!freqs || !symbols || num_symbols == 0 || num_symbols > HUFFMAN_MAX_SYMBOLS || !out_table) {
        return false;
    }

    memset(out_table, 0, sizeof(HuffmanTable));
    out_table->num_symbols = (uint16_t)num_symbols;
    for (size_t i = 0; i < num_symbols; i++) {
        out_table->symbols[i] = symbols[i];
    }

    if (num_symbols == 1) {
        out_table->codes[0].code = 0;
        out_table->codes[0].length = 1;
        return true;
    }

    /* Initialize min-heap */
    HeapNode nodes[HUFFMAN_MAX_SYMBOLS * 2];
    size_t node_count = 0;
    HeapNode *heap[HUFFMAN_MAX_SYMBOLS * 2];
    size_t heap_size = 0;

    for (size_t i = 0; i < num_symbols; i++) {
        HeapNode *n = &nodes[node_count++];
        n->freq = (freqs[i] == 0) ? 1 : freqs[i];
        n->symbol = symbols[i];
        n->left = NULL;
        n->right = NULL;
        heap[heap_size++] = n;
    }

    /* Build Huffman tree by repeatedly combining two lowest frequencies */
    while (heap_size > 1) {
        /* Find min 1 */
        size_t min1 = 0;
        for (size_t i = 1; i < heap_size; i++) {
            if (heap[i]->freq < heap[min1]->freq) min1 = i;
        }
        HeapNode *n1 = heap[min1];
        heap[min1] = heap[--heap_size];

        /* Find min 2 */
        size_t min2 = 0;
        for (size_t i = 1; i < heap_size; i++) {
            if (heap[i]->freq < heap[min2]->freq) min2 = i;
        }
        HeapNode *n2 = heap[min2];
        heap[min2] = heap[--heap_size];

        /* Create parent node */
        HeapNode *parent = &nodes[node_count++];
        parent->freq = n1->freq + n2->freq;
        parent->symbol = 0;
        parent->left = n1;
        parent->right = n2;

        heap[heap_size++] = parent;
    }

    HeapNode *root = heap[0];
    generate_codes(root, 0, 0, out_table);
    return true;
}

BufferError huffman_table_write(const HuffmanTable *table, Buffer *target) {
    if (!table || !target) return BUF_ERR_PARAM;
    BufferError err = buffer_append_u16_be(target, table->num_symbols);
    if (err != BUF_OK) return err;

    for (uint16_t i = 0; i < table->num_symbols; i++) {
        err = buffer_append_u16_be(target, table->symbols[i]);
        if (err != BUF_OK) return err;

        err = buffer_append_u8(target, table->codes[i].length);
        if (err != BUF_OK) return err;

        err = buffer_append_u32_be(target, table->codes[i].code);
        if (err != BUF_OK) return err;
    }
    return BUF_OK;
}

bool huffman_table_read(BitReader *reader, HuffmanTable *out_table) {
    if (!reader || !out_table) return false;
    uint32_t num_syms = 0;
    if (!bit_reader_get_bits(reader, 16, &num_syms)) return false;
    if (num_syms > HUFFMAN_MAX_SYMBOLS) return false;

    out_table->num_symbols = (uint16_t)num_syms;
    for (uint16_t i = 0; i < num_syms; i++) {
        uint32_t sym = 0;
        uint32_t len = 0;
        uint32_t code = 0;
        if (!bit_reader_get_bits(reader, 16, &sym)) return false;
        if (!bit_reader_get_bits(reader, 8, &len)) return false;
        if (!bit_reader_get_bits(reader, 32, &code)) return false;

        out_table->symbols[i] = (uint16_t)sym;
        out_table->codes[i].length = (uint8_t)len;
        out_table->codes[i].code = code;
    }
    return true;
}

BufferError huffman_encode_symbol(BitWriter *writer, const HuffmanTable *table, uint16_t symbol) {
    if (!writer || !table) return BUF_ERR_PARAM;
    for (uint16_t i = 0; i < table->num_symbols; i++) {
        if (table->symbols[i] == symbol) {
            return bit_writer_put_bits(writer, table->codes[i].code, table->codes[i].length);
        }
    }
    return BUF_ERR_BOUNDS;
}

bool huffman_decode_symbol(BitReader *reader, const HuffmanTable *table, uint16_t *out_symbol) {
    if (!reader || !table || !out_symbol) return false;
    uint32_t cur_code = 0;
    uint8_t cur_len = 0;

    while (cur_len < 32) {
        uint8_t bit = 0;
        if (!bit_reader_get_bit(reader, &bit)) return false;
        cur_code = (cur_code << 1) | bit;
        cur_len++;

        for (uint16_t i = 0; i < table->num_symbols; i++) {
            if (table->codes[i].length == cur_len && table->codes[i].code == cur_code) {
                *out_symbol = table->symbols[i];
                return true;
            }
        }
    }
    return false;
}

/* -------------------------------------------------------------
 * In-Loop Deblocking Filter
 * ------------------------------------------------------------- */

void deblock_filter_plane(uint8_t *plane, uint32_t stride, uint32_t width, uint32_t height, int quality) {
    if (!plane || width < 16 || height < 16 || quality >= 98) {
        return;
    }

    int qp_proxy = 100 - quality;
    if (qp_proxy < 1) qp_proxy = 1;

    /* Adaptive filtering thresholds based on quantization strength */
    int alpha = 4 + (qp_proxy >> 2);     /* Boundary difference threshold */
    int beta  = 2 + (qp_proxy >> 3);     /* Internal surface flatness threshold */
    int max_c = 1 + (qp_proxy >> 4);     /* Maximum boundary sample adjustment */
    int max_c_inner = max_c >> 1;

    /* 1. Filter Vertical 8x8 block edges */
    for (uint32_t bx = BLOCK_SIZE; bx < width; bx += BLOCK_SIZE) {
        for (uint32_t y = 0; y < height; y++) {
            uint8_t *row = plane + (size_t)y * stride;

            int p0 = (int)row[bx - 1];
            int p1 = (int)row[bx - 2];
            int p2 = (bx >= 3) ? (int)row[bx - 3] : p1;

            int q0 = (int)row[bx];
            int q1 = (bx + 1 < width) ? (int)row[bx + 1] : q0;
            int q2 = (bx + 2 < width) ? (int)row[bx + 2] : q1;

            int d0 = abs(p0 - q0);
            if (d0 == 0 || d0 >= alpha) continue;

            int dp = abs(p1 - p0);
            int dq = abs(q1 - q0);
            if (dp >= beta || dq >= beta) continue;

            /* Filter boundary samples p0 and q0 */
            int delta = (4 * (q0 - p0) + (p1 - q1) + 4) >> 3;
            if (delta > max_c) delta = max_c;
            else if (delta < -max_c) delta = -max_c;

            row[bx - 1] = utils_clamp_u8(p0 + delta);
            row[bx]     = utils_clamp_u8(q0 - delta);

            /* Filter inner samples p1 and q1 if regions are exceptionally flat */
            if (max_c_inner > 0) {
                if (abs(p2 - p0) < beta) {
                    int delta_p = (p2 + ((p0 + q0 + 1) >> 1) - 2 * p1) >> 1;
                    if (delta_p > max_c_inner) delta_p = max_c_inner;
                    else if (delta_p < -max_c_inner) delta_p = -max_c_inner;
                    row[bx - 2] = utils_clamp_u8(p1 + delta_p);
                }
                if (abs(q2 - q0) < beta && (bx + 1 < width)) {
                    int delta_q = (q2 + ((p0 + q0 + 1) >> 1) - 2 * q1) >> 1;
                    if (delta_q > max_c_inner) delta_q = max_c_inner;
                    else if (delta_q < -max_c_inner) delta_q = -max_c_inner;
                    row[bx + 1] = utils_clamp_u8(q1 + delta_q);
                }
            }
        }
    }

    /* 2. Filter Horizontal 8x8 block edges */
    for (uint32_t by = BLOCK_SIZE; by < height; by += BLOCK_SIZE) {
        uint8_t *row_p0 = plane + (size_t)(by - 1) * stride;
        uint8_t *row_p1 = plane + (size_t)(by - 2) * stride;
        uint8_t *row_p2 = (by >= 3) ? plane + (size_t)(by - 3) * stride : row_p1;

        uint8_t *row_q0 = plane + (size_t)by * stride;
        uint8_t *row_q1 = (by + 1 < height) ? plane + (size_t)(by + 1) * stride : row_q0;
        uint8_t *row_q2 = (by + 2 < height) ? plane + (size_t)(by + 2) * stride : row_q1;

        for (uint32_t x = 0; x < width; x++) {
            int p0 = (int)row_p0[x];
            int p1 = (int)row_p1[x];
            int p2 = (int)row_p2[x];

            int q0 = (int)row_q0[x];
            int q1 = (int)row_q1[x];
            int q2 = (int)row_q2[x];

            int d0 = abs(p0 - q0);
            if (d0 == 0 || d0 >= alpha) continue;

            int dp = abs(p1 - p0);
            int dq = abs(q1 - q0);
            if (dp >= beta || dq >= beta) continue;

            /* Filter boundary samples p0 and q0 */
            int delta = (4 * (q0 - p0) + (p1 - q1) + 4) >> 3;
            if (delta > max_c) delta = max_c;
            else if (delta < -max_c) delta = -max_c;

            row_p0[x] = utils_clamp_u8(p0 + delta);
            row_q0[x] = utils_clamp_u8(q0 - delta);

            /* Filter inner samples p1 and q1 if regions are exceptionally flat */
            if (max_c_inner > 0) {
                if (abs(p2 - p0) < beta) {
                    int delta_p = (p2 + ((p0 + q0 + 1) >> 1) - 2 * p1) >> 1;
                    if (delta_p > max_c_inner) delta_p = max_c_inner;
                    else if (delta_p < -max_c_inner) delta_p = -max_c_inner;
                    row_p1[x] = utils_clamp_u8(p1 + delta_p);
                }
                if (abs(q2 - q0) < beta && (by + 1 < height)) {
                    int delta_q = (q2 + ((p0 + q0 + 1) >> 1) - 2 * q1) >> 1;
                    if (delta_q > max_c_inner) delta_q = max_c_inner;
                    else if (delta_q < -max_c_inner) delta_q = -max_c_inner;
                    row_q1[x] = utils_clamp_u8(q1 + delta_q);
                }
            }
        }
    }
}

void deblock_filter_frame(Frame *frame, int quality) {
    if (!frame) return;
    int planes = (frame->format == PIX_FMT_RGB24) ? 1 : 3;
    for (int p = 0; p < planes; p++) {
        deblock_filter_plane(frame->data[p], frame->stride[p],
                             frame->plane_width[p], frame->plane_height[p], quality);
    }
}
