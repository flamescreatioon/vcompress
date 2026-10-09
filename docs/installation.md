# Installation and Setup Guide

`vcompress` is built using standard C17 with minimal OS-native dependencies. It compiles cleanly on Windows (MinGW-w64), Linux (GCC / Clang), and macOS.

---

## 1. Prerequisites

### Windows
- **MinGW-w64** (GCC 10.0+ recommended) with `make` or `mingw32-make`.
  - Available via [WinLibs](https://winlibs.com/), MSYS2 (`pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make`), or Chocolatey (`choco install mingw`).
- **Windows Media Foundation** (built into Windows Vista and newer; header files and import libraries `mfplat`, `mfreadwrite`, `mfuuid`, `ole32` are provided standard with MinGW-w64).

### Linux (Ubuntu / Debian / Fedora / Arch)
- **GCC** (10+) or **Clang** (11+)
- **Make** (`build-essential`)
- **POSIX Threads** (`libpthread`, typically part of `glibc`)

### macOS
- **Xcode Command Line Tools** (`xcode-select --install`)
- **Clang** & **Make**

---

## 2. Compilation

Clone the repository and compile using `make` (or `mingw32-make` on Windows):

```bash
# Clone the repository
git clone https://github.com/flamescreatioon/vcompress.git
cd vcompress

# Build release executable (-O3 optimization, strict C17 standards)
mingw32-make        # Windows (MinGW)
# or
make                # Linux / macOS
```

The build produces the binary `vcompress.exe` (Windows) or `vcompress` (Linux/macOS).

### Compilation Flags
The build uses strict warning and optimization flags:
```text
-std=c17 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -O3
```

---

## 3. Running Test Suites

Verify build integrity and mathematical accuracy before deployment:

### Automated Unit Test Suite
Runs all 10 core subsystem unit tests (buffers, bitstream, color conversion, DCT/IDCT accuracy, quantization, RLE/Huffman, container I/O, thread pool, roundtrip encoder/decoder):

```bash
mingw32-make test   # Windows
# or
make test           # Linux / macOS
```

Expected output:
```text
==========================================
  vcompress Automated Unit Test Suite
==========================================
Running test_buffer_operations... PASS
Running test_bitstream_operations... PASS
Running test_frame_lifecycle... PASS
Running test_rgb_yuv_conversion... PASS
Running test_dct_idct_accuracy... PASS
Running test_quant_and_zigzag... PASS
Running test_rle_and_huffman... PASS
Running test_vc_container... PASS
Running test_thread_pool... PASS
Running test_encoder_decoder_roundtrip... PASS
------------------------------------------
Tests Run:    10
Tests Passed: 10
Tests Failed: 0
------------------------------------------
```

### Comprehensive Codec Diagnostic Suite
Runs 10 rigorous stress tests covering row addressing, lossless buffer copying, color conversions, high-frequency checkerboards, odd geometries (e.g., 13x11, 7x5), corner pixel stability, quality scaling, and container orientation:

```bash
mingw32-make diagnostic   # Windows
# or
make diagnostic           # Linux / macOS
```

---

## 4. Cleaning the Build

To remove object files and compiled test binaries:

```bash
mingw32-make clean   # Windows
# or
make clean           # Linux / macOS
```
