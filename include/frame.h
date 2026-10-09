#ifndef VCOMPRESS_FRAME_H
#define VCOMPRESS_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PIX_FMT_RGB24  = 0,
    PIX_FMT_YUV444 = 1,
    PIX_FMT_YUV422 = 2,
    PIX_FMT_YUV420 = 3
} PixelFormat;

const char *pixel_format_name(PixelFormat fmt);
bool pixel_format_from_string(const char *str, PixelFormat *out_fmt);

typedef struct Frame {
    uint32_t width;
    uint32_t height;
    PixelFormat format;
    uint64_t pts;

    /* Stride in bytes per plane (plane 0: Y or RGB, plane 1: U, plane 2: V) */
    uint32_t stride[3];

    /* Height per plane in lines */
    uint32_t plane_height[3];

    /* Width per plane in samples */
    uint32_t plane_width[3];

    /* Plane data pointers */
    uint8_t *data[3];

    /* Base allocated buffer and total bytes */
    uint8_t *raw_buf;
    size_t raw_size;
} Frame;

/* Allocate a new frame with proper stride alignment. Returns NULL on OOM/invalid params. */
Frame *frame_create(uint32_t width, uint32_t height, PixelFormat format);

/* Free frame and its pixel buffers */
void frame_destroy(Frame *frame);

/* Deep copy src frame into dst (creates dst if *dst == NULL, otherwise reuses dst if geometry matches) */
bool frame_copy(const Frame *src, Frame **dst);

/* Convert a frame from one pixel format to another (e.g. RGB24 -> YUV420, YUV420 -> RGB24) */
Frame *frame_convert(const Frame *src, PixelFormat target_fmt);

/* Resize a frame using bilinear interpolation. Returns new resized frame. */
Frame *frame_resize(const Frame *src, uint32_t new_width, uint32_t new_height);

/* Compute Peak Signal-to-Noise Ratio (PSNR) in dB between two frames of matching geometry */
double frame_compute_psnr(const Frame *f1, const Frame *f2);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_FRAME_H */
