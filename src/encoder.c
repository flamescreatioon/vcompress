#include "encoder.h"
#include "compressor.h"
#include "thread_pool.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

typedef struct BlockData {
    int16_t zigzag[BLOCK_AREA];
    uint8_t skip; /* 1 if block skipped in P-frame, 0 if coded */
} BlockData;

struct VideoEncoder {
    EncoderConfig config;
    PixelFormat internal_format;
    uint32_t frame_index;
    Frame *ref_frame; /* Reconstructed reference frame */

    uint16_t qmatrix_luma[BLOCK_AREA];
    uint16_t qmatrix_chroma[BLOCK_AREA];

    ThreadPool *pool;
};

VideoEncoder *encoder_create(const EncoderConfig *config) {
    if (!config || config->width == 0 || config->height == 0) return NULL;

    VideoEncoder *enc = (VideoEncoder *)calloc(1, sizeof(VideoEncoder));
    if (!enc) return NULL;

    enc->config = *config;
    enc->internal_format = (config->format == PIX_FMT_RGB24) ? PIX_FMT_YUV444 : config->format;
    if (enc->config.gop_size == 0) enc->config.gop_size = 15;
    if (enc->config.quality < 1) enc->config.quality = 1;
    if (enc->config.quality > 100) enc->config.quality = 100;
    if (enc->config.inter_threshold <= 0) enc->config.inter_threshold = 128;

    quant_matrix_init(enc->qmatrix_luma, g_default_luma_qmatrix, enc->config.quality);
    quant_matrix_init(enc->qmatrix_chroma, g_default_chroma_qmatrix, enc->config.quality);

    enc->ref_frame = frame_create(enc->config.width, enc->config.height, enc->internal_format);
    if (!enc->ref_frame) {
        free(enc);
        return NULL;
    }

    if (enc->config.threads > 1) {
        enc->pool = thread_pool_create(enc->config.threads);
    }

    return enc;
}

void encoder_destroy(VideoEncoder *enc) {
    if (!enc) return;
    if (enc->pool) {
        thread_pool_destroy(enc->pool);
        enc->pool = NULL;
    }
    if (enc->ref_frame) {
        frame_destroy(enc->ref_frame);
        enc->ref_frame = NULL;
    }
    free(enc);
}

typedef struct BlockTaskArgs {
    const uint8_t *cur_plane;
    uint8_t *ref_plane;
    uint32_t cur_stride;
    uint32_t ref_stride;
    uint32_t plane_w;
    uint32_t plane_h;
    const uint16_t *qmatrix;
    bool is_p_frame;
    int inter_threshold;
    uint32_t lambda;
    uint32_t block_x_start;
    uint32_t block_x_end;
    uint32_t block_y;
    uint32_t num_blocks_x;
    BlockData *blocks;
} BlockTaskArgs;

static void process_block_row(void *arg) {
    BlockTaskArgs *args = (BlockTaskArgs *)arg;
    int16_t raw_block[BLOCK_AREA];
    int32_t dct_coeff[BLOCK_AREA];
    int16_t quant_coeff[BLOCK_AREA];
    int32_t dequant_coeff[BLOCK_AREA];
    int16_t recon_block[BLOCK_AREA];

    uint32_t by = args->block_y;

    for (uint32_t bx = args->block_x_start; bx < args->block_x_end; bx++) {
        size_t b_idx = (size_t)by * args->num_blocks_x + bx;
        BlockData *bd = &args->blocks[b_idx];

        uint32_t px0 = bx * BLOCK_SIZE;
        uint32_t py0 = by * BLOCK_SIZE;

        /* Calculate block spatial variance for Adaptive Quantization (AQ) */
        uint32_t block_var = calc_block_variance_u8(args->cur_plane, args->cur_stride,
                                                    args->plane_w, args->plane_h, px0, py0);

        /* P-Frame Rate-Distortion Optimization (RDO) */
        if (args->is_p_frame) {
            /* 1. Calculate Distortion for SKIP mode: Sum of Squared Errors (SSE) */
            uint64_t sse_skip = 0;
            for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
                uint32_t py = (py0 + y < args->plane_h) ? (py0 + y) : (args->plane_h - 1);
                const uint8_t *cur_row = args->cur_plane + (size_t)py * args->cur_stride;
                const uint8_t *ref_row = args->ref_plane + (size_t)py * args->ref_stride;

                for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
                    uint32_t px = (px0 + x < args->plane_w) ? (px0 + x) : (args->plane_w - 1);
                    int32_t diff = (int32_t)cur_row[px] - (int32_t)ref_row[px];
                    sse_skip += (uint64_t)(diff * diff);
                    raw_block[y * BLOCK_SIZE + x] = (int16_t)diff;
                }
            }

            /* Fast skip: if block is identical or within skip tolerance */
            if (sse_skip == 0 || sse_skip <= (uint64_t)args->inter_threshold) {
                bd->skip = 1;
                memset(bd->zigzag, 0, sizeof(bd->zigzag));
                continue;
            }

            /* 2. Candidate Transform & Adaptive Quantization (AQ) */
            dct_8x8_fast(raw_block, dct_coeff);
            quantize_block_aq(dct_coeff, args->qmatrix, block_var, quant_coeff);

            /* Check if all AC & DC coefficients quantized to zero */
            bool all_zero = true;
            for (int i = 0; i < BLOCK_AREA; i++) {
                if (quant_coeff[i] != 0) {
                    all_zero = false;
                    break;
                }
            }

            if (all_zero) {
                /* Quantization eliminated entire residual: skip costs 1 bit vs transmitting empty block */
                bd->skip = 1;
                memset(bd->zigzag, 0, sizeof(bd->zigzag));
                continue;
            }

            /* 3. Reconstruct candidate block and evaluate Distortion for CODED mode */
            dequantize_block(quant_coeff, args->qmatrix, dequant_coeff);
            idct_8x8_fast(dequant_coeff, recon_block);

            uint64_t sse_coded = 0;
            for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
                uint32_t py = (py0 + y < args->plane_h) ? (py0 + y) : (args->plane_h - 1);
                const uint8_t *cur_row = args->cur_plane + (size_t)py * args->cur_stride;
                const uint8_t *ref_row = args->ref_plane + (size_t)py * args->ref_stride;

                for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
                    uint32_t px = (px0 + x < args->plane_w) ? (px0 + x) : (args->plane_w - 1);
                    int32_t recon_val = utils_clamp_u8((int32_t)ref_row[px] + recon_block[y * BLOCK_SIZE + x]);
                    int32_t diff = (int32_t)cur_row[px] - recon_val;
                    sse_coded += (uint64_t)(diff * diff);
                }
            }

            /* 4. Estimate Bitrate (R) for CODED mode */
            zigzag_reorder(quant_coeff, bd->zigzag);
            RLESymbol candidate_syms[MAX_BLOCK_SYMBOLS];
            size_t n_syms = rle_encode_block(bd->zigzag, candidate_syms, MAX_BLOCK_SYMBOLS);

            uint32_t rate_bits = 1; /* 1 bit for skip=0 header */
            for (size_t s = 0; s < n_syms; s++) {
                int16_t v = candidate_syms[s].value;
                uint8_t cat = 0;
                if (v != 0) {
                    int32_t abs_v = (v < 0) ? -v : v;
                    while (abs_v > 0) {
                        cat++;
                        abs_v >>= 1;
                    }
                }
                rate_bits += 4 + (uint32_t)cat; /* ~4 bits for Huffman symbol + magnitude bits */
            }

            /* 5. Lagrangian Cost Evaluation: J = D + lambda * R */
            uint64_t cost_skip = sse_skip + (uint64_t)args->lambda * 1;
            uint64_t cost_coded = sse_coded + (uint64_t)args->lambda * (uint64_t)rate_bits;

            if (cost_skip <= cost_coded) {
                /* Lagrangian RDO favors SKIP */
                bd->skip = 1;
                memset(bd->zigzag, 0, sizeof(bd->zigzag));
                continue;
            }

            /* Lagrangian RDO favors CODED */
            bd->skip = 0;
            for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
                uint32_t py = py0 + y;
                if (py >= args->plane_h) continue;
                uint8_t *ref_row = args->ref_plane + (size_t)py * args->ref_stride;
                for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
                    uint32_t px = px0 + x;
                    if (px >= args->plane_w) continue;
                    ref_row[px] = utils_clamp_u8((int32_t)ref_row[px] + recon_block[y * BLOCK_SIZE + x]);
                }
            }
            continue;
        }

        /* Intra Frame block */
        bd->skip = 0;
        for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
            uint32_t py = (py0 + y < args->plane_h) ? (py0 + y) : (args->plane_h - 1);
            const uint8_t *cur_row = args->cur_plane + (size_t)py * args->cur_stride;

            for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
                uint32_t px = (px0 + x < args->plane_w) ? (px0 + x) : (args->plane_w - 1);
                raw_block[y * BLOCK_SIZE + x] = (int16_t)((int32_t)cur_row[px] - 128);
            }
        }

        /* 2D DCT -> Adaptive Quantization (AQ) -> Zigzag */
        dct_8x8_fast(raw_block, dct_coeff);
        quantize_block_aq(dct_coeff, args->qmatrix, block_var, quant_coeff);
        zigzag_reorder(quant_coeff, bd->zigzag);

        /* Reconstruct local reference block to keep encoder & decoder synchronized */
        dequantize_block(quant_coeff, args->qmatrix, dequant_coeff);
        idct_8x8_fast(dequant_coeff, recon_block);

        for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
            uint32_t py = py0 + y;
            if (py >= args->plane_h) continue;

            uint8_t *ref_row = args->ref_plane + (size_t)py * args->ref_stride;
            for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
                uint32_t px = px0 + x;
                if (px >= args->plane_w) continue;

                int32_t val = (int32_t)recon_block[y * BLOCK_SIZE + x] + 128;
                ref_row[px] = utils_clamp_u8(val);
            }
        }
    }
}

