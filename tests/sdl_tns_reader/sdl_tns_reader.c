#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "ATNS"
#define BASE_SIZE 18

typedef struct {
    char magic[4];
    uint16_t version;
    uint32_t header_len;
    uint64_t cover_size;
} Header;

static uint16_t read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *p) {
    return (uint64_t)p[0] |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static int load_file(const char *path, uint8_t **out_data, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("fopen");
        return 0;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }

    long size_long = ftell(f);
    if (size_long < 0) {
        fclose(f);
        return 0;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }

    size_t size = (size_t)size_long;
    uint8_t *data = (uint8_t *)malloc(size);
    if (!data) {
        fclose(f);
        return 0;
    }

    if (fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        return 0;
    }

    fclose(f);
    *out_data = data;
    *out_size = size;
    return 1;
}

static int parse_header(const uint8_t *file_data, size_t file_size, Header *out) {
    if (file_size < BASE_SIZE) {
        return 0;
    }

    memcpy(out->magic, file_data, 4);
    out->version = read_u16_le(file_data + 4);
    out->header_len = read_u32_le(file_data + 6);
    out->cover_size = read_u64_le(file_data + 10);

    if (memcmp(out->magic, MAGIC, 4) != 0) {
        return 0;
    }

    if (out->header_len < BASE_SIZE || out->header_len > file_size) {
        return 0;
    }

    if ((uint64_t)out->header_len + out->cover_size > (uint64_t)file_size) {
        return 0;
    }

    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.tns>\n", argv[0]);
        return 1;
    }

    uint8_t *file_data = NULL;
    size_t file_size = 0;
    if (!load_file(argv[1], &file_data, &file_size)) {
        fprintf(stderr, "Failed to read file: %s\n", argv[1]);
        return 1;
    }

    Header h;
    if (!parse_header(file_data, file_size, &h)) {
        fprintf(stderr, "Invalid ATNS file\n");
        free(file_data);
        return 1;
    }

    size_t json_len = h.header_len - BASE_SIZE;
    const uint8_t *json_data = file_data + BASE_SIZE;
    const uint8_t *cover_data = file_data + h.header_len;
    const uint8_t *audio_data = cover_data + h.cover_size;
    size_t audio_size = file_size - (size_t)(audio_data - file_data);

    printf("Magic: %.4s\n", h.magic);
    printf("Version: %u\n", (unsigned)h.version);
    printf("Header length: %u\n", h.header_len);
    printf("Cover size: %llu bytes\n", (unsigned long long)h.cover_size);
    printf("Audio size: %zu bytes\n", audio_size);
    printf("JSON metadata: %.*s\n", (int)json_len, (const char *)json_data);

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        free(file_data);
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow(
        "ATNS Reader",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640,
        480,
        SDL_WINDOW_SHOWN
    );

    if (!win) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        free(file_data);
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        free(file_data);
        return 1;
    }

    SDL_RWops *rw = SDL_RWFromConstMem(cover_data, (int)h.cover_size);
    SDL_Surface *cover_surface = NULL;
    if (rw) {
        cover_surface = SDL_LoadBMP_RW(rw, 1);
    }

    SDL_Texture *cover_texture = NULL;
    int cover_w = 0, cover_h = 0;
    if (cover_surface) {
        cover_texture = SDL_CreateTextureFromSurface(ren, cover_surface);
        cover_w = cover_surface->w;
        cover_h = cover_surface->h;
        SDL_FreeSurface(cover_surface);
    }

    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = 0;
            } else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) {
                running = 0;
            }
        }

        SDL_SetRenderDrawColor(ren, 16, 16, 16, 255);
        SDL_RenderClear(ren);

        if (cover_texture) {
            int ww, wh;
            SDL_GetWindowSize(win, &ww, &wh);

            float sx = (float)(ww - 40) / (float)cover_w;
            float sy = (float)(wh - 120) / (float)cover_h;
            float s = sx < sy ? sx : sy;
            if (s > 1.0f) s = 1.0f;

            int dw = (int)(cover_w * s);
            int dh = (int)(cover_h * s);

            SDL_Rect dst = {(ww - dw) / 2, 20, dw, dh};
            SDL_RenderCopy(ren, cover_texture, NULL, &dst);
        }

        int ww, wh;
        SDL_GetWindowSize(win, &ww, &wh);
        SDL_SetRenderDrawColor(ren, 64, 200, 255, 255);
        int graph_y = wh - 70;
        int graph_h = 50;
        for (int x = 0; x < ww; x++) {
            size_t idx = (size_t)((double)x / (double)ww * (double)audio_size);
            if (idx >= audio_size) idx = audio_size - 1;
            int val = audio_data[idx];
            int hpx = (val * graph_h) / 255;
            SDL_RenderDrawLine(ren, x, graph_y + graph_h, x, graph_y + graph_h - hpx);
        }

        SDL_RenderPresent(ren);
    }

    if (cover_texture) SDL_DestroyTexture(cover_texture);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(file_data);
    return 0;
}
