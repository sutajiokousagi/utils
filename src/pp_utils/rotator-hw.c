#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>


#define MALLOC(x) malloc(x)
#define FREE(x) free(x)

#include "rotator.h"
#include "post.h"

#define ROTATOR_CTRLCFG         0x77200000
#define ROTATOR_SRCADDRREG0     0x77200004
#define ROTATOR_SRCADDRREG1     0x77200008
#define ROTATOR_SRCADDRREG2     0x7720000C
#define ROTATOR_SRCSIZEREG      0x77200010
#define ROTATOR_DESTADDRREG0    0x77200018
#define ROTATOR_DESTADDRREG1    0x7720001C
#define ROTATOR_DESTADDRREG2    0x77200020
#define ROTATOR_STATREG         0x7720002C

#define ROTATOR_BASE            0x77200000
#define ROTATOR_TOP             0x77210000




static inline unsigned long rotator_read(Rotator *rot, int offset) {
    if(!rot->mem)
        return 0;
    return rot->mem[offset-ROTATOR_BASE];
}

static inline int rotator_write(Rotator *rot, int offset, unsigned long value) {
    if(!rot->mem)
        return 0;
    fprintf(stderr, "rotator_write(%p, %08x, %08x)\n", rot, offset, value);
    rot->mem[offset-ROTATOR_BASE]=value;
    return 0;
}


struct Rotator *rotator_alloc() {
    return MALLOC(sizeof(Rotator));
}

Rotator *rotator_init(struct Rotator *rot) {
    char *errstr = NULL;


    bzero(rot, sizeof(struct Rotator));

    rot->memfd = open("/dev/mem", O_RDWR);
    if( rot->memfd < 0 ) {
        errstr = "Unable to open /dev/mem";
        goto init_error;
    }

    // Now, mmap the device to get access to it.  
    // See the documentaiton for information on these offsets, page 19-3.
    rot->mem = mmap(0, ROTATOR_TOP-ROTATOR_BASE, PROT_READ | PROT_WRITE, 
                    MAP_SHARED, rot->memfd, ROTATOR_BASE);
    if( -1 == (long)(rot->mem) ) {
        errstr = "Unable to mmap file";
        goto init_error;
    }

    return rot;

init_error:
    if(rot && rot->mem) {
        munmap(rot->mem, ROTATOR_TOP-ROTATOR_BASE);
        rot->mem = NULL;
    }
    if(rot && rot->memfd) {
        close(rot->memfd);
        rot->memfd = 0;
    }

    if( rot ) {
        FREE(rot);
    }

    perror(errstr);

    return 0;
}


