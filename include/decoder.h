#ifndef VCOMPRESS_DECODER_H
#define VCOMPRESS_DECODER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "frame.h"
#include "encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VideoDecoder VideoDecoder;

/* Create a new video decoder instance with target geometry and format */
VideoDecoder *decoder_create(uint32_t width, uint32_t height, PixelFormat format, int quality);

/* Destroy video decoder instance */
void decoder_destroy(VideoDecoder *dec);

/* Decode a compressed packet into a reconstructed Frame.
 * Returned Frame is owned by caller (free with frame_destroy). */
Frame *decoder_decode_frame(VideoDecoder *dec, const uint8_t *packet_data, size_t packet_size);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_DECODER_H */
