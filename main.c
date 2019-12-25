#include <stdio.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <assert.h>
#include <math.h>
#include <locale.h>
#include <libheif/heif.h>
#include <time.h>
#include <errno.h>
#include "media.h"

#define TIME_BETWEEN_IMAGES 10000

static const char* SUPPORTED_EXTENSIONS[] = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tif", ".tiff", ".heic", NULL};

void toggleFullscreen(SDL_Window* window) {
    printf("toggle fullscreen\n");
    unsigned int isFullscreen = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN;
    SDL_SetWindowFullscreen(window, isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN);
}

void help() {
    fprintf(stderr, "usage: icloudframe -d <media_directory> -f <path_to_ttf> [-s <font_size=32>]\n");
}

int cmpExtension (const char* file, const char* extension) {
    const char* lastXChars = file + strlen(file) - strlen(extension);
    if (lastXChars < file) {
        return -1; //file shorter than extension
    }
    return strcasecmp(lastXChars, extension);
}

int main(int argc, const char* argv[]) {
    const char* ttfPath = NULL;
    int fontSize = 32;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            mediaDir = argv[i + 1];
        } else if (strcmp(argv[i], "-f") == 0) {
            ttfPath = argv[i + 1];
        } else if (strcmp(argv[i], "-s") == 0) {
            const char* fontSizeStr = argv[i + 1];
            if (fontSizeStr) {
                fontSize = (int) strtol(fontSizeStr, NULL, 10);
                if (errno) {
                    fprintf(stderr, "failed to interpret number %s: %s\n", fontSizeStr, strerror(errno));
                    help();
                    return 1;
                }
            }
        }
    }
    if (ttfPath == NULL || mediaDir == NULL || fontSize <= 0) {
        help();
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("icloudframe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    assert(window);

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    assert(renderer);

    if (TTF_Init() < 0) {
        fprintf(stderr, "failed to init TTF\n");
        return 1;
    }

    TTF_Font* font = TTF_OpenFont(ttfPath, fontSize);
    if (font == NULL) {
        fprintf(stderr, "error opening ttf %s: %s\n", ttfPath, TTF_GetError());
        return 1;
    }

    SDL_Color textColor;
    textColor.r = 255;
    textColor.g = 255;
    textColor.b = 255;
    textColor.a = 255;

    SDL_Color textColorBg;
    textColorBg.r = 0;
    textColorBg.b = 0;
    textColorBg.g = 0;
    textColorBg.a = 85;

    setlocale(LC_ALL, "C");

    time_t lastSwitch = -1;

    struct media *mediaInfo = NULL;
    SDL_Surface* surface = NULL;
    SDL_Texture* texture = NULL;
    SDL_Surface* textSurface = NULL;
    SDL_Texture* textTexture = NULL;
    struct heif_context* heifContext = NULL;

    while (1) {
        if (shouldRefreshMediaDb()) {
            refreshMediaDb();
        }
        time_t now = time(NULL);
        if (now - lastSwitch > TIME_BETWEEN_IMAGES) {
            lastSwitch = now;
            SDL_DestroyTexture(texture);
            SDL_FreeSurface(surface);
            SDL_DestroyTexture(textTexture);
            SDL_FreeSurface(textSurface);
            if (heifContext != NULL) {
                heif_context_free(heifContext);
            }
            while (1) {
                mediaInfo = getRandomMedia();
                for (int i = 0;; i++) {
                    const char* extension = SUPPORTED_EXTENSIONS[i];
                    if (extension == NULL) break;
                    if (cmpExtension(mediaInfo->relativePath, extension) == 0) goto foundSupportedMedia;
                }
            }
            foundSupportedMedia:;
            printf("load %s\n", mediaInfo->relativePath);
            size_t fullpathLen = strlen(mediaDir) + 1 + strlen(mediaInfo->relativePath) + 1;
            char fullpath[fullpathLen];
            int fullpathCharsWritten = snprintf(fullpath, fullpathLen, "%s/%s", mediaDir, mediaInfo->relativePath);
            assert(fullpathCharsWritten == fullpathLen - 1);
            if (cmpExtension(fullpath, ".heic") == 0) {
                heifContext = heif_context_alloc();
                heif_context_read_from_file(heifContext, fullpath, NULL);
                struct heif_image_handle* handle;
                heif_context_get_primary_image_handle(heifContext, &handle);
                struct heif_image* img;
                heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, NULL);
                assert(heif_image_get_chroma_format(img) == heif_chroma_interleaved_24bit);
                int stride;
                const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
                int width = heif_image_get_width(img, heif_channel_interleaved);
                int height = heif_image_get_height(img, heif_channel_interleaved);
                surface = SDL_CreateRGBSurfaceWithFormatFrom((void*) data, width, height, 24, stride, SDL_PIXELFORMAT_RGB24);
                if (!surface) {
                    fprintf(stderr, "failed to create surface from heic image RGB data: %s\n", SDL_GetError());
                    return 1;
                }
            } else {
                surface = IMG_Load(fullpath);
                if (!surface) {
                    fprintf(stderr, "IMG_Load error: %s\n", IMG_GetError());
                    return 1;
                }
            }
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            assert(texture);

            textSurface = TTF_RenderText_Shaded(font, mediaInfo->createdDate, textColor, textColorBg);
            assert(textSurface);
            textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            assert(textTexture);
        }

        int windowWidth, windowHeight;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);

        double idealXShrinkFactor = (double) windowWidth / (double) surface->w;
        double idealYShrinkFactor = (double) windowHeight / (double) surface->h;
        double shrinkFactor = fmin(idealXShrinkFactor, idealYShrinkFactor);
        double shrunkWidth = shrinkFactor * surface->w;
        double shrunkHeight = shrinkFactor * surface->h;
        double middleX = (double) windowWidth / 2;
        double middleY = (double) windowHeight / 2;
        SDL_Rect rect;
        rect.x = (int) (middleX - shrunkWidth / 2);
        rect.y = (int) (middleY - shrunkHeight / 2);
        rect.w = (int) shrunkWidth;
        rect.h = (int) shrunkHeight;

        SDL_Rect textRect;
        if (TTF_SizeText(font, mediaInfo->createdDate, &textRect.w, &textRect.h)) {
            fprintf(stderr, "error getting size of text %s: %s\n", mediaInfo->createdDate, TTF_GetError());
            return 1;
        }
        textRect.x = windowWidth - textRect.w;
        textRect.y = windowHeight - textRect.h;

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, &rect);
        SDL_RenderCopy(renderer, textTexture, NULL, &textRect);
        SDL_RenderPresent(renderer);

        SDL_ClearError();
        SDL_Event e;
        while (1) {
            time(&now);
            time_t maxWaitTime = TIME_BETWEEN_IMAGES + lastSwitch - now;
            if (SDL_WaitEventTimeout(&e, maxWaitTime)) {
                if (e.type == SDL_QUIT) {
                    return 0;
                } else if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.sym == SDLK_f) {
                        toggleFullscreen(window);
                    }
                } else if (e.type == SDL_WINDOWEVENT) {
                    goto reloop;
                }
            } else {
                const char* error = SDL_GetError();
                if (*error == '\0') {
                    printf("timed out waiting for event\n");
                    //no error, timed out
                    lastSwitch = -1; //force new photo (in case SDL_WaitEventTimeout ends a little too early)
                    goto reloop;
                } else {
                    fprintf(stderr, "error waiting for SDL event: %s\n", error);
                    return 1;
                }
            }
        }
        reloop:;
    }
}
