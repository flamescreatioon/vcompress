#define __USE_MINGW_ANSI_STDIO 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "frame.h"
#include "compressor.h"
#include "encoder.h"
#include "decoder.h"
#include "container.h"
#include "utils.h"

static void compute_metrics(const Frame *f1, const Frame *f2, double *out_mse, double *out_psnr, int *out_max_err) {
    if (!f1 || !f2 || f1->width != f2->width || f1->height != f2->height) {
        if (out_mse) *out_mse = 999999.0;
        if (out_psnr) *out_psnr = 0.0;
        if (out_max_err) *out_max_err = 255;
        return;
    }

    uint64_t sum_sq = 0;
    size_t total_samples = 0;
    int max_err = 0;

    int planes = (f1->format == PIX_FMT_RGB24) ? 1 : 3;
    for (int p = 0; p < planes; p++) {
        uint32_t pw = f1->plane_width[p];
        uint32_t ph = f1->plane_height[p];
        size_t bpp = (f1->format == PIX_FMT_RGB24) ? 3 : 1;

        for (uint32_t y = 0; y < ph; y++) {
            const uint8_t *r1 = f1->data[p] + (size_t)y * f1->stride[p];
            const uint8_t *r2 = f2->data[p] + (size_t)y * f2->stride[p];
            for (size_t x = 0; x < (size_t)pw * bpp; x++) {
                int diff = abs((int)r1[x] - (int)r2[x]);
                if (diff > max_err) max_err = diff;
                sum_sq += (uint64_t)(diff * diff);
                total_samples++;
            }
        }
    }

    double mse = (total_samples > 0) ? ((double)sum_sq / (double)total_samples) : 0.0;
    double psnr = (mse > 1e-10) ? (10.0 * log10((255.0 * 255.0) / mse)) : 99.99;

    if (out_mse) *out_mse = mse;
    if (out_psnr) *out_psnr = psnr;
    if (out_max_err) *out_max_err = max_err;
}

static void check_stage(const char *name, const Frame *orig, const Frame *recon, double min_psnr) {
    double mse = 0, psnr = 0;
    int max_err = 0;
    compute_metrics(orig, recon, &mse, &psnr, &max_err);
    printf("  %-38s : PSNR = %6.2f dB, MSE = %7.2f, MaxErr = %3d", name, psnr, mse, max_err);
    if (psnr < min_psnr) {
        printf(" [FAIL: < %.1f dB]\n", min_psnr);
    } else {
        printf(" [PASS]\n");
    }
}

