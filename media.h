//
// Created by sam on 12/23/19.
//

#ifndef ICLOUDFRAME_MEDIA_H
#define ICLOUDFRAME_MEDIA_H

#include <SDL2/SDL_render.h>

// https://developer.apple.com/documentation/imageio/cgimagepropertyorientation
// exif image orientation
enum orientation {

    ORIENTATION_UP = 1, ORIENTATION_UP_MIRRORED, ORIENTATION_DOWN, ORIENTATION_DOWN_MIRRORED,
    ORIENTATION_LEFT_MIRRORED, ORIENTATION_RIGHT, ORIENTATION_RIGHT_MIRRORED, ORIENTATION_LEFT

};

double getAngleForOrientation(enum orientation orientation);
SDL_RendererFlip getFlipForOrientation(enum orientation orientation);

struct media {
    const char* relativePath;
    int isFavorite;
    enum orientation orientation;
    const char* createdDate;
    int hasLivePhoto;
};

extern const char* mediaDir;

int refreshMediaDb();
int shouldRefreshMediaDb();
struct media* getRandomMedia();

#endif //ICLOUDFRAME_MEDIA_H
