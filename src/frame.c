#include "frame.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char *pixel_format_name(PixelFormat fmt) {
    switch (fmt) {
        case PIX_FMT_RGB24:  return "RGB24";
        case PIX_FMT_YUV444: return "YUV444";
        case PIX_FMT_YUV422: return "YUV422";
        case PIX_FMT_YUV420: return "YUV420";
        default:             return "UNKNOWN";
    }
}

bool pixel_format_from_string(const char *str, PixelFormat *out_fmt) {
    if (!str || !out_fmt) return false;
    if (strcmp(str, "rgb") == 0 || strcmp(str, "rgb24") == 0 || strcmp(str, "RGB24") == 0) {
        *out_fmt = PIX_FMT_RGB24;
        return true;
    }
    if (strcmp(str, "444") == 0 || strcmp(str, "yuv444") == 0 || strcmp(str, "YUV444") == 0) {
        *out_fmt = PIX_FMT_YUV444;
        return true;
    }
    if (strcmp(str, "422") == 0 || strcmp(str, "yuv422") == 0 || strcmp(str, "YUV422") == 0) {
        *out_fmt = PIX_FMT_YUV422;
        return true;
    }
    if (strcmp(str, "420") == 0 || strcmp(str, "yuv420") == 0 || strcmp(str, "YUV420") == 0) {
        *out_fmt = PIX_FMT_YUV420;
        return true;
    }
    return false;
}

static uint32_t align_up(uint32_t v, uint32_t a) {
    return (v + a - 1) & ~(a - 1);
}

Frame *frame_create(uint32_t width, uint32_t height, PixelFormat format) {
    if (width == 0 || height == 0) return NULL;
    /* Sanity limit: max 16384x16384 */
    if (width > 16384 || height > 16384) return NULL;

    Frame *frame = (Frame *)calloc(1, sizeof(Frame));
    if (!frame) return NULL;

    frame->width = width;
    frame->height = height;
    frame->format = format;
    frame->pts = 0;

    size_t total_bytes = 0;

    switch (format) {
        case PIX_FMT_RGB24: {
            uint32_t stride = align_up(width * 3, 16);
            frame->stride[0] = stride;
            frame->plane_width[0] = width;
            frame->plane_height[0] = height;
            total_bytes = (size_t)stride * height;
            break;
        }
        case PIX_FMT_YUV444: {
            uint32_t stride = align_up(width, 16);
            for (int i = 0; i < 3; i++) {
                frame->stride[i] = stride;
                frame->plane_width[i] = width;
                frame->plane_height[i] = height;
                total_bytes += (size_t)stride * height;
            }
            break;
        }
        case PIX_FMT_YUV422: {
            uint32_t y_stride = align_up(width, 16);
            uint32_t uv_w = (width + 1) / 2;
            uint32_t uv_stride = align_up(uv_w, 16);

            frame->stride[0] = y_stride;
            frame->plane_width[0] = width;
            frame->plane_height[0] = height;
            total_bytes += (size_t)y_stride * height;

            for (int i = 1; i < 3; i++) {
                frame->stride[i] = uv_stride;
                frame->plane_width[i] = uv_w;
                frame->plane_height[i] = height;
                total_bytes += (size_t)uv_stride * height;
            }
            break;
        }
        case PIX_FMT_YUV420: {
            uint32_t y_stride = align_up(width, 16);
            uint32_t uv_w = (width + 1) / 2;
            uint32_t uv_h = (height + 1) / 2;
            uint32_t uv_stride = align_up(uv_w, 16);

            frame->stride[0] = y_stride;
            frame->plane_width[0] = width;
            frame->plane_height[0] = height;
            total_bytes += (size_t)y_stride * height;

            for (int i = 1; i < 3; i++) {
                frame->stride[i] = uv_stride;
                frame->plane_width[i] = uv_w;
                frame->plane_height[i] = uv_h;
                total_bytes += (size_t)uv_stride * uv_h;
            }
            break;
        }
    }

    frame->raw_size = total_bytes;
    frame->raw_buf = (uint8_t *)calloc(1, total_bytes > 0 ? total_bytes : 1);
    if (!frame->raw_buf) {
        free(frame);
        return NULL;
    }

    /* Assign plane pointers */
    uint8_t *ptr = frame->raw_buf;
    if (format == PIX_FMT_RGB24) {
        frame->data[0] = ptr;
        frame->data[1] = NULL;
        frame->data[2] = NULL;
    } else {
        frame->data[0] = ptr;
        ptr += (size_t)frame->stride[0] * frame->plane_height[0];
        frame->data[1] = ptr;
        ptr += (size_t)frame->stride[1] * frame->plane_height[1];
        frame->data[2] = ptr;
    }

    return frame;
}

