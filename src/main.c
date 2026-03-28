#include <os.h>
#include <SDL/SDL.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "atns_format.h"


#define MAX_PATH_LEN 512
#define LIST_VISIBLE_ROWS 10
#define LIST_TOP_Y 18
#define LIST_ROW_H 20

#define TITLE_FALLBACK "<untitled>"
#define ARTIST_FALLBACK "<unknown>"

#define UART_BASE      0x90020000
#define UART_DR        0x00
#define UART_FR        0x18
#define UART_FR_TXFF   (1 << 5)
#define UART_FR_BUSY   (1 << 3)

#define WIDTH  320
#define HEIGHT 240
#define AUDIO_BYTES_PER_SECOND 11520
#define SEEK_SECONDS 10
#define PLAYBACK_CHUNK_BYTES 1024

static uint16_t playback_framebuf[WIDTH * HEIGHT];

typedef struct {
    char *path;
    char *title;
} FileEntry;

typedef struct {
    FileEntry *items;
    size_t count;
    size_t capacity;
} FileList;

static int file_list_add(FileList *list, const char *path, const char *title) {
    if (list->count == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 16 : list->capacity * 2;
        FileEntry *new_items = (FileEntry *)realloc(list->items, new_capacity * sizeof(FileEntry));
        if (!new_items) {
            return 0;
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }

    size_t path_len = strlen(path);
    size_t title_len = strlen(title);
    char *path_copy = (char *)malloc(path_len + 1);
    char *title_copy = (char *)malloc(title_len + 1);
    if (!path_copy || !title_copy) {
        free(path_copy);
        free(title_copy);
        return 0;
    }

    memcpy(path_copy, path, path_len + 1);
    memcpy(title_copy, title, title_len + 1);

    list->items[list->count].path = path_copy;
    list->items[list->count].title = title_copy;
    list->count++;
    return 1;
}

static void list_supported_files_recursive(const char *base_path, FileList *result) {
    DIR *dir = opendir(base_path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if ((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)) {
            continue;
        }

        char full_path[MAX_PATH_LEN];
        int n = snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);
        if (n < 0 || n >= (int)sizeof(full_path)) {
            continue; // path too long
        }

        struct stat st;
        if (stat(full_path, &st) != 0) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            list_supported_files_recursive(full_path, result);
        } else if (S_ISREG(st.st_mode) && atns_has_magic(full_path)) {
            atns_metadata_t metadata;
            const char *display_title = TITLE_FALLBACK;

            if (atns_read_metadata(full_path, &metadata) && metadata.title[0] != '\0') {
                display_title = metadata.title;
            }

            file_list_add(result, full_path, display_title);
        }
    }

    closedir(dir);
}

static FileList list_supported_files(const char *base_path) {
    FileList result;
    result.items = NULL;
    result.count = 0;
    result.capacity = 0;

    list_supported_files_recursive(base_path, &result);
    return result;
}

