#include "wav.h"

#include <stdio.h>
#include <string.h>

static unsigned le32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

/* Walk RIFF chunks; only the first bytes of the file are read. */
double ffs_wav_duration(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1.0;

    unsigned char hdr[256];
    size_t n = fread(hdr, 1, sizeof(hdr), f);
    fclose(f);
    if (n < 44)
        return -1.0;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0)
        return -1.0;

    unsigned byte_rate = 0;
    long data_size = -1;
    size_t off = 12;
    while (off + 8 <= n) {
        const unsigned char *chunk = hdr + off;
        unsigned size = le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16 && off + 8 + 16 <= n)
            byte_rate = le32(chunk + 8 + 8); /* fmt.byte_rate */
        if (memcmp(chunk, "data", 4) == 0) {
            data_size = (long)size;
            break;
        }
        size_t advance = 8 + (size_t)size + (size & 1);
        if (advance == 0)
            break;
        off += advance;
    }

    if (byte_rate == 0 || data_size < 0)
        return -1.0;
    return (double)data_size / (double)byte_rate;
}
