#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

// Include the emulator's font
#include "../../var8x10font.h"

static const int FONT_WIDTH = 8;
static const int FONT_HEIGHT = 10;

void renderText(SDL_Renderer* renderer, const char* text, int x, int y, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    
    int currentX = x;
    for (size_t i = 0; i < strlen(text); i++) {
        unsigned char c = text[i];
        if (c < 32 || c > 255) c = '?';
        
        int cindex = c - 32;
        int kernStart = var8x10font_kern[cindex][0];
        int kernEnd = var8x10font_kern[cindex][1];
        int charWidth = FONT_WIDTH - kernStart - kernEnd;
        
        for (int row = 0; row < FONT_HEIGHT; row++) {
            for (int col = 0; col < FONT_WIDTH; col++) {
                // The font is stored as a character array of "#" and "."
                // Each character is 128 characters wide in the font array (16 chars * 8 width)
                // But the font array has many rows.
                
                int fontRow = row; 
                // var8x10font is an array of const char*
                // Each row of the font is a single string.
                // We need to find the correct row and then the correct offset.
                
                int charColInSheet = cindex % 16;
                int charRowInSheet = cindex / 16;
                
                int totalRow = charRowInSheet * FONT_HEIGHT + row;
                int totalCol = charColInSheet * FONT_WIDTH + col;
                
                if (var8x10font[totalRow][totalCol] == '#') {
                    SDL_RenderDrawPoint(renderer, currentX + col - kernStart, y + row);
                }
            }
        }
        currentX += charWidth;
    }
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow("Text Test", 100, 100, 640, 480, SDL_WINDOW_SHOWN);
    if (win == NULL) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (ren == NULL) {
        SDL_DestroyWindow(win);
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool quit = false;
    SDL_Event e;
    SDL_Color white = {255, 255, 255, 255};

    while (!quit) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
        }

        SDL_SetRenderDrawColor(ren, 50, 50, 50, 255);
        SDL_RenderClear(ren);

        // Draw the black transparent box like in the emulator
        SDL_Rect bg = {100, 100, 200, 50};
        SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 180);
        SDL_RenderFillRect(ren, &bg);

        // Render the text "hello word"
        renderText(ren, "hello word", 110, 120, white);

        SDL_RenderPresent(ren);
    }

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

