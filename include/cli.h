#ifndef VCOMPRESS_CLI_H
#define VCOMPRESS_CLI_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VCOMPRESS_VERSION_STR "0.1.0"

typedef struct CliOptions {
    char *input_path;
    char *output_path;
    int quality;            /* 1 - 100, default 60 */
    uint32_t threads;       /* default 1 */
    uint32_t fps;           /* override fps, 0 = auto from container */
    uint32_t resize_width;  /* 0 = no resize */
    uint32_t resize_height;
    uint32_t gop_size;      /* Keyframe interval, default 15 */
    PixelFormat subsample;  /* default PIX_FMT_YUV420 */
    bool info_only;         /* --info */
    bool benchmark;         /* --benchmark */
    bool decompress;        /* -d / --decompress */
    bool verbose;           /* -v / --verbose */
    bool help;              /* -h / --help */
    bool version;           /* --version */
} CliOptions;

/* Initialize CLI options with defaults */
void cli_options_init(CliOptions *opts);

/* Clean up dynamically allocated memory in CliOptions */
void cli_options_cleanup(CliOptions *opts);

/* Parse command-line arguments. Returns 0 on success, negative on error. */
int cli_parse_args(int argc, char **argv, CliOptions *opts);

/* Print help text to stdout */
void cli_print_help(const char *prog_name);

/* Print version text to stdout */
void cli_print_version(void);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_CLI_H */
