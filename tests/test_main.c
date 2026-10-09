#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "buffer.h"
#include "file.h"
#include "frame.h"
#include "compressor.h"
#include "encoder.h"
#include "decoder.h"
#include "container.h"
#include "thread_pool.h"
#include "utils.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s (line %d): %s\n", __func__, __LINE__, msg); \
        return false; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("Running %s...", #fn); \
    if (fn()) { \
        printf(" PASS\n"); \
        g_tests_passed++; \
    } else { \
        printf(" FAILED\n"); \
    } \
} while (0)

/* 1. Buffer Tests */
static bool test_buffer_operations(void) {
    Buffer buf;
    buffer_init(&buf);
    TEST_ASSERT(buf.size == 0 && buf.capacity == 0, "Initial buffer state");

    uint8_t data[] = {1, 2, 3, 4, 5};
    TEST_ASSERT(buffer_append(&buf, data, sizeof(data)) == BUF_OK, "Append data");
    TEST_ASSERT(buf.size == 5, "Buffer size after append");

    TEST_ASSERT(buffer_append_u8(&buf, 0xAA) == BUF_OK, "Append u8");
    TEST_ASSERT(buffer_append_u16_be(&buf, 0x1234) == BUF_OK, "Append u16");
    TEST_ASSERT(buffer_append_u32_be(&buf, 0xDEADBEEF) == BUF_OK, "Append u32");
    TEST_ASSERT(buf.size == 5 + 1 + 2 + 4, "Total size");

    uint8_t read_u8 = 0;
    TEST_ASSERT(buffer_read(&buf, 5, &read_u8, 1) == BUF_OK, "Read byte");
    TEST_ASSERT(read_u8 == 0xAA, "Read correct byte");

    uint8_t read_u32[4];
    TEST_ASSERT(buffer_read(&buf, 8, read_u32, 4) == BUF_OK, "Read u32");
    TEST_ASSERT(read_u32_be(read_u32) == 0xDEADBEEF, "Check u32 value");

    /* Bounds check */
    uint8_t dummy[10];
    TEST_ASSERT(buffer_read(&buf, buf.size, dummy, 5) == BUF_ERR_BOUNDS, "Out of bounds read rejected");

    buffer_free(&buf);
    TEST_ASSERT(buf.data == NULL && buf.size == 0, "Buffer freed");
    return true;
}

/* 2. Bitstream Writer & Reader Tests */
static bool test_bitstream_operations(void) {
    Buffer buf;
    buffer_init(&buf);

    BitWriter bw;
    bit_writer_init(&bw, &buf);

    /* Write 3 bits: 5 (101_2), 5 bits: 17 (10001_2), 12 bits: 0xABC */
    TEST_ASSERT(bit_writer_put_bits(&bw, 5, 3) == BUF_OK, "Write 3 bits");
    TEST_ASSERT(bit_writer_put_bits(&bw, 17, 5) == BUF_OK, "Write 5 bits");
    TEST_ASSERT(bit_writer_put_bits(&bw, 0xABC, 12) == BUF_OK, "Write 12 bits");
    TEST_ASSERT(bit_writer_flush(&bw) == BUF_OK, "Flush writer");

    BitReader br;
    bit_reader_init(&br, buf.data, buf.size);

    uint32_t val3 = 0, val5 = 0, val12 = 0;
    TEST_ASSERT(bit_reader_get_bits(&br, 3, &val3), "Read 3 bits");
    TEST_ASSERT(val3 == 5, "Val 3 matches");

    TEST_ASSERT(bit_reader_get_bits(&br, 5, &val5), "Read 5 bits");
    TEST_ASSERT(val5 == 17, "Val 5 matches");

    TEST_ASSERT(bit_reader_get_bits(&br, 12, &val12), "Read 12 bits");
    TEST_ASSERT(val12 == 0xABC, "Val 12 matches");

    buffer_free(&buf);
    return true;
}

