#ifndef VCOMPRESS_CONTAINER_H
#define VCOMPRESS_CONTAINER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "file.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VC_MAGIC 0x56434D50 /* 'VCMP' in ASCII */
#define VC_VERSION 1

/* Header for custom .vc format */
typedef struct VCHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t frame_count;
    uint8_t  pixel_format;
    uint8_t  quality;
    uint16_t gop_size;
    uint64_t index_offset;
} VCHeader;

/* Container metadata inspection structure */
typedef struct ContainerInfo {
    char container_name[32];
    char video_codec[32];
    char audio_codec[32];
    uint32_t width;
    uint32_t height;
    double fps;
    uint32_t fps_num;
    uint32_t fps_den;
    uint32_t frame_count;
    double duration_sec;
    uint64_t bitrate_bps;
    uint64_t file_size_bytes;
    int video_stream_count;
    int audio_stream_count;
    PixelFormat pixel_format;
} ContainerInfo;

/* Inspect any supported container (MP4, Y4M, VC, Raw) and fill ContainerInfo */
bool container_inspect(const char *path, ContainerInfo *info);

/* Print inspected container properties formatted cleanly */
void container_print_info(const ContainerInfo *info, const char *path);

/* -------------------------------------------------------------
 * Custom .vc File Writer / Reader
 * ------------------------------------------------------------- */

typedef struct VCWriter {
    FileHandle *fh;
    VCHeader header;
    uint64_t *frame_offsets;
    size_t frame_count_written;
    size_t frame_capacity;
} VCWriter;

VCWriter *vc_writer_open(const char *path, const VCHeader *header);
bool vc_writer_write_frame(VCWriter *writer, uint8_t frame_type, const uint8_t *data, size_t size, size_t uncompressed_size);
bool vc_writer_close(VCWriter *writer);

typedef struct VCReader {
    FileHandle *fh;
    VCHeader header;
    uint64_t *frame_offsets;
    size_t current_frame;
} VCReader;

VCReader *vc_reader_open(const char *path);
bool vc_reader_read_frame(VCReader *reader, uint8_t *out_frame_type, Buffer *out_packet, size_t *out_uncompressed_size);
bool vc_reader_seek_frame(VCReader *reader, size_t frame_index);
void vc_reader_close(VCReader *reader);

/* -------------------------------------------------------------
 * Uncompressed Video Interop (Y4M / RAW) Reader & Writer
 * ------------------------------------------------------------- */

typedef struct Y4MReader {
    FileHandle *fh;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    PixelFormat format;
    size_t frame_size;
} Y4MReader;

Y4MReader *y4m_reader_open(const char *path);
Frame *y4m_reader_read_frame(Y4MReader *reader);
void y4m_reader_close(Y4MReader *reader);

typedef struct Y4MWriter {
    FileHandle *fh;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    PixelFormat format;
} Y4MWriter;

Y4MWriter *y4m_writer_open(const char *path, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den, PixelFormat format);
bool y4m_writer_write_frame(Y4MWriter *writer, const Frame *frame);
void y4m_writer_close(Y4MWriter *writer);

/* -------------------------------------------------------------
 * MP4 Frame Reader (Native Decoded Video Frame Extraction)
 * ------------------------------------------------------------- */

typedef struct MP4Reader MP4Reader;

MP4Reader *mp4_reader_open(const char *path, uint32_t *out_width, uint32_t *out_height, uint32_t *out_fps_num, uint32_t *out_fps_den);
Frame *mp4_reader_read_frame(MP4Reader *reader);
void mp4_reader_close(MP4Reader *reader);

/* -------------------------------------------------------------
 * Unified Video Reader (Seamlessly opens MP4, Y4M, etc.)
 * ------------------------------------------------------------- */

typedef struct VideoReader {
    bool is_mp4;
    Y4MReader *y4m;
    MP4Reader *mp4;
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    PixelFormat format;
} VideoReader;

VideoReader *video_reader_open(const char *path);
Frame *video_reader_read_frame(VideoReader *reader);
void video_reader_close(VideoReader *reader);

/* -------------------------------------------------------------
 * MP4 Video Writer (Native Encoded MP4 Output)
 * ------------------------------------------------------------- */

typedef struct MP4Writer MP4Writer;

MP4Writer *mp4_writer_open(const char *path, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den, int quality);
bool mp4_writer_write_frame(MP4Writer *writer, const Frame *frame);
void mp4_writer_close(MP4Writer *writer);

/* -------------------------------------------------------------
 * Unified Video Writer (Seamlessly writes MP4, .vc, or .y4m)
 * ------------------------------------------------------------- */

typedef struct VideoWriter {
    bool is_mp4;
    bool is_vc;
    bool is_y4m;
    MP4Writer *mp4;
    VCWriter *vc;
    Y4MWriter *y4m;
} VideoWriter;

VideoWriter *video_writer_open(const char *path, uint32_t width, uint32_t height, uint32_t fps_num, uint32_t fps_den, PixelFormat format, int quality, uint32_t gop_size);
bool video_writer_write_frame(VideoWriter *writer, const Frame *frame, uint8_t frame_type, const uint8_t *compressed_data, size_t compressed_size, size_t uncompressed_size);
bool video_writer_close(VideoWriter *writer);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_CONTAINER_H */
