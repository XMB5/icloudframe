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

#define SECONDS_BETWEEN_IMAGES 15

static const char* SUPPORTED_EXTENSIONS[] = {".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tif", ".tiff", ".heic", NULL};
static const SDL_Color textColor = {255, 255, 255, 255};
static const SDL_Color textColorBg = {0, 0, 0, 85};

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

// returns a random struct media* ending with an extension from SUPPORTED_EXTENSIONS
// still might not be able to load the image if it is too large (>8192 in any dimension)
struct media* getRandomSupportedMedia() {
    struct media* mediaInfo;
    while (1) {
        mediaInfo = getRandomMedia();
        for (int i = 0;; i++) {
            const char* extension = SUPPORTED_EXTENSIONS[i];
            if (extension == NULL) break;
            if (cmpExtension(mediaInfo->relativePath, extension) == 0) goto foundSupportedMedia;
        }
    }
    foundSupportedMedia:;
    return mediaInfo;
}

struct loadedMedia {
    struct media* mediaInfo;
    SDL_Surface* surface;
    SDL_Texture* texture;
    SDL_Surface* textSurface;
    SDL_Texture* textTexture;
    struct heif_context* heifContext;
};

void freeLoadedMedia(struct loadedMedia* loadedMedia) {
    if (loadedMedia->texture) SDL_DestroyTexture(loadedMedia->texture);
    if (loadedMedia->surface) SDL_FreeSurface(loadedMedia->surface);
    if (loadedMedia->textTexture) SDL_DestroyTexture(loadedMedia->textTexture);
    if (loadedMedia->textSurface) SDL_FreeSurface(loadedMedia->textSurface);
    if (loadedMedia->heifContext) heif_context_free(loadedMedia->heifContext);
    free(loadedMedia);
}


struct loadedMedia* loadRandomSupportedMedia(SDL_Renderer* renderer, TTF_Font* font) {
    struct loadedMedia* loadedMedia;
    while (1) {
        loadedMedia = calloc(1, sizeof(struct loadedMedia));
        loadedMedia->mediaInfo = getRandomSupportedMedia();
        printf("load %s\n", loadedMedia->mediaInfo->relativePath);
        size_t fullpathLen = strlen(mediaDir) + 1 + strlen(loadedMedia->mediaInfo->relativePath) + 1;
        char fullpath[fullpathLen];
        int fullpathCharsWritten = snprintf(fullpath, fullpathLen, "%s/%s", mediaDir, loadedMedia->mediaInfo->relativePath);
        assert(fullpathCharsWritten == fullpathLen - 1);
        if (cmpExtension(fullpath, ".heic") == 0) {
            loadedMedia->heifContext = heif_context_alloc();
            heif_context_read_from_file(loadedMedia->heifContext, fullpath, NULL);
            struct heif_image_handle* handle;
            heif_context_get_primary_image_handle(loadedMedia->heifContext, &handle);
            struct heif_image* img;
            heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, NULL);
            assert(heif_image_get_chroma_format(img) == heif_chroma_interleaved_24bit);
            int stride;
            const uint8_t* data = heif_image_get_plane_readonly(img, heif_channel_interleaved, &stride);
            int width = heif_image_get_width(img, heif_channel_interleaved);
            int height = heif_image_get_height(img, heif_channel_interleaved);
            loadedMedia->surface = SDL_CreateRGBSurfaceWithFormatFrom((void*) data, width, height, 24, stride, SDL_PIXELFORMAT_RGB24);
            if (!loadedMedia->surface) {
                fprintf(stderr, "failed to create surface from heic image RGB data: %s\n", SDL_GetError());
                return NULL;
            }
        } else {
            loadedMedia->surface = IMG_Load(fullpath);
            if (!loadedMedia->surface) {
                fprintf(stderr, "IMG_Load error: %s\n", IMG_GetError());
                return NULL;
            }
        }
        if (loadedMedia->surface->w <= 8192 && loadedMedia->surface->h <= 8192) {
            break;
        } else {
            printf("image too large, loading a new one\n");
            freeLoadedMedia(loadedMedia);
        }
    }

    loadedMedia->texture = SDL_CreateTextureFromSurface(renderer, loadedMedia->surface);
    if(loadedMedia->texture == NULL) {
        fprintf(stderr, "failed to create texture from surface for image %s: %s\n", loadedMedia->mediaInfo->relativePath, SDL_GetError());
        return NULL;
    }

    loadedMedia->textSurface = TTF_RenderText_Shaded(font, loadedMedia->mediaInfo->createdDate, textColor, textColorBg);
    if (!loadedMedia->textSurface) {
        fprintf(stderr, "failed to render text %s: %s\n", loadedMedia->mediaInfo->createdDate, TTF_GetError());
        return NULL;
    }
    loadedMedia->textTexture = SDL_CreateTextureFromSurface(renderer, loadedMedia->textSurface);
    if (!loadedMedia->textTexture) {
        fprintf(stderr, "failed to create texture from surface for text %s: %s\n", loadedMedia->mediaInfo->createdDate, TTF_GetError());
        return NULL;
    }

    return loadedMedia;
}

