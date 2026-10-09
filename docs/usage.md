# Usage and CLI Guide

`vcompress` is a command-line tool designed for video inspection, compression, format transcoding, and educational codec experimentation.

---

## 1. Command Syntax

```bash
vcompress [options] [input] [output]
```

### CLI Options Reference

| Flag / Option | Description | Default |
|---|---|---|
| `-h, --help` | Display command-line usage information | - |
| `-v, --version` | Display version information (`0.1.0`) | - |
| `-i, --info <file>` | Inspect container and stream metadata without compressing | - |
| `-d, --decompress` | Decompress a `.vc` archive back to uncompressed `.y4m` video | - |
| `-b, --benchmark` | Run compression benchmark reporting FPS, time, and peak memory | - |
| `-q, --quality <1-100>` | Quantization quality factor (1 = smallest/lowest, 100 = highest) | `60` |
| `-t, --threads <N>` | Number of worker threads for block processing | Auto-detect (cores) |
| `-g, --gop <N>` | Group-of-Pictures keyframe interval (I-frame frequency) | `15` |
| `-r, --resize <WxH>` | Downscale frame dimensions before encoding (e.g. `640x360`) | Input resolution |

---

## 2. Common Workflows

### 1. Inspect Video Metadata
Inspect MP4, Y4M, or native `.vc` files to check container format, resolution, frame rate, duration, and stream counts:

```bash
vcompress --info video.mp4
```

Example Output:
```text
vcompress 0.1.0

File:          video.mp4
Container:     MP4 (ISO/IEC 14496-12)
Video Codec:   H.264 / AVC
Resolution:    1920x1080
Frame rate:    29.97 fps
Duration:      12.80s
File size:     111.4 MB
```

---

### 2. Compress MP4 to a Smaller MP4
Directly re-encode an MP4 video file to reduce file size:

```bash
# High compression (quality 40)
vcompress input.mp4 output.mp4 --quality 40

# Balanced compression with multithreading
vcompress input.mp4 output.mp4 --quality 60 --threads 8

# Downscaling resolution while compressing
vcompress input.mp4 output.mp4 --quality 50 --resize 1280x720
```

---

### 3. Compress Video into Native `.vc` Container
Encode uncompressed video (`.y4m`) or MP4 into the custom educational `.vc` bitstream container:

```bash
vcompress input.y4m output.vc --quality 50 --threads 8
```

Output:
```text
vcompress 0.1.0

Input:
  File:       input.y4m
  Size:       6.6 MB
  Resolution: 320x240
  FPS:        30

Compression:
  Quality:    50
  Threads:    8

Processing:
  [##############################] 100% (0.08s)
  Frames: 60
  Time:   0.08s

Output:
  Size:       125.9 KB
  Reduction:  98.1%
  Ratio:      53.63:1
```

---

### 4. Decompress `.vc` Files (`-d`)
Reconstruct `.vc` compressed video back into standard uncompressed Y4M format:

```bash
vcompress -d output.vc reconstructed.y4m
```

You can view `reconstructed.y4m` directly using standard media players such as VLC or mpv.

---

### 5. Benchmark Compression Speed & Throughput (`--benchmark`)
Measure compression throughput (frames per second), encoding time, compression ratio, and peak RSS memory:

```bash
vcompress --benchmark input.y4m --threads 8 --quality 60
```

Example Output:
```text
Benchmark
─────────
Frames:          383
Resolution:      1920x1080
Threads:         8
Input:           111.4 MB
Output:          9.3 MB

Compression:     91.7%
Ratio:           11.98:1

Time:            14.0 sec
Throughput:      27.4 FPS
Peak memory:     48.2 MB
```

---

## 3. Working with Drone & Camera Footage (10-bit HDR / D-Log M)

When compressing footage from professional drones (e.g. DJI D-Log M / 10-bit HEVC), hardware decoders without 10-bit support may report 0 frames or decode washed-out colors.

Convert the footage to standard 8-bit YUV 4:2:0 prior to processing:
```bash
ffmpeg -i drone_input.mp4 -pix_fmt yuv420p -c:v libx264 -crf 18 drone_8bit.mp4
vcompress drone_8bit.mp4 compressed.mp4 --quality 50 --threads 8
```