static void free_file_list(FileList *list) {
    size_t i;
    for (i = 0; i < list->count; i++) {
        free(list->items[i].path);
        free(list->items[i].title);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

static char to_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static const uint8_t *glyph_for_char(char c) {
    static const uint8_t g_space[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t g_unknown[7] = {0x1F, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    static const uint8_t g_dot[7] = {0, 0, 0, 0, 0, 0x0C, 0x0C};
    static const uint8_t g_dash[7] = {0, 0, 0, 0x1F, 0, 0, 0};
    static const uint8_t g_underscore[7] = {0, 0, 0, 0, 0, 0, 0x1F};
    static const uint8_t g_slash[7] = {0x01, 0x02, 0x04, 0x08, 0x10, 0, 0};
    static const uint8_t g_lparen[7] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
    static const uint8_t g_rparen[7] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};

    static const uint8_t g_0[7] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    static const uint8_t g_1[7] = {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t g_2[7] = {0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F};
    static const uint8_t g_3[7] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t g_4[7] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    static const uint8_t g_5[7] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    static const uint8_t g_6[7] = {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    static const uint8_t g_7[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    static const uint8_t g_8[7] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    static const uint8_t g_9[7] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};

    static const uint8_t g_A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t g_B[7] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    static const uint8_t g_C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    static const uint8_t g_D[7] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    static const uint8_t g_E[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    static const uint8_t g_F[7] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t g_G[7] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
    static const uint8_t g_H[7] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    static const uint8_t g_I[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    static const uint8_t g_J[7] = {0x1F, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    static const uint8_t g_K[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    static const uint8_t g_L[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    static const uint8_t g_M[7] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    static const uint8_t g_N[7] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    static const uint8_t g_O[7] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t g_P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    static const uint8_t g_Q[7] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    static const uint8_t g_R[7] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    static const uint8_t g_S[7] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    static const uint8_t g_T[7] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    static const uint8_t g_U[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    static const uint8_t g_V[7] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    static const uint8_t g_W[7] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    static const uint8_t g_X[7] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    static const uint8_t g_Y[7] = {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04};
    static const uint8_t g_Z[7] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};

    c = to_upper_ascii(c);
    switch (c) {
        case ' ': return g_space;
        case '.': return g_dot;
        case '-': return g_dash;
        case '_': return g_underscore;
        case '/': return g_slash;
        case '(': return g_lparen;
        case ')': return g_rparen;
        case '0': return g_0;
        case '1': return g_1;
        case '2': return g_2;
        case '3': return g_3;
        case '4': return g_4;
        case '5': return g_5;
        case '6': return g_6;
        case '7': return g_7;
        case '8': return g_8;
        case '9': return g_9;
        case 'A': return g_A;
        case 'B': return g_B;
        case 'C': return g_C;
        case 'D': return g_D;
        case 'E': return g_E;
        case 'F': return g_F;
        case 'G': return g_G;
        case 'H': return g_H;
        case 'I': return g_I;
        case 'J': return g_J;
        case 'K': return g_K;
        case 'L': return g_L;
        case 'M': return g_M;
        case 'N': return g_N;
        case 'O': return g_O;
        case 'P': return g_P;
        case 'Q': return g_Q;
        case 'R': return g_R;
        case 'S': return g_S;
        case 'T': return g_T;
        case 'U': return g_U;
        case 'V': return g_V;
        case 'W': return g_W;
        case 'X': return g_X;
        case 'Y': return g_Y;
        case 'Z': return g_Z;
        default: return g_unknown;
    }
}

static void draw_char_5x7(SDL_Surface *screen, int x, int y, char c, uint32_t color) {
    int row;
    const uint8_t *glyph = glyph_for_char(c);
    for (row = 0; row < 7; row++) {
        int col;
        for (col = 0; col < 5; col++) {
            if (glyph[row] & (1 << (4 - col))) {
                SDL_Rect px = {x + col, y + row, 1, 1};
                SDL_FillRect(screen, &px, color);
            }
        }
    }
}

static void draw_text_5x7(SDL_Surface *screen, int x, int y, const char *text, int max_chars, uint32_t color) {
    int i;
    for (i = 0; text[i] && i < max_chars; i++) {
        draw_char_5x7(screen, x + i * 6, y, text[i], color);
    }
}

static void draw_text_5x7_centered(SDL_Surface *screen, int y, const char *text, int max_chars, uint32_t color) {
    int len = 0;
    int x;
    while (text[len] && len < max_chars) {
        len++;
    }
    x = (WIDTH - len * 6) / 2;
    if (x < 0) {
        x = 0;
    }
    draw_text_5x7(screen, x, y, text, max_chars, color);
}

static void draw_text_5x7_in_rect(SDL_Surface *screen, SDL_Rect rect, const char *text, int max_chars, uint32_t color) {
    int len = 0;
    int x;
    while (text[len] && len < max_chars) {
        len++;
    }
    x = rect.x + (rect.w - len * 6) / 2;
    if (x < rect.x + 2) {
        x = rect.x + 2;
    }
    draw_text_5x7(screen, x, rect.y + (rect.h - 7) / 2, text, max_chars, color);
}

static uint16_t read_u16_le_local(const uint8_t *buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_u32_le_local(const uint8_t *buf) {
    return (uint32_t)buf[0]
        | ((uint32_t)buf[1] << 8)
        | ((uint32_t)buf[2] << 16)
        | ((uint32_t)buf[3] << 24);
}

static uint16_t rgb888_to_565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void fb_fill(uint16_t color) {
    size_t i;
    for (i = 0; i < (size_t)(WIDTH * HEIGHT); i++) {
        playback_framebuf[i] = color;
    }
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t color) {
    int yy;
    int xx;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;

    if (x1 > WIDTH) x1 = WIDTH;
    if (y1 > HEIGHT) y1 = HEIGHT;

    for (yy = y0; yy < y1; yy++) {
        for (xx = x0; xx < x1; xx++) {
            playback_framebuf[yy * WIDTH + xx] = color;
        }
    }
}

static void fb_put_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
        return;
    }
    playback_framebuf[y * WIDTH + x] = color;
}

static void draw_char_5x7_fb(int x, int y, char c, uint16_t color) {
    int row;
    const uint8_t *glyph = glyph_for_char(c);
    for (row = 0; row < 7; row++) {
        int col;
        for (col = 0; col < 5; col++) {
            if (glyph[row] & (1 << (4 - col))) {
                fb_put_pixel(x + col, y + row, color);
            }
        }
    }
}

static void draw_text_5x7_fb(int x, int y, const char *text, int max_chars, uint16_t color) {
    int i;
    for (i = 0; text[i] && i < max_chars; i++) {
        draw_char_5x7_fb(x + i * 6, y, text[i], color);
    }
}

static void draw_text_5x7_fb_centered(int y, const char *text, int max_chars, uint16_t color) {
    int len = 0;
    int x;
    while (text[len] && len < max_chars) {
        len++;
    }
    x = (WIDTH - len * 6) / 2;
    if (x < 0) {
        x = 0;
    }
    draw_text_5x7_fb(x, y, text, max_chars, color);
}

static int draw_bmp_centered_to_fb(const uint8_t *bmp, size_t bmp_size, int top_margin, int *out_bottom_y) {
    uint32_t pixel_offset;
    uint32_t dib_size;
    int32_t bmp_w;
    int32_t bmp_h_raw;
    int bmp_h;
    uint16_t bpp;
    uint32_t compression;
    size_t row_stride;
    int dst_x;
    int dst_y;
    int y;

    if (!bmp || bmp_size < 54) {
        return 0;
    }
    if (bmp[0] != 'B' || bmp[1] != 'M') {
        return 0;
    }

    pixel_offset = read_u32_le_local(bmp + 10);
    dib_size = read_u32_le_local(bmp + 14);
    if (dib_size < 40) {
        return 0;
    }

    bmp_w = (int32_t)read_u32_le_local(bmp + 18);
    bmp_h_raw = (int32_t)read_u32_le_local(bmp + 22);
    bpp = read_u16_le_local(bmp + 28);
    compression = read_u32_le_local(bmp + 30);

    if (bmp_w <= 0 || bmp_h_raw == 0) {
        return 0;
    }
    if (bpp != 24 && bpp != 32) {
        return 0;
    }
    if (compression != 0) {
        return 0;
    }

    bmp_h = bmp_h_raw < 0 ? -bmp_h_raw : bmp_h_raw;
    row_stride = (size_t)((((int)bmp_w * (int)bpp) + 31) / 32) * 4;

    if (pixel_offset >= bmp_size) {
        return 0;
    }

    dst_x = (WIDTH - bmp_w) / 2;
    dst_y = top_margin;

    for (y = 0; y < bmp_h; y++) {
        int src_y = bmp_h_raw < 0 ? y : (bmp_h - 1 - y);
        const uint8_t *row;
        int x;

        if ((size_t)pixel_offset + (size_t)src_y * row_stride >= bmp_size) {
            return 0;
        }

        row = bmp + pixel_offset + (size_t)src_y * row_stride;

        for (x = 0; x < bmp_w; x++) {
            size_t px_off = (size_t)x * (size_t)(bpp / 8);
            uint8_t b;
            uint8_t g;
            uint8_t r;

            if ((size_t)pixel_offset + (size_t)src_y * row_stride + px_off + (size_t)(bpp / 8) > bmp_size) {
                return 0;
            }

            b = row[px_off + 0];
            g = row[px_off + 1];
            r = row[px_off + 2];

            fb_put_pixel(dst_x + x, dst_y + y, rgb888_to_565(r, g, b));
        }
    }

    if (out_bottom_y) {
        *out_bottom_y = dst_y + bmp_h;
    }

    return 1;
}

static void render_now_playing_frame(const atns_song_t *song) {
    int text_y;
    const char *title;
    const char *artist;

    fb_fill(0x0000);

    text_y = 112;
    if (draw_bmp_centered_to_fb(song->cover_data, song->cover_size, 8, &text_y)) {
        text_y += 10;
    }

    title = song->metadata.title[0] ? song->metadata.title : TITLE_FALLBACK;
    artist = song->metadata.artist[0] ? song->metadata.artist : ARTIST_FALLBACK;

    draw_text_5x7_fb_centered(text_y, title, 48, 0xFFFF);
    draw_text_5x7_fb_centered(text_y + 14, artist, 48, 0xBDF7);
}

static void draw_playback_progress(size_t position, size_t total, int paused) {
    int bar_x = 10;
    int bar_y = HEIGHT - 10;
    int bar_w = WIDTH - 20;
    int bar_h = 6;
    int fill_w = 0;

    if (total > 0) {
        fill_w = (int)((position * (size_t)bar_w) / total);
        if (fill_w > bar_w) {
            fill_w = bar_w;
        }
    }

    fb_fill_rect(bar_x, bar_y, bar_w, bar_h, 0x3186);
    fb_fill_rect(bar_x, bar_y, fill_w, bar_h, 0x07E0);

    fb_fill_rect(190, HEIGHT - 24, 120, 8, 0x0000);
    if (paused) {
        draw_text_5x7_fb(190, HEIGHT - 24, "PAUSED", 20, 0xF800);
    } else {
        draw_text_5x7_fb(190, HEIGHT - 24, "PLAY", 20, 0x07E0);
    }

    lcd_blit(playback_framebuf, SCR_320x240_565);
}

static void play_song(const atns_song_t *song) {
    volatile uint32_t *uart_dr = (volatile uint32_t *)(UART_BASE + UART_DR);
    volatile uint32_t *uart_fr = (volatile uint32_t *)(UART_BASE + UART_FR);
    size_t position = 0;
    int paused = 0;
    int running = 1;
    size_t seek_bytes = (size_t)AUDIO_BYTES_PER_SECOND * (size_t)SEEK_SECONDS;
    int ui_dirty = 1;
    SDL_bool prev_esc = SDL_FALSE;
    SDL_bool prev_enter = SDL_FALSE;
    SDL_bool prev_left = SDL_FALSE;
    SDL_bool prev_right = SDL_FALSE;

    lcd_init(SCR_320x240_565);
    render_now_playing_frame(song);
    draw_playback_progress(0, song->audio_size, 0);

    while (running && position < song->audio_size) {
        SDL_bool esc = isKeyPressed(KEY_NSPIRE_ESC) ? SDL_TRUE : SDL_FALSE;
        SDL_bool enter = (isKeyPressed(KEY_NSPIRE_ENTER) || isKeyPressed(KEY_NSPIRE_RET)) ? SDL_TRUE : SDL_FALSE;
        SDL_bool left = isKeyPressed(KEY_NSPIRE_LEFT) ? SDL_TRUE : SDL_FALSE;
        SDL_bool right = isKeyPressed(KEY_NSPIRE_RIGHT) ? SDL_TRUE : SDL_FALSE;

        if (esc && !prev_esc) {
            running = 0;
        }

        if (enter && !prev_enter) {
            paused = !paused;
            ui_dirty = 1;
        }

        if (left && !prev_left) {
            if (position > seek_bytes) {
                position -= seek_bytes;
            } else {
                position = 0;
            }
            ui_dirty = 1;
        }

        if (right && !prev_right) {
            position += seek_bytes;
            if (position > song->audio_size) {
                position = song->audio_size;
            }
            ui_dirty = 1;
        }

        prev_esc = esc;
        prev_enter = enter;
        prev_left = left;
        prev_right = right;

        if (ui_dirty) {
            draw_playback_progress(position, song->audio_size, paused);
            ui_dirty = 0;
        }

        if (paused) {
            volatile int delay;
            for (delay = 0; delay < 50000; delay++) {
            }
            continue;
        }

        {
            size_t chunk_end = position + PLAYBACK_CHUNK_BYTES;
            if (chunk_end > song->audio_size) {
                chunk_end = song->audio_size;
            }

            while (position < chunk_end) {
                while (*uart_fr & UART_FR_TXFF) {
                }
                *uart_dr = song->audio_data[position];
                position++;
            }
        }
    }

    while (*uart_fr & UART_FR_BUSY) {
    }
}

static SDL_Surface *init_menu_screen(void) {
    SDL_Surface *screen;

    if (SDL_Init(SDL_INIT_VIDEO) == -1) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return NULL;
    }

    SDL_ShowCursor(SDL_DISABLE);
    screen = SDL_SetVideoMode(320, 240, has_colors ? 16 : 8, SDL_SWSURFACE);
    if (!screen) {
        printf("SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return NULL;
    }

    return screen;
}

static void draw_file_list(SDL_Surface *screen, const FileList *files, size_t selected, size_t scroll) {
    size_t row;
    uint32_t bg_color = SDL_MapRGB(screen->format, 18, 18, 24);
    uint32_t row_color = SDL_MapRGB(screen->format, 36, 42, 58);
    uint32_t selected_color = SDL_MapRGB(screen->format, 70, 130, 210);
    uint32_t border_color = SDL_MapRGB(screen->format, 220, 220, 220);

    SDL_FillRect(screen, NULL, bg_color);

    for (row = 0; row < LIST_VISIBLE_ROWS; row++) {
        size_t index = scroll + row;
        if (index >= files->count) {
            break;
        }

        SDL_Rect row_rect = {8, LIST_TOP_Y + (int)row * LIST_ROW_H, 304, LIST_ROW_H - 2};
        SDL_FillRect(screen, &row_rect, index == selected ? selected_color : row_color);

        SDL_Rect border = {row_rect.x, row_rect.y + row_rect.h - 1, row_rect.w, 1};
        SDL_FillRect(screen, &border, border_color);

        draw_text_5x7_in_rect(
            screen,
            row_rect,
            files->items[index].title,
            48,
            SDL_MapRGB(screen->format, 255, 255, 255)
        );
    }

    if (files->count == 0) {
        draw_text_5x7_centered(
            screen,
            24,
            "NO ATNS FILES FOUND",
            49,
            SDL_MapRGB(screen->format, 255, 220, 120)
        );
    }

    draw_text_5x7_centered(
        screen,
        HEIGHT - 24,
        "UP/DOWN SELECT  ENTER PLAY  ESC QUIT",
        49,
        SDL_MapRGB(screen->format, 180, 220, 255)
    );

    draw_text_5x7_centered(
        screen,
        HEIGHT - 14,
        "IN PLAYER: ENTER PAUSE  LEFT/RIGHT SEEK",
        49,
        SDL_MapRGB(screen->format, 130, 180, 230)
    );
}

int main(void) {
    SDL_Surface *screen;
    SDL_bool running = SDL_TRUE;
    FileList files;
    size_t selected = 0;
    size_t scroll = 0;
    SDL_bool prev_up = SDL_FALSE;
    SDL_bool prev_down = SDL_FALSE;
    SDL_bool prev_enter = SDL_FALSE;

    files = list_supported_files("/");
    printf("Found %u ATNS files\n", (unsigned)files.count);
    {
        size_t i;
        for (i = 0; i < files.count; i++) {
            printf("[%u] %s | %s\n", (unsigned)i, files.items[i].title, files.items[i].path);
        }
    }

    screen = init_menu_screen();
    if (!screen) {
        free_file_list(&files);
        return 1;
    }

    while (running) {
        SDL_Event event;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = SDL_FALSE;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = SDL_FALSE;
            }
        }

        if (isKeyPressed(KEY_NSPIRE_ESC)) {
            running = SDL_FALSE;
        }

        if (files.count > 0) {
            SDL_bool up = isKeyPressed(KEY_NSPIRE_UP) ? SDL_TRUE : SDL_FALSE;
            SDL_bool down = isKeyPressed(KEY_NSPIRE_DOWN) ? SDL_TRUE : SDL_FALSE;
            SDL_bool enter = isKeyPressed(KEY_NSPIRE_ENTER) ? SDL_TRUE : SDL_FALSE;

            if (up && !prev_up) {
                if (selected == 0) {
                    selected = files.count - 1;
                } else {
                    selected--;
                }
                printf("Selected: %s | %s\n", files.items[selected].title, files.items[selected].path);
            }

            if (down && !prev_down) {
                selected++;
                if (selected >= files.count) {
                    selected = 0;
                }
                printf("Selected: %s | %s\n", files.items[selected].title, files.items[selected].path);
            }

            if (selected < scroll) {
                scroll = selected;
            }
            if (selected >= scroll + LIST_VISIBLE_ROWS) {
                scroll = selected - (LIST_VISIBLE_ROWS - 1);
            }

            if (enter && !prev_enter) {
                atns_song_t song;
                printf("Confirmed: %s | %s\n", files.items[selected].title, files.items[selected].path);

                if (atns_load_song(files.items[selected].path, &song)) {
                    SDL_Quit();
                    play_song(&song);
                    atns_free_song(&song);

                    screen = init_menu_screen();
                    if (!screen) {
                        free_file_list(&files);
                        return 1;
                    }

                    prev_up = SDL_FALSE;
                    prev_down = SDL_FALSE;
                    prev_enter = SDL_FALSE;
                }

                printf("Failed to parse selected ATNS file\n");
            }

            prev_up = up;
            prev_down = down;
            prev_enter = enter;
        }

        draw_file_list(screen, &files, selected, scroll);
        SDL_Flip(screen);

        SDL_Delay(16);
    }

    free_file_list(&files);
    SDL_Quit();
    return 0;
}