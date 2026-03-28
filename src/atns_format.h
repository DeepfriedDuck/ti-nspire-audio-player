#ifndef ATNS_FORMAT_H
#define ATNS_FORMAT_H

#include <stdint.h>
#include <stddef.h>

#define ATNS_MAGIC "ATNS"
#define ATNS_BASE_SIZE 18

#define ATNS_TITLE_MAX 96
#define ATNS_ARTIST_MAX 96

typedef struct {
    uint16_t version;
    uint32_t header_len;
    uint64_t cover_size;
    char title[ATNS_TITLE_MAX];
    char artist[ATNS_ARTIST_MAX];
} atns_metadata_t;

typedef struct {
    atns_metadata_t metadata;
    uint8_t *buffer;
    size_t file_size;
    const uint8_t *cover_data;
    size_t cover_size;
    const uint8_t *audio_data;
    size_t audio_size;
} atns_song_t;

int atns_has_magic(const char *path);
int atns_read_metadata(const char *path, atns_metadata_t *out_metadata);
int atns_load_song(const char *path, atns_song_t *out_song);
void atns_free_song(atns_song_t *song);

#endif