SDL_Rect getRectForMedia(struct loadedMedia* loadedMedia, SDL_Window* window) {
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    double idealXShrinkFactor = (double) windowWidth / (double) loadedMedia->surface->w;
    double idealYShrinkFactor = (double) windowHeight / (double) loadedMedia->surface->h;
    double shrinkFactor = fmin(idealXShrinkFactor, idealYShrinkFactor);
    double shrunkWidth = shrinkFactor * loadedMedia->surface->w;
    double shrunkHeight = shrinkFactor * loadedMedia->surface->h;
    double middleX = (double) windowWidth / 2;
    double middleY = (double) windowHeight / 2;
    SDL_Rect rect;
    rect.x = (int) (middleX - shrunkWidth / 2);
    rect.y = (int) (middleY - shrunkHeight / 2);
    rect.w = (int) shrunkWidth;
    rect.h = (int) shrunkHeight;
    return rect;
}

SDL_Rect getTextRectForMedia(struct loadedMedia* loadedMedia, SDL_Window* window, TTF_Font* font) {
    int windowWidth, windowHeight;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);

    SDL_Rect textRect;
    if (TTF_SizeText(font, loadedMedia->mediaInfo->createdDate, &textRect.w, &textRect.h)) {
        fprintf(stderr, "error getting size of text %s: %s\n", loadedMedia->mediaInfo->createdDate, TTF_GetError());
        textRect.w = -1;
        return textRect;
    }
    textRect.x = windowWidth - textRect.w;
    textRect.y = windowHeight - textRect.h;
    return textRect;
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

    setbuf(stdout, NULL); //buffering messes with logging when the program crashes

    SDL_Window* window = SDL_CreateWindow("icloudframe", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 0, 0, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_RESIZABLE);
    assert(window);

    SDL_ShowCursor(SDL_DISABLE);

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

    setlocale(LC_ALL, "C");

    time_t lastSwitch = -1;

    struct loadedMedia* loadedMedia = NULL;
    struct loadedMedia* nextLoadedMedia = NULL;

    while (1) {
        time_t now = time(NULL);
        if (now - lastSwitch > SECONDS_BETWEEN_IMAGES) {
            lastSwitch = now;
            if (loadedMedia) {
                freeLoadedMedia(loadedMedia);
                loadedMedia = NULL;
            }

            if (shouldRefreshMediaDb()) {
                refreshMediaDb();
                if (nextLoadedMedia) {
                    freeLoadedMedia(nextLoadedMedia);
                    nextLoadedMedia = NULL;
                }
            }

            if (nextLoadedMedia) {
                loadedMedia = nextLoadedMedia;
                nextLoadedMedia = NULL;
            } else {
                loadedMedia = loadRandomSupportedMedia(renderer, font);
                if (loadedMedia == NULL) {
                    return 1;
                }
            }
        }

        SDL_Rect rect = getRectForMedia(loadedMedia, window);
        SDL_Rect textRect = getTextRectForMedia(loadedMedia, window, font);
        if (textRect.w == -1) {
            //error
            return 1;
        }

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, loadedMedia->texture, NULL, &rect);
        SDL_RenderCopy(renderer, loadedMedia->textTexture, NULL, &textRect);
        SDL_RenderPresent(renderer);

        if (nextLoadedMedia == NULL) {
            nextLoadedMedia = loadRandomSupportedMedia(renderer, font);
            if (nextLoadedMedia == NULL) {
                return 1;
            }
        }

        SDL_ClearError();
        SDL_Event e;
        while (1) {
            time(&now);
            time_t maxWaitSeconds = SECONDS_BETWEEN_IMAGES + lastSwitch - now;
            if (SDL_WaitEventTimeout(&e, (int) maxWaitSeconds * 1000)) {
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
