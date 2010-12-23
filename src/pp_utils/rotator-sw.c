#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>

#define MALLOC(x) malloc(x)
#define FREE(x) free(x)
extern unsigned char debug_level;


#include "rotator.h"
#include "post.h"



struct Rotator *rotator_alloc() {
    return MALLOC(sizeof(Rotator));
}

Rotator *rotator_init(struct Rotator *rot) {
    bzero(rot, sizeof(struct Rotator));
    return rot;
}


#define PIXEL_OFFSET(x, y)    ((image->width*(y) )+(x))
#define PIXEL_OFFSET_90(x, y) ((image->height*(y))+(x))





#define GET_PIXEL_32(x, y) (image->data_l)[PIXEL_OFFSET(x, y)]

#define PUT_PIXEL_32_0(x, y, s) \
    ((unsigned long *)scratchpad)[PIXEL_OFFSET(x, y)]=s
#define PUT_PIXEL_32_90(x, y, s) \
    ((unsigned long *)scratchpad)[PIXEL_OFFSET_90(image->height-1-y, x)]=s
#define PUT_PIXEL_32_270(x, y, s) \
    ((unsigned long *)scratchpad)[PIXEL_OFFSET_90(y, image->width-1-x)]=s
#define PUT_PIXEL_32_180(x, y, s) \
    ((unsigned long *)scratchpad)[PIXEL_OFFSET(image->width-1-x, image->height-1-y)]=s


static int rotator_rotate_rgb24(struct Rotator *rot, struct Image *image,
                                int amount) {
    unsigned char scratchpad[image->width*image->height*image->bpp];
    int  x, y;
    long c;
    for(x=0; x<image->width; x++) {
        for(y=0; y<image->height; y++) {
            c = GET_PIXEL_32(x, y);
            if(90 == amount)
                PUT_PIXEL_32_90(x, y, c);
            else if(180 == amount)
                PUT_PIXEL_32_180(x, y, c);
            else if(270 == amount)
                PUT_PIXEL_32_270(x, y, c);
            else
                PUT_PIXEL_32_0(x, y, c);
        }
    }
    memcpy(image->data, scratchpad, image->width * image->height * image->bpp);
    return 0;
}





#define GET_PIXEL_16(x, y) (image->data_s)[PIXEL_OFFSET(x, y)]

#define PUT_PIXEL_16_0(x, y, s) \
    ((unsigned short *)scratchpad)[PIXEL_OFFSET(x, y)]=s
#define PUT_PIXEL_16_90(x, y, s) \
    ((unsigned short *)scratchpad)[PIXEL_OFFSET_90(image->height-1-y, x)]=s
#define PUT_PIXEL_16_270(x, y, s) \
    ((unsigned short *)scratchpad)[PIXEL_OFFSET_90(y, image->width-1-x)]=s
#define PUT_PIXEL_16_180(x, y, s) \
    ((unsigned short *)scratchpad)[PIXEL_OFFSET(image->width-1-x, image->height-1-y)]=s

static int rotator_rotate_rgb16(struct Rotator *rot, struct Image *image,
                                int amount) {
    unsigned char scratchpad[image->width*image->height*image->bpp];
    int  x, y;
    long c;
    for(x=0; x<image->width; x++) {
        for(y=0; y<image->height; y++) {
            c = GET_PIXEL_16(x, y);
            if(90 == amount)
                PUT_PIXEL_16_90(x, y, c);
            else if(180 == amount)
                PUT_PIXEL_16_180(x, y, c);
            else if(270 == amount)
                PUT_PIXEL_16_270(x, y, c);
            else
                PUT_PIXEL_16_0(x, y, c);
        }
    }
    memcpy(image->data, scratchpad, image->width * image->height * image->bpp);
    return 0;
}



// Rotates a YCbYCr-formatted image.
static int rotator_rotate_ycbycr(struct Rotator *rot, struct Image *image,
                                int amount) {
    unsigned char scratchpad[image->width*image->height*image->bpp];
    int  x, y;
    long c, c1;
    for(x=0; x<image->width; x++) {
        for(y=0; y<image->height; y++) {
            c = GET_PIXEL_16(x, y);

            // For 90-degree rotations, we need to interpolate the red/blue
            // component for components in a grid pattern.
            if(90 == amount) {
                if((x&1)^(y&1))
                    c1 = GET_PIXEL_16(x, y);
                else
                    c1 = GET_PIXEL_16(x+1, y);
                c=(c&0x00FF)+(c1&0xFF00);
                PUT_PIXEL_16_90(x, y, c);
            }

            // Simply swap the Cr and Cb components if it's a 180-degree
            // rotation.
            else if(180 == amount) {
                c1 = GET_PIXEL_16(x+(x&1?-1:1), y);
                c=(c&0x00FF)+(c1&0xFF00);
                PUT_PIXEL_16_180(x, y, c);
            }

            // For 270-degree rotations, we need to interpolate the red/blue
            // component for components in a grid pattern.
            else if(270 == amount) {
                if((x&1)^(y&1))
                    c1 = GET_PIXEL_16(x+1, y);
                else
                    c1 = GET_PIXEL_16(x, y);

                c=(c&0x00FF)+(c1&0xFF00);
                PUT_PIXEL_16_270(x, y, c);
            }
            else
                PUT_PIXEL_16_0(x, y, c);
        }
    }
    memcpy(image->data, scratchpad, image->width * image->height * image->bpp);
    return 0;
}









int rotator_rotate(struct Rotator *rot, struct Image *image, int amount) {
    int ret = 0;

    if(debug_level >= 2)
        fprintf(stderr, "Entered rotator_rotate(%p, %p, %d)\n",
                rot, image, amount);

    if(!image->data) {
        fprintf(stderr, "No image data available\n");
        ret = -1; goto cleanup;
    }

    // Short-circuit for case where no rotation is necessary.
    if(!amount) {
        ret = 0; goto cleanup;
    }

    if(RGB24==image->colorspace)
        rotator_rotate_rgb24(rot, image, amount);
    else if(RGB16==image->colorspace)
        rotator_rotate_rgb16(rot, image, amount);
    else if(YCBYCR==image->colorspace)
        rotator_rotate_ycbycr(rot, image, amount);
    else {
        fprintf(stderr, "Unable to handle image's colorspace\n");
        ret = -1; goto cleanup;
    }


    // For side rotations, flip the width and height values.
    if(amount==90 || amount==270) {
        int t = image->width;
        image->width = image->height;
        image->height = t;
    }


cleanup:
    if(debug_level >= 2)
        fprintf(stderr, "Returning from rotator_rotate: %d\n", ret);
    return ret;
}


void rotator_free(Rotator *rotator) {

    if(!rotator)
        return;

    if(rotator->freed) {
        fprintf(stderr, "Rotator has already been freed!  Double rotator_free()?\n");
        return;
    }

    // Mark this as having been freed.
    rotator->freed = 1;

    // Tell the system the rotator has been freed.
    FREE(rotator);
}


