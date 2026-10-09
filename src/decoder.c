#include "decoder.h"
#include "compressor.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

struct VideoDecoder {
    uint32_t width;
    uint32_t height;
    PixelFormat format;
    PixelFormat internal_format;
    int quality;
    Frame *ref_frame;

    uint16_t qmatrix_luma[BLOCK_AREA];
    uint16_t qmatrix_chroma[BLOCK_AREA];
};

VideoDecoder *decoder_create(uint32_t width, uint32_t height, PixelFormat format, int quality) {
    if (width == 0 || height == 0) return NULL;

    VideoDecoder *dec = (VideoDecoder *)calloc(1, sizeof(VideoDecoder));
    if (!dec) return NULL;

    dec->width = width;
    dec->height = height;
    dec->format = format;
    dec->internal_format = (format == PIX_FMT_RGB24) ? PIX_FMT_YUV444 : format;
    dec->quality = (quality < 1) ? 60 : (quality > 100 ? 100 : quality);

    quant_matrix_init(dec->qmatrix_luma, g_default_luma_qmatrix, dec->quality);
    quant_matrix_init(dec->qmatrix_chroma, g_default_chroma_qmatrix, dec->quality);

    dec->ref_frame = frame_create(width, height, dec->internal_format);
    if (!dec->ref_frame) {
        free(dec);
        return NULL;
    }

    return dec;
}

void decoder_destroy(VideoDecoder *dec) {
    if (!dec) return;
    if (dec->ref_frame) {
        frame_destroy(dec->ref_frame);
        dec->ref_frame = NULL;
    }
    free(dec);
}

Frame *decoder_decode_frame(VideoDecoder *dec, const uint8_t *packet_data, size_t packet_size) {
    if (!dec || !packet_data || packet_size < 2) return NULL;

    uint8_t frame_type = packet_data[0];
    bool is_p_frame = (frame_type == FRAME_TYPE_P);

    BitReader reader;
    bit_reader_init(&reader, packet_data + 1, packet_size - 1);

    int planes = 3;

    for (int p = 0; p < planes; p++) {
        uint32_t pw = dec->ref_frame->plane_width[p];
        uint32_t ph = dec->ref_frame->plane_height[p];
        uint32_t num_blocks_x = (pw + BLOCK_SIZE - 1) / BLOCK_SIZE;
        uint32_t num_blocks_y = (ph + BLOCK_SIZE - 1) / BLOCK_SIZE;
        size_t total_blocks = (size_t)num_blocks_x * num_blocks_y;

        const uint16_t *qmat = (p == 0) ? dec->qmatrix_luma : dec->qmatrix_chroma;

        /* Read Huffman table for this plane */
        HuffmanTable huff_table;
        if (!huffman_table_read(&reader, &huff_table)) {
            return NULL;
        }

        /* Read skip mask if P-frame */
        uint8_t *skip_mask = (uint8_t *)calloc(total_blocks, 1);
        if (!skip_mask) return NULL;

        if (is_p_frame) {
            for (size_t b = 0; b < total_blocks; b++) {
                uint8_t skip_bit = 0;
                if (!bit_reader_get_bit(&reader, &skip_bit)) {
                    free(skip_mask);
                    return NULL;
                }
                skip_mask[b] = skip_bit;
            }
        }

        /* Decode each block */
        for (uint32_t by = 0; by < num_blocks_y; by++) {
            for (uint32_t bx = 0; bx < num_blocks_x; bx++) {
                size_t b_idx = (size_t)by * num_blocks_x + bx;
                if (is_p_frame && skip_mask[b_idx]) {
                    /* Block unchanged from reference frame */
                    continue;
                }

                /* Decode RLE symbols for this block */
                RLESymbol symbols[MAX_BLOCK_SYMBOLS];
                size_t sym_count = 0;
                int coeff_count = 0;

                while (coeff_count < BLOCK_AREA && sym_count < MAX_BLOCK_SYMBOLS) {
                    uint16_t code_sym = 0;
                    if (!huffman_decode_symbol(&reader, &huff_table, &code_sym)) {
                        break;
                    }

                    uint8_t run = (uint8_t)((code_sym >> 4) & 0x0F);
                    uint8_t cat = (uint8_t)(code_sym & 0x0F);

                    int16_t val = 0;
                    if (cat > 0) {
                        uint32_t val_bits = 0;
                        if (!bit_reader_get_bits(&reader, cat, &val_bits)) {
                            break;
                        }
                        /* Restore sign */
                        if (val_bits >= (1U << (cat - 1))) {
                            val = (int16_t)val_bits;
                        } else {
                            val = (int16_t)((int32_t)val_bits - ((1 << cat) - 1));
                        }
                    }

                    symbols[sym_count].run = run;
                    symbols[sym_count].value = val;
                    sym_count++;

                    if (run == 0 && val == 0 && coeff_count > 0) {
                        /* EOB */
                        break;
                    }

                    coeff_count += run + 1;
                }

                int16_t zigzag[BLOCK_AREA];
                rle_decode_block(symbols, sym_count, zigzag);

                int16_t quant_coeff[BLOCK_AREA];
                unzigzag_reorder(zigzag, quant_coeff);

                int32_t dequant_coeff[BLOCK_AREA];
                dequantize_block(quant_coeff, qmat, dequant_coeff);

                int16_t recon_block[BLOCK_AREA];
                idct_8x8_fast(dequant_coeff, recon_block);

                /* Write into reference frame */
                uint32_t px0 = bx * BLOCK_SIZE;
                uint32_t py0 = by * BLOCK_SIZE;

                for (uint32_t y = 0; y < BLOCK_SIZE; y++) {
                    uint32_t py = py0 + y;
                    if (py >= ph) continue;

                    uint8_t *ref_row = dec->ref_frame->data[p] + (size_t)py * dec->ref_frame->stride[p];
                    for (uint32_t x = 0; x < BLOCK_SIZE; x++) {
                        uint32_t px = px0 + x;
                        if (px >= pw) continue;

                        int32_t v = recon_block[y * BLOCK_SIZE + x];
                        if (is_p_frame) {
                            v += (int32_t)ref_row[px];
                        } else {
                            v += 128;
                        }
                        ref_row[px] = utils_clamp_u8(v);
                    }
                }
            }
        }

        free(skip_mask);
        bit_reader_align_byte(&reader);
    }

    if (dec->format != dec->internal_format) {
        return frame_convert(dec->ref_frame, dec->format);
    }

    /* Output a fresh copy of current frame */
    Frame *out_frame = NULL;
    frame_copy(dec->ref_frame, &out_frame);
    return out_frame;
}
