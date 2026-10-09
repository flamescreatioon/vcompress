#include "container.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct IMFMediaSink IMFMediaSink;
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

/* -------------------------------------------------------------
 * Custom .vc File Writer & Reader Implementation
 * ------------------------------------------------------------- */

VCWriter *vc_writer_open(const char *path, const VCHeader *header) {
    if (!path || !header) return NULL;

    FileHandle *fh = file_open(path, "wb+");
    if (!fh) return NULL;

    VCWriter *writer = (VCWriter *)calloc(1, sizeof(VCWriter));
    if (!writer) {
        file_close(fh);
        return NULL;
    }

    writer->fh = fh;
    writer->header = *header;
    writer->header.magic = VC_MAGIC;
    writer->header.version = VC_VERSION;
    writer->header.frame_count = 0;
    writer->header.index_offset = 0;

    writer->frame_capacity = 256;
    writer->frame_offsets = (uint64_t *)malloc(writer->frame_capacity * sizeof(uint64_t));
    if (!writer->frame_offsets) {
        file_close(fh);
        free(writer);
        return NULL;
    }

    /* Write placeholder 64-byte file header */
    uint8_t hdr_buf[64] = {0};
    write_u32_be(hdr_buf + 0,  writer->header.magic);
    write_u16_be(hdr_buf + 4,  writer->header.version);
    write_u16_be(hdr_buf + 6,  writer->header.flags);
    write_u32_be(hdr_buf + 8,  writer->header.width);
    write_u32_be(hdr_buf + 12, writer->header.height);
    write_u32_be(hdr_buf + 16, writer->header.fps_num);
    write_u32_be(hdr_buf + 20, writer->header.fps_den);
    write_u32_be(hdr_buf + 24, writer->header.frame_count);
    hdr_buf[28] = writer->header.pixel_format;
    hdr_buf[29] = writer->header.quality;
    write_u16_be(hdr_buf + 30, writer->header.gop_size);
    write_u64_be(hdr_buf + 32, writer->header.index_offset);

    if (file_write(fh, hdr_buf, sizeof(hdr_buf)) != FILE_OK) {
        vc_writer_close(writer);
        return NULL;
    }

    return writer;
}

bool vc_writer_write_frame(VCWriter *writer, uint8_t frame_type, const uint8_t *data, size_t size, size_t uncompressed_size) {
    if (!writer || !writer->fh || !data) return false;

    int64_t cur_pos = file_tell(writer->fh);
    if (cur_pos < 0) return false;

    if (writer->frame_count_written >= writer->frame_capacity) {
        size_t new_cap = writer->frame_capacity * 2;
        uint64_t *new_offsets = (uint64_t *)realloc(writer->frame_offsets, new_cap * sizeof(uint64_t));
        if (!new_offsets) return false;
        writer->frame_offsets = new_offsets;
        writer->frame_capacity = new_cap;
    }

    writer->frame_offsets[writer->frame_count_written] = (uint64_t)cur_pos;

    /* Frame Chunk Header: 13 bytes
     * [0..3]   uint32 frame_index
     * [4]      uint8  frame_type (0=I, 1=P)
     * [5..8]   uint32 compressed_size
     * [9..12]  uint32 uncompressed_size
     */
    uint8_t chunk_hdr[13];
    write_u32_be(chunk_hdr + 0, (uint32_t)writer->frame_count_written);
    chunk_hdr[4] = frame_type;
    write_u32_be(chunk_hdr + 5, (uint32_t)size);
    write_u32_be(chunk_hdr + 9, (uint32_t)uncompressed_size);

    if (file_write(writer->fh, chunk_hdr, sizeof(chunk_hdr)) != FILE_OK) return false;
    if (file_write(writer->fh, data, size) != FILE_OK) return false;

    writer->frame_count_written++;
    return true;
}

bool vc_writer_close(VCWriter *writer) {
    if (!writer) return false;
    bool success = true;

    if (writer->fh) {
        int64_t index_pos = file_tell(writer->fh);
        if (index_pos >= 0) {
            /* Write frame index table */
            for (size_t i = 0; i < writer->frame_count_written; i++) {
                uint8_t off_buf[8];
                write_u64_be(off_buf, writer->frame_offsets[i]);
                if (file_write(writer->fh, off_buf, 8) != FILE_OK) {
                    success = false;
                    break;
                }
            }

            /* Seek back to beginning to rewrite updated header */
            if (file_seek(writer->fh, 0, SEEK_SET) == FILE_OK) {
                uint8_t hdr_buf[64] = {0};
                write_u32_be(hdr_buf + 0,  writer->header.magic);
                write_u16_be(hdr_buf + 4,  writer->header.version);
                write_u16_be(hdr_buf + 6,  writer->header.flags);
                write_u32_be(hdr_buf + 8,  writer->header.width);
                write_u32_be(hdr_buf + 12, writer->header.height);
                write_u32_be(hdr_buf + 16, writer->header.fps_num);
                write_u32_be(hdr_buf + 20, writer->header.fps_den);
                write_u32_be(hdr_buf + 24, (uint32_t)writer->frame_count_written);
                hdr_buf[28] = writer->header.pixel_format;
                hdr_buf[29] = writer->header.quality;
                write_u16_be(hdr_buf + 30, writer->header.gop_size);
                write_u64_be(hdr_buf + 32, (uint64_t)index_pos);

                file_write(writer->fh, hdr_buf, sizeof(hdr_buf));
            }
        }
        file_close(writer->fh);
        writer->fh = NULL;
    }

    if (writer->frame_offsets) {
        free(writer->frame_offsets);
        writer->frame_offsets = NULL;
    }
    free(writer);
    return success;
}