/* 3. Frame Lifecycle & Memory Geometry Tests */
static bool test_frame_lifecycle(void) {
    Frame *f = frame_create(320, 240, PIX_FMT_YUV420);
    TEST_ASSERT(f != NULL, "Frame allocated");
    TEST_ASSERT(f->width == 320 && f->height == 240, "Geometry matches");
    TEST_ASSERT(f->stride[0] >= 320, "Luma stride aligned");
    TEST_ASSERT(f->stride[1] >= 160 && f->stride[2] >= 160, "Chroma strides aligned");
    TEST_ASSERT(f->plane_width[1] == 160 && f->plane_height[1] == 120, "Chroma subsampling dimensions");

    /* Deep copy */
    Frame *copy = NULL;
    TEST_ASSERT(frame_copy(f, &copy), "Frame copy succeeds");
    TEST_ASSERT(copy != NULL && copy->width == 320, "Copied frame valid");

    frame_destroy(copy);
    frame_destroy(f);
    return true;
}

/* 4. RGB <-> YUV Conversion Roundtrip Tests */
static bool test_rgb_yuv_conversion(void) {
    Frame *rgb = frame_create(64, 64, PIX_FMT_RGB24);
    TEST_ASSERT(rgb != NULL, "RGB frame created");

    /* Fill with synthetic pattern */
    for (uint32_t y = 0; y < rgb->height; y++) {
        uint8_t *row = rgb->data[0] + (size_t)y * rgb->stride[0];
        for (uint32_t x = 0; x < rgb->width; x++) {
            row[x * 3 + 0] = (uint8_t)((x * 4) & 0xFF);
            row[x * 3 + 1] = (uint8_t)((y * 4) & 0xFF);
            row[x * 3 + 2] = (uint8_t)(((x + y) * 2) & 0xFF);
        }
    }

    Frame *yuv = frame_convert(rgb, PIX_FMT_YUV420);
    TEST_ASSERT(yuv != NULL, "Convert to YUV420");

    Frame *rgb_recon = frame_convert(yuv, PIX_FMT_RGB24);
    TEST_ASSERT(rgb_recon != NULL, "Convert back to RGB24");

    double psnr = frame_compute_psnr(rgb, rgb_recon);
    TEST_ASSERT(psnr > 32.0, "PSNR is high (>32 dB) for color conversion");

    frame_destroy(rgb);
    frame_destroy(yuv);
    frame_destroy(rgb_recon);
    return true;
}

/* 5. 8x8 DCT and IDCT Mathematical Accuracy */
static bool test_dct_idct_accuracy(void) {
    int16_t input[BLOCK_AREA];
    double dct_out[BLOCK_AREA];
    int16_t idct_out[BLOCK_AREA];

    /* Sample 8x8 gradient block */
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            input[y * 8 + x] = (int16_t)((x * 12 + y * 15) - 100);
        }
    }

    dct_8x8(input, dct_out);
    idct_8x8(dct_out, idct_out);

    /* Verify maximum reconstruction error is within rounding precision (+-1) */
    for (int i = 0; i < BLOCK_AREA; i++) {
        int diff = abs(input[i] - idct_out[i]);
        TEST_ASSERT(diff <= 1, "DCT / IDCT roundtrip within +-1");
    }

    return true;
}

/* 6. Quantization and Zigzag Tests */
static bool test_quant_and_zigzag(void) {
    uint16_t qmatrix[BLOCK_AREA];
    quant_matrix_init(qmatrix, g_default_luma_qmatrix, 60);

    int32_t coeffs[BLOCK_AREA] = {0};
    coeffs[0] = 350;
    coeffs[1] = -120;
    coeffs[8] = 45;

    int16_t quant[BLOCK_AREA];
    quantize_block(coeffs, qmatrix, quant);

    int16_t zigzag[BLOCK_AREA];
    zigzag_reorder(quant, zigzag);
    TEST_ASSERT(zigzag[0] == quant[0], "DC coefficient remains first in zigzag");

    int16_t unzig[BLOCK_AREA];
    unzigzag_reorder(zigzag, unzig);
    for (int i = 0; i < BLOCK_AREA; i++) {
        TEST_ASSERT(unzig[i] == quant[i], "Zigzag unzigzag 1:1 roundtrip");
    }

    return true;
}

