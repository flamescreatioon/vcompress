#include "cli.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void cli_options_init(CliOptions *opts) {
    if (!opts) return;
    memset(opts, 0, sizeof(CliOptions));
    opts->quality = 60;
    opts->threads = 1;
    opts->fps = 0; /* Auto / inherit */
    opts->resize_width = 0;
    opts->resize_height = 0;
    opts->gop_size = 15;
    opts->subsample = PIX_FMT_YUV420;
    opts->info_only = false;
    opts->benchmark = false;
    opts->decompress = false;
    opts->verbose = false;
    opts->help = false;
    opts->version = false;
}

void cli_options_cleanup(CliOptions *opts) {
    if (!opts) return;
    if (opts->input_path) {
        free(opts->input_path);
        opts->input_path = NULL;
    }
    if (opts->output_path) {
        free(opts->output_path);
        opts->output_path = NULL;
    }
}

void cli_print_version(void) {
    printf("vcompress %s\n", VCOMPRESS_VERSION_STR);
}

void cli_print_help(const char *prog_name) {
    printf("vcompress %s - Lightweight C Video Compression Utility\n\n", VCOMPRESS_VERSION_STR);
    printf("Usage:\n");
    printf("  %s [options] <input> <output>    Compress or decompress video\n", prog_name);
    printf("  %s --info <input>                Inspect container and stream metadata\n", prog_name);
    printf("  %s --benchmark <input>           Run benchmark without writing output\n\n", prog_name);
    printf("Options:\n");
    printf("  -q, --quality <1-100>            Compression quality (default: 60)\n");
    printf("  -t, --threads <N>                Number of worker threads (default: 1)\n");
    printf("      --fps <N>                    Force output framerate\n");
    printf("      --resize <WIDTH>x<HEIGHT>    Resize video frames (e.g. 1280x720)\n");
    printf("      --subsample <444|422|420>    Chroma subsampling format (default: 420)\n");
    printf("      --gop <N>                    GOP / keyframe interval (default: 15)\n");
    printf("  -d, --decompress                 Decompress .vc file to .y4m\n");
    printf("      --info                       Inspect container and video metadata\n");
    printf("      --benchmark                  Run performance and throughput benchmark\n");
    printf("  -v, --verbose                    Enable verbose logging output\n");
    printf("  -h, --help                       Show this help message and exit\n");
    printf("      --version                    Show version information and exit\n");
}

int cli_parse_args(int argc, char **argv, CliOptions *opts) {
    if (!opts) return -1;
    cli_options_init(opts);

    if (argc < 2) {
        opts->help = true;
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            opts->help = true;
            return 0;
        } else if (strcmp(arg, "--version") == 0) {
            opts->version = true;
            return 0;
        } else if (strcmp(arg, "--info") == 0) {
            opts->info_only = true;
        } else if (strcmp(arg, "--benchmark") == 0) {
            opts->benchmark = true;
        } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--decompress") == 0) {
            opts->decompress = true;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opts->verbose = true;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quality") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --quality requires an argument (1-100)\n");
                return -1;
            }
            opts->quality = atoi(argv[++i]);
            if (opts->quality < 1 || opts->quality > 100) {
                fprintf(stderr, "Error: Quality must be between 1 and 100\n");
                return -1;
            }
        } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--threads") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --threads requires an integer argument\n");
                return -1;
            }
            int t = atoi(argv[++i]);
            opts->threads = (t > 0) ? (uint32_t)t : 1;
        } else if (strcmp(arg, "--fps") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --fps requires an integer argument\n");
                return -1;
            }
            opts->fps = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(arg, "--gop") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --gop requires an integer argument\n");
                return -1;
            }
            opts->gop_size = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(arg, "--subsample") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --subsample requires 444, 422, or 420\n");
                return -1;
            }
            const char *sub = argv[++i];
            if (!pixel_format_from_string(sub, &opts->subsample)) {
                fprintf(stderr, "Error: Invalid subsample mode '%s'. Use 444, 422, or 420.\n", sub);
                return -1;
            }
        } else if (strcmp(arg, "--resize") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --resize requires WIDTHxHEIGHT argument (e.g. 1280x720)\n");
                return -1;
            }
            const char *res = argv[++i];
            uint32_t w = 0, h = 0;
            if (sscanf(res, "%ux%u", &w, &h) != 2 || w == 0 || h == 0) {
                fprintf(stderr, "Error: Invalid resize format '%s'. Expected WIDTHxHEIGHT\n", res);
                return -1;
            }
            opts->resize_width = w;
            opts->resize_height = h;
        } else if (arg[0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", arg);
            return -1;
        } else {
            /* Positional argument */
            if (!opts->input_path) {
                opts->input_path = utils_strdup(arg);
            } else if (!opts->output_path) {
                opts->output_path = utils_strdup(arg);
            } else {
                fprintf(stderr, "Error: Unexpected additional argument '%s'\n", arg);
                return -1;
            }
        }
    }

    if (!opts->input_path && !opts->help && !opts->version) {
        fprintf(stderr, "Error: No input file specified.\n");
        return -1;
    }

    if (!opts->info_only && !opts->benchmark && !opts->output_path && !opts->help && !opts->version) {
        fprintf(stderr, "Error: Output file required unless --info or --benchmark is specified.\n");
        return -1;
    }

    return 0;
}