VCReader *vc_reader_open(const char *path) {
    if (!path) return NULL;

    FileHandle *fh = file_open(path, "rb");
    if (!fh) return NULL;

    uint8_t hdr_buf[64];
    size_t read_bytes = 0;
    if (file_read(fh, hdr_buf, sizeof(hdr_buf), &read_bytes) != FILE_OK || read_bytes != sizeof(hdr_buf)) {
        file_close(fh);
        return NULL;
    }

    VCReader *reader = (VCReader *)calloc(1, sizeof(VCReader));
    if (!reader) {
        file_close(fh);
        return NULL;
    }
    reader->fh = fh;

    reader->header.magic        = read_u32_be(hdr_buf + 0);
    reader->header.version      = read_u16_be(hdr_buf + 4);
    reader->header.flags        = read_u16_be(hdr_buf + 6);
    reader->header.width        = read_u32_be(hdr_buf + 8);
    reader->header.height       = read_u32_be(hdr_buf + 12);
    reader->header.fps_num      = read_u32_be(hdr_buf + 16);
    reader->header.fps_den      = read_u32_be(hdr_buf + 20);
    reader->header.frame_count  = read_u32_be(hdr_buf + 24);
    reader->header.pixel_format = hdr_buf[28];
    reader->header.quality      = hdr_buf[29];
    reader->header.gop_size     = read_u16_be(hdr_buf + 30);
    reader->header.index_offset = read_u64_be(hdr_buf + 32);

    if (reader->header.magic != VC_MAGIC) {
        vc_reader_close(reader);
        return NULL;
    }

    /* Read index table if available */
    if (reader->header.index_offset > 0 && reader->header.frame_count > 0) {
        if (file_seek(fh, (int64_t)reader->header.index_offset, SEEK_SET) == FILE_OK) {
            reader->frame_offsets = (uint64_t *)malloc(reader->header.frame_count * sizeof(uint64_t));
            if (reader->frame_offsets) {
                for (uint32_t i = 0; i < reader->header.frame_count; i++) {
                    uint8_t off_buf[8];
                    if (file_read(fh, off_buf, 8, &read_bytes) == FILE_OK && read_bytes == 8) {
                        reader->frame_offsets[i] = read_u64_be(off_buf);
                    } else {
                        reader->frame_offsets[i] = 0;
                    }
                }
            }
        }
    }

    /* Seek back to first frame (immediately following 64-byte header) */
    file_seek(fh, 64, SEEK_SET);
    reader->current_frame = 0;
    return reader;
}

bool vc_reader_read_frame(VCReader *reader, uint8_t *out_frame_type, Buffer *out_packet, size_t *out_uncompressed_size) {
    if (!reader || !reader->fh || !out_frame_type || !out_packet) return false;
    if (reader->current_frame >= reader->header.frame_count) return false;

    if (reader->frame_offsets && reader->frame_offsets[reader->current_frame] > 0) {
        file_seek(reader->fh, (int64_t)reader->frame_offsets[reader->current_frame], SEEK_SET);
    }

    uint8_t chunk_hdr[13];
    size_t read_bytes = 0;
    if (file_read(reader->fh, chunk_hdr, sizeof(chunk_hdr), &read_bytes) != FILE_OK || read_bytes != sizeof(chunk_hdr)) {
        return false;
    }

    *out_frame_type = chunk_hdr[4];
    uint32_t comp_size = read_u32_be(chunk_hdr + 5);
    uint32_t uncomp_size = read_u32_be(chunk_hdr + 9);
    if (out_uncompressed_size) *out_uncompressed_size = (size_t)uncomp_size;

    buffer_clear(out_packet);
    if (buffer_reserve(out_packet, comp_size) != BUF_OK) return false;

    if (file_read(reader->fh, out_packet->data, comp_size, &read_bytes) != FILE_OK || read_bytes != comp_size) {
        return false;
    }
    out_packet->size = comp_size;

    reader->current_frame++;
    return true;
}

bool vc_reader_seek_frame(VCReader *reader, size_t frame_index) {
    if (!reader || !reader->fh || !reader->frame_offsets) return false;
    if (frame_index >= reader->header.frame_count) return false;

    if (file_seek(reader->fh, (int64_t)reader->frame_offsets[frame_index], SEEK_SET) != FILE_OK) return false;
    reader->current_frame = frame_index;
    return true;
}

void vc_reader_close(VCReader *reader) {
    if (!reader) return;
    if (reader->fh) {
        file_close(reader->fh);
        reader->fh = NULL;
    }
    if (reader->frame_offsets) {
        free(reader->frame_offsets);
        reader->frame_offsets = NULL;
    }
    free(reader);
}

/* -------------------------------------------------------------
 * Y4M (YUV4MPEG2) Reader & Writer Implementation
 * ------------------------------------------------------------- */

Y4MReader *y4m_reader_open(const char *path) {
    if (!path) return NULL;
    FileHandle *fh = file_open(path, "rb");
    if (!fh) return NULL;

    char header[256] = {0};
    size_t pos = 0;
    while (pos < sizeof(header) - 1) {
        char c = 0;
        size_t n = 0;
        if (file_read(fh, &c, 1, &n) != FILE_OK || n != 1) {
            file_close(fh);
            return NULL;
        }
        if (c == '\n') break;
        header[pos++] = c;
    }
    header[pos] = '\0';

    if (strncmp(header, "YUV4MPEG2 ", 10) != 0) {
        file_close(fh);
        return NULL;
    }

    Y4MReader *reader = (Y4MReader *)calloc(1, sizeof(Y4MReader));
    if (!reader) {
        file_close(fh);
        return NULL;
    }
    reader->fh = fh;
    reader->fps_num = 30;
    reader->fps_den = 1;
    reader->format = PIX_FMT_YUV420;

    /* Parse tags: W<width> H<height> F<num>:<den> C<chroma> */
    char *token = strtok(header + 10, " ");
    while (token) {
        if (token[0] == 'W') {
            reader->width = (uint32_t)strtoul(token + 1, NULL, 10);
        } else if (token[0] == 'H') {
            reader->height = (uint32_t)strtoul(token + 1, NULL, 10);
        } else if (token[0] == 'F') {
            sscanf(token + 1, "%u:%u", &reader->fps_num, &reader->fps_den);
            if (reader->fps_den == 0) reader->fps_den = 1;
        } else if (token[0] == 'C') {
            if (strncmp(token + 1, "444", 3) == 0) reader->format = PIX_FMT_YUV444;
            else if (strncmp(token + 1, "422", 3) == 0) reader->format = PIX_FMT_YUV422;
            else reader->format = PIX_FMT_YUV420;
        }
        token = strtok(NULL, " ");
    }

    if (reader->width == 0 || reader->height == 0) {
        y4m_reader_close(reader);
        return NULL;
    }

    return reader;
}

Frame *y4m_reader_read_frame(Y4MReader *reader) {
    if (!reader || !reader->fh) return NULL;

    /* Look for "FRAME\n" header */
    char frame_hdr[6] = {0};
    size_t n = 0;
    if (file_read(reader->fh, frame_hdr, 6, &n) != FILE_OK || n != 6) {
        return NULL;
    }

    if (strncmp(frame_hdr, "FRAME\n", 6) != 0) {
        /* Might have optional frame tags: scan until newline */
        if (strncmp(frame_hdr, "FRAME", 5) == 0) {
            char c = frame_hdr[5];
            while (c != '\n') {
                if (file_read(reader->fh, &c, 1, &n) != FILE_OK || n != 1) return NULL;
            }
        } else {
            return NULL;
        }
    }

    Frame *frame = frame_create(reader->width, reader->height, reader->format);
    if (!frame) return NULL;

    /* Read planar data directly into frame */
    for (int p = 0; p < 3; p++) {
        uint32_t w = frame->plane_width[p];
        uint32_t h = frame->plane_height[p];
        for (uint32_t y = 0; y < h; y++) {
            uint8_t *dst = frame->data[p] + (size_t)y * frame->stride[p];
            if (file_read(reader->fh, dst, w, &n) != FILE_OK || n != w) {
                frame_destroy(frame);
                return NULL;
            }
        }
    }

    return frame;
}