void frame_destroy(Frame *frame) {
    if (!frame) return;
    if (frame->raw_buf) {
        free(frame->raw_buf);
        frame->raw_buf = NULL;
    }
    free(frame);
}

bool frame_copy(const Frame *src, Frame **dst) {
    if (!src || !dst) return false;

    if (*dst == NULL) {
        *dst = frame_create(src->width, src->height, src->format);
        if (!*dst) return false;
    } else if ((*dst)->width != src->width || (*dst)->height != src->height || (*dst)->format != src->format) {
        frame_destroy(*dst);
        *dst = frame_create(src->width, src->height, src->format);
        if (!*dst) return false;
    }

    Frame *d = *dst;
    d->pts = src->pts;

    int planes = (src->format == PIX_FMT_RGB24) ? 1 : 3;
    for (int p = 0; p < planes; p++) {
        uint32_t line_bytes = (src->format == PIX_FMT_RGB24) ? src->plane_width[0] * 3 : src->plane_width[p];
        for (uint32_t y = 0; y < src->plane_height[p]; y++) {
            const uint8_t *s_line = src->data[p] + (size_t)y * src->stride[p];
            uint8_t *d_line = d->data[p] + (size_t)y * d->stride[p];
            memcpy(d_line, s_line, line_bytes);
        }
    }
    return true;
}

/* RGB <-> YUV Conversion Functions (Full-range BT.601) */

static void rgb_to_yuv_pixel(uint8_t r, uint8_t g, uint8_t b, uint8_t *y, uint8_t *u, uint8_t *v) {
    int32_t y_val = ( 77 * r + 150 * g +  29 * b + 128) >> 8;
    int32_t u_val = (-43 * r -  85 * g + 128 * b + 128) >> 8;
    int32_t v_val = (128 * r - 107 * g -  21 * b + 128) >> 8;

    *y = utils_clamp_u8(y_val);
    *u = utils_clamp_u8(u_val + 128);
    *v = utils_clamp_u8(v_val + 128);
}

static void yuv_to_rgb_pixel(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    int32_t c = (int32_t)y;
    int32_t d = (int32_t)u - 128;
    int32_t e = (int32_t)v - 128;

    int32_t r_val = c + ((359 * e + 128) >> 8);
    int32_t g_val = c - ((88 * d + 183 * e + 128) >> 8);
    int32_t b_val = c + ((454 * d + 128) >> 8);

    *r = utils_clamp_u8(r_val);
    *g = utils_clamp_u8(g_val);
    *b = utils_clamp_u8(b_val);
}