/* 7. RLE & Huffman Coding Roundtrip Tests */
static bool test_rle_and_huffman(void) {
    int16_t zigzag[BLOCK_AREA] = {0};
    zigzag[0] = 42; /* DC */
    zigzag[3] = -5;
    zigzag[7] = 2;
    /* Remaining are zeros */

    RLESymbol symbols[BLOCK_AREA];
    size_t n_syms = rle_encode_block(zigzag, symbols, BLOCK_AREA);
    TEST_ASSERT(n_syms > 0, "RLE symbols produced");

    int16_t recon_zigzag[BLOCK_AREA];
    rle_decode_block(symbols, n_syms, recon_zigzag);

    for (int i = 0; i < BLOCK_AREA; i++) {
        TEST_ASSERT(zigzag[i] == recon_zigzag[i], "RLE encode/decode roundtrip exact");
    }

    /* Huffman test */
    uint32_t freqs[4] = {100, 50, 20, 10};
    uint16_t sym_list[4] = {0, 1, 2, 3};
    HuffmanTable table;
    TEST_ASSERT(huffman_build_table(freqs, sym_list, 4, &table), "Huffman table built");

    Buffer buf;
    buffer_init(&buf);
    BitWriter bw;
    bit_writer_init(&bw, &buf);

    TEST_ASSERT(huffman_encode_symbol(&bw, &table, 0) == BUF_OK, "Encode sym 0");
    TEST_ASSERT(huffman_encode_symbol(&bw, &table, 2) == BUF_OK, "Encode sym 2");
    TEST_ASSERT(huffman_encode_symbol(&bw, &table, 1) == BUF_OK, "Encode sym 1");
    bit_writer_flush(&bw);

    BitReader br;
    bit_reader_init(&br, buf.data, buf.size);

    uint16_t d0 = 99, d1 = 99, d2 = 99;
    TEST_ASSERT(huffman_decode_symbol(&br, &table, &d0), "Decode sym 0");
    TEST_ASSERT(d0 == 0, "Decoded 0 matches");
    TEST_ASSERT(huffman_decode_symbol(&br, &table, &d2), "Decode sym 2");
    TEST_ASSERT(d2 == 2, "Decoded 2 matches");
    TEST_ASSERT(huffman_decode_symbol(&br, &table, &d1), "Decode sym 1");
    TEST_ASSERT(d1 == 1, "Decoded 1 matches");

    buffer_free(&buf);
    return true;
}

