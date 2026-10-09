# Architecture and Implementation Details

`vcompress` is designed as a clean, educational reference implementation of an end-to-end video codec in pure C17.

---

## 1. Pipeline Overview

```text
Input Video Frame (RGB / YUV)
           │
           ▼
[Color Space Conversion]   ──► Full-range BT.601 RGB24 ↔ YUV planar conversion
           │
           ▼
[Chroma Subsampling]       ──► 4:2:0 downsampling (2x2 pixel luma-to-chroma box filter)
           │
           ├───► Intra Frame (I-Frame): Level-shifted zero-centered spatial values (-128)
           │
           └───► Inter Frame (P-Frame): Motion SAD comparison + block residual difference
           │
           ▼
[8x8 2D Integer DCT]       ──► 13-bit fixed-point separable transform (S = 8192)
           │
           ▼
[Dead-Zone Quantization]   ──► Scaled JPEG matrices + 5/8 * q dead-zone threshold
           │
           ▼
[Zigzag Diagonal Scan]     ──► Continuous low-to-high spatial frequency reordering
           │
           ▼
[Run-Length Encoding]      ──► DC coefficient + AC (run, value) pairs + ZRL + EOB
           │
           ▼
[Canonical Huffman Coding] ──► Frequency min-heap tree builder + prefix-code bitstream
           │
           ▼
[Bitstream Serialization]  ──► 64-bit buffered BitWriter into .vc or MP4 container
```

---

## 2. Low-Level Subsystems

### 2.1 Fast Integer Fixed-Point 8x8 DCT / IDCT
Earlier floating-point transforms ($O(N^4)$ matrix multiplies with `cos()` and `floor()`) are computationally prohibitive for real-time video encoding.

`vcompress` employs a **13-bit fixed-point integer transform** ($S = 8192$):
- Precomputed fixed-point basis table:
  $$\text{table}[u][x] = \text{round}\left(C(u) \cdot \cos\left(\frac{(2x + 1)u\pi}{16}\right) \cdot 8192\right)$$
- Transform arithmetic:
  Separable 1D row transform followed by 1D column transform. Every step executes pure 32-bit integer arithmetic with rounded bitshifts:
  $$\text{out} = (\text{accum} + 4096) \gg 13$$
- Mathematical precision: Deviates by at most $\pm 1$ LSB compared to double-precision floating point, passing all numerical accuracy tests while accelerating block transforms by **5x-10x**.

---

### 2.2 Dead-Zone Frequency Quantization
Standard uniform scalar quantization produces noisy low-amplitude AC coefficients in high frequencies that disrupt long zero-runs.

`vcompress` implements dead-zone quantization:
- **DC Coefficient**: Retains standard mid-tread rounding to preserve the mean brightness across blocks.
- **AC Coefficients**: Evaluates a dead-zone threshold $\text{thresh} = \frac{5 \cdot q}{8}$.
  - If $|c| < \text{thresh}$, the coefficient is truncated to $0$.
  - If $|c| \ge \text{thresh}$, the coefficient is quantized with an offset:
    $$\text{quant}(c) = \text{sgn}(c) \cdot \left\lfloor \frac{|c| + \frac{3q}{8}}{q} \right\rfloor$$

This eliminates insignificant high-frequency visual noise, extends zero-runs, and yields a **20.4% reduction in bitstream size** without perceptible distortion.

---

### 2.3 Entropy Coding: RLE and Huffman Min-Heap
- **RLE Encoding**: Packs quantized coefficients into `(run, value)` tuples. Runs of 16 zeros are encoded as `ZRL` (`run = 15, value = 0`). Trailing zero coefficients terminate with an End-of-Block (`EOB`, `run = 0, value = 0`) symbol.
- **Huffman Table Construction**: Dynamic frequency-based Huffman coding uses a min-heap priority queue to combine least-frequent symbols, producing optimal variable-length prefix codes.
- **Bitstream I/O**: A 64-bit word accumulator buffers bit sequences to prevent byte-by-byte write overhead.

---

### 2.4 Multithreading Architecture
The encoder leverages a custom POSIX thread pool (`thread_pool.c`):
- Master thread partitions image planes into horizontal row slices.
- Worker threads pull block tasks from a synchronized work queue guarded by a mutex and condition variables (`not_empty`, `not_full`, `idle`).
- Thread-safe local block reconstruction ensures encoder and decoder reference frames stay bit-identical.
