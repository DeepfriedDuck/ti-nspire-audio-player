#include "atns_format.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16_le(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *buf) {
    return (uint32_t)buf[0]
        | ((uint32_t)buf[1] << 8)
        | ((uint32_t)buf[2] << 16)
        | ((uint32_t)buf[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *buf) {
    return (uint64_t)buf[0]
        | ((uint64_t)buf[1] << 8)
        | ((uint64_t)buf[2] << 16)
        | ((uint64_t)buf[3] << 24)
        | ((uint64_t)buf[4] << 32)
        | ((uint64_t)buf[5] << 40)
        | ((uint64_t)buf[6] << 48)
        | ((uint64_t)buf[7] << 56);
}

static void copy_json_string_value(const char *json, const char *key, char *dest, size_t dest_size) {
    char pattern[64];
    const char *start;
    size_t i;

    if (dest_size == 0) {
        return;
    }

    dest[0] = '\0';

    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    start = strstr(json, pattern);
    if (!start) {
        return;
    }

    start += strlen(pattern);

    i = 0;
    while (start[i] && start[i] != '"' && i + 1 < dest_size) {
        if (start[i] == '\\' && start[i + 1] != '\0') {
            i++;
        }
        dest[i] = start[i];
        i++;
    }

    dest[i] = '\0';
}

int atns_has_magic(const char *path) {
    uint8_t magic[4];
    FILE *file = fopen(path, "rb");
    size_t bytes_read;

    if (!file) {
        return 0;
    }

    bytes_read = fread(magic, 1, sizeof(magic), file);
    fclose(file);

    if (bytes_read != sizeof(magic)) {
        return 0;
    }

    return memcmp(magic, ATNS_MAGIC, sizeof(magic)) == 0;
}

int atns_read_metadata(const char *path, atns_metadata_t *out_metadata) {
    uint8_t base_header[ATNS_BASE_SIZE];
    char *json;
    size_t json_len;
    FILE *file;

    if (!out_metadata) {
        return 0;
    }

    memset(out_metadata, 0, sizeof(*out_metadata));

    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }

    if (fread(base_header, 1, sizeof(base_header), file) != sizeof(base_header)) {
        fclose(file);
        return 0;
    }

    if (memcmp(base_header, ATNS_MAGIC, 4) != 0) {
        fclose(file);
        return 0;
    }

    out_metadata->version = read_u16_le(base_header + 4);
    out_metadata->header_len = read_u32_le(base_header + 6);
    out_metadata->cover_size = read_u64_le(base_header + 10);

    if (out_metadata->header_len < ATNS_BASE_SIZE || out_metadata->header_len > 65535) {
        fclose(file);
        return 0;
    }

    json_len = out_metadata->header_len - ATNS_BASE_SIZE;
    json = (char *)malloc(json_len + 1);
    if (!json) {
        fclose(file);
        return 0;
    }

    if (fread(json, 1, json_len, file) != json_len) {
        free(json);
        fclose(file);
        return 0;
    }

    json[json_len] = '\0';

    copy_json_string_value(json, "title", out_metadata->title, sizeof(out_metadata->title));
    copy_json_string_value(json, "artist", out_metadata->artist, sizeof(out_metadata->artist));

    free(json);
    fclose(file);
    return 1;
}

int atns_load_song(const char *path, atns_song_t *out_song) {
    FILE *file;
    long size_long;
    size_t json_len;
    char *json;

    if (!out_song) {
        return 0;
    }

    memset(out_song, 0, sizeof(*out_song));

    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    size_long = ftell(file);
    if (size_long <= 0) {
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    out_song->file_size = (size_t)size_long;
    out_song->buffer = (uint8_t *)malloc(out_song->file_size);
    if (!out_song->buffer) {
        fclose(file);
        return 0;
    }

    if (fread(out_song->buffer, 1, out_song->file_size, file) != out_song->file_size) {
        fclose(file);
        atns_free_song(out_song);
        return 0;
    }

    fclose(file);

    if (out_song->file_size < ATNS_BASE_SIZE) {
        atns_free_song(out_song);
        return 0;
    }

    if (memcmp(out_song->buffer, ATNS_MAGIC, 4) != 0) {
        atns_free_song(out_song);
        return 0;
    }

    out_song->metadata.version = read_u16_le(out_song->buffer + 4);
    out_song->metadata.header_len = read_u32_le(out_song->buffer + 6);
    out_song->metadata.cover_size = read_u64_le(out_song->buffer + 10);

    if (out_song->metadata.header_len < ATNS_BASE_SIZE) {
        atns_free_song(out_song);
        return 0;
    }

    if (out_song->metadata.header_len > out_song->file_size) {
        atns_free_song(out_song);
        return 0;
    }

    out_song->cover_size = (size_t)out_song->metadata.cover_size;
    if ((size_t)out_song->metadata.header_len + out_song->cover_size > out_song->file_size) {
        atns_free_song(out_song);
        return 0;
    }

    out_song->cover_data = out_song->buffer + out_song->metadata.header_len;
    out_song->audio_data = out_song->cover_data + out_song->cover_size;
    out_song->audio_size = out_song->file_size - (size_t)(out_song->audio_data - out_song->buffer);

    json_len = out_song->metadata.header_len - ATNS_BASE_SIZE;
    json = (char *)malloc(json_len + 1);
    if (!json) {
        atns_free_song(out_song);
        return 0;
    }

    memcpy(json, out_song->buffer + ATNS_BASE_SIZE, json_len);
    json[json_len] = '\0';

    copy_json_string_value(json, "title", out_song->metadata.title, sizeof(out_song->metadata.title));
    copy_json_string_value(json, "artist", out_song->metadata.artist, sizeof(out_song->metadata.artist));

    free(json);
    return 1;
}

void atns_free_song(atns_song_t *song) {
    if (!song) {
        return;
    }

    free(song->buffer);
    memset(song, 0, sizeof(*song));
}