int main(void) {
    printf("==========================================\n");
    printf("  COMPREHENSIVE CODEC DIAGNOSTIC SUITE\n");
    printf("==========================================\n\n");

    /* -------------------------------------------------------------
     * Stage 1: Frame allocation & row addressing
     * ------------------------------------------------------------- */
    printf("--- Stage 1: Frame Allocation & Row Addressing ---\n");
    {
        uint32_t w = 17, h = 13;
        Frame *f = frame_create(w, h, PIX_FMT_YUV420);
        bool ok = (f != NULL);
        if (ok) {
            /* Verify row_address = data + y * stride */
            for (uint32_t y = 0; y < h; y++) {
                uint8_t *expected = f->data[0] + (size_t)y * f->stride[0];
                /* check non-overlapping with next row */
                if (y + 1 < h) {
                    uint8_t *next = f->data[0] + (size_t)(y + 1) * f->stride[0];
                    if (next < expected + w) ok = false;
                }
            }
            frame_destroy(f);
        }
        printf("  Frame allocation and row addressing    : %s\n", ok ? "[PASS]" : "[FAIL]");
    }

    /* -------------------------------------------------------------
     * Stage 2: Frame Copying (Exact equality requirement)
     * ------------------------------------------------------------- */
    printf("--- Stage 2: Frame Copy (Lossless Exact Equality) ---\n");
    {
        Frame *src = frame_create(32, 24, PIX_FMT_RGB24);
        for (uint32_t y = 0; y < 24; y++) {
            for (uint32_t x = 0; x < 32 * 3; x++) {
                src->data[0][y * src->stride[0] + x] = (uint8_t)((x * 17 + y * 31) & 0xFF);
            }
        }
        Frame *dst = NULL;
        frame_copy(src, &dst);
        bool match = (dst != NULL && dst->width == src->width && dst->height == src->height);
        if (match) {
            for (uint32_t y = 0; y < 24; y++) {
                if (memcmp(src->data[0] + y * src->stride[0], dst->data[0] + y * dst->stride[0], 32 * 3) != 0) {
                    match = false;
                    break;
                }
            }
        }
        printf("  Frame copy exact byte equality         : %s\n", match ? "[PASS]" : "[FAIL]");
        frame_destroy(src);
        frame_destroy(dst);
    }

    /* -------------------------------------------------------------
     * Stage 3: Color Conversion & Chroma Subsampling Roundtrips
     * ------------------------------------------------------------- */
    printf("--- Stage 3: Color Conversion & Chroma Subsampling ---\n");
    {
        Frame *rgb = frame_create(64, 64, PIX_FMT_RGB24);
        for (uint32_t y = 0; y < 64; y++) {
            for (uint32_t x = 0; x < 64; x++) {
                rgb->data[0][y * rgb->stride[0] + x * 3 + 0] = (uint8_t)(x * 4);
                rgb->data[0][y * rgb->stride[0] + x * 3 + 1] = (uint8_t)(y * 4);
                rgb->data[0][y * rgb->stride[0] + x * 3 + 2] = (uint8_t)(255 - (x + y) * 2);
            }
        }

        /* RGB24 -> YUV444 -> RGB24 */
        Frame *yuv444 = frame_convert(rgb, PIX_FMT_YUV444);
        Frame *rgb_recon_444 = frame_convert(yuv444, PIX_FMT_RGB24);
        check_stage("RGB24 -> YUV444 -> RGB24", rgb, rgb_recon_444, 45.0);

        /* RGB24 -> YUV420 -> RGB24 (Subsampling loss expected) */
        Frame *yuv420 = frame_convert(rgb, PIX_FMT_YUV420);
        Frame *rgb_recon_420 = frame_convert(yuv420, PIX_FMT_RGB24);
        check_stage("RGB24 -> YUV420 -> RGB24", rgb, rgb_recon_420, 32.0);

        frame_destroy(rgb);
        frame_destroy(yuv444);
        frame_destroy(rgb_recon_444);
        frame_destroy(yuv420);
        frame_destroy(rgb_recon_420);
    }

    /* -------------------------------------------------------------
     * Stage 4: Forward and Inverse DCT Accuracy
     * ------------------------------------------------------------- */
    printf("--- Stage 4: DCT & IDCT Fast Numerical Accuracy ---\n");
    {
        int16_t block[BLOCK_AREA];
        int32_t dct_coeff[BLOCK_AREA];
        int16_t recon[BLOCK_AREA];

        for (int i = 0; i < BLOCK_AREA; i++) {
            block[i] = (int16_t)((i * 13 - 40) % 120);
        }

        dct_8x8_fast(block, dct_coeff);
        idct_8x8_fast(dct_coeff, recon);

        int max_dct_diff = 0;
        for (int i = 0; i < BLOCK_AREA; i++) {
            int d = abs(block[i] - recon[i]);
            if (d > max_dct_diff) max_dct_diff = d;
        }
        printf("  DCT-IDCT fast max deviation            : %d (expected <= 2) %s\n",
               max_dct_diff, (max_dct_diff <= 2) ? "[PASS]" : "[FAIL]");
    }

    /* -------------------------------------------------------------
     * Stage 5: Dense RLE (ZRL handling) and Huffman Roundtrip
     * ------------------------------------------------------------- */
    printf("--- Stage 5: Dense RLE (ZRL runs) & Huffman Roundtrip ---\n");
    {
        /* Create a block with lots of alternating zeros to test ZRL runs */
        int16_t dense_zigzag[BLOCK_AREA] = {0};
        dense_zigzag[0] = 50; /* DC */
        for (int i = 1; i < BLOCK_AREA; i += 17) {
            dense_zigzag[i] = (int16_t)(i * 3 - 10);
        }

        RLESymbol syms[MAX_BLOCK_SYMBOLS];
        size_t n_syms = rle_encode_block(dense_zigzag, syms, MAX_BLOCK_SYMBOLS);

        int16_t recon_zigzag[BLOCK_AREA];
        rle_decode_block(syms, n_syms, recon_zigzag);

        bool rle_ok = (memcmp(dense_zigzag, recon_zigzag, sizeof(dense_zigzag)) == 0);
        printf("  Dense RLE encode/decode roundtrip      : %s (symbols count = %zu)\n",
               rle_ok ? "[PASS]" : "[FAIL]", n_syms);
    }

    /* -------------------------------------------------------------
     * Test 1: Checkerboard 16x16
     * ------------------------------------------------------------- */
    printf("\nTest 1: Checkerboard 16x16 (High frequency test)\n");
    {
        Frame *f = frame_create(16, 16, PIX_FMT_YUV420);
        for (uint32_t y = 0; y < 16; y++) {
            for (uint32_t x = 0; x < 16; x++) {
                f->data[0][y * f->stride[0] + x] = ((x + y) % 2 == 0) ? 240 : 16;
            }
        }
        memset(f->data[1], 128, (size_t)f->stride[1] * f->plane_height[1]);
        memset(f->data[2], 128, (size_t)f->stride[2] * f->plane_height[2]);

        EncoderConfig cfg = {
            .width = 16, .height = 16, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = 90, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(16, 16, PIX_FMT_YUV420, 90);

        Buffer pkt;
        buffer_init(&pkt);
        bool is_key = false;
        encoder_encode_frame(enc, f, &pkt, &is_key);
        Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

        check_stage("Checkerboard 16x16 (Q=90)", f, rec, 25.0);

        frame_destroy(f);
        frame_destroy(rec);
        buffer_free(&pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 2: Horizontal Gradient
     * ------------------------------------------------------------- */
    printf("\nTest 2: Horizontal Gradient 32x32\n");
    {
        Frame *f = frame_create(32, 32, PIX_FMT_YUV420);
        for (uint32_t y = 0; y < 32; y++) {
            for (uint32_t x = 0; x < 32; x++) {
                f->data[0][y * f->stride[0] + x] = (uint8_t)(16 + (x * 200) / 32);
            }
        }
        memset(f->data[1], 128, (size_t)f->stride[1] * f->plane_height[1]);
        memset(f->data[2], 128, (size_t)f->stride[2] * f->plane_height[2]);

        EncoderConfig cfg = {
            .width = 32, .height = 32, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = 85, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(32, 32, PIX_FMT_YUV420, 85);

        Buffer pkt;
        buffer_init(&pkt);
        bool is_key = false;
        encoder_encode_frame(enc, f, &pkt, &is_key);
        Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

        check_stage("Horizontal Gradient (Q=85)", f, rec, 35.0);

        frame_destroy(f);
        frame_destroy(rec);
        buffer_free(&pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 3: Vertical Gradient
     * ------------------------------------------------------------- */
    printf("\nTest 3: Vertical Gradient 32x32\n");
    {
        Frame *f = frame_create(32, 32, PIX_FMT_YUV420);
        for (uint32_t y = 0; y < 32; y++) {
            for (uint32_t x = 0; x < 32; x++) {
                f->data[0][y * f->stride[0] + x] = (uint8_t)(16 + (y * 200) / 32);
            }
        }
        memset(f->data[1], 128, (size_t)f->stride[1] * f->plane_height[1]);
        memset(f->data[2], 128, (size_t)f->stride[2] * f->plane_height[2]);

        EncoderConfig cfg = {
            .width = 32, .height = 32, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = 85, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(32, 32, PIX_FMT_YUV420, 85);

        Buffer pkt;
        buffer_init(&pkt);
        bool is_key = false;
        encoder_encode_frame(enc, f, &pkt, &is_key);
        Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

        check_stage("Vertical Gradient (Q=85)", f, rec, 35.0);

        frame_destroy(f);
        frame_destroy(rec);
        buffer_free(&pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 4: Odd Dimensions (including 13x11 non-multiple-of-8)
     * ------------------------------------------------------------- */
    printf("\nTest 4: Odd Dimensions & Non-Multiple-of-8\n");
    uint32_t odd_sizes[][2] = {{13, 11}, {15, 11}, {7, 5}, {17, 19}, {23, 31}};
    for (size_t i = 0; i < sizeof(odd_sizes)/sizeof(odd_sizes[0]); i++) {
        uint32_t w = odd_sizes[i][0];
        uint32_t h = odd_sizes[i][1];
        char label[64];
        snprintf(label, sizeof(label), "Odd geometry %ux%u", w, h);

        Frame *f = frame_create(w, h, PIX_FMT_YUV420);
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                f->data[0][y * f->stride[0] + x] = (uint8_t)(30 + (x * 150) / w + (y * 50) / h);
            }
        }
        for (uint32_t cy = 0; cy < f->plane_height[1]; cy++) {
            for (uint32_t cx = 0; cx < f->plane_width[1]; cx++) {
                f->data[1][cy * f->stride[1] + cx] = (uint8_t)(64 + (cx * 100) / f->plane_width[1]);
                f->data[2][cy * f->stride[2] + cx] = (uint8_t)(192 - (cy * 100) / f->plane_height[2]);
            }
        }

        EncoderConfig cfg = {
            .width = w, .height = h, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = 80, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(w, h, PIX_FMT_YUV420, 80);

        Buffer pkt;
        buffer_init(&pkt);
        bool is_key = false;
        encoder_encode_frame(enc, f, &pkt, &is_key);
        Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

        check_stage(label, f, rec, 30.0);

        frame_destroy(f);
        frame_destroy(rec);
        buffer_free(&pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 5: 4x4 Corner Colors in RGB24
     * ------------------------------------------------------------- */
    printf("\nTest 5: 4x4 Image with distinct corner colors (RGB24)\n");
    {
        Frame *f = frame_create(4, 4, PIX_FMT_RGB24);
        /* Red top-left */
        f->data[0][0*f->stride[0] + 0*3 + 0] = 255;
        /* Green top-right */
        f->data[0][0*f->stride[0] + 3*3 + 1] = 255;
        /* Blue bottom-left */
        f->data[0][3*f->stride[0] + 0*3 + 2] = 255;
        /* Yellow bottom-right */
        f->data[0][3*f->stride[0] + 3*3 + 0] = 255;
        f->data[0][3*f->stride[0] + 3*3 + 1] = 255;

        EncoderConfig cfg = {
            .width = 4, .height = 4, .format = PIX_FMT_RGB24,
            .fps_num = 30, .fps_den = 1, .quality = 95, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(4, 4, PIX_FMT_RGB24, 95);

        Buffer pkt;
        buffer_init(&pkt);
        bool is_key = false;
        encoder_encode_frame(enc, f, &pkt, &is_key);
        Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

        check_stage("4x4 RGB24 Corners (Q=95)", f, rec, 25.0);

        printf("  Corner values:\n");
        printf("    Top-Left orig:     (%3u, %3u, %3u) -> recon: (%3u, %3u, %3u)\n",
               f->data[0][0], f->data[0][1], f->data[0][2],
               rec->data[0][0], rec->data[0][1], rec->data[0][2]);
        printf("    Top-Right orig:    (%3u, %3u, %3u) -> recon: (%3u, %3u, %3u)\n",
               f->data[0][9], f->data[0][10], f->data[0][11],
               rec->data[0][9], rec->data[0][10], rec->data[0][11]);
        printf("    Bottom-Left orig:  (%3u, %3u, %3u) -> recon: (%3u, %3u, %3u)\n",
               f->data[0][3*f->stride[0]], f->data[0][3*f->stride[0]+1], f->data[0][3*f->stride[0]+2],
               rec->data[0][3*rec->stride[0]], rec->data[0][3*rec->stride[0]+1], rec->data[0][3*rec->stride[0]+2]);
        printf("    Bottom-Right orig: (%3u, %3u, %3u) -> recon: (%3u, %3u, %3u)\n",
               f->data[0][3*f->stride[0]+9], f->data[0][3*f->stride[0]+10], f->data[0][3*f->stride[0]+11],
               rec->data[0][3*rec->stride[0]+9], rec->data[0][3*rec->stride[0]+10], rec->data[0][3*rec->stride[0]+11]);

        frame_destroy(f);
        frame_destroy(rec);
        buffer_free(&pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 6: Single bright pixel at known coordinate
     * ------------------------------------------------------------- */
    printf("\nTest 6: Single bright pixel at known coordinate (10, 10)\n");
    {
        uint32_t w = 32, h = 32;
        Frame *f = frame_create(w, h, PIX_FMT_YUV420);
        memset(f->data[0], 16, (size_t)f->stride[0] * h);
        memset(f->data[1], 128, (size_t)f->stride[1] * f->plane_height[1]);
        memset(f->data[2], 128, (size_t)f->stride[2] * f->plane_height[2]);

        uint32_t px = 10, py = 10;
        f->data[0][py * f->stride[0] + px] = 235; /* Bright pixel */

        EncoderConfig cfg = {
            .width = w, .height = h, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = 95, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(w, h, PIX_FMT_YUV420, 95);

        Buffer pkt;
        buffer_init(&pkt);
        bool is_key = false;
        encoder_encode_frame(enc, f, &pkt, &is_key);
        Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

        uint8_t recon_val = rec->data[0][py * rec->stride[0] + px];
        printf("  Single pixel (10, 10): orig = 235 -> recon = %u\n", recon_val);
        check_stage("Single bright pixel (Q=95)", f, rec, 30.0);

        frame_destroy(f);
        frame_destroy(rec);
        buffer_free(&pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 7: Low, Medium, High Quality Measurable Correctness
     * ------------------------------------------------------------- */
    printf("\nTest 7: Quality Scaling (Low=20, Medium=60, High=95)\n");
    int qualities[] = {20, 60, 95};
    for (int q_idx = 0; q_idx < 3; q_idx++) {
        int q = qualities[q_idx];
        uint32_t w = 64, h = 64;
        Frame *f = frame_create(w, h, PIX_FMT_YUV420);
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                f->data[0][y * f->stride[0] + x] = (uint8_t)(16 + (x * 120) / w + (y * 100) / h);
            }
        }
        memset(f->data[1], 128, (size_t)f->stride[1] * f->plane_height[1]);
        memset(f->data[2], 128, (size_t)f->stride[2] * f->plane_height[2]);

        EncoderConfig cfg = {
            .width = w, .height = h, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = q, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(w, h, PIX_FMT_YUV420, q);

        Buffer pkt;
        buffer_init(&pkt);
        bool is_key = false;
        encoder_encode_frame(enc, f, &pkt, &is_key);
        Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

        char label[64];
        snprintf(label, sizeof(label), "Quality Q=%2d (compressed %zu bytes)", q, pkt.size);
        check_stage(label, f, rec, 25.0 + (double)q * 0.25);

        frame_destroy(f);
        frame_destroy(rec);
        buffer_free(&pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 8: End-to-End MP4 Container Orientation Verification
     * ------------------------------------------------------------- */
    printf("\nTest 8: MP4 Container Orientation Verification\n");
    {
        uint32_t w = 128, h = 128;
        const char *mp4_test_file = "test_orientation_diag.mp4";

        /* Write a frame where top half is White (255) and bottom half is Black (0) */
        Frame *f_orig = frame_create(w, h, PIX_FMT_RGB24);
        for (uint32_t y = 0; y < h / 2; y++) {
            memset(f_orig->data[0] + (size_t)y * f_orig->stride[0], 240, w * 3);
        }
        for (uint32_t y = h / 2; y < h; y++) {
            memset(f_orig->data[0] + (size_t)y * f_orig->stride[0], 16, w * 3);
        }

        MP4Writer *writer = mp4_writer_open(mp4_test_file, w, h, 30, 1, 90);
        bool write_ok = false;
        if (writer) {
            write_ok = mp4_writer_write_frame(writer, f_orig);
            mp4_writer_close(writer);
        }

        uint32_t rw = 0, rh = 0, rnum = 0, rden = 0;
        MP4Reader *reader = mp4_reader_open(mp4_test_file, &rw, &rh, &rnum, &rden);
        bool read_ok = false;
        Frame *f_read = NULL;
        if (reader) {
            f_read = mp4_reader_read_frame(reader);
            read_ok = (f_read != NULL);
            mp4_reader_close(reader);
        }

        bool orient_ok = false;
        if (write_ok && read_ok && f_read) {
            /* Check that top row (y=10) is still white (>150) and bottom row (y=118) is dark (<100) */
            uint8_t top_val = f_read->data[0][10 * f_read->stride[0] + 10 * 3];
            uint8_t bot_val = f_read->data[0][118 * f_read->stride[0] + 10 * 3];
            printf("  MP4 roundtrip sample: Top row = %3u (expected bright), Bottom row = %3u (expected dark)\n",
                   top_val, bot_val);
            if (top_val > 150 && bot_val < 100) {
                orient_ok = true;
            }
        }
        printf("  MP4 vertical orientation check         : %s\n", orient_ok ? "[PASS]" : "[FAIL]");

        if (f_orig) frame_destroy(f_orig);
        if (f_read) frame_destroy(f_read);
        remove(mp4_test_file);
    }

    /* -------------------------------------------------------------
     * Test 9: Multi-frame Sequence with P-Frames (GOP=10)
     * ------------------------------------------------------------- */
    printf("\nTest 9: Multi-frame Sequence with P-Frames (GOP=10)\n");
    {
        uint32_t w = 64, h = 64;
        EncoderConfig cfg = {
            .width = w, .height = h, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = 75, .gop_size = 10,
            .threads = 2, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        VideoDecoder *dec = decoder_create(w, h, PIX_FMT_YUV420, 75);

        double total_psnr = 0;
        int num_frames = 15;
        for (int i = 0; i < num_frames; i++) {
            Frame *f = frame_create(w, h, PIX_FMT_YUV420);
            memset(f->data[0], 100, (size_t)f->stride[0] * h);
            memset(f->data[1], 128, (size_t)f->stride[1] * f->plane_height[1]);
            memset(f->data[2], 128, (size_t)f->stride[2] * f->plane_height[2]);

            /* Moving block */
            uint32_t bx = (uint32_t)(5 + i * 3);
            uint32_t by = (uint32_t)(5 + i * 2);
            for (uint32_t y = by; y < by + 12 && y < h; y++) {
                for (uint32_t x = bx; x < bx + 12 && x < w; x++) {
                    f->data[0][y * f->stride[0] + x] = 220;
                }
            }

            Buffer pkt;
            buffer_init(&pkt);
            bool is_key = false;
            encoder_encode_frame(enc, f, &pkt, &is_key);
            Frame *rec = decoder_decode_frame(dec, pkt.data, pkt.size);

            double psnr = frame_compute_psnr(f, rec);
            total_psnr += psnr;

            frame_destroy(f);
            frame_destroy(rec);
            buffer_free(&pkt);
        }

        double avg_psnr = total_psnr / num_frames;
        printf("  Sequence (15 frames, GOP=10)        : Avg PSNR = %6.2f dB", avg_psnr);
        if (avg_psnr >= 30.0) printf(" [PASS]\n");
        else printf(" [FAIL]\n");

        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    /* -------------------------------------------------------------
     * Test 10: Robustness to Truncated & Malformed Bitstreams
     * ------------------------------------------------------------- */
    printf("\nTest 10: Robustness to Truncated & Malformed Bitstreams\n");
    {
        uint32_t w = 32, h = 32;
        VideoDecoder *dec = decoder_create(w, h, PIX_FMT_YUV420, 80);

        /* 1. NULL input */
        Frame *r1 = decoder_decode_frame(dec, NULL, 100);
        /* 2. Packet too small (0 or 1 byte) */
        uint8_t tiny[1] = {0};
        Frame *r2 = decoder_decode_frame(dec, tiny, 1);
        /* 3. Random garbage bitstream */
        uint8_t garbage[64];
        memset(garbage, 0xAA, sizeof(garbage));
        Frame *r3 = decoder_decode_frame(dec, garbage, sizeof(garbage));
        /* 4. Valid packet truncated midway */
        EncoderConfig cfg = {
            .width = w, .height = h, .format = PIX_FMT_YUV420,
            .fps_num = 30, .fps_den = 1, .quality = 80, .gop_size = 1,
            .threads = 1, .inter_threshold = 128
        };
        VideoEncoder *enc = encoder_create(&cfg);
        Frame *src = frame_create(w, h, PIX_FMT_YUV420);
        Buffer valid_pkt;
        buffer_init(&valid_pkt);
        bool is_key = false;
        encoder_encode_frame(enc, src, &valid_pkt, &is_key);

        Frame *r4 = NULL;
        if (valid_pkt.size > 10) {
            /* Decode only first 10 bytes */
            r4 = decoder_decode_frame(dec, valid_pkt.data, 10);
        }

        bool handled_gracefully = (r1 == NULL && r2 == NULL);
        printf("  Truncated / malformed input handled    : %s (no crash or hang)\n",
               handled_gracefully ? "[PASS]" : "[FAIL]");

        if (r3) frame_destroy(r3);
        if (r4) frame_destroy(r4);
        frame_destroy(src);
        buffer_free(&valid_pkt);
        encoder_destroy(enc);
        decoder_destroy(dec);
    }

    printf("\n==========================================\n");
    printf("  ALL DIAGNOSTIC TESTS COMPLETED\n");
    printf("==========================================\n");
    return 0;
}
