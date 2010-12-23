#ifndef __JPEG_H__
#define __JPEG_H__


#include "image.h"

struct Jpeg {
    struct Image parent;
    int   decode_handle, encode_handle;
};



struct Jpeg *jpeg_alloc();
struct Jpeg *jpeg_init(struct Jpeg *);
void         jpeg_free(struct Jpeg *);
int          jpeg_read_file(struct Jpeg *jpeg, const char *filename,
                            int fallback);
int          jpeg_read_ram(struct Jpeg *jpeg, char *file_data, 
                           long file_size, int fallback);
int          jpeg_write(struct Jpeg *jpeg, const char *filename);


#endif //__JPEG_H__
