#include "file.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define fseek_64 _fseeki64
#define ftell_64 _ftelli64
#else
#define fseek_64 fseeko
#define ftell_64 ftello
#endif

FileHandle *file_open(const char *path, const char *mode) {
    if (!path || !mode) return NULL;

    FILE *fp = fopen(path, mode);
    if (!fp) return NULL;

    FileHandle *fh = (FileHandle *)malloc(sizeof(FileHandle));
    if (!fh) {
        fclose(fp);
        return NULL;
    }

    fh->fp = fp;
    fh->path = utils_strdup(path);
    fh->is_write = (strchr(mode, 'w') != NULL || strchr(mode, '+') != NULL || strchr(mode, 'a') != NULL);
    return fh;
}

void file_close(FileHandle *fh) {
    if (!fh) return;
    if (fh->fp) {
        fclose(fh->fp);
        fh->fp = NULL;
    }
    if (fh->path) {
        free(fh->path);
        fh->path = NULL;
    }
    free(fh);
}

FileError file_read(FileHandle *fh, void *buf, size_t size, size_t *bytes_read) {
    if (!fh || !fh->fp || !buf) return FILE_ERR_PARAM;
    if (size == 0) {
        if (bytes_read) *bytes_read = 0;
        return FILE_OK;
    }

    size_t read_count = fread(buf, 1, size, fh->fp);
    if (bytes_read) *bytes_read = read_count;

    if (read_count == size) {
        return FILE_OK;
    }

    if (feof(fh->fp)) {
        return FILE_ERR_EOF;
    }

    return FILE_ERR_IO;
}

FileError file_write(FileHandle *fh, const void *buf, size_t size) {
    if (!fh || !fh->fp || !buf) return FILE_ERR_PARAM;
    if (size == 0) return FILE_OK;

    size_t written = fwrite(buf, 1, size, fh->fp);
    if (written != size) {
        return FILE_ERR_IO;
    }

    return FILE_OK;
}

FileError file_seek(FileHandle *fh, int64_t offset, int whence) {
    if (!fh || !fh->fp) return FILE_ERR_PARAM;
    if (fseek_64(fh->fp, offset, whence) != 0) {
        return FILE_ERR_IO;
    }
    return FILE_OK;
}

int64_t file_tell(FileHandle *fh) {
    if (!fh || !fh->fp) return -1;
    return (int64_t)ftell_64(fh->fp);
}

int64_t file_size(FileHandle *fh) {
    if (!fh || !fh->fp) return -1;
    int64_t cur = file_tell(fh);
    if (cur < 0) return -1;

    if (file_seek(fh, 0, SEEK_END) != FILE_OK) return -1;
    int64_t end_pos = file_tell(fh);
    file_seek(fh, cur, SEEK_SET);

    return end_pos;
}

FileError file_read_all(const char *path, uint8_t **out_data, size_t *out_size) {
    if (!path || !out_data || !out_size) return FILE_ERR_PARAM;
    *out_data = NULL;
    *out_size = 0;

    FileHandle *fh = file_open(path, "rb");
    if (!fh) return FILE_ERR_NOT_FOUND;

    int64_t sz = file_size(fh);
    if (sz < 0) {
        file_close(fh);
        return FILE_ERR_IO;
    }

    /* Guard against allocating excessive memory or negative sizes */
    if (sz > (int64_t)(1024ULL * 1024 * 1024 * 2)) { /* 2GB max single slurp */
        file_close(fh);
        return FILE_ERR_NOMEM;
    }

    size_t alloc_sz = (size_t)sz;
    uint8_t *data = (uint8_t *)malloc(alloc_sz > 0 ? alloc_sz : 1);
    if (!data) {
        file_close(fh);
        return FILE_ERR_NOMEM;
    }

    size_t bytes_read = 0;
    FileError err = file_read(fh, data, alloc_sz, &bytes_read);
    file_close(fh);

    if (err != FILE_OK && err != FILE_ERR_EOF) {
        free(data);
        return err;
    }

    *out_data = data;
    *out_size = bytes_read;
    return FILE_OK;
}
