#include "wav.h"

#include <stdio.h>
#include <string.h>

static unsigned le32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

/* Walk RIFF chunks with fseek. AVAudioFile (FreeFlow's writer) typically
 * inserts a ~4 KB "FLLR" pad so the data chunk sits well past the first
 * 256 bytes; never load the audio payload. */
double ffs_wav_duration(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1.0;

    unsigned char riff[12];
    if (fread(riff, 1, 12, f) != 12) {
        fclose(f);
        return -1.0;
    }
    if (memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        fclose(f);
        return -1.0;
    }

    unsigned byte_rate = 0;
    long data_size = -1;
    for (;;) {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8)
            break;
        unsigned size = le32(ch + 4);
        if (memcmp(ch, "fmt ", 4) == 0) {
            unsigned char fmt[16];
            if (size < 16 || fread(fmt, 1, 16, f) != 16) {
                fclose(f);
                return -1.0;
            }
            byte_rate = le32(fmt + 8); /* nAvgBytesPerSec */
            long leftover = (long)size - 16;
            if (leftover > 0 && fseek(f, leftover, SEEK_CUR) != 0) {
                fclose(f);
                return -1.0;
            }
        } else if (memcmp(ch, "data", 4) == 0) {
            data_size = (long)size;
            break; /* payload is not read */
        } else if (fseek(f, (long)size, SEEK_CUR) != 0) {
            break;
        }
        if (size & 1) {
            if (fseek(f, 1, SEEK_CUR) != 0)
                break;
        }
    }
    fclose(f);
    if (byte_rate == 0 || data_size < 0)
        return -1.0;
    return (double)data_size / (double)byte_rate;
}