Frame *frame_convert(const Frame *src, PixelFormat target_fmt) {
    if (!src) return NULL;
    if (src->format == target_fmt) {
        Frame *out = NULL;
        frame_copy(src, &out);
        return out;
    }

    Frame *dst = frame_create(src->width, src->height, target_fmt);
    if (!dst) return NULL;
    dst->pts = src->pts;

    /* Handle RGB24 -> YUV420 directly (most common video path) */
    if (src->format == PIX_FMT_RGB24 && target_fmt == PIX_FMT_YUV420) {
        for (uint32_t y = 0; y < src->height; y++) {
            const uint8_t *src_row = src->data[0] + (size_t)y * src->stride[0];
            uint8_t *y_row = dst->data[0] + (size_t)y * dst->stride[0];

            for (uint32_t x = 0; x < src->width; x++) {
                uint8_t r = src_row[x * 3 + 0];
                uint8_t g = src_row[x * 3 + 1];
                uint8_t b = src_row[x * 3 + 2];
                uint8_t y_val, u_val, v_val;
                rgb_to_yuv_pixel(r, g, b, &y_val, &u_val, &v_val);
                y_row[x] = y_val;
            }
        }

        /* Subsample chroma 2x2 box average */
        uint32_t uv_w = dst->plane_width[1];
        uint32_t uv_h = dst->plane_height[1];
        for (uint32_t cy = 0; cy < uv_h; cy++) {
            uint8_t *u_row = dst->data[1] + (size_t)cy * dst->stride[1];
            uint8_t *v_row = dst->data[2] + (size_t)cy * dst->stride[2];

            uint32_t y0 = cy * 2;
            uint32_t y1 = (y0 + 1 < src->height) ? (y0 + 1) : y0;

            const uint8_t *r0 = src->data[0] + (size_t)y0 * src->stride[0];
            const uint8_t *r1 = src->data[0] + (size_t)y1 * src->stride[0];

            for (uint32_t cx = 0; cx < uv_w; cx++) {
                uint32_t x0 = cx * 2;
                uint32_t x1 = (x0 + 1 < src->width) ? (x0 + 1) : x0;

                uint8_t u00, v00, u01, v01, u10, v10, u11, v11, dummy;
                rgb_to_yuv_pixel(r0[x0*3], r0[x0*3+1], r0[x0*3+2], &dummy, &u00, &v00);
                rgb_to_yuv_pixel(r0[x1*3], r0[x1*3+1], r0[x1*3+2], &dummy, &u01, &v01);
                rgb_to_yuv_pixel(r1[x0*3], r1[x0*3+1], r1[x0*3+2], &dummy, &u10, &v10);
                rgb_to_yuv_pixel(r1[x1*3], r1[x1*3+1], r1[x1*3+2], &dummy, &u11, &v11);

                u_row[cx] = (uint8_t)(((uint32_t)u00 + u01 + u10 + u11 + 2) / 4);
                v_row[cx] = (uint8_t)(((uint32_t)v00 + v01 + v10 + v11 + 2) / 4);
            }
        }
        return dst;
    }

    /* Handle YUV420 -> RGB24 */
    if (src->format == PIX_FMT_YUV420 && target_fmt == PIX_FMT_RGB24) {
        for (uint32_t y = 0; y < src->height; y++) {
            const uint8_t *y_row = src->data[0] + (size_t)y * src->stride[0];
            const uint8_t *u_row = src->data[1] + (size_t)(y / 2) * src->stride[1];
            const uint8_t *v_row = src->data[2] + (size_t)(y / 2) * src->stride[2];
            uint8_t *dst_row = dst->data[0] + (size_t)y * dst->stride[0];

            for (uint32_t x = 0; x < src->width; x++) {
                uint8_t y_val = y_row[x];
                uint8_t u_val = u_row[x / 2];
                uint8_t v_val = v_row[x / 2];
                yuv_to_rgb_pixel(y_val, u_val, v_val, &dst_row[x * 3], &dst_row[x * 3 + 1], &dst_row[x * 3 + 2]);
            }
        }
        return dst;
    }

    /* General fallback via intermediate YUV444 */
    Frame *yuv444 = NULL;
    if (src->format == PIX_FMT_YUV444) {
        frame_copy(src, &yuv444);
    } else if (src->format == PIX_FMT_RGB24) {
        yuv444 = frame_create(src->width, src->height, PIX_FMT_YUV444);
        for (uint32_t y = 0; y < src->height; y++) {
            const uint8_t *s = src->data[0] + (size_t)y * src->stride[0];
            uint8_t *dy = yuv444->data[0] + (size_t)y * yuv444->stride[0];
            uint8_t *du = yuv444->data[1] + (size_t)y * yuv444->stride[1];
            uint8_t *dv = yuv444->data[2] + (size_t)y * yuv444->stride[2];
            for (uint32_t x = 0; x < src->width; x++) {
                rgb_to_yuv_pixel(s[x*3], s[x*3+1], s[x*3+2], &dy[x], &du[x], &dv[x]);
            }
        }
    } else {
        /* Subsampled YUV -> YUV444 upsample */
        yuv444 = frame_create(src->width, src->height, PIX_FMT_YUV444);
        /* Copy Y */
        for (uint32_t y = 0; y < src->height; y++) {
            memcpy(yuv444->data[0] + (size_t)y * yuv444->stride[0],
                   src->data[0] + (size_t)y * src->stride[0],
                   src->width);
        }
        /* Upsample U and V */
        int x_scale = (src->format == PIX_FMT_YUV422 || src->format == PIX_FMT_YUV420) ? 2 : 1;
        int y_scale = (src->format == PIX_FMT_YUV420) ? 2 : 1;

        for (int p = 1; p < 3; p++) {
            for (uint32_t y = 0; y < src->height; y++) {
                uint32_t sy = y / (uint32_t)y_scale;
                const uint8_t *s = src->data[p] + (size_t)sy * src->stride[p];
                uint8_t *d = yuv444->data[p] + (size_t)y * yuv444->stride[p];
                for (uint32_t x = 0; x < src->width; x++) {
                    d[x] = s[x / (uint32_t)x_scale];
                }
            }
        }
    }

    if (!yuv444) {
        frame_destroy(dst);
        return NULL;
    }

    /* Convert YUV444 -> destination format */
    if (target_fmt == PIX_FMT_RGB24) {
        for (uint32_t y = 0; y < dst->height; y++) {
            const uint8_t *sy = yuv444->data[0] + (size_t)y * yuv444->stride[0];
            const uint8_t *su = yuv444->data[1] + (size_t)y * yuv444->stride[1];
            const uint8_t *sv = yuv444->data[2] + (size_t)y * yuv444->stride[2];
            uint8_t *d = dst->data[0] + (size_t)y * dst->stride[0];
            for (uint32_t x = 0; x < dst->width; x++) {
                yuv_to_rgb_pixel(sy[x], su[x], sv[x], &d[x*3], &d[x*3+1], &d[x*3+2]);
            }
        }
    } else if (target_fmt == PIX_FMT_YUV444) {
        frame_copy(yuv444, &dst);
    } else if (target_fmt == PIX_FMT_YUV422) {
        for (uint32_t y = 0; y < dst->height; y++) {
            memcpy(dst->data[0] + (size_t)y * dst->stride[0],
                   yuv444->data[0] + (size_t)y * yuv444->stride[0],
                   dst->width);
            for (int p = 1; p < 3; p++) {
                const uint8_t *s = yuv444->data[p] + (size_t)y * yuv444->stride[p];
                uint8_t *d = dst->data[p] + (size_t)y * dst->stride[p];
                for (uint32_t x = 0; x < dst->plane_width[p]; x++) {
                    uint32_t s0 = s[x * 2];
                    uint32_t s1 = (x * 2 + 1 < dst->width) ? s[x * 2 + 1] : s0;
                    d[x] = (uint8_t)((s0 + s1 + 1) / 2);
                }
            }
        }
    } else if (target_fmt == PIX_FMT_YUV420) {
        for (uint32_t y = 0; y < dst->height; y++) {
            memcpy(dst->data[0] + (size_t)y * dst->stride[0],
                   yuv444->data[0] + (size_t)y * yuv444->stride[0],
                   dst->width);
        }
        for (int p = 1; p < 3; p++) {
            for (uint32_t cy = 0; cy < dst->plane_height[p]; cy++) {
                uint32_t y0 = cy * 2;
                uint32_t y1 = (y0 + 1 < dst->height) ? (y0 + 1) : y0;
                const uint8_t *r0 = yuv444->data[p] + (size_t)y0 * yuv444->stride[p];
                const uint8_t *r1 = yuv444->data[p] + (size_t)y1 * yuv444->stride[p];
                uint8_t *d = dst->data[p] + (size_t)cy * dst->stride[p];
                for (uint32_t cx = 0; cx < dst->plane_width[p]; cx++) {
                    uint32_t x0 = cx * 2;
                    uint32_t x1 = (x0 + 1 < dst->width) ? (x0 + 1) : x0;
                    d[cx] = (uint8_t)(((uint32_t)r0[x0] + r0[x1] + r1[x0] + r1[x1] + 2) / 4);
                }
            }
        }
    }

    frame_destroy(yuv444);
    return dst;
}

