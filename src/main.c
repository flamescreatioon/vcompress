#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "file.h"
#include "buffer.h"
#include "frame.h"
#include "compressor.h"
#include "encoder.h"
#include "decoder.h"
#include "container.h"
#include "thread_pool.h"
#include "utils.h"

static void render_progress_bar(uint32_t current, uint32_t total, double elapsed_sec) {
    if (total == 0) return;
    int width = 30;
    double frac = (double)current / (double)total;
    if (frac > 1.0) frac = 1.0;
    int filled = (int)(frac * (double)width);

    printf("\r  [");
    for (int i = 0; i < width; i++) {
        if (i < filled) putchar('#');
        else putchar(' ');
    }
    printf("] %3d%% (%.1fs)", (int)(frac * 100.0), elapsed_sec);
    fflush(stdout);
}

static int run_decompress(const CliOptions *opts) {
    printf("vcompress %s - Decompressor\n\n", VCOMPRESS_VERSION_STR);

    VCReader *reader = vc_reader_open(opts->input_path);
    if (!reader) {
        fprintf(stderr, "Error: Failed to open compressed file '%s'\n", opts->input_path);
        return 1;
    }

    uint32_t width = reader->header.width;
    uint32_t height = reader->header.height;
    uint32_t fps_num = reader->header.fps_num;
    uint32_t fps_den = reader->header.fps_den;
    PixelFormat fmt = (PixelFormat)reader->header.pixel_format;
    uint32_t frame_count = reader->header.frame_count;
    int quality = reader->header.quality;

    printf("Input (.vc):\n");
    printf("  File:       %s\n", opts->input_path);
    printf("  Resolution: %ux%u\n", width, height);
    printf("  Format:     %s\n", pixel_format_name(fmt));
    printf("  Frames:     %u\n", frame_count);
    printf("  Quality:    %d\n\n", quality);

    Y4MWriter *writer = y4m_writer_open(opts->output_path, width, height, fps_num, fps_den, fmt);
    if (!writer) {
        fprintf(stderr, "Error: Failed to create output file '%s'\n", opts->output_path);
        vc_reader_close(reader);
        return 1;
    }

    VideoDecoder *dec = decoder_create(width, height, fmt, quality);
    if (!dec) {
        fprintf(stderr, "Error: Failed to initialize video decoder\n");
        y4m_writer_close(writer);
        vc_reader_close(reader);
        return 1;
    }

    printf("Processing:\n");
    double start_time = utils_get_time_sec();
    Buffer packet;
    buffer_init(&packet);

    uint32_t decoded_frames = 0;
    uint8_t frame_type = 0;
    size_t uncompressed_sz = 0;

    while (vc_reader_read_frame(reader, &frame_type, &packet, &uncompressed_sz)) {
        Frame *f = decoder_decode_frame(dec, packet.data, packet.size);
        if (!f) {
            fprintf(stderr, "\nError decoding frame %u\n", decoded_frames);
            break;
        }

        y4m_writer_write_frame(writer, f);
        frame_destroy(f);

        decoded_frames++;
        if (frame_count > 0 && decoded_frames % 5 == 0) {
            double cur_time = utils_get_time_sec();
            render_progress_bar(decoded_frames, frame_count, cur_time - start_time);
        }
    }

    double total_time = utils_get_time_sec() - start_time;
    if (frame_count > 0) render_progress_bar(decoded_frames, frame_count, total_time);
    printf("\n  Frames: %u\n  Time:   %.1fs\n  FPS:    %.1f\n\n",
           decoded_frames, total_time, (total_time > 0) ? ((double)decoded_frames / total_time) : 0.0);

    buffer_free(&packet);
    decoder_destroy(dec);
    y4m_writer_close(writer);
    vc_reader_close(reader);

    printf("Decompression complete: %s\n", opts->output_path);
    return 0;
}

