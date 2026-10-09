# The `.vc` Video Container Specification

## 1. Overview
The `.vc` container format is a lightweight, low-overhead, binary format designed specifically for the `vcompress` educational video compression pipeline. It supports both intra-frame (I-frame) and inter-frame (P-frame) compressed payloads with seeking support through an indexed table.

All multi-byte numeric values are encoded in **Big-Endian (Network Byte Order)** to ensure binary cross-platform portability.

---

## 2. File Layout

A `.vc` file consists of three sequential sections:

```text
+-------------------------------------------------------------+
|  File Header (64 bytes)                                     |
+-------------------------------------------------------------+
|  Frame Chunk 0 (Header: 13 bytes + Compressed Bitstream)    |
|  Frame Chunk 1 (Header: 13 bytes + Compressed Bitstream)    |
|  ...                                                        |
|  Frame Chunk N-1                                            |
+-------------------------------------------------------------+
|  Index Table (N * 8 bytes)                                  |
+-------------------------------------------------------------+
```

---

## 3. File Header (64 Bytes)

The file begins with a fixed-size 64-byte header located at offset `0x00`:

| Offset (Bytes) | Field Name     | Type     | Description |
|----------------|----------------|----------|-------------|
| `0x00 - 0x03`  | `magic`        | `uint32` | Magic identification bytes: `0x56434D50` (`'V','C','M','P'`) |
| `0x04 - 0x05`  | `version`      | `uint16` | Container version (currently `1`) |
| `0x06 - 0x07`  | `flags`        | `uint16` | Bit flags for container options (reserved, default 0) |
| `0x08 - 0x0B`  | `width`        | `uint32` | Video frame width in pixels |
| `0x0C - 0x0F`  | `height`       | `uint32` | Video frame height in pixels |
| `0x10 - 0x13`  | `fps_num`      | `uint32` | Frame rate numerator (e.g., 30 or 30000) |
| `0x14 - 0x17`  | `fps_den`      | `uint32` | Frame rate denominator (e.g., 1 or 1001) |
| `0x18 - 0x1B`  | `frame_count`  | `uint32` | Total number of frames in the video stream |
| `0x1C`         | `pixel_format` | `uint8`  | Pixel format: `0`=RGB24, `1`=YUV444, `2`=YUV422, `3`=YUV420 |
| `0x1D`         | `quality`      | `uint8`  | Quantization quality scale factor (`1` to `100`) |
| `0x1E - 0x1F`  | `gop_size`     | `uint16` | Group-of-Pictures keyframe interval (e.g., 15) |
| `0x20 - 0x27`  | `index_offset` | `uint64` | Byte offset in file to the start of the Index Table |
| `0x28 - 0x3F`  | `reserved`     | `uint8[24]` | Reserved for future extensions (zero-padded) |

---

## 4. Frame Chunk Layout

Each frame is stored in a separate chunk containing a 13-byte header followed by the compressed payload:

| Offset (Bytes) | Field Name          | Type     | Description |
|----------------|---------------------|----------|-------------|
| `0x00 - 0x03`  | `frame_index`       | `uint32` | Zero-based index of this frame |
| `0x04`         | `frame_type`        | `uint8`  | `0` = Intra Keyframe (I-frame), `1` = Predicted (P-frame) |
| `0x05 - 0x08`  | `compressed_size`   | `uint32` | Size of the following compressed payload in bytes |
| `0x09 - 0x0C`  | `uncompressed_size` | `uint32` | Size of raw uncompressed planar frame in bytes |
| `0x0D ...`     | `payload`           | `uint8[]`| Compressed bitstream data |

---

## 5. Frame Bitstream Structure

The `payload` of each frame chunk contains:
1. `frame_type` byte (redundant verification byte).
2. Per-plane data (1 plane for RGB24, 3 planes for YUV):
   - **Huffman Table**:
     - `uint16 num_symbols`: Number of active symbols in this plane's table.
     - For each symbol:
       - `uint16 symbol`: 8-bit RLE code symbol (`(run << 4) | category`).
       - `uint8 length`: Bit length of the prefix code (1-32).
       - `uint32 code`: Variable-length prefix code.
   - **Bitstream**:
     - If P-frame: 1-bit per 8x8 block skip flag (`1` = unchanged from reference, `0` = coded residual).
     - For each coded block:
       - DC coefficient + AC coefficients encoded via Huffman prefix codes + sign/magnitude bits.
       - End Of Block (EOB) symbol `0x00` terminates all remaining zero AC coefficients.
     - Flushed to nearest byte boundary with zero-padding bits.

---

## 6. Index Table

Located at `index_offset` (typically at the end of the file):
- Array of `uint64` file offsets (one 8-byte entry per frame).
- Enables $O(1)$ constant-time seeking to any arbitrary frame without parsing intermediate bitstream data.