Frame *frame_resize(const Frame *src, uint32_t new_width, uint32_t new_height) {
    if (!src || new_width == 0 || new_height == 0) return NULL;
    if (src->width == new_width && src->height == new_height) {
        Frame *out = NULL;
        frame_copy(src, &out);
        return out;
    }

    Frame *dst = frame_create(new_width, new_height, src->format);
    if (!dst) return NULL;
    dst->pts = src->pts;

    int planes = (src->format == PIX_FMT_RGB24) ? 1 : 3;
    for (int p = 0; p < planes; p++) {
        uint32_t sw = src->plane_width[p];
        uint32_t sh = src->plane_height[p];
        uint32_t dw = dst->plane_width[p];
        uint32_t dh = dst->plane_height[p];

        if (src->format == PIX_FMT_RGB24) {
            for (uint32_t dy = 0; dy < dh; dy++) {
                float sy = ((float)dy + 0.5f) * ((float)sh / (float)dh) - 0.5f;
                int y0 = (int)floorf(sy);
                int y1 = y0 + 1;
                float fy = sy - (float)y0;
                if (y0 < 0) y0 = 0;
                if (y1 >= (int)sh) y1 = (int)sh - 1;

                const uint8_t *row0 = src->data[0] + (size_t)y0 * src->stride[0];
                const uint8_t *row1 = src->data[0] + (size_t)y1 * src->stride[0];
                uint8_t *out_row = dst->data[0] + (size_t)dy * dst->stride[0];

                for (uint32_t dx = 0; dx < dw; dx++) {
                    float sx = ((float)dx + 0.5f) * ((float)sw / (float)dw) - 0.5f;
                    int x0 = (int)floorf(sx);
                    int x1 = x0 + 1;
                    float fx = sx - (float)x0;
                    if (x0 < 0) x0 = 0;
                    if (x1 >= (int)sw) x1 = (int)sw - 1;

                    for (size_t c = 0; c < 3; c++) {
                        float p00 = (float)row0[(size_t)x0 * 3 + c];
                        float p10 = (float)row0[(size_t)x1 * 3 + c];
                        float p01 = (float)row1[(size_t)x0 * 3 + c];
                        float p11 = (float)row1[(size_t)x1 * 3 + c];

                        float val = (1.0f - fx) * (1.0f - fy) * p00 +
                                    fx * (1.0f - fy) * p10 +
                                    (1.0f - fx) * fy * p01 +
                                    fx * fy * p11;
                        out_row[(size_t)dx * 3 + c] = utils_clamp_u8((int32_t)(val + 0.5f));
                    }
                }
            }
        } else {
            /* Single channel plane */
            for (uint32_t dy = 0; dy < dh; dy++) {
                float sy = ((float)dy + 0.5f) * ((float)sh / (float)dh) - 0.5f;
                int y0 = (int)floorf(sy);
                int y1 = y0 + 1;
                float fy = sy - (float)y0;
                if (y0 < 0) y0 = 0;
                if (y1 >= (int)sh) y1 = (int)sh - 1;

                const uint8_t *row0 = src->data[p] + (size_t)y0 * src->stride[p];
                const uint8_t *row1 = src->data[p] + (size_t)y1 * src->stride[p];
                uint8_t *out_row = dst->data[p] + (size_t)dy * dst->stride[p];

                for (uint32_t dx = 0; dx < dw; dx++) {
                    float sx = ((float)dx + 0.5f) * ((float)sw / (float)dw) - 0.5f;
                    int x0 = (int)floorf(sx);
                    int x1 = x0 + 1;
                    float fx = sx - (float)x0;
                    if (x0 < 0) x0 = 0;
                    if (x1 >= (int)sw) x1 = (int)sw - 1;

                    float p00 = (float)row0[(size_t)x0];
                    float p10 = (float)row0[(size_t)x1];
                    float p01 = (float)row1[(size_t)x0];
                    float p11 = (float)row1[(size_t)x1];

                    float val = (1.0f - fx) * (1.0f - fy) * p00 +
                                fx * (1.0f - fy) * p10 +
                                (1.0f - fx) * fy * p01 +
                                fx * fy * p11;
                    out_row[dx] = utils_clamp_u8((int32_t)(val + 0.5f));
                }
            }
        }
    }

    return dst;
}

