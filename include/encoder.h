#ifndef VCOMPRESS_ENCODER_H
#define VCOMPRESS_ENCODER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FRAME_TYPE_I = 0, /* Intra-coded keyframe */
    FRAME_TYPE_P = 1  /* Inter-coded predicted frame */
} FrameType;

typedef struct EncoderConfig {
    uint32_t width;
    uint32_t height;
    PixelFormat format;
    uint32_t fps_num;
    uint32_t fps_den;
    int quality;           /* 1 - 100 */
    uint32_t gop_size;     /* Keyframe interval (e.g. 15 or 30) */
    uint32_t threads;      /* Number of worker threads (default 1) */
    int inter_threshold;   /* SAD threshold per block to skip unchanged blocks */
    bool verbose;
} EncoderConfig;

typedef struct VideoEncoder VideoEncoder;

/* Create a new video encoder instance */
VideoEncoder *encoder_create(const EncoderConfig *config);

/* Destroy encoder instance and free internal reference frames */
void encoder_destroy(VideoEncoder *enc);

/* Encode a single video frame. Resulting compressed bitstream is appended to `out_packet`.
 * `is_keyframe` is populated with true if this was encoded as an I-frame. */
bool encoder_encode_frame(VideoEncoder *enc, const Frame *frame, Buffer *out_packet, bool *is_keyframe);

/* Flush any pending delayed frames (if applicable) */
bool encoder_flush(VideoEncoder *enc, Buffer *out_packet);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_ENCODER_H */