/* 8. Custom .vc Container Header and Frame Serialization */
static bool test_vc_container(void) {
    const char *tmp_path = "test_container_tmp.vc";

    VCHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.width = 160;
    hdr.height = 120;
    hdr.fps_num = 30;
    hdr.fps_den = 1;
    hdr.pixel_format = PIX_FMT_YUV420;
    hdr.quality = 70;
    hdr.gop_size = 10;

    VCWriter *writer = vc_writer_open(tmp_path, &hdr);
    TEST_ASSERT(writer != NULL, "Open .vc writer");

    uint8_t payload1[] = {0x00, 0x11, 0x22, 0x33};
    uint8_t payload2[] = {0x01, 0x44, 0x55, 0x66, 0x77};

    TEST_ASSERT(vc_writer_write_frame(writer, FRAME_TYPE_I, payload1, sizeof(payload1), 100), "Write frame 0");
    TEST_ASSERT(vc_writer_write_frame(writer, FRAME_TYPE_P, payload2, sizeof(payload2), 100), "Write frame 1");
    TEST_ASSERT(vc_writer_close(writer), "Close .vc writer");

    /* Read back */
    VCReader *reader = vc_reader_open(tmp_path);
    TEST_ASSERT(reader != NULL, "Open .vc reader");
    TEST_ASSERT(reader->header.width == 160 && reader->header.height == 120, "Geometry matches");
    TEST_ASSERT(reader->header.frame_count == 2, "Frame count 2");

    uint8_t f_type = 0;
    Buffer packet;
    buffer_init(&packet);
    size_t uncomp_sz = 0;

    TEST_ASSERT(vc_reader_read_frame(reader, &f_type, &packet, &uncomp_sz), "Read frame 0");
    TEST_ASSERT(f_type == FRAME_TYPE_I, "Frame 0 is I-frame");
    TEST_ASSERT(packet.size == sizeof(payload1), "Payload size matches");
    TEST_ASSERT(memcmp(packet.data, payload1, sizeof(payload1)) == 0, "Payload content matches");

    TEST_ASSERT(vc_reader_read_frame(reader, &f_type, &packet, &uncomp_sz), "Read frame 1");
    TEST_ASSERT(f_type == FRAME_TYPE_P, "Frame 1 is P-frame");
    TEST_ASSERT(packet.size == sizeof(payload2), "Payload size matches");
    TEST_ASSERT(memcmp(packet.data, payload2, sizeof(payload2)) == 0, "Payload content matches");

    buffer_free(&packet);
    vc_reader_close(reader);

    remove(tmp_path);
    return true;
}

/* 9. Thread Pool Execution & Synchronization */
typedef struct TPTestArg {
    int val;
    int *accumulator;
} TPTestArg;

static void tp_test_fn(void *arg) {
    TPTestArg *tpa = (TPTestArg *)arg;
    *tpa->accumulator += tpa->val;
}

static bool test_thread_pool(void) {
    ThreadPool *pool = thread_pool_create(4);
    TEST_ASSERT(pool != NULL, "Thread pool created");

    int acc = 0;
    TPTestArg args[10];
    for (int i = 0; i < 10; i++) {
        args[i].val = i + 1;
        args[i].accumulator = &acc;
        thread_pool_submit(pool, tp_test_fn, &args[i]);
    }

    thread_pool_wait(pool);
    thread_pool_destroy(pool);

    /* Sum 1..10 = 55 */
    TEST_ASSERT(acc == 55, "All thread pool tasks completed");
    return true;
}

/* 10. End-to-End Encoder / Decoder Roundtrip */
static bool test_encoder_decoder_roundtrip(void) {
    EncoderConfig enc_cfg;
    memset(&enc_cfg, 0, sizeof(enc_cfg));
    enc_cfg.width = 64;
    enc_cfg.height = 64;
    enc_cfg.format = PIX_FMT_YUV420;
    enc_cfg.fps_num = 30;
    enc_cfg.fps_den = 1;
    enc_cfg.quality = 75;
    enc_cfg.gop_size = 5;
    enc_cfg.threads = 1;

    VideoEncoder *enc = encoder_create(&enc_cfg);
    TEST_ASSERT(enc != NULL, "Encoder created");

    VideoDecoder *dec = decoder_create(64, 64, PIX_FMT_YUV420, 75);
    TEST_ASSERT(dec != NULL, "Decoder created");

    /* Create test frame with moving square across 3 frames */
    for (int f_idx = 0; f_idx < 3; f_idx++) {
        Frame *f = frame_create(64, 64, PIX_FMT_YUV420);
        /* Gray background */
        memset(f->data[0], 128, (size_t)f->stride[0] * f->plane_height[0]);
        memset(f->data[1], 128, (size_t)f->stride[1] * f->plane_height[1]);
        memset(f->data[2], 128, (size_t)f->stride[2] * f->plane_height[2]);

        /* Moving white patch */
        uint32_t px = (uint32_t)(10 + f_idx * 8);
        uint32_t py = (uint32_t)(10 + f_idx * 4);
        for (uint32_t y = py; y < py + 16; y++) {
            for (uint32_t x = px; x < px + 16; x++) {
                f->data[0][(size_t)y * f->stride[0] + x] = 230;
            }
        }

        Buffer packet;
        buffer_init(&packet);
        bool is_key = false;
        TEST_ASSERT(encoder_encode_frame(enc, f, &packet, &is_key), "Encode frame");

        Frame *decoded = decoder_decode_frame(dec, packet.data, packet.size);
        TEST_ASSERT(decoded != NULL, "Decode frame");

        double psnr = frame_compute_psnr(f, decoded);
        TEST_ASSERT(psnr > 30.0, "Decoded PSNR > 30 dB");

        frame_destroy(f);
        frame_destroy(decoded);
        buffer_free(&packet);
    }

    encoder_destroy(enc);
    decoder_destroy(dec);
    return true;
}