double frame_compute_psnr(const Frame *f1, const Frame *f2) {
    if (!f1 || !f2) return 0.0;
    if (f1->width != f2->width || f1->height != f2->height || f1->format != f2->format) return 0.0;

    int planes = (f1->format == PIX_FMT_RGB24) ? 1 : 3;
    uint64_t total_sq_diff = 0;
    uint64_t total_samples = 0;

    for (int p = 0; p < planes; p++) {
        uint32_t w = (f1->format == PIX_FMT_RGB24) ? f1->plane_width[0] * 3 : f1->plane_width[p];
        uint32_t h = f1->plane_height[p];
        for (uint32_t y = 0; y < h; y++) {
            const uint8_t *s1 = f1->data[p] + (size_t)y * f1->stride[p];
            const uint8_t *s2 = f2->data[p] + (size_t)y * f2->stride[p];
            for (uint32_t x = 0; x < w; x++) {
                int32_t diff = (int32_t)s1[x] - (int32_t)s2[x];
                total_sq_diff += (uint64_t)(diff * diff);
                total_samples++;
            }
        }
    }

    if (total_samples == 0) return 0.0;
    if (total_sq_diff == 0) return 99.0; /* Identical */

    double mse = (double)total_sq_diff / (double)total_samples;
    return 10.0 * log10((255.0 * 255.0) / mse);
}
