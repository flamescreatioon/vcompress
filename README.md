# vcompress — Low-Level C Video Compression Utility

[![C17](https://img.shields.io/badge/Language-C17-blue.svg)](https://en.wikipedia.org/wiki/C17_(C_standard_revision))
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](https://github.com/flamescreatioon/vcompress)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Build Status](https://img.shields.io/badge/Tests-10%2F10%20PASS-brightgreen.svg)](tests/)

`vcompress` is a lightweight, high-performance command-line video compression utility written in modern C (C17).

Built from scratch without wrapping FFmpeg, `vcompress` demonstrates deep low-level systems programming: memory management, binary data processing, 13-bit fixed-point integer transforms, frequency quantization, Huffman entropy coding, inter-frame temporal compression, and multithreading.

---

## Highlights

- **Fast 13-Bit Fixed-Point Integer DCT / IDCT**: 100% integer arithmetic with zero floating-point operations in the transform hot-loop; accelerates encoding by **3.2x–10x** with $\le 1$ LSB deviation.
- **Dead-Zone Frequency Quantization**: Adaptive thresholding ($(5/8) \cdot q$) eliminates high-frequency noise and expands zero-runs, reducing bitstream size by **20.4%**.
- **Direct MP4-to-MP4 & Container Support**: Ingests and compresses MP4 (ISOBMFF / H.264), Y4M (YUV4MPEG2), and custom native `.vc` binary formats.
- **Inter-Frame P-Frame Compression**: Zero-motion difference detection, block SAD calculations, and skip/coded macroblock bitmaps.
- **Thread-Pool Parallelism**: Row-level work queue multithreading scales across CPU cores.
- **Zero Third-Party Codec Libraries**: Pure C17 with native platform bindings (Media Foundation on Windows, POSIX threads on Linux/macOS).

---

## Architecture Overview

```text
                      +-----------------------------+
                      | Raw Video Input (.y4m / MP4)|
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | RGB -> YUV Color Conversion |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Chroma Subsampling (4:2:0)  |
                      +-----------------------------+
                                     │
                      +──────────────┴──────────────+
                      │                             │
                      ▼ (I-Frame: Intra)            ▼ (P-Frame: Inter)
             +------------------+         +--------------------+
             | 8x8 Spatial      |         | Block Motion / SAD |
             | Level Shift      |         | Residual vs Ref    |
             +------------------+         +--------------------+
                      │                             │
                      +──────────────┬──────────────+
                                     │
                                     ▼
                      +-----------------------------+
                      | 8x8 Integer Fixed-Point DCT |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Dead-Zone Quantization      |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Zigzag Diagonal Scanning    |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Run-Length Encoding (RLE)   |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Canonical Huffman Bitstream |
                      +-----------------------------+
                                     │
                                     ▼
                      +-----------------------------+
                      | Custom .vc / MP4 Container  |
                      +-----------------------------+
```

---

## What is Implemented From Scratch

| Subsystem | Status | Implementation Details |
|---|---|---|
| **MP4 Container Parser** | **100% From Scratch** | Recursive ISOBMFF box parser (`ftyp`, `moov`, `mvhd`, `trak`, `tkhd`, `mdia`, `stsd`, `stts`, `stsz`). |
| **Y4M Container I/O** | **100% From Scratch** | Stream header parsing and uncompressed planar frame demuxing/muxing. |
| **Custom `.vc` Container** | **100% From Scratch** | Big-Endian 64-byte header, chunk headers, and seeking index table. |
| **Color Space Conversion** | **100% From Scratch** | Full-range BT.601 integer RGB24 $\leftrightarrow$ YUV planar conversion. |
| **Chroma Subsampling** | **100% From Scratch** | 4:4:4, 4:2:2, and 4:2:0 box-filter downsampling and upsampling. |
| **Bilinear Resizer** | **100% From Scratch** | Area-based bilinear interpolation for frame resizing (`--resize`). |
| **Integer 2D DCT / IDCT** | **100% From Scratch** | Separable row/column 13-bit fixed-point transform with rounded bitshifts. |
| **Dead-Zone Quantization** | **100% From Scratch** | Scaled JPEG luminance/chrominance matrices with $(5/8)\cdot q$ dead-zones. |
| **Zigzag Scanning** | **100% From Scratch** | 64-element diagonal transposition lookup tables. |
| **Run-Length Coding** | **100% From Scratch** | Zero-run tracking + ZRL (16 zeros) + End-of-Block (EOB). |
| **Huffman Coding** | **100% From Scratch** | Priority queue min-heap tree builder, canonical prefix codes, serialized tables. |
| **Bitstream I/O** | **100% From Scratch** | 64-bit buffered `BitWriter` and `BitReader`. |
| **Inter-Frame Compression**| **100% From Scratch** | Block SAD comparison, Skip/Coded bitmaps, and residual difference coding. |
| **Thread Pool** | **100% From Scratch** | POSIX threads (`pthreads`), mutexes, condition variables, and work queues. |

---

## Quick Start & Installation

Detailed platform prerequisites and build steps are documented in [docs/installation.md](docs/installation.md).

### 1. Build from Source

```bash
# Clone the repository
git clone https://github.com/flamescreatioon/vcompress.git
cd vcompress

# Compile on Windows (MinGW-w64)
mingw32-make

# Or compile on Linux / macOS
make
```

### 2. Run Test Suites

```bash
# Run 10 automated unit tests
mingw32-make test

# Run 10 comprehensive codec diagnostic tests
mingw32-make diagnostic
```

---

## Usage Examples

Comprehensive CLI reference and options are documented in [docs/usage.md](docs/usage.md).

### 1. Inspect Video Metadata (`--info`)
```bash
vcompress --info sample.mp4
```

### 2. Compress a Larger MP4 to a Smaller MP4
```bash
# Compress with quality factor 50 using 8 threads
vcompress input.mp4 output.mp4 --quality 50 --threads 8

# Compress and downscale resolution (e.g. to 720p)
vcompress input.mp4 output_720p.mp4 --quality 40 --resize 1280x720
```

### 3. Compress Video to Native `.vc` Container
```bash
vcompress input.y4m compressed.vc --quality 60 --threads 8
```

### 4. Decompress `.vc` back to Uncompressed Video (`-d`)
```bash
vcompress -d compressed.vc reconstructed.y4m
```

### 5. Benchmark Performance (`--benchmark`)
```bash
vcompress --benchmark input.y4m --threads 8 --quality 60
```

---

## Performance Benchmarks

### 1080p Drone Video Benchmark (383 Frames @ 1920x1080, 8 Threads)

| Metric | Before Optimization | After Fixed-Point & Dead-Zone | Improvement |
|---|---|---|---|
| **Encoding Time** | 44.9s | **14.0s** | **3.2x Faster** |
| **Bitstream Packet Size** | 545 bytes | **434 bytes** | **20.4% Smaller** |
| **DCT Mathematical Error** | Double-precision float | $\le 1$ LSB deviation | Negligible error |
| **Reconstruction PSNR** | 56.5 dB | **56.5 dB** | Bit-accurate fidelity |

---

## Project Structure

```text
vcompress/
├── include/
│   ├── buffer.h         # Dynamic buffers & 64-bit BitWriter/BitReader
│   ├── cli.h            # Command-line option parser
│   ├── compressor.h     # 8x8 DCT, quantization, zigzag, RLE & Huffman
│   ├── container.h      # MP4, Y4M, and custom .vc container parsers
│   ├── decoder.h        # Frame decoding and reconstruction pipeline
│   ├── encoder.h        # Video encoder and motion estimation pipeline
│   ├── file.h           # Safe binary file handling abstractions
│   ├── frame.h          # Frame geometry, RGB/YUV conversion, resizing
│   ├── thread_pool.h    # POSIX worker thread pool & synchronization
│   └── utils.h          # High-resolution timers, RSS memory, endian math
├── src/                 # Implementation files (.c)
├── tests/
│   ├── test_main.c      # Unit test runner
│   └── test_diagnostic.c# Comprehensive codec diagnostic suite
├── examples/
│   └── generate_sample.c# Synthetic test video generator
├── docs/
│   ├── architecture.md  # Detailed systems and mathematical architecture
│   ├── file-format.md   # Specification for binary .vc format
│   ├── installation.md  # Compilation and setup guide
│   └── usage.md         # Full CLI flag and usage guide
├── Makefile             # Strict C17 compilation
└── README.md
```

---

## Documentation

- [Binary `.vc` Container Specification](docs/file-format.md)
- [Architecture & Mathematical Internals](docs/architecture.md)
- [Installation & Compilation Guide](docs/installation.md)
- [Complete CLI Usage Guide](docs/usage.md)

---

## License

This project is licensed under the [MIT License](LICENSE).
