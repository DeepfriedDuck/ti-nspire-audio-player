#include <os.h>
#include <SDL/SDL.h>
#include <stdio.h>

int main(void) {
    SDL_Surface *screen;
    SDL_Surface *image;
    SDL_bool running = SDL_TRUE;

    if (SDL_Init(SDL_INIT_VIDEO) == -1) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    screen = SDL_SetVideoMode(320, 240, has_colors ? 16 : 8, SDL_SWSURFACE);
    if (!screen) {
        printf("SDL_SetVideoMode failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    image = SDL_LoadBMP("cover.bmp");
    if (!image) {
        printf("SDL_LoadBMP failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_BlitSurface(image, NULL, screen, NULL);
    SDL_Flip(screen);

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

        SDL_Delay(16);
    }

    SDL_FreeSurface(image);
    SDL_Quit();
    return 0;
}