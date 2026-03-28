#include <os.h>
#include <stdint.h>
#include <string.h>

// PL011 UART registers (dock connector pin 4 = Tx)
#define UART_BASE      0x90020000
#define UART_DR        0x00    // Data register
#define UART_FR        0x18    // Flag register

#define UART_FR_TXFF   (1 << 5)  // TX FIFO full
#define UART_FR_BUSY   (1 << 3)  // UART busy

#define WIDTH   320
#define HEIGHT  240
#define COLOR_GREEN    0x07E0
#define COLOR_BLACK    0x0000

static uint16_t framebuf[WIDTH * HEIGHT];

static void fill_and_blit(uint16_t color) {
    int i;
    for (i = 0; i < WIDTH * HEIGHT; i++)
        framebuf[i] = color;
    lcd_blit(framebuf, SCR_320x240_565);
}

// Embedded song.bin (linked via objcopy)
extern const uint8_t _binary_song_bin_start[];
extern const uint8_t _binary_song_bin_end[];

#define audio_data      _binary_song_bin_start
#define AUDIO_DATA_SIZE ((size_t)(_binary_song_bin_end - _binary_song_bin_start))

int main(void) {
    volatile uint32_t *uart_dr = (volatile uint32_t *)(UART_BASE + UART_DR);
    volatile uint32_t *uart_fr = (volatile uint32_t *)(UART_BASE + UART_FR);

    lcd_init(SCR_320x240_565);
    fill_and_blit(COLOR_GREEN);

    // Stream PDM data through UART at default 115200 baud
    // Don't touch UART config -- just use it as-is
    // Output is on dock connector Pin 4 (Tx)
    unsigned int s;
    for (s = 0; s < AUDIO_DATA_SIZE; s++) {
        // Wait for space in TX FIFO
        while (*uart_fr & UART_FR_TXFF);

        *uart_dr = audio_data[s];

        // Check ESC every 4096 bytes
        if ((s & 0xFFF) == 0 && isKeyPressed(KEY_NSPIRE_ESC))
            break;
    }

    // Wait for TX to finish
    while (*uart_fr & UART_FR_BUSY);

    fill_and_blit(COLOR_BLACK);

    return 0;
}
