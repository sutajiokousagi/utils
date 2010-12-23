#ifndef __IMAGE_H__
#define __IMAGE_H__


struct Image {
    int   width, height;
    int   colorspace;
    int   bpp;
//    char *data;
    union {
        char  *data;
        short *data_s;
        long  *data_l;
    };
};

#endif //__IMAGE_H__
