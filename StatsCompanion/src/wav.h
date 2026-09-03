#ifndef FFS_WAV_H
#define FFS_WAV_H

/* Duration in seconds parsed from the WAV header only (never loads audio
 * data). Returns < 0 when the file is missing or not a parseable WAV. */
double ffs_wav_duration(const char *path);

#endif
