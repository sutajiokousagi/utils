#ifndef __POSTPROCESSOR_H__
#define __POSTPROCESSOR_H__

#include "post.h"
#include "image.h"

struct PostProcessor {
    int fd;
};

struct PostProcessor *postprocessor_alloc();
struct PostProcessor *postprocessor_init(struct PostProcessor *pp);
void postprocessor_free(struct PostProcessor *pp);

int postprocessor_scale(struct PostProcessor *pp,
        struct Image *image, int width, int height);
int postprocessor_change_colorspace(struct PostProcessor *pp,
        struct Image *image, int colorspace);
int postprocessor_scale_and_change_colorspace(struct PostProcessor *pp,
        struct Image *image, int width, int height, int colorspace);



#endif //__POSTPROCESSOR_H__
