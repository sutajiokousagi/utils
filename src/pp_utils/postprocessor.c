
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
//#include <ctype.h>
//#include <errno.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "postprocessor.h"

#define MALLOC(x) malloc(x)
#define FREE(x) free(x)
#define REALLOC(x, s) realloc(x, s)
extern unsigned char debug_level;


struct PostProcessor *postprocessor_alloc() {
    return MALLOC(sizeof(struct PostProcessor));
}

struct PostProcessor *postprocessor_init(struct PostProcessor *pp) {
    return pp;
}

void postprocessor_free(struct PostProcessor *pp) {
    FREE(pp);
}

static int postprocessor_run(struct PostProcessor *pp,
                struct Image *image, int width, int height, int colorspace) {
    int         ret = 0;
    int         bpp = image->bpp;
    int         fd  = 0;
    int         pp_total_buffer_size = 0;
    char       *pp_input = NULL, *pp_output;
    pp_params   pp_param;


    if(debug_level >= 2)
        fprintf(stderr, "Entered postprocessor_run(%p, %p, %d, %d, %d)\n",
                pp, image, width, height, colorspace);


    // The postprocessor has some pretty wacky constraints.
    // From the manual, Section 15.4: 15.4 IMAGE SIZE AND SCALE RATIO 
    /*
        The RGB graphic source image size is determined by number of pixels
        adjacent to horizontal and vertical directions. YCbCr420 and YCbCr422
        source image size is determined only by numbers of Y samples adjacent
        to horizontal and vertical directions. Destination image size is
        determined by dimension of final RGB graphic image, after color
        space conversion if source image is YCbCr image. 
        As explained in the previous section, SRC_Width and DST_Width
        satisfies the word boundary constraints in such a way that the
        number of horizontal pixel can be represented by kn where
        n = 1,2,3, ... and k = 1 / 2 / 8 for 24bppRGB / 16bppRGB / YCbCr420
        image, respectively.  Also SRC_Width must be 4’s multiple of 
        PreScale_H_Ratio and SRC_Height must be 2’s multiple
        of PreScale_V_Ratio. 
    */
    fprintf(stderr, "%d x %d x %d\n", image->width, image->height,
            image->colorspace);
    if((image->colorspace == YCBYCR || image->colorspace == YUV444)
            && (image->width&0x07)) {
        int new_width = image->width-(image->width&0x07);
        char temp_data[new_width*image->height*image->bpp];
        char *in_ptr  = image->data;
        char *out_ptr = (char *)temp_data;
        int current_x;
        if(debug_level >= 1)
            fprintf(stderr, "Warning: Source image is YCBYCR, and width is %d. "
                            "Pearing down to %d to make a multiple of 8\n",
                            image->width, new_width);

        for(current_x=0; current_x<image->height; current_x++) {
            memcpy(out_ptr, in_ptr, new_width*bpp);
            in_ptr  += image->width*image->bpp;
            out_ptr += new_width*image->bpp;
        }
        memcpy(image->data, temp_data, new_width*image->height*image->bpp);
        image->width=new_width;
    }
    else if(image->colorspace == RGB16 && (image->width&0x01)) {
        int new_width = image->width-(image->width&0x01);
        char temp_data[new_width*image->height*image->bpp];
        char *in_ptr  = image->data;
        char *out_ptr = (char *)temp_data;
        int current_x;
        if(debug_level >= 1)
            fprintf(stderr, "Warning: Source image is RGB16, and width is %d. "
                            "Pearing down to %d to make a multiple of 2\n",
                            image->width, new_width);

        for(current_x=0; current_x<image->height; current_x++) {
            memcpy(out_ptr, in_ptr, new_width*bpp);
            in_ptr  += image->width*image->bpp;
            out_ptr += new_width*image->bpp;
        }
        memcpy(image->data, temp_data, new_width*image->height*image->bpp);
        image->width=new_width;
    }
    else if((colorspace == YCBYCR || colorspace == YUV444) && (width&0x07)) {
        if(debug_level >= 1)
            fprintf(stderr, "Warning: Destination image is YCBYCR, and width "
                            "is %d.  Pearing down to %d to make a multiple "
                            "of 8\n", width, width-(width&0x07));
        width=width-(width&0x07);
    }
    else if(colorspace == RGB16 && (width&0x01)) {
        if(debug_level >= 1)
            fprintf(stderr, "Warning: Destination image is RGB16, and width "
                            "is %d.  Pearing down to %d to make a multiple "
                            "of 2\n", width, width-(width&0x01));
        width=width-(width&0x01);
    }


    // Open the postprocessor device node.
    fd = open(PP_DEV_NAME, O_RDWR|O_NDELAY);
    if( fd < 0 ) {
        perror("Couldn't open postprocessor");
        ret = -1;
        goto cleanup;
    }


    // Figure out how much memory we have to work with.
    pp_total_buffer_size = ioctl(fd, PPROC_GET_BUF_SIZE);


    // Figure out where to feed data to have it show up in the postprocessor.
    pp_input = (char *) mmap(0, pp_total_buffer_size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fd, 0);
    if( (int)pp_input == -1 ) {
        perror("Unable to map memory for postprocessor");
        ret = -1;
        goto cleanup;
    }


    // Figure out where data coming from the postprocessor ends up.
    pp_output = pp_input + ioctl(fd, PPROC_GET_INBUF_SIZE);



    // Set post processor parameters.
    pp_param.SrcStartX      = 0;
    pp_param.SrcStartY      = 0;
    pp_param.SrcFullWidth   = image->width;
    pp_param.SrcFullHeight  = image->height;
    pp_param.SrcWidth       = pp_param.SrcFullWidth;
    pp_param.SrcHeight      = pp_param.SrcFullHeight;
    pp_param.SrcCSpace      = image->colorspace;

    pp_param.DstStartX      = 0;
    pp_param.DstStartY      = 0;
    pp_param.DstFullWidth   = width;
    pp_param.DstFullHeight  = height;
    pp_param.DstWidth       = pp_param.DstFullWidth;
    pp_param.DstHeight      = pp_param.DstFullHeight;
    pp_param.DstCSpace      = colorspace;

    pp_param.OutPath        = POST_DMA;
    pp_param.Mode           = ONE_SHOT;

    pp_param.SrcFrmSt       = ioctl(fd, PPROC_GET_PHY_INBUF_ADDR);
    pp_param.DstFrmSt       = pp_param.SrcFrmSt+ioctl(fd, PPROC_GET_INBUF_SIZE);

    ioctl(fd, PPROC_SET_PARAMS, &pp_param);


    // Copy the image to the postprocessor.
    memcpy(pp_input, image->data, image->width*image->height*image->bpp);


    // Run the postprocessor.
    errno=0;
    ret = ioctl(fd, PPROC_START);
    if(errno || ret) {
        perror("Unable to scale using hardware scaler");
        ret = errno; goto cleanup;
    }



    // Figure out the bpp of the destination colorspace.
    switch(colorspace) {
        case RGB24:
            bpp=4;
            break;
        case YCBYCR:
            bpp = 2;
            break;
        case YC422:
            bpp = 2;
            break;
        case RGB16:
            bpp = 2;
            break;
        default:
            bpp = 4;
            break;
    }


    // Copy the result back to the image, making the data structure larger
    // if necessary.
    if( width*height*bpp > image->width*image->height*image->bpp ) {
        if(debug_level >= 1)
            fprintf(stderr, "Reallocating image.  "
                            "Was:%dx%dx%d  Now: %dx%dx%d\n",
                            image->width, image->height, image->bpp,
                            width, height, bpp);
        FREE(image->data);
        image->data = MALLOC(width*height*bpp);
    }

    // XXX This can sometimes crash, especially when the pp's memory is
    // exhausted.
    memcpy(image->data, pp_output, width*height*bpp);
    if(debug_level >= 1)
        fprintf(stderr, "Source: %p  Dest: %p\n", pp_input, pp_output);


    image->bpp        = bpp;
    image->width      = width;
    image->height     = height;
    image->colorspace = colorspace;



cleanup:
    if(pp_input) {
        munmap(pp_input, pp_total_buffer_size);
        pp_input = pp_output = NULL;
    }

    if(fd) {
        close(fd);
        fd = 0;
    }
    
    if(debug_level >= 2)
        fprintf(stderr, "Returning from postprocessor_run: %d\n", ret);

    return ret;
}

int postprocessor_change_colorspace(struct PostProcessor *pp,
                struct Image *image, int colorspace) {
    return postprocessor_run(pp, image, image->width, image->height,
                             colorspace);
}

int postprocessor_scale(struct PostProcessor *pp,
                struct Image *image, int width, int height) {
    return postprocessor_run(pp, image, width, height, image->colorspace);
}


int postprocessor_scale_and_change_colorspace(struct PostProcessor *pp,
                struct Image *image, int width, int height, int colorspace) {
    return postprocessor_run(pp, image, width, height, colorspace);
}