void y4m_reader_close(Y4MReader *reader) {
    if (!reader) return;
    if (reader->fh) file_close(reader->fh);
    free(reader);
}

Y4MWriter *y4m_writer_open(const char *path, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den, PixelFormat format) {
    if (!path || width == 0 || height == 0) return NULL;
    FileHandle *fh = file_open(path, "wb");
    if (!fh) return NULL;

    PixelFormat write_format = format;
    if (write_format == PIX_FMT_RGB24) {
        write_format = PIX_FMT_YUV420;
    }

    const char *chroma_str = "420jpeg";
    if (write_format == PIX_FMT_YUV444) chroma_str = "444";
    else if (write_format == PIX_FMT_YUV422) chroma_str = "422";

    char header[256];
    int len = snprintf(header, sizeof(header), "YUV4MPEG2 W%u H%u F%u:%u Ip A1:1 C%s\n",
                       width, height, fps_num, fps_den ? fps_den : 1, chroma_str);

    if (file_write(fh, header, (size_t)len) != FILE_OK) {
        file_close(fh);
        return NULL;
    }

    Y4MWriter *writer = (Y4MWriter *)calloc(1, sizeof(Y4MWriter));
    if (!writer) {
        file_close(fh);
        return NULL;
    }
    writer->fh = fh;
    writer->width = width;
    writer->height = height;
    writer->fps_num = fps_num;
    writer->fps_den = fps_den;
    writer->format = write_format;
    return writer;
}

bool y4m_writer_write_frame(Y4MWriter *writer, const Frame *frame) {
    if (!writer || !writer->fh || !frame) return false;

    Frame *converted = NULL;
    const Frame *src = frame;
    if (frame->format != writer->format) {
        converted = frame_convert(frame, writer->format);
        if (!converted) return false;
        src = converted;
    }

    if (file_write(writer->fh, "FRAME\n", 6) != FILE_OK) {
        if (converted) frame_destroy(converted);
        return false;
    }

    for (int p = 0; p < 3; p++) {
        uint32_t w = src->plane_width[p];
        uint32_t h = src->plane_height[p];
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *line = src->data[p] + (size_t)y * src->stride[p];
            if (file_write(writer->fh, line, w) != FILE_OK) {
                if (converted) frame_destroy(converted);
                return false;
            }
        }
    }

    if (converted) frame_destroy(converted);
    return true;
}

void y4m_writer_close(Y4MWriter *writer) {
    if (!writer) return;
    if (writer->fh) file_close(writer->fh);
    free(writer);
}

/* -------------------------------------------------------------
 * MP4 (ISOBMFF) Container Parser from scratch
 * ------------------------------------------------------------- */

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | ((uint32_t)(d)))

typedef struct MP4Context {
    uint8_t *data;
    size_t size;
    size_t pos;
    ContainerInfo *info;
} MP4Context;

static void parse_mp4_boxes(MP4Context *ctx, size_t end_pos, int depth);

static void parse_mvhd(MP4Context *ctx, size_t box_end) {
    if (ctx->pos + 28 > box_end) return;
    uint8_t version = ctx->data[ctx->pos];
    ctx->pos += 4; /* skip version + flags */

    uint32_t timescale = 0;
    uint64_t duration = 0;

    if (version == 1) {
        if (ctx->pos + 28 > box_end) return;
        ctx->pos += 16; /* skip creation & modification time (64-bit each) */
        timescale = read_u32_be(ctx->data + ctx->pos);
        ctx->pos += 4;
        duration = read_u64_be(ctx->data + ctx->pos);
        ctx->pos += 8;
    } else {
        if (ctx->pos + 16 > box_end) return;
        ctx->pos += 8; /* skip creation & modification time (32-bit each) */
        timescale = read_u32_be(ctx->data + ctx->pos);
        ctx->pos += 4;
        duration = read_u32_be(ctx->data + ctx->pos);
        ctx->pos += 4;
    }

    if (timescale > 0) {
        ctx->info->duration_sec = (double)duration / (double)timescale;
    }
}

static void parse_tkhd(MP4Context *ctx, size_t box_end) {
    if (ctx->pos + 4 > box_end) return;
    uint8_t version = ctx->data[ctx->pos];
    ctx->pos += 4; /* version + flags */

    if (version == 1) {
        if (ctx->pos + 32 > box_end) return;
        ctx->pos += 32; /* creation(8), mod(8), track_id(4), reserved(4), duration(8) */
    } else {
        if (ctx->pos + 20 > box_end) return;
        ctx->pos += 20; /* creation(4), mod(4), track_id(4), reserved(4), duration(4) */
    }

    /* reserved(8) + layer(2) + alternate_group(2) + volume(2) + reserved(2) + matrix(36) = 52 bytes */
    if (ctx->pos + 52 > box_end) return;
    ctx->pos += 52;

    if (ctx->pos + 8 <= box_end) {
        uint32_t w = read_u32_be(ctx->data + ctx->pos) >> 16; /* 16.16 fixed-point */
        uint32_t h = read_u32_be(ctx->data + ctx->pos + 4) >> 16;
        if (w > 0 && h > 0 && ctx->info->width == 0) {
            ctx->info->width = w;
            ctx->info->height = h;
        }
    }
}

static void parse_hdlr(MP4Context *ctx, size_t box_end) {
    if (ctx->pos + 16 > box_end) return;
    ctx->pos += 8; /* version/flags + pre_defined */
    uint32_t handler_type = read_u32_be(ctx->data + ctx->pos);
    ctx->pos += 4;

    if (handler_type == FOURCC('v', 'i', 'd', 'e')) {
        ctx->info->video_stream_count++;
    } else if (handler_type == FOURCC('s', 'o', 'u', 'n')) {
        ctx->info->audio_stream_count++;
        if (ctx->info->audio_codec[0] == '\0') {
            snprintf(ctx->info->audio_codec, sizeof(ctx->info->audio_codec), "AAC / Audio");
        }
    }
}

