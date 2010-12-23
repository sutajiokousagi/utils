#ifndef __ROTATOR_H__
#define __ROTATOR_H__

#include "image.h"

/*
union image_data {
    unsigned char  *image_c;
    unsigned short *image_s;
    unsigned long  *image_l;
};
*/

typedef struct Rotator {
    char *mem;
//    union image_data input;
//    union image_data output;
//    char *pp_addr;
    int   memfd;
//    int   mode;
//    int   width;
//    int   height;
//    int   rotation;
//    int   flip;
    int   freed;
//    int   pp_fd;
} Rotator;

Rotator *rotator_alloc();
Rotator *rotator_init(struct Rotator *rot);

int rotator_rotate(struct Rotator *rot, struct Image *image, int amount);

/*
void rotator_set_rotation(Rotator *rotator, int amount);
void rotator_set_flip(Rotator *rotator,     int direction);
void rotator_start(Rotator *rotator);
void rotator_set_size(Rotator *rotator, int width, int height);
void rotator_set_image(Rotator *rotator, char *data);
void rotator_set_mode(Rotator *rotator, int mode);
int  rotator_idle(Rotator *rotator);
*/
int  rotator_wait(Rotator *rotator);
void rotator_free(Rotator *rotator);

char *rotator_get_result(Rotator *rotator);


#endif //__ROTATOR_H__
