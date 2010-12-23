#ifndef __IMAGE_H__
#define __IMAGE_H__

#include "chobject.h"

struct ChImage {
    unsigned long width, height;
    unsigned long colorspace;
    unsigned char *data;
};

#endif //__IMAGE_H__
