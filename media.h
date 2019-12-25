//
// Created by sam on 12/23/19.
//

#ifndef ICLOUDFRAME_MEDIA_H
#define ICLOUDFRAME_MEDIA_H

struct media {
    const char* relativePath;
    int isFavorite;
    const char* createdDate;
    int hasLivePhoto;
};

extern const char* mediaDir;

int refreshMediaDb();
int shouldRefreshMediaDb();
struct media* getRandomMedia();

#endif //ICLOUDFRAME_MEDIA_H
