#ifndef VCOMPRESS_FILE_H
#define VCOMPRESS_FILE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FILE_OK = 0,
    FILE_ERR_IO = -1,
    FILE_ERR_NOT_FOUND = -2,
    FILE_ERR_PARAM = -3,
    FILE_ERR_NOMEM = -4,
    FILE_ERR_EOF = -5
} FileError;

typedef struct FileHandle {
    FILE *fp;
    char *path;
    bool is_write;
} FileHandle;

/* Open a file safely. Mode can be "rb", "wb", "r+b", etc. */
FileHandle *file_open(const char *path, const char *mode);

/* Close and free file handle */
void file_close(FileHandle *fh);

/* Read exactly `size` bytes. Returns FILE_OK if all read, or FILE_ERR_* */
FileError file_read(FileHandle *fh, void *buf, size_t size, size_t *bytes_read);

/* Write exactly `size` bytes. Returns FILE_OK on success */
FileError file_write(FileHandle *fh, const void *buf, size_t size);

/* Seek to offset. Whence is SEEK_SET, SEEK_CUR, or SEEK_END */
FileError file_seek(FileHandle *fh, int64_t offset, int whence);

/* Get current position */
int64_t file_tell(FileHandle *fh);

/* Get total file size in bytes */
int64_t file_size(FileHandle *fh);

/* Slurp entire file into an allocated heap buffer. Buffer owned by caller. */
FileError file_read_all(const char *path, uint8_t **out_data, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* VCOMPRESS_FILE_H */