static void parse_stsd(MP4Context *ctx, size_t box_end) {
    if (ctx->pos + 16 > box_end) return;
    ctx->pos += 8; /* version + flags + entry_count */

    if (ctx->pos + 8 <= box_end) {
        uint32_t entry_size = read_u32_be(ctx->data + ctx->pos);
        uint32_t format = read_u32_be(ctx->data + ctx->pos + 4);
        (void)entry_size;

        if (format == FOURCC('a', 'v', 'c', '1')) {
            snprintf(ctx->info->video_codec, sizeof(ctx->info->video_codec), "H.264 / AVC");
        } else if (format == FOURCC('h', 'v', 'c', '1') || format == FOURCC('h', 'e', 'v', '1')) {
            snprintf(ctx->info->video_codec, sizeof(ctx->info->video_codec), "H.265 / HEVC");
        } else if (format == FOURCC('m', 'p', '4', 'v')) {
            snprintf(ctx->info->video_codec, sizeof(ctx->info->video_codec), "MPEG-4 Part 2");
        } else if (format == FOURCC('v', 'p', '0', '9')) {
            snprintf(ctx->info->video_codec, sizeof(ctx->info->video_codec), "VP9");
        } else if (format == FOURCC('a', 'v', '0', '1')) {
            snprintf(ctx->info->video_codec, sizeof(ctx->info->video_codec), "AV1");
        } else if (format == FOURCC('m', 'p', '4', 'a')) {
            snprintf(ctx->info->audio_codec, sizeof(ctx->info->audio_codec), "AAC Audio");
        }
    }
}

static void parse_stts(MP4Context *ctx, size_t box_end) {
    if (ctx->pos + 8 > box_end) return;
    ctx->pos += 4; /* version + flags */
    uint32_t entry_count = read_u32_be(ctx->data + ctx->pos);
    ctx->pos += 4;

    uint32_t total_samples = 0;
    for (uint32_t i = 0; i < entry_count && ctx->pos + 8 <= box_end; i++) {
        uint32_t sample_count = read_u32_be(ctx->data + ctx->pos);
        ctx->pos += 8;
        total_samples += sample_count;
    }

    if (total_samples > 0 && ctx->info->frame_count == 0) {
        ctx->info->frame_count = total_samples;
    }
}

static void parse_stsz(MP4Context *ctx, size_t box_end) {
    if (ctx->pos + 12 > box_end) return;
    ctx->pos += 8; /* version + flags + sample_size */
    uint32_t sample_count = read_u32_be(ctx->data + ctx->pos);
    ctx->pos += 4;

    if (sample_count > 0 && ctx->info->frame_count == 0) {
        ctx->info->frame_count = sample_count;
    }
}

static void parse_mp4_boxes(MP4Context *ctx, size_t end_pos, int depth) {
    if (depth > 12) return; /* Guard against deep recursion */

    while (ctx->pos + 8 <= end_pos) {
        size_t box_start = ctx->pos;
        uint32_t size32 = read_u32_be(ctx->data + ctx->pos);
        uint32_t type = read_u32_be(ctx->data + ctx->pos + 4);
        ctx->pos += 8;

        size_t box_size = (size_t)size32;
        if (size32 == 1) {
            /* 64-bit extended size */
            if (ctx->pos + 8 > end_pos) break;
            box_size = (size_t)read_u64_be(ctx->data + ctx->pos);
            ctx->pos += 8;
        } else if (size32 == 0) {
            /* Extends to EOF */
            box_size = end_pos - box_start;
        }

        if (box_size < 8 || box_start + box_size > end_pos) {
            break;
        }

        size_t box_payload_end = box_start + box_size;

        if (type == FOURCC('m', 'o', 'o', 'v') ||
            type == FOURCC('t', 'r', 'a', 'k') ||
            type == FOURCC('m', 'd', 'i', 'a') ||
            type == FOURCC('m', 'i', 'n', 'f') ||
            type == FOURCC('s', 't', 'b', 'l')) {
            parse_mp4_boxes(ctx, box_payload_end, depth + 1);
        } else if (type == FOURCC('m', 'v', 'h', 'd')) {
            parse_mvhd(ctx, box_payload_end);
        } else if (type == FOURCC('t', 'k', 'h', 'd')) {
            parse_tkhd(ctx, box_payload_end);
        } else if (type == FOURCC('h', 'd', 'l', 'r')) {
            parse_hdlr(ctx, box_payload_end);
        } else if (type == FOURCC('s', 't', 's', 'd')) {
            parse_stsd(ctx, box_payload_end);
        } else if (type == FOURCC('s', 't', 't', 's')) {
            parse_stts(ctx, box_payload_end);
        } else if (type == FOURCC('s', 't', 's', 'z')) {
            parse_stsz(ctx, box_payload_end);
        }

        ctx->pos = box_payload_end;
    }
}

/* -------------------------------------------------------------
 * Container Inspection Entry Point
 * ------------------------------------------------------------- */

bool container_inspect(const char *path, ContainerInfo *info) {
    if (!path || !info) return false;
    memset(info, 0, sizeof(ContainerInfo));

    FileHandle *fh = file_open(path, "rb");
    if (!fh) return false;

    int64_t fsz = file_size(fh);
    if (fsz < 0) {
        file_close(fh);
        return false;
    }
    info->file_size_bytes = (uint64_t)fsz;

    uint8_t probe[64] = {0};
    size_t bytes_read = 0;
    file_read(fh, probe, sizeof(probe), &bytes_read);
    file_close(fh);

    /* 1. Check Custom .vc Format */
    if (bytes_read >= 64 && read_u32_be(probe) == VC_MAGIC) {
        snprintf(info->container_name, sizeof(info->container_name), "vcompress (.vc)");
        snprintf(info->video_codec, sizeof(info->video_codec), "VC-DCT/Huffman");
        info->width = read_u32_be(probe + 8);
        info->height = read_u32_be(probe + 12);
        info->fps_num = read_u32_be(probe + 16);
        info->fps_den = read_u32_be(probe + 20);
        info->fps = info->fps_den ? (double)info->fps_num / (double)info->fps_den : 30.0;
        info->frame_count = read_u32_be(probe + 24);
        info->pixel_format = (PixelFormat)probe[28];
        info->video_stream_count = 1;
        info->audio_stream_count = 0;

        if (info->fps > 0 && info->frame_count > 0) {
            info->duration_sec = (double)info->frame_count / info->fps;
            if (info->duration_sec > 0) {
                info->bitrate_bps = (uint64_t)(((double)info->file_size_bytes * 8.0) / info->duration_sec);
            }
        }
        return true;
    }

    /* 2. Check Y4M Format */
    if (bytes_read >= 10 && strncmp((const char *)probe, "YUV4MPEG2 ", 10) == 0) {
        Y4MReader *y4m = y4m_reader_open(path);
        if (y4m) {
            snprintf(info->container_name, sizeof(info->container_name), "YUV4MPEG2 (.y4m)");
            snprintf(info->video_codec, sizeof(info->video_codec), "Raw Uncompressed YUV");
            info->width = y4m->width;
            info->height = y4m->height;
            info->fps_num = y4m->fps_num;
            info->fps_den = y4m->fps_den;
            info->fps = y4m->fps_den ? (double)y4m->fps_num / (double)y4m->fps_den : 30.0;
            info->pixel_format = y4m->format;
            info->video_stream_count = 1;
            info->audio_stream_count = 0;

            /* Estimate frame count from file size */
            size_t y_sz = (size_t)y4m->width * y4m->height;
            size_t f_sz = (y4m->format == PIX_FMT_YUV420) ? (y_sz * 3 / 2) : (y_sz * 3);
            f_sz += 6; /* FRAME\n header */
            if (f_sz > 0) {
                info->frame_count = (uint32_t)(info->file_size_bytes / f_sz);
                if (info->fps > 0) {
                    info->duration_sec = (double)info->frame_count / info->fps;
                    if (info->duration_sec > 0) {
                        info->bitrate_bps = (uint64_t)(((double)info->file_size_bytes * 8.0) / info->duration_sec);
                    }
                }
            }
            y4m_reader_close(y4m);
            return true;
        }
    }

    /* 3. Check MP4 / QuickTime (ISOBMFF) */
    if (bytes_read >= 12 && (read_u32_be(probe + 4) == FOURCC('f', 't', 'y', 'p') ||
                             read_u32_be(probe + 4) == FOURCC('m', 'o', 'o', 'v') ||
                             read_u32_be(probe + 4) == FOURCC('m', 'd', 'a', 't'))) {
        snprintf(info->container_name, sizeof(info->container_name), "MP4 / QuickTime (ISOBMFF)");

        uint8_t *file_data = NULL;
        size_t file_len = 0;
        /* Slurp up to 16MB of header to parse boxes without loading gigantic files into memory */
        size_t inspect_limit = (size_t)info->file_size_bytes;
        if (inspect_limit > 16 * 1024 * 1024) inspect_limit = 16 * 1024 * 1024;

        FileHandle *mp4_fh = file_open(path, "rb");
        if (mp4_fh) {
            file_data = (uint8_t *)malloc(inspect_limit);
            if (file_data) {
                file_read(mp4_fh, file_data, inspect_limit, &file_len);
                MP4Context ctx;
                ctx.data = file_data;
                ctx.size = file_len;
                ctx.pos = 0;
                ctx.info = info;

                parse_mp4_boxes(&ctx, file_len, 0);
                free(file_data);
            }
            file_close(mp4_fh);
        }

        if (info->video_codec[0] == '\0') {
            snprintf(info->video_codec, sizeof(info->video_codec), "H.264 / AVC (Standard)");
        }
        if (info->audio_codec[0] == '\0' && info->audio_stream_count > 0) {
            snprintf(info->audio_codec, sizeof(info->audio_codec), "AAC / LC");
        }

        if (info->duration_sec > 0.0 && info->frame_count > 0) {
            info->fps = (double)info->frame_count / info->duration_sec;
            info->fps_num = (uint32_t)(info->fps * 1000.0 + 0.5);
            info->fps_den = 1000;
        } else if (info->fps == 0.0) {
            info->fps = 30.0;
            info->fps_num = 30;
            info->fps_den = 1;
        }

        if (info->duration_sec > 0.0) {
            info->bitrate_bps = (uint64_t)(((double)info->file_size_bytes * 8.0) / info->duration_sec);
        }

        return true;
    }

    /* Fallback Unknown binary format */
    snprintf(info->container_name, sizeof(info->container_name), "Raw / Unknown Binary");
    snprintf(info->video_codec, sizeof(info->video_codec), "Uncompressed / Custom");
    return true;
}

void container_print_info(const ContainerInfo *info, const char *path) {
    if (!info) return;

    char sz_buf[64];
    utils_format_bytes(info->file_size_bytes, sz_buf, sizeof(sz_buf));

    printf("File:          %s\n", path ? path : "unknown");
    printf("Container:     %s\n", info->container_name);
    printf("Video Codec:   %s\n", info->video_codec[0] ? info->video_codec : "None");
    if (info->audio_stream_count > 0) {
        printf("Audio Codec:   %s\n", info->audio_codec[0] ? info->audio_codec : "Audio");
    }
    printf("Resolution:    %ux%u\n", info->width, info->height);
    printf("Frame rate:    %.2f fps\n", info->fps > 0 ? info->fps : 30.0);
    if (info->frame_count > 0) {
        printf("Frame count:   %u\n", info->frame_count);
    }
    if (info->duration_sec > 0) {
        printf("Duration:      %.2fs\n", info->duration_sec);
    }
    if (info->bitrate_bps > 0) {
        double mbps = (double)info->bitrate_bps / 1000000.0;
        printf("Bitrate:       %.2f Mbps\n", mbps);
    }
    printf("Video streams: %d\n", info->video_stream_count > 0 ? info->video_stream_count : 1);
    printf("Audio streams: %d\n", info->audio_stream_count);
    printf("File size:     %s\n", sz_buf);
}

/* -------------------------------------------------------------
 * MP4 Frame Reader (Native Decoded Video Frame Extraction)
 * ------------------------------------------------------------- */

#if defined(_WIN32)

struct MP4Reader {
    IMFSourceReader *source_reader;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    int32_t actual_stride;
};

MP4Reader *mp4_reader_open(const char *path, uint32_t *out_width, uint32_t *out_height, uint32_t *out_fps_num, uint32_t *out_fps_den) {
    if (!path) return NULL;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        return NULL;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *wpath = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    IMFAttributes *pAttr = NULL;
    MFCreateAttributes(&pAttr, 1);
    if (pAttr) {
        pAttr->lpVtbl->SetUINT32(pAttr, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }

    IMFSourceReader *pReader = NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath, pAttr, &pReader);
    free(wpath);
    if (pAttr) pAttr->lpVtbl->Release(pAttr);
    if (FAILED(hr) || !pReader) {
        return NULL;
    }

    pReader->lpVtbl->SetStreamSelection(pReader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    pReader->lpVtbl->SetStreamSelection(pReader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);

    IMFMediaType *pType = NULL;
    hr = MFCreateMediaType(&pType);
    if (FAILED(hr)) {
        pReader->lpVtbl->Release(pReader);
        return NULL;
    }

    pType->lpVtbl->SetGUID(pType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    pType->lpVtbl->SetGUID(pType, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);

    hr = pReader->lpVtbl->SetCurrentMediaType(pReader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pType);
    pType->lpVtbl->Release(pType);
    if (FAILED(hr)) {
        pReader->lpVtbl->Release(pReader);
        return NULL;
    }

    IMFMediaType *pCurrentType = NULL;
    pReader->lpVtbl->GetCurrentMediaType(pReader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &pCurrentType);

    uint32_t width = 0, height = 0;
    uint32_t fps_num = 30, fps_den = 1;
    int32_t actual_stride = 0;

    if (pCurrentType) {
        UINT64 frame_size_val = 0;
        if (SUCCEEDED(pCurrentType->lpVtbl->GetUINT64(pCurrentType, &MF_MT_FRAME_SIZE, &frame_size_val))) {
            width = (uint32_t)(frame_size_val >> 32);
            height = (uint32_t)(frame_size_val & 0xFFFFFFFFULL);
        }
        UINT64 frame_rate_val = 0;
        if (SUCCEEDED(pCurrentType->lpVtbl->GetUINT64(pCurrentType, &MF_MT_FRAME_RATE, &frame_rate_val))) {
            fps_num = (uint32_t)(frame_rate_val >> 32);
            fps_den = (uint32_t)(frame_rate_val & 0xFFFFFFFFULL);
        }
        UINT32 stride_val = 0;
        if (SUCCEEDED(pCurrentType->lpVtbl->GetUINT32(pCurrentType, &MF_MT_DEFAULT_STRIDE, &stride_val))) {
            actual_stride = (int32_t)stride_val;
        } else {
            LONG def_stride = 0;
            if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &def_stride))) {
                actual_stride = (int32_t)def_stride;
            } else {
                actual_stride = (int32_t)(width * 4);
            }
        }
        pCurrentType->lpVtbl->Release(pCurrentType);
    }

    if (width == 0 || height == 0) {
        pReader->lpVtbl->Release(pReader);
        return NULL;
    }

    MP4Reader *reader = (MP4Reader *)calloc(1, sizeof(MP4Reader));
    if (!reader) {
        pReader->lpVtbl->Release(pReader);
        return NULL;
    }

    reader->source_reader = pReader;
    reader->width = width;
    reader->height = height;
    reader->fps_num = fps_num;
    reader->fps_den = fps_den ? fps_den : 1;
    reader->actual_stride = (actual_stride != 0) ? actual_stride : (int32_t)(width * 4);

    if (out_width) *out_width = width;
    if (out_height) *out_height = height;
    if (out_fps_num) *out_fps_num = fps_num;
    if (out_fps_den) *out_fps_den = reader->fps_den;

    return reader;
}

Frame *mp4_reader_read_frame(MP4Reader *reader) {
    if (!reader || !reader->source_reader) return NULL;

    DWORD streamIndex = 0;
    DWORD flags = 0;
    LONGLONG timeStamp = 0;
    IMFSample *pSample = NULL;

    while (true) {
        HRESULT hr = reader->source_reader->lpVtbl->ReadSample(
            reader->source_reader,
            (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,
            &streamIndex,
            &flags,
            &timeStamp,
            &pSample
        );

        if (FAILED(hr) || (flags & (DWORD)MF_SOURCE_READERF_ENDOFSTREAM)) {
            return NULL;
        }

        if (pSample) break;
    }

    IMFMediaBuffer *pBuffer = NULL;
    HRESULT hr = pSample->lpVtbl->ConvertToContiguousBuffer(pSample, &pBuffer);
    if (FAILED(hr)) {
        pSample->lpVtbl->Release(pSample);
        return NULL;
    }

    BYTE *pData = NULL;
    DWORD currentLength = 0;
    hr = pBuffer->lpVtbl->Lock(pBuffer, &pData, NULL, &currentLength);
    if (FAILED(hr) || !pData) {
        pBuffer->lpVtbl->Release(pBuffer);
        pSample->lpVtbl->Release(pSample);
        return NULL;
    }

    Frame *frame = frame_create(reader->width, reader->height, PIX_FMT_RGB24);
    if (frame) {
        frame->pts = (uint64_t)timeStamp;
        uint32_t w = reader->width;
        uint32_t h = reader->height;
        int32_t stride = reader->actual_stride;
        size_t abs_stride = (size_t)(stride < 0 ? -stride : stride);

        for (uint32_t y = 0; y < h; y++) {
            uint32_t src_y = (stride < 0) ? (h - 1 - y) : y;
            const uint8_t *src_row = pData + (size_t)src_y * abs_stride;
            uint8_t *dst_row = frame->data[0] + (size_t)y * frame->stride[0];

            for (uint32_t x = 0; x < w; x++) {
                uint8_t b = src_row[(size_t)x * 4 + 0];
                uint8_t g = src_row[(size_t)x * 4 + 1];
                uint8_t r = src_row[(size_t)x * 4 + 2];
                dst_row[(size_t)x * 3 + 0] = r;
                dst_row[(size_t)x * 3 + 1] = g;
                dst_row[(size_t)x * 3 + 2] = b;
            }
        }
    }

    pBuffer->lpVtbl->Unlock(pBuffer);
    pBuffer->lpVtbl->Release(pBuffer);
    pSample->lpVtbl->Release(pSample);

    return frame;
}

void mp4_reader_close(MP4Reader *reader) {
    if (!reader) return;
    if (reader->source_reader) {
        reader->source_reader->lpVtbl->Release(reader->source_reader);
        reader->source_reader = NULL;
    }
    free(reader);
    MFShutdown();
    CoUninitialize();
}

#else

struct MP4Reader { int dummy; };
MP4Reader *mp4_reader_open(const char *path, uint32_t *out_width, uint32_t *out_height, uint32_t *out_fps_num, uint32_t *out_fps_den) {
    (void)path; (void)out_width; (void)out_height; (void)out_fps_num; (void)out_fps_den;
    return NULL;
}
Frame *mp4_reader_read_frame(MP4Reader *reader) { (void)reader; return NULL; }
void mp4_reader_close(MP4Reader *reader) { (void)reader; }

#endif

/* -------------------------------------------------------------
 * Unified Video Reader Implementation
 * ------------------------------------------------------------- */

VideoReader *video_reader_open(const char *path) {
    if (!path) return NULL;

    /* Check if input is Y4M */
    Y4MReader *y4m = y4m_reader_open(path);
    if (y4m) {
        VideoReader *vr = (VideoReader *)calloc(1, sizeof(VideoReader));
        if (!vr) {
            y4m_reader_close(y4m);
            return NULL;
        }
        vr->is_mp4 = false;
        vr->y4m = y4m;
        vr->width = y4m->width;
        vr->height = y4m->height;
        vr->fps_num = y4m->fps_num;
        vr->fps_den = y4m->fps_den;
        vr->format = y4m->format;
        return vr;
    }

    /* Check if input is MP4 */
    uint32_t w = 0, h = 0, fps_n = 30, fps_d = 1;
    MP4Reader *mp4 = mp4_reader_open(path, &w, &h, &fps_n, &fps_d);
    if (mp4) {
        VideoReader *vr = (VideoReader *)calloc(1, sizeof(VideoReader));
        if (!vr) {
            mp4_reader_close(mp4);
            return NULL;
        }
        vr->is_mp4 = true;
        vr->mp4 = mp4;
        vr->width = w;
        vr->height = h;
        vr->fps_num = fps_n;
        vr->fps_den = fps_d;
        vr->format = PIX_FMT_RGB24;
        return vr;
    }

    return NULL;
}

Frame *video_reader_read_frame(VideoReader *reader) {
    if (!reader) return NULL;
    if (reader->is_mp4) {
        return mp4_reader_read_frame(reader->mp4);
    } else {
        return y4m_reader_read_frame(reader->y4m);
    }
}

void video_reader_close(VideoReader *reader) {
    if (!reader) return;
    if (reader->is_mp4 && reader->mp4) {
        mp4_reader_close(reader->mp4);
    } else if (reader->y4m) {
        y4m_reader_close(reader->y4m);
    }
    free(reader);
}

/* -------------------------------------------------------------
 * MP4 Video Writer Implementation
 * ------------------------------------------------------------- */

#if defined(_WIN32)

struct MP4Writer {
    IMFSinkWriter *sink_writer;
    DWORD stream_index;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint64_t frame_index;
    LONGLONG frame_duration;
};

MP4Writer *mp4_writer_open(const char *path, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den, int quality) {
    if (!path || width == 0 || height == 0) return NULL;
    if (fps_num == 0) fps_num = 30;
    if (fps_den == 0) fps_den = 1;
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        return NULL;
    }

    int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (wlen <= 0) return NULL;
    wchar_t *wpath = (wchar_t *)malloc((size_t)wlen * sizeof(wchar_t));
    if (!wpath) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, wlen);

    IMFSinkWriter *pWriter = NULL;
    HRESULT hr = MFCreateSinkWriterFromURL(wpath, NULL, NULL, &pWriter);
    free(wpath);
    if (FAILED(hr) || !pWriter) return NULL;

    /* Calculate target bitrate from resolution, FPS, and quality (1-100) */
    double fps = (double)fps_num / (double)fps_den;
    double bpp = 0.04 + ((double)quality / 100.0) * 0.12; /* 0.04 to 0.16 bits per pixel */
    uint32_t target_bitrate = (uint32_t)((double)width * (double)height * fps * bpp);
    if (target_bitrate < 200000) target_bitrate = 200000;
    if (target_bitrate > 40000000) target_bitrate = 40000000;

    /* Output media type: H.264 / AVC */
    IMFMediaType *pOutType = NULL;
    hr = MFCreateMediaType(&pOutType);
    if (FAILED(hr)) {
        pWriter->lpVtbl->Release(pWriter);
        return NULL;
    }

    pOutType->lpVtbl->SetGUID(pOutType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    pOutType->lpVtbl->SetGUID(pOutType, &MF_MT_SUBTYPE, &MFVideoFormat_H264);
    pOutType->lpVtbl->SetUINT32(pOutType, &MF_MT_AVG_BITRATE, target_bitrate);
    pOutType->lpVtbl->SetUINT32(pOutType, &MF_MT_INTERLACE_MODE, (UINT32)MFVideoInterlace_Progressive);
    pOutType->lpVtbl->SetUINT64(pOutType, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    pOutType->lpVtbl->SetUINT64(pOutType, &MF_MT_FRAME_RATE, ((UINT64)fps_num << 32) | fps_den);
    pOutType->lpVtbl->SetUINT64(pOutType, &MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);

    DWORD streamIndex = 0;
    hr = pWriter->lpVtbl->AddStream(pWriter, pOutType, &streamIndex);
    pOutType->lpVtbl->Release(pOutType);
    if (FAILED(hr)) {
        pWriter->lpVtbl->Release(pWriter);
        return NULL;
    }

    /* Input media type: RGB32 uncompressed */
    IMFMediaType *pInType = NULL;
    hr = MFCreateMediaType(&pInType);
    if (FAILED(hr)) {
        pWriter->lpVtbl->Release(pWriter);
        return NULL;
    }

    pInType->lpVtbl->SetGUID(pInType, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    pInType->lpVtbl->SetGUID(pInType, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    pInType->lpVtbl->SetUINT32(pInType, &MF_MT_DEFAULT_STRIDE, (UINT32)(width * 4));
    pInType->lpVtbl->SetUINT32(pInType, &MF_MT_INTERLACE_MODE, (UINT32)MFVideoInterlace_Progressive);
    pInType->lpVtbl->SetUINT64(pInType, &MF_MT_FRAME_SIZE, ((UINT64)width << 32) | height);
    pInType->lpVtbl->SetUINT64(pInType, &MF_MT_FRAME_RATE, ((UINT64)fps_num << 32) | fps_den);
    pInType->lpVtbl->SetUINT64(pInType, &MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1 << 32) | 1);

    hr = pWriter->lpVtbl->SetInputMediaType(pWriter, streamIndex, pInType, NULL);
    pInType->lpVtbl->Release(pInType);
    if (FAILED(hr)) {
        pWriter->lpVtbl->Release(pWriter);
        return NULL;
    }

    hr = pWriter->lpVtbl->BeginWriting(pWriter);
    if (FAILED(hr)) {
        pWriter->lpVtbl->Release(pWriter);
        return NULL;
    }

    MP4Writer *writer = (MP4Writer *)calloc(1, sizeof(MP4Writer));
    if (!writer) {
        pWriter->lpVtbl->Release(pWriter);
        return NULL;
    }

    writer->sink_writer = pWriter;
    writer->stream_index = streamIndex;
    writer->width = width;
    writer->height = height;
    writer->fps_num = fps_num;
    writer->fps_den = fps_den;
    writer->frame_index = 0;
    writer->frame_duration = (LONGLONG)((10000000ULL * (UINT64)fps_den) / (UINT64)fps_num);

    return writer;
}

bool mp4_writer_write_frame(MP4Writer *writer, const Frame *frame) {
    if (!writer || !writer->sink_writer || !frame) return false;

    Frame *rgb_frame = NULL;
    const Frame *src = frame;
    if (frame->format != PIX_FMT_RGB24) {
        rgb_frame = frame_convert(frame, PIX_FMT_RGB24);
        if (!rgb_frame) return false;
        src = rgb_frame;
    }

    DWORD buffer_len = writer->width * writer->height * 4;
    IMFMediaBuffer *pBuffer = NULL;
    HRESULT hr = MFCreateMemoryBuffer(buffer_len, &pBuffer);
    if (FAILED(hr) || !pBuffer) {
        if (rgb_frame) frame_destroy(rgb_frame);
        return false;
    }

    BYTE *pData = NULL;
    hr = pBuffer->lpVtbl->Lock(pBuffer, &pData, NULL, NULL);
    if (FAILED(hr) || !pData) {
        pBuffer->lpVtbl->Release(pBuffer);
        if (rgb_frame) frame_destroy(rgb_frame);
        return false;
    }

    uint32_t w = writer->width;
    uint32_t h = writer->height;

    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src_row = src->data[0] + (size_t)y * src->stride[0];
        BYTE *dst_row = pData + (size_t)y * (w * 4);

        for (uint32_t x = 0; x < w; x++) {
            uint8_t r = src_row[(size_t)x * 3 + 0];
            uint8_t g = src_row[(size_t)x * 3 + 1];
            uint8_t b = src_row[(size_t)x * 3 + 2];
            dst_row[(size_t)x * 4 + 0] = b;
            dst_row[(size_t)x * 4 + 1] = g;
            dst_row[(size_t)x * 4 + 2] = r;
            dst_row[(size_t)x * 4 + 3] = 0xFF;
        }
    }

    pBuffer->lpVtbl->Unlock(pBuffer);
    pBuffer->lpVtbl->SetCurrentLength(pBuffer, buffer_len);

    IMFSample *pSample = NULL;
    hr = MFCreateSample(&pSample);
    if (FAILED(hr) || !pSample) {
        pBuffer->lpVtbl->Release(pBuffer);
        if (rgb_frame) frame_destroy(rgb_frame);
        return false;
    }

    pSample->lpVtbl->AddBuffer(pSample, pBuffer);
    pSample->lpVtbl->SetSampleTime(pSample, (LONGLONG)writer->frame_index * writer->frame_duration);
    pSample->lpVtbl->SetSampleDuration(pSample, writer->frame_duration);

    hr = writer->sink_writer->lpVtbl->WriteSample(writer->sink_writer, writer->stream_index, pSample);

    pSample->lpVtbl->Release(pSample);
    pBuffer->lpVtbl->Release(pBuffer);
    if (rgb_frame) frame_destroy(rgb_frame);

    if (FAILED(hr)) return false;

    writer->frame_index++;
    return true;
}

void mp4_writer_close(MP4Writer *writer) {
    if (!writer) return;
    if (writer->sink_writer) {
        writer->sink_writer->lpVtbl->Finalize(writer->sink_writer);
        writer->sink_writer->lpVtbl->Release(writer->sink_writer);
        writer->sink_writer = NULL;
    }
    free(writer);
    MFShutdown();
    CoUninitialize();
}

#else

struct MP4Writer { int dummy; };
MP4Writer *mp4_writer_open(const char *path, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den, int quality) {
    (void)path; (void)width; (void)height; (void)fps_num; (void)fps_den; (void)quality;
    return NULL;
}
bool mp4_writer_write_frame(MP4Writer *writer, const Frame *frame) {
    (void)writer; (void)frame;
    return false;
}
void mp4_writer_close(MP4Writer *writer) {
    (void)writer;
}

#endif

/* -------------------------------------------------------------
 * Unified Video Writer Implementation
 * ------------------------------------------------------------- */

static bool str_ends_with_ext(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    size_t len = strlen(str);
    size_t slen = strlen(suffix);
    if (len < slen) return false;
    const char *p = str + len - slen;
    while (*p && *suffix) {
        char c1 = *p;
        char c2 = *suffix;
        if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 'a' - 'A');
        if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 'a' - 'A');
        if (c1 != c2) return false;
        p++;
        suffix++;
    }
    return true;
}