bool encoder_encode_frame(VideoEncoder *enc, const Frame *frame, Buffer *out_packet, bool *is_keyframe) {
    if (!enc || !frame || !out_packet) return false;

    /* Check geometry */
    if (frame->width != enc->config.width || frame->height != enc->config.height) {
        return false;
    }

    bool keyframe = (enc->frame_index == 0) || (enc->frame_index % enc->config.gop_size == 0);
    if (is_keyframe) *is_keyframe = keyframe;

    uint8_t frame_type = keyframe ? FRAME_TYPE_I : FRAME_TYPE_P;

    /* Frame bitstream packet layout:
     * [0] Frame type (0 = I, 1 = P)
     * Followed by Huffman table and bitstream
     */
    buffer_clear(out_packet);
    buffer_append_u8(out_packet, frame_type);

    Frame *conv_frame = NULL;
    const Frame *input_frame = frame;
    if (frame->format != enc->internal_format) {
        conv_frame = frame_convert(frame, enc->internal_format);
        if (!conv_frame) return false;
        input_frame = conv_frame;
    }

    int planes = 3;

    /* Process each plane */
    for (int p = 0; p < planes; p++) {
        uint32_t pw = input_frame->plane_width[p];
        uint32_t ph = input_frame->plane_height[p];
        uint32_t num_blocks_x = (pw + BLOCK_SIZE - 1) / BLOCK_SIZE;
        uint32_t num_blocks_y = (ph + BLOCK_SIZE - 1) / BLOCK_SIZE;
        size_t total_blocks = (size_t)num_blocks_x * num_blocks_y;

        BlockData *blocks = (BlockData *)calloc(total_blocks, sizeof(BlockData));
        if (!blocks) {
            if (conv_frame) frame_destroy(conv_frame);
            return false;
        }

        const uint16_t *qmat = (p == 0) ? enc->qmatrix_luma : enc->qmatrix_chroma;

        /* Calculate Lagrangian lambda for Rate-Distortion Optimization (RDO)
         * lambda is proportional to average quantization step squared */
        uint64_t q_sum = 0;
        for (int i = 0; i < BLOCK_AREA; i++) {
            q_sum += qmat[i];
        }
        uint32_t q_avg = (uint32_t)(q_sum / BLOCK_AREA);
        if (q_avg < 1) q_avg = 1;
        uint32_t lambda = (q_avg * q_avg) / 16;
        if (lambda < 1) lambda = 1;

        if (enc->pool) {
            BlockTaskArgs *args_list = (BlockTaskArgs *)malloc(num_blocks_y * sizeof(BlockTaskArgs));
            for (uint32_t by = 0; by < num_blocks_y; by++) {
                BlockTaskArgs *args = &args_list[by];
                args->cur_plane = input_frame->data[p];
                args->ref_plane = enc->ref_frame->data[p];
                args->cur_stride = input_frame->stride[p];
                args->ref_stride = enc->ref_frame->stride[p];
                args->plane_w = pw;
                args->plane_h = ph;
                args->qmatrix = qmat;
                args->is_p_frame = !keyframe;
                args->inter_threshold = enc->config.inter_threshold;
                args->lambda = lambda;
                args->block_x_start = 0;
                args->block_x_end = num_blocks_x;
                args->block_y = by;
                args->num_blocks_x = num_blocks_x;
                args->blocks = blocks;

                thread_pool_submit(enc->pool, process_block_row, args);
            }
            thread_pool_wait(enc->pool);
            free(args_list);
        } else {
            for (uint32_t by = 0; by < num_blocks_y; by++) {
                BlockTaskArgs args;
                args.cur_plane = input_frame->data[p];
                args.ref_plane = enc->ref_frame->data[p];
                args.cur_stride = input_frame->stride[p];
                args.ref_stride = enc->ref_frame->stride[p];
                args.plane_w = pw;
                args.plane_h = ph;
                args.qmatrix = qmat;
                args.is_p_frame = !keyframe;
                args.inter_threshold = enc->config.inter_threshold;
                args.lambda = lambda;
                args.block_x_start = 0;
                args.block_x_end = num_blocks_x;
                args.block_y = by;
                args.num_blocks_x = num_blocks_x;
                args.blocks = blocks;

                process_block_row(&args);
            }
        }

        /* -------------------------------------------------------------
         * Entropy Coding Stage:
         * 1. Collect symbol histogram from all coded blocks
         * 2. Build canonical Huffman tree
         * 3. Serialize Huffman table into bitstream
         * 4. Encode skip mask (for P-frames) and RLE symbols
         * ------------------------------------------------------------- */

        /* Allocate RLE symbols storage */
        RLESymbol *all_symbols = (RLESymbol *)malloc(total_blocks * MAX_BLOCK_SYMBOLS * sizeof(RLESymbol));
        size_t total_symbols = 0;

        uint32_t freqs[256] = {0};

        for (size_t b = 0; b < total_blocks; b++) {
            if (!keyframe && blocks[b].skip) continue;

            RLESymbol block_syms[MAX_BLOCK_SYMBOLS];
            size_t n_syms = rle_encode_block(blocks[b].zigzag, block_syms, MAX_BLOCK_SYMBOLS);

            for (size_t s = 0; s < n_syms; s++) {
                all_symbols[total_symbols++] = block_syms[s];

                /* Map (run, val_mag) to 8-bit alphabet */
                uint8_t run = block_syms[s].run & 0x0F;
                int16_t v = block_syms[s].value;
                uint8_t cat = 0;
                if (v != 0) {
                    int32_t abs_v = (v < 0) ? -v : v;
                    while (abs_v > 0) {
                        cat++;
                        abs_v >>= 1;
                    }
                }
                uint8_t code_sym = (uint8_t)((run << 4) | (cat & 0x0F));
                freqs[code_sym]++;
            }
        }

        /* Collect active symbols */
        uint32_t active_freqs[256];
        uint16_t active_syms[256];
        size_t num_active = 0;

        if (freqs[0] == 0) freqs[0] = 1; /* Ensure EOB symbol 0 always exists */

        for (int i = 0; i < 256; i++) {
            if (freqs[i] > 0) {
                active_freqs[num_active] = freqs[i];
                active_syms[num_active] = (uint16_t)i;
                num_active++;
            }
        }

        HuffmanTable huff_table;
        huffman_build_table(active_freqs, active_syms, num_active, &huff_table);

        /* Write serialized Huffman table to out_packet */
        huffman_table_write(&huff_table, out_packet);

        /* Write Bitstream: Skip mask + Huffman codes */
        BitWriter bw;
        bit_writer_init(&bw, out_packet);

        /* If P-frame, write skip bits for each block */
        if (!keyframe) {
            for (size_t b = 0; b < total_blocks; b++) {
                bit_writer_put_bits(&bw, blocks[b].skip ? 1 : 0, 1);
            }
        }

        /* Write encoded symbols */
        for (size_t s = 0; s < total_symbols; s++) {
            uint8_t run = all_symbols[s].run & 0x0F;
            int16_t v = all_symbols[s].value;
            uint8_t cat = 0;
            if (v != 0) {
                int32_t abs_v = (v < 0) ? -v : v;
                while (abs_v > 0) {
                    cat++;
                    abs_v >>= 1;
                }
            }
            uint8_t code_sym = (uint8_t)((run << 4) | (cat & 0x0F));
            huffman_encode_symbol(&bw, &huff_table, code_sym);

            if (cat > 0) {
                /* Write sign and magnitude bits */
                uint32_t val_bits = (v > 0) ? (uint32_t)v : (uint32_t)((1 << cat) - 1 + v);
                bit_writer_put_bits(&bw, val_bits, cat);
            }
        }

        bit_writer_flush(&bw);

        free(all_symbols);
        free(blocks);
    }

    /* Apply in-loop deblocking filter on reconstructed reference frame */
    deblock_filter_frame(enc->ref_frame, enc->config.quality);

    if (conv_frame) frame_destroy(conv_frame);
    enc->frame_index++;
    return true;
}

bool encoder_flush(VideoEncoder *enc, Buffer *out_packet) {
    (void)enc;
    (void)out_packet;
    return true;
}
