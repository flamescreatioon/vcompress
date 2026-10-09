#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "frame.h"
#include "container.h"

int main(int argc, char **argv) {
    const char *out_path = (argc > 1) ? argv[1] : "sample.y4m";
    uint32_t width = (argc > 2) ? (uint32_t)atoi(argv[2]) : 320;
    uint32_t height = (argc > 3) ? (uint32_t)atoi(argv[3]) : 240;
    uint32_t frame_count = (argc > 4) ? (uint32_t)atoi(argv[4]) : 60;
    uint32_t fps = (argc > 5) ? (uint32_t)atoi(argv[5]) : 30;

    printf("Generating synthetic test video:\n");
    printf("  Output:     %s\n", out_path);
    printf("  Resolution: %ux%u\n", width, height);
    printf("  Frames:     %u\n", frame_count);
    printf("  Framerate:  %u fps\n\n", fps);

    Y4MWriter *writer = y4m_writer_open(out_path, width, height, fps, 1, PIX_FMT_YUV420);
    if (!writer) {
        fprintf(stderr, "Error: Failed to open %s for writing\n", out_path);
        return 1;
    }

    Frame *frame = frame_create(width, height, PIX_FMT_YUV420);
    if (!frame) {
        fprintf(stderr, "Error: Failed to allocate frame\n");
        y4m_writer_close(writer);
        return 1;
    }

    for (uint32_t i = 0; i < frame_count; i++) {
        /* Generate animated synthetic background + bouncing ball */
        double t = (double)i / (double)fps;

        /* Fill background gradient on Y */
        for (uint32_t y = 0; y < height; y++) {
            uint8_t *y_line = frame->data[0] + (size_t)y * frame->stride[0];
            for (uint32_t x = 0; x < width; x++) {
                int32_t val = (int32_t)(64 + ((x * 128) / width) + 32 * sin(t * 3.0 + (double)y * 0.05));
                if (val < 16) val = 16;
                if (val > 235) val = 235;
                y_line[x] = (uint8_t)val;
            }
        }

        /* Fill U and V with color variation */
        uint32_t uv_w = frame->plane_width[1];
        uint32_t uv_h = frame->plane_height[1];
        for (uint32_t cy = 0; cy < uv_h; cy++) {
            uint8_t *u_line = frame->data[1] + (size_t)cy * frame->stride[1];
            uint8_t *v_line = frame->data[2] + (size_t)cy * frame->stride[2];
            for (uint32_t cx = 0; cx < uv_w; cx++) {
                u_line[cx] = (uint8_t)(128 + 60 * cos(t * 2.0 + (double)cx * 0.05));
                v_line[cx] = (uint8_t)(128 + 60 * sin(t * 2.5 + (double)cy * 0.05));
            }
        }

        /* Draw bouncing bright ball */
        double ball_x = (double)(width / 2) + (double)(width / 3) * cos(t * 4.0);
        double ball_y = (double)(height / 2) + (double)(height / 3) * sin(t * 5.0);
        double radius = 18.0;

        int32_t min_y = (int32_t)(ball_y - radius); if (min_y < 0) min_y = 0;
        int32_t max_y = (int32_t)(ball_y + radius); if (max_y >= (int32_t)height) max_y = (int32_t)height - 1;
        int32_t min_x = (int32_t)(ball_x - radius); if (min_x < 0) min_x = 0;
        int32_t max_x = (int32_t)(ball_x + radius); if (max_x >= (int32_t)width) max_x = (int32_t)width - 1;

        for (int32_t y = min_y; y <= max_y; y++) {
            uint8_t *y_line = frame->data[0] + (size_t)y * frame->stride[0];
            for (int32_t x = min_x; x <= max_x; x++) {
                double dx = (double)x - ball_x;
                double dy = (double)y - ball_y;
                if (dx * dx + dy * dy <= radius * radius) {
                    y_line[(size_t)x] = 240; /* High brightness */
                }
            }
        }

        y4m_writer_write_frame(writer, frame);
    }

    frame_destroy(frame);
    y4m_writer_close(writer);

    printf("Done! Generated %u frames in %s\n", frame_count, out_path);
    return 0;
}