int rotator_rotate(struct Rotator *rot, struct Image *image, int amount) {
    // Open up a connection to the postprocessor.  This'll give us a nice
    // chunk of memory to work with.
    int         fd = 0;
    int         pp_total_buffer_size = 0;
    char       *pp_input = NULL, *pp_output;
    int         flip = 0, rotation;
    int         ret = 0;
    int         status;
    int         format;

    if(!image->data) {
        fprintf(stderr, "No image data available\n");
        return -1;
    }



    // Short-circuit for case where no rotation is necessary.
    if(!amount)
        return 0;


    // Start by figuring out the format.
    if(image->colorspace==YCBYCR)
        format=3;
    else if(image->colorspace==RGB24)
        format=5;
    else if(image->colorspace==RGB16)
        format=4;
    else {
        fprintf(stderr, "Unsupported colorspace for rotator: %d\n",
                image->colorspace);
        ret = -1; goto cleanup;
    }




    // Open the postprocessor device node.  We're stealing its memory.
    fd = open(PP_DEV_NAME, O_RDWR|O_NDELAY);
    if( fd < 0 ) {
        perror("Couldn't open postprocessor");
        ret = -1; goto cleanup;
    }

    // Figure out how much memory we have to work with.
    pp_total_buffer_size = ioctl(fd, PPROC_GET_BUF_SIZE);


    // Figure out where to feed data to have it show up in the rotator.
    pp_input = (char *) mmap(0, pp_total_buffer_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
    if( (int)pp_input == -1 ) {
        perror("Unable to map memory for postprocessor to rotate");
        ret = -1;
        goto cleanup;
    }

    // Figure out where data coming from the rotator ends up.
    pp_output = pp_input + ioctl(fd, PPROC_GET_INBUF_SIZE);



    /*
     * Setting the width and height to multiples of 8 (16?) seems to make
     * it work.  Not quickly, but it'll "work".
    image->height = 64;
    image->width = 64;
    */


    // Set up the rotator's parameters.
    rotator_write(rot, ROTATOR_SRCADDRREG0, 
        ioctl(fd, PPROC_GET_PHY_INBUF_ADDR));
//    rotator_write(rot, ROTATOR_SRCADDRREG1, 
//        ioctl(fd, PPROC_GET_PHY_INBUF_ADDR));
//    rotator_write(rot, ROTATOR_SRCADDRREG2, 
//        ioctl(fd, PPROC_GET_PHY_INBUF_ADDR));
    rotator_write(rot, ROTATOR_DESTADDRREG0, 
        ioctl(fd, PPROC_GET_PHY_INBUF_ADDR)+ioctl(fd, PPROC_GET_INBUF_SIZE));
//    rotator_write(rot, ROTATOR_DESTADDRREG1, 
//        ioctl(fd, PPROC_GET_PHY_INBUF_ADDR)+ioctl(fd, PPROC_GET_INBUF_SIZE));
//    rotator_write(rot, ROTATOR_DESTADDRREG2, 
//        ioctl(fd, PPROC_GET_PHY_INBUF_ADDR)+ioctl(fd, PPROC_GET_INBUF_SIZE));


    // Copy the data to the rotator.
    memcpy(pp_input, image->data, image->width*image->height*image->bpp);


    // Set up the size of the image.
    rotator_write(rot, ROTATOR_SRCSIZEREG, (0xFFFF0000&((image->height)<<16))
                                         | (0x0000FFFF&((image->width )    )) );


    // Determine the rotation and flip parameters.
    rotation = 0;
    if(90==amount)
        rotation = 1;
    else if(180==amount)
        rotation = 2;
    else if(270==amount)
        rotation = 3;

    // Normalize the flip value (Yes, it's silly, but these might be
    // replaced by enums soon, so the value could change).
    flip = 0;
    if(1==flip)
        flip = 1;
    else if(2==flip)
        flip = 2;

    fprintf(stderr, "Rotation: %d  Flip: %d\n", rotation, flip);


    while((status=(rotator_read(rot, ROTATOR_STATREG))&0x03)) {
        static char *statuses[] = {
            "IDLE",
            "Reserved",
            "BUSY (Rotating image)",
            "BUSY (Rotating image, jobs pending)"
        };
        fprintf(stderr, "Initially waiting for rotator: %s", statuses[status&0x03]);
        if(status&0x03) {
            fprintf(stderr, "  Line %d", status>>16);
        }
        fprintf(stderr, "  Interrupt? %d", (status>>8)&1);
        fprintf(stderr, "\n");
        sleep(1);
    }
    if((status=(rotator_read(rot, ROTATOR_CTRLCFG))&1)) {
        fprintf(stderr, "Rotator hasn't yet started to move image\n");
    }
    else {
        fprintf(stderr, "Looks like the rotator moved the image.\n");
    }

    // Run the rotator.
    rotator_write(rot, ROTATOR_CTRLCFG,
            0<<24 | format<<13 | rotation<<6 | flip<<4 | 1);



    while((status=(rotator_read(rot, ROTATOR_STATREG))&0x03)) {
        static char *statuses[] = {
            "IDLE",
            "Reserved",
            "BUSY (Rotating image)",
            "BUSY (Rotating image, jobs pending)"
        };
        fprintf(stderr, "Waiting for rotator: %s", statuses[status&0x03]);
        if(status&0x03) {
            fprintf(stderr, "  Line %d", status>>16);
        }
        fprintf(stderr, "  Interrupt? %d", (status>>8)&1);
        fprintf(stderr, "\n");
        sleep(1);
    }


    // Copy the image back.
    memcpy(image->data, pp_output, image->width * image->height * image->bpp);


    // For side rotations, flip the width and height values.
    if(amount==90 || amount==270) {
        int t = image->width;
        image->width = image->height;
        image->height = t;
    }

cleanup:
    if(pp_input) {
        munmap(pp_input, pp_total_buffer_size);
        pp_input = pp_output = NULL;
    }

    if(fd) {
        close(fd);
        fd = 0;
    }



    return ret;
}


void rotator_free(Rotator *rotator) {

    if(!rotator)
        return;

    if(rotator->freed) {
        fprintf(stderr, "Rotator has already been freed!  Double rotator_free()?\n");
        return;
    }


   // Get rid of the mmaped file.
    if( rotator->mem ) {
        munmap(rotator->mem, ROTATOR_TOP-ROTATOR_BASE);
        rotator->mem = NULL;
    }

    // Close the file handle used to communicate with the kernel.
    if(rotator->memfd) {
        close(rotator->memfd);
        rotator->memfd=0;
    }



    // Mark this as having been freed.
    rotator->freed = 1;

    // Tell the system the rotator has been freed.
    FREE(rotator);
}