int main(int argc, char **argv) {
    CliOptions opts;
    if (cli_parse_args(argc, argv, &opts) != 0) {
        cli_options_cleanup(&opts);
        return 1;
    }

    if (opts.help) {
        cli_print_help(argv[0]);
        cli_options_cleanup(&opts);
        return 0;
    }

    if (opts.version) {
        cli_print_version();
        cli_options_cleanup(&opts);
        return 0;
    }

    /* Phase 3 — Container Inspection Mode */
    if (opts.info_only) {
        ContainerInfo info;
        if (!container_inspect(opts.input_path, &info)) {
            fprintf(stderr, "Error: Could not inspect file '%s'\n", opts.input_path);
            cli_options_cleanup(&opts);
            return 1;
        }
        printf("vcompress %s\n\n", VCOMPRESS_VERSION_STR);
        container_print_info(&info, opts.input_path);
        cli_options_cleanup(&opts);
        return 0;
    }

    /* Decompression Mode */
    if (opts.decompress) {
        int ret = run_decompress(&opts);
        cli_options_cleanup(&opts);
        return ret;
    }

    /* -------------------------------------------------------------
     * Compression & Benchmark Pipeline
     * ------------------------------------------------------------- */

    ContainerInfo in_info;
    if (!container_inspect(opts.input_path, &in_info)) {
        fprintf(stderr, "Error: Unable to inspect input '%s'\n", opts.input_path);
        cli_options_cleanup(&opts);
        return 1;
    }

    uint32_t width = in_info.width ? in_info.width : 640;
    uint32_t height = in_info.height ? in_info.height : 480;
    uint32_t fps_num = in_info.fps_num ? in_info.fps_num : 30;
    uint32_t fps_den = in_info.fps_den ? in_info.fps_den : 1;
    if (opts.fps > 0) {
        fps_num = opts.fps;
        fps_den = 1;
    }
    PixelFormat target_format = opts.subsample;

    /* Override with resize if specified */
    uint32_t enc_width = (opts.resize_width > 0) ? opts.resize_width : width;
    uint32_t enc_height = (opts.resize_height > 0) ? opts.resize_height : height;

    /* Setup input reader (supports MP4 and Y4M) */
    VideoReader *vr = video_reader_open(opts.input_path);
    if (!vr) {
        fprintf(stderr, "Error: Unsupported input format or unable to open video: '%s'\n", opts.input_path);
        cli_options_cleanup(&opts);
        return 1;
    }

    enc_width = (opts.resize_width > 0) ? opts.resize_width : vr->width;
    enc_height = (opts.resize_height > 0) ? opts.resize_height : vr->height;

    char in_sz_buf[64];
    utils_format_bytes(in_info.file_size_bytes, in_sz_buf, sizeof(in_sz_buf));

    if (!opts.benchmark) {
        printf("vcompress %s\n\n", VCOMPRESS_VERSION_STR);
        printf("Input:\n");
        printf("  File:       %s\n", opts.input_path);
        printf("  Size:       %s\n", in_sz_buf);
        printf("  Resolution: %ux%u\n", enc_width, enc_height);
        printf("  FPS:        %u\n\n", fps_num / (fps_den ? fps_den : 1));

        printf("Compression:\n");
        printf("  Quality:    %d\n", opts.quality);
        printf("  Threads:    %u\n\n", opts.threads);
    }

    EncoderConfig enc_cfg;
    memset(&enc_cfg, 0, sizeof(enc_cfg));
    enc_cfg.width = enc_width;
    enc_cfg.height = enc_height;
    enc_cfg.format = target_format;
    enc_cfg.fps_num = fps_num;
    enc_cfg.fps_den = fps_den;
    enc_cfg.quality = opts.quality;
    enc_cfg.gop_size = opts.gop_size;
    enc_cfg.threads = opts.threads;
    enc_cfg.inter_threshold = 128;
    enc_cfg.verbose = opts.verbose;

    VideoEncoder *encoder = encoder_create(&enc_cfg);
    if (!encoder) {
        fprintf(stderr, "Error: Failed to initialize encoder\n");
        video_reader_close(vr);
        cli_options_cleanup(&opts);
        return 1;
    }

    VideoWriter *writer = NULL;
    if (!opts.benchmark) {
        writer = video_writer_open(opts.output_path, enc_width, enc_height, fps_num, fps_den, target_format, opts.quality, opts.gop_size);
        if (!writer) {
            fprintf(stderr, "Error: Failed to open output '%s'\n", opts.output_path);
            encoder_destroy(encoder);
            video_reader_close(vr);
            cli_options_cleanup(&opts);
            return 1;
        }
    }

    if (!opts.benchmark) {
        printf("Processing:\n");
    }

    double start_time = utils_get_time_sec();
    uint32_t processed_frames = 0;
    uint64_t total_compressed_bytes = 0;
    uint64_t total_raw_bytes = 0;

    Buffer packet;
    buffer_init(&packet);

    Frame *raw_frame = NULL;
    while ((raw_frame = video_reader_read_frame(vr)) != NULL) {
        Frame *work_frame = raw_frame;
        Frame *resized_frame = NULL;
        Frame *converted_frame = NULL;

        if (raw_frame->width != enc_width || raw_frame->height != enc_height) {
            resized_frame = frame_resize(raw_frame, enc_width, enc_height);
            work_frame = resized_frame;
        }

        if (work_frame->format != target_format) {
            converted_frame = frame_convert(work_frame, target_format);
            work_frame = converted_frame;
        }

        bool is_keyframe = false;
        if (!encoder_encode_frame(encoder, work_frame, &packet, &is_keyframe)) {
            fprintf(stderr, "\nError encoding frame %u\n", processed_frames);
            if (resized_frame) frame_destroy(resized_frame);
            if (converted_frame) frame_destroy(converted_frame);
            frame_destroy(raw_frame);
            break;
        }

        total_compressed_bytes += packet.size;
        total_raw_bytes += work_frame->raw_size;

        if (writer) {
            uint8_t f_type = is_keyframe ? FRAME_TYPE_I : FRAME_TYPE_P;
            video_writer_write_frame(writer, work_frame, f_type, packet.data, packet.size, work_frame->raw_size);
        }

        processed_frames++;

        if (resized_frame) frame_destroy(resized_frame);
        if (converted_frame) frame_destroy(converted_frame);
        frame_destroy(raw_frame);

        if (!opts.benchmark && in_info.frame_count > 0 && processed_frames % 5 == 0) {
            double cur_time = utils_get_time_sec();
            render_progress_bar(processed_frames, in_info.frame_count, cur_time - start_time);
        }
    }

    if (processed_frames == 0) {
        fprintf(stderr, "\nError: No frames could be decoded from '%s'.\n", opts.input_path);
        fprintf(stderr, "The video stream may use an unsupported codec profile (e.g. 10-bit H.264 High 10 Profile / HEVC).\n");
        if (writer) {
            video_writer_close(writer);
            remove(opts.output_path);
        }
        buffer_free(&packet);
        encoder_destroy(encoder);
        video_reader_close(vr);
        cli_options_cleanup(&opts);
        return 1;
    }

    double total_time = utils_get_time_sec() - start_time;

    if (writer) {
        video_writer_close(writer);
    }

    buffer_free(&packet);
    encoder_destroy(encoder);
    video_reader_close(vr);

    if (in_info.frame_count > 0 && !opts.benchmark) {
        render_progress_bar(processed_frames, in_info.frame_count, total_time);
    }

    uint64_t final_output_bytes = total_compressed_bytes;
    if (!opts.benchmark && opts.output_path) {
        FileHandle *out_fh = file_open(opts.output_path, "rb");
        if (out_fh) {
            int64_t sz = file_size(out_fh);
            if (sz > 0) {
                final_output_bytes = (uint64_t)sz;
            }
            file_close(out_fh);
        }
    }

    uint64_t ref_input_bytes = in_info.file_size_bytes > 0 ? in_info.file_size_bytes : total_raw_bytes;
    double reduction = 0.0;
    double ratio = 1.0;
    if (ref_input_bytes > 0 && final_output_bytes > 0) {
        reduction = (1.0 - ((double)final_output_bytes / (double)ref_input_bytes)) * 100.0;
        ratio = (double)ref_input_bytes / (double)final_output_bytes;
    }

    char out_sz_buf[64];
    utils_format_bytes(final_output_bytes, out_sz_buf, sizeof(out_sz_buf));

    if (opts.benchmark) {
        char peak_mem_buf[64];
        utils_format_bytes(utils_get_peak_rss(), peak_mem_buf, sizeof(peak_mem_buf));
        char raw_sz_buf[64];
        utils_format_bytes(total_raw_bytes, raw_sz_buf, sizeof(raw_sz_buf));

        printf("\nBenchmark\n");
        printf("─────────\n");
        printf("Frames:          %u\n", processed_frames);
        printf("Resolution:      %ux%u\n", enc_width, enc_height);
        printf("Threads:         %u\n", opts.threads);
        printf("Input:           %s\n", raw_sz_buf);
        printf("Output:          %s\n\n", out_sz_buf);
        printf("Compression:     %.1f%%\n", reduction);
        printf("Ratio:           %.2f:1\n\n", ratio);
        printf("Time:            %.2f sec\n", total_time);
        printf("Throughput:      %.1f FPS\n", (total_time > 0) ? ((double)processed_frames / total_time) : 0.0);
        printf("Peak memory:     %s\n", peak_mem_buf);
    } else {
        printf("\n  Frames: %u\n", processed_frames);
        printf("  Time:   %.1fs\n\n", total_time);
        printf("Output:\n");
        printf("  Size:       %s\n", out_sz_buf);
        printf("  Reduction:  %.1f%%\n", reduction);
        printf("  Ratio:      %.2f:1\n", ratio);
    }

    cli_options_cleanup(&opts);
    return 0;
}
