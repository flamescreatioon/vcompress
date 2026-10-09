#ifndef VCOMPRESS_UTILS_H
#define VCOMPRESS_UTILS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Clamping helper */
static inline int32_t utils_clamp_i32(int32_t v, int32_t low, int32_t high) {
    if (v < low) return low;
    if (v > high) return high;
    return v;
}

static inline uint8_t utils_clamp_u8(int32_t v) {
    return (uint8_t)utils_clamp_i32(v, 0, 255);
}

/* Big-endian binary reading/writing */
static inline uint16_t read_u16_be(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

static inline uint64_t read_u64_be(const uint8_t *p) {
    return ((uint64_t)read_u32_be(p) << 32) | (uint64_t)read_u32_be(p + 4);
}

static inline void write_u16_be(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)((val >> 8) & 0xFF);
    p[1] = (uint8_t)(val & 0xFF);
}

static inline void write_u32_be(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)((val >> 24) & 0xFF);
    p[1] = (uint8_t)((val >> 16) & 0xFF);
    p[2] = (uint8_t)((val >> 8) & 0xFF);
    p[3] = (uint8_t)(val & 0xFF);
}

static inline void write_u64_be(uint8_t *p, uint64_t val) {
    write_u32_be(p, (uint32_t)((val >> 32) & 0xFFFFFFFF));
    write_u32_be(p + 4, (uint32_t)(val & 0xFFFFFFFF));
}

/* High-resolution timer */
double utils_get_time_sec(void);

/* Format bytes into human readable string (e.g. "184.7 MB") */
void utils_format_bytes(uint64_t bytes, char *buf, size_t buf_size);

/* Get current process peak RSS in bytes */
size_t utils_get_peak_rss(void);

/* Safe string duplicate (allocates memory with malloc) */
char *utils_strdup(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_UTILS_H */