static bool test_deblock_filter(void) {
    /* Test 1: Artificial 8x8 block boundary step should be smoothed */
    uint8_t plane[16 * 16];
    uint32_t stride = 16;
    uint32_t width = 16;
    uint32_t height = 16;

    /* Fill left 8x8 block with 100 and right 8x8 block with 108 */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            plane[y * stride + x] = 100;
        }
        for (uint32_t x = 8; x < 16; x++) {
            plane[y * stride + x] = 108;
        }
    }

    deblock_filter_plane(plane, stride, width, height, 50);

    /* Boundary pixels (x=7 and x=8) must be smoothed closer together */
    uint8_t p0_smoothed = plane[4 * stride + 7];
    uint8_t q0_smoothed = plane[4 * stride + 8];
    TEST_ASSERT(p0_smoothed > 100, "Left boundary pixel smoothed upward");
    TEST_ASSERT(q0_smoothed < 108, "Right boundary pixel smoothed downward");
    TEST_ASSERT((q0_smoothed - p0_smoothed) < 8, "Boundary discontinuity reduced");

    /* Test 2: Natural sharp edge (> alpha) must be preserved without blurring */
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < 8; x++) {
            plane[y * stride + x] = 50;
        }
        for (uint32_t x = 8; x < 16; x++) {
            plane[y * stride + x] = 220;
        }
    }

    deblock_filter_plane(plane, stride, width, height, 50);

    TEST_ASSERT(plane[4 * stride + 7] == 50, "Sharp edge left preserved");
    TEST_ASSERT(plane[4 * stride + 8] == 220, "Sharp edge right preserved");

    /* Test 3: Frame-level wrapper safety */
    Frame *test_frame = frame_create(32, 32, PIX_FMT_YUV420);
    TEST_ASSERT(test_frame != NULL, "Frame created for deblock test");
    deblock_filter_frame(test_frame, 60);
    deblock_filter_frame(NULL, 60);
    frame_destroy(test_frame);

    return true;
}

int main(void) {
    printf("==========================================\n");
    printf("  vcompress Automated Unit Test Suite\n");
    printf("==========================================\n\n");

    RUN_TEST(test_buffer_operations);
    RUN_TEST(test_bitstream_operations);
    RUN_TEST(test_frame_lifecycle);
    RUN_TEST(test_rgb_yuv_conversion);
    RUN_TEST(test_dct_idct_accuracy);
    RUN_TEST(test_quant_and_zigzag);
    RUN_TEST(test_rle_and_huffman);
    RUN_TEST(test_vc_container);
    RUN_TEST(test_thread_pool);
    RUN_TEST(test_deblock_filter);
    RUN_TEST(test_encoder_decoder_roundtrip);

    printf("\n------------------------------------------\n");
    printf("Tests Run:    %d\n", g_tests_run);
    printf("Tests Passed: %d\n", g_tests_passed);
    printf("Tests Failed: %d\n", g_tests_run - g_tests_passed);
    printf("------------------------------------------\n");

    return (g_tests_run == g_tests_passed) ? 0 : 1;
}