VideoWriter *video_writer_open(const char *path, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den, PixelFormat format, int quality, uint32_t gop_size) {
    if (!path) return NULL;

    VideoWriter *vw = (VideoWriter *)calloc(1, sizeof(VideoWriter));
    if (!vw) return NULL;

    if (str_ends_with_ext(path, ".mp4")) {
        vw->is_mp4 = true;
        vw->mp4 = mp4_writer_open(path, width, height, fps_num, fps_den, quality);
        if (!vw->mp4) {
            free(vw);
            return NULL;
        }
        return vw;
    }

    if (str_ends_with_ext(path, ".y4m")) {
        vw->is_y4m = true;
        vw->y4m = y4m_writer_open(path, width, height, fps_num, fps_den, format);
        if (!vw->y4m) {
            free(vw);
            return NULL;
        }
        return vw;
    }

    /* Default: .vc format */
    vw->is_vc = true;
    VCHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.width = width;
    hdr.height = height;
    hdr.fps_num = fps_num;
    hdr.fps_den = fps_den;
    hdr.pixel_format = (uint8_t)format;
    hdr.quality = (uint8_t)quality;
    hdr.gop_size = (uint16_t)gop_size;

    vw->vc = vc_writer_open(path, &hdr);
    if (!vw->vc) {
        free(vw);
        return NULL;
    }
    return vw;
}

bool video_writer_write_frame(VideoWriter *writer, const Frame *frame, uint8_t frame_type, const uint8_t *compressed_data, size_t compressed_size, size_t uncompressed_size) {
    if (!writer) return false;

    if (writer->is_mp4) {
        return mp4_writer_write_frame(writer->mp4, frame);
    } else if (writer->is_y4m) {
        return y4m_writer_write_frame(writer->y4m, frame);
    } else if (writer->is_vc) {
        return vc_writer_write_frame(writer->vc, frame_type, compressed_data, compressed_size, uncompressed_size);
    }
    return false;
}

bool video_writer_close(VideoWriter *writer) {
    if (!writer) return false;
    bool ok = true;
    if (writer->is_mp4 && writer->mp4) {
        mp4_writer_close(writer->mp4);
    } else if (writer->is_y4m && writer->y4m) {
        y4m_writer_close(writer->y4m);
    } else if (writer->is_vc && writer->vc) {
        ok = vc_writer_close(writer->vc);
    }
    free(writer);
    return ok;
}
