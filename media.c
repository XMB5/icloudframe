//
// Created by sam on 12/23/19.
//

#include "media.h"
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <json-c/json.h>
#include <unistd.h>
#include <stdint.h>

// seconds between refreshing media database
#define UPDATE_INTERVAL_SECONDS 3600
#define FAVORITE_WEIGHT 10
#define DB_JSON_FILE "/db.json"

static int numFavoriteMedias;
static int numNormalMedias;
static struct media* favoriteMedias = NULL;
static struct media* normalMedias = NULL;
static json_object* dbJson = NULL;

const char* mediaDir = NULL;

void resetMedias() {
    if (favoriteMedias) {
        free(favoriteMedias);
    }
    numFavoriteMedias = 0;
    if (normalMedias) {
        free(normalMedias);
    }
    numNormalMedias = 0;
    if (dbJson) {
        json_object_put(dbJson);
    }
}

static time_t lastUpdate = -1;

int shouldRefreshMediaDb() {
    time_t now = time(NULL);
    return now - lastUpdate > UPDATE_INTERVAL_SECONDS;
}

int refreshMediaDb() {
    printf("refresh media db\n");
    lastUpdate = time(NULL);
    size_t mediaDirLen = strlen(mediaDir);
    char jsonFile[mediaDirLen + strlen(DB_JSON_FILE) + 1];
    memcpy(jsonFile, mediaDir, mediaDirLen);
    strcpy(jsonFile + mediaDirLen, DB_JSON_FILE);

    resetMedias();

    dbJson = json_object_from_file(jsonFile);
    if (dbJson == (void*) -1) {
        fprintf(stderr, "failed to read json from file %s\n", jsonFile);
        return 1;
    }
    size_t numMedias = json_object_array_length(dbJson);
    struct json_object* tmpJsonObj;
    for (size_t i = 0; i < numMedias; i++) {
        struct json_object* mediaJson = json_object_array_get_idx(dbJson, i);
        json_object_object_get_ex(mediaJson, "isFavorite", &tmpJsonObj);
        if (json_object_get_boolean(tmpJsonObj)) {
            numFavoriteMedias++;
        } else {
            numNormalMedias++;
        }
    }
    favoriteMedias = malloc(sizeof(struct media) * numFavoriteMedias);
    normalMedias = malloc(sizeof(struct media) * numNormalMedias);
    int favoriteIndex = 0;
    int normalIndex = 0;
    for (size_t i = 0; i < numMedias; i++) {
        struct json_object* mediaJson = json_object_array_get_idx(dbJson, i);
        struct media* mediaInfo;
        json_object_object_get_ex(mediaJson, "isFavorite", &tmpJsonObj);
        int isFavorite = json_object_get_boolean(tmpJsonObj);
        if (isFavorite) {
            mediaInfo = favoriteMedias + favoriteIndex;
            favoriteIndex++;
        } else {
            mediaInfo = normalMedias + normalIndex;
            normalIndex++;
        }
        mediaInfo->isFavorite = isFavorite;
        json_object_object_get_ex(mediaJson, "hasLivePhoto", &tmpJsonObj);
        mediaInfo->hasLivePhoto = json_object_get_boolean(tmpJsonObj);
        json_object_object_get_ex(mediaJson, "createdDate", &tmpJsonObj);
        mediaInfo->createdDate = json_object_get_string(tmpJsonObj);
        json_object_object_get_ex(mediaJson, "relativePath", &tmpJsonObj);
        mediaInfo->relativePath = json_object_get_string(tmpJsonObj);
    }
    return 0;
}

// 2^32
#define POW_2_32 0x100000000

struct media* getRandomMedia() {
    int uRandomFd = open("/dev/urandom", O_RDONLY);
    if (uRandomFd < 0) {
        perror("unable to open /dev/urandom");
        return NULL;
    }
    uint32_t randomNums[2];
    int bytesLeft = sizeof(randomNums);
    while (bytesLeft > 0) {
        size_t numRead = read(uRandomFd, randomNums, bytesLeft);
        if (numRead < 0) {
            perror("unable to read random number from /dev/urandom");
            return NULL;
        } else {
            bytesLeft -= numRead;
        }
    }
    close(uRandomFd);
    int numFavoriteWeighted = FAVORITE_WEIGHT * numFavoriteMedias;
    double percentFavorite = (double) numFavoriteWeighted / (numFavoriteWeighted + numNormalMedias);
    double normalizedRandNum1 = (double) randomNums[0] / POW_2_32;
    int favorite = normalizedRandNum1 <= percentFavorite;
    struct media* mediasList = favorite ? favoriteMedias : normalMedias;
    int numMediasInList = favorite ? numFavoriteMedias : numNormalMedias;
    int chosenMediaNum = (int) (numMediasInList * ((double) randomNums[1] / POW_2_32));
    return mediasList + chosenMediaNum;
}