/*
 * The fast rotation process goes like this:
    1) Open the file.
    2) Call the rotator on it.
    3) Open the output file.
    4) Write the file to it.
    5) Profit!
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <math.h>

#ifdef USE_LIB_JPEG
#include <jpeglib.h>
#endif


#include "jpeg.h"
#include "post.h"
#include "JPGApi.h"

#define MALLOC(x)     malloc(x)
#define FREE(x)       free(x)
#define REALLOC(x, s) realloc(x, s)

extern unsigned char debug_level;


struct Jpeg *jpeg_alloc() {
    struct Jpeg *p = MALLOC(sizeof(struct Jpeg));
    return p;
}

struct Jpeg *jpeg_init(struct Jpeg *jpeg) {
    bzero(jpeg, sizeof(struct Jpeg));
    return jpeg;
}

void jpeg_free(struct Jpeg *jpeg) {
    if( !jpeg )
        return;

    if( ((struct Image *)jpeg)->data ) {
        free(((struct Image *)jpeg)->data);
        ((struct Image *)jpeg)->data = NULL;
    }
    FREE(jpeg);
    return;
}



int jpeg_read_file(struct Jpeg *jpeg, const char *file_name, int fallback) {
    int         fd;
    long        file_size = 0;
    int         ret = 0;
    char       *file_data = NULL;
    struct stat s;



    // Read the JPEG image in.
    fd = open(file_name, O_RDONLY);
    if(fd < 0) {
        perror("Unable to open file for reading");
        ret = -1;
        goto cleanup;
    }
    fstat(fd, &s);
    file_size = s.st_size;

    // ...and mmap the JPEG image to RAM.
    file_data = (char *)mmap(0, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if ((int)file_data == -1) {
        perror("Input file memory mapping failed");
        ret = -1;
        goto cleanup;
    }

    ret = jpeg_read_ram(jpeg, file_data, file_size, fallback);
cleanup:
    if(file_data) {
        munmap(file_data, file_size);
        file_data = NULL;
    }
    if(fd) {
        close(fd);
        fd = 0;
    }
    return ret;
}

#ifdef USE_LIB_JPEG

static void init_source(j_decompress_ptr cinfo) {
    return;
}
static boolean fill_input_buffer(j_decompress_ptr cinfo) {
    return TRUE;
}
static void skip_input_data(j_decompress_ptr cinfo, long num_bytes) {
    cinfo->src->next_input_byte += num_bytes;
    cinfo->src->bytes_in_buffer -= num_bytes;
    return;
}
static void term_source(j_decompress_ptr cinfo) {
    return;
}

int jpeg_read_ram_sw(struct Jpeg *jpeg, char *file_data, long file_size) {
    struct jpeg_decompress_struct src_info;
    struct jpeg_error_mgr src_err;
    struct Image *image = (struct Image *)jpeg;
    int row = 0;
    struct jpeg_source_mgr mgr;

    src_info.err = jpeg_std_error(&src_err);
    jpeg_create_decompress(&src_info);

    // Hack up our own src to pull data from memory, rather than a file.
    mgr.bytes_in_buffer   = file_size;
    mgr.next_input_byte   = (JOCTET *)file_data;
    mgr.init_source       = init_source;
    mgr.fill_input_buffer = fill_input_buffer;
    mgr.resync_to_restart = jpeg_resync_to_restart;
    mgr.term_source       = term_source;
    mgr.skip_input_data   = skip_input_data;

    src_info.src = &mgr;

    jpeg_read_header(&src_info, TRUE);

    jpeg_calc_output_dimensions(&src_info);

    image->width      = src_info.image_width;
    image->height     = src_info.image_height;
    image->colorspace = RGB24;
    image->bpp        = 4;
    image->data       = (char *)MALLOC(image->width*image->height*image->bpp);

    char *ptr = image->data;
    jpeg_start_decompress(&src_info);

    while(row < image->height) {
        char source[image->width*image->bpp];
        char *src_ptr = source;
        int i;
        jpeg_read_scanlines(&src_info, (JSAMPARRAY)&src_ptr, 1);

        // By default, libjpeg uses packed 24-bit pixels.  Unpack this to
        // 32-bit xRGB values that the postprocessor can handle.
        // An alternative would be to recompile libjpeg to support the
        // appropriate format, but that's likely to break more things.
        for(i=0; i<image->width; i++) {
            (*(int *)ptr)=(src_ptr[0]<<16)+(src_ptr[1]<<8)+(src_ptr[2]);
            src_ptr+=3;
            ptr += image->bpp;
        }
        row++;
    }


    jpeg_finish_decompress(&src_info);
    jpeg_destroy_decompress(&src_info);

    return 0;
}

#endif
 

int jpeg_read_ram(struct Jpeg *jpeg, char *file_data, long file_size,
        int software_fallback) {

    long          image_size;
    int           dh;
    int           ret = 0;
    char         *jpeg_input, *jpeg_output;
    struct Image *image = (struct Image *)jpeg;


    // Set up the JPEG decoder.
    dh = SsbSipJPEGDecodeInit();
    if(dh < 0) {
        fprintf(stderr, "Unable to obtain JPEG decode handle");
        ret = -1;
        goto cleanup;
    }


    // Figure out where to stuff the raw JPEG file.
    jpeg_input = SsbSipJPEGGetDecodeInBuf(dh, file_size);
    if(jpeg_input == NULL){
        perror("Could not get JPEG input buffer");
        ret = -1; goto cleanup;
    }


    // Put JPEG image in the decoder's input buffer.
    memcpy(jpeg_input, file_data, file_size);




    // Decode the JPEG frame.
    if(SsbSipJPEGDecodeExe(dh) != JPEG_OK) {
        fprintf(stderr, "SsbSipJPEGDecodeExe returned decoding failed\n");
        ret = -1; goto cleanup;
    }


    // Get the output buffer address.
    jpeg_output = SsbSipJPEGGetDecodeOutBuf(dh, &image_size);
    if(jpeg_output == NULL){
        fprintf(stderr, "Could not get JPEG output buffer");
        ret = -1; goto cleanup;
    }


    // Figure out the dimensions of the image.
    SsbSipJPEGGetConfig(JPEG_GET_DECODE_WIDTH,
            (INT32 *)&(image->width));
    SsbSipJPEGGetConfig(JPEG_GET_DECODE_HEIGHT,
            (INT32 *)&(image->height));
    SsbSipJPEGGetConfig(JPEG_GET_SAMPING_MODE,
            (INT32 *)&(image->colorspace));

    // Convert the colorspace from the JPEG processor to the postprocessor.
    /*
    fprintf(stderr, "JPEG colorspace: %d\n", image->colorspace);
    switch(image->colorspace) {
        case JPG_420:
            image->colorspace = YC420;
            break;
        case JPG_422:
            image->colorspace = YCBYCR;
            break;
        case JPG_444:
            image->colorspace = YUV444;
            break;
        case JPG_411:
            image->colorspace = YCBYCR;
            break;
        case JPG_400:
            image->colorspace = RGB8;
            break;
        default:
            fprintf(stderr, "Unrecognized type: %d\n", image->colorspace);
            break;
    }
    */
    image->colorspace = YCBYCR;
    image->bpp = 2;


    // Copy the image to the destination.
    if(image->data)
        FREE(image->data);
    image->data = (char *)MALLOC(image_size);
    memcpy(image->data, jpeg_output, image_size);

cleanup:

    if(dh) {
        SsbSipJPEGDecodeDeInit(dh);
        dh = 0;
    }

#ifdef USE_LIB_JPEG
    if(ret && software_fallback) {
        return jpeg_read_ram_sw(jpeg, file_data, file_size);
    }
#endif

    return ret;
}



int jpeg_write(struct Jpeg *jpeg, const char *file_name) {
    long        file_size;
    int         eh = 0;
    int         ret;
    FILE       *file_output;
    char       *jpeg_input, *jpeg_output;
    int         height, width, image_size;
    int         temp_colorspace;
    struct Image *image = (struct Image *)jpeg;

    if( !((struct Image *)jpeg)->data ) {
        fprintf(stderr, "No image data to write to JPEG\n");
        ret = -1;
        goto cleanup;
    }


    width      = image->width;
    height     = image->height;
    image_size = width*height*2;



    // Now set up the encoder.
    eh = SsbSipJPEGEncodeInit();
    if(eh < 0) {
        perror("Unable to obtain JPEG encode handle");
        ret = -1;
        goto cleanup;
    }


    jpeg_input = SsbSipJPEGGetEncodeInBuf(eh, image_size);
    if(jpeg_input == NULL) {
        perror("Could not get JPEG input buffer");
        ret = -1;
        goto cleanup;
    }




    // Copy the rotated YUV data from the rotator back to the JPEG driver.
    memcpy(jpeg_input, image->data, image_size);




    // Convert the postprocessor's colorpsace into a JPEG colorspace.
    /*
    switch(image->colorspace) {
        case YC420:
            temp_colorspace = JPG_420;
            break;
        case YCBYCR:
            temp_colorspace = JPG_422;
            break;
        case YUV444:
            temp_colorspace = JPG_444;
            break;
//        case YCBYCR:
//            temp_colorspace = JPG_411;
//            break;
        case RGB8:
            temp_colorspace = JPG_400;
            break;
    }
    */
    temp_colorspace = JPG_422;



    // Set up a bunch of encoding parameters.
   if((ret = SsbSipJPEGSetConfig(JPEG_SET_SAMPING_MODE,
                   temp_colorspace)) != JPEG_OK) {
       perror("Unable to set encoding sampling mode");
       goto cleanup;
    }

    if((ret = SsbSipJPEGSetConfig(JPEG_SET_ENCODE_WIDTH,
                    image->width)) != JPEG_OK) {
       perror("Unable to set encoding width");
       goto cleanup;
    }

    if((ret = SsbSipJPEGSetConfig(JPEG_SET_ENCODE_HEIGHT,
                    image->height)) != JPEG_OK) {
       perror("Unable to set encoding height");
       goto cleanup;
    }

    if((ret = SsbSipJPEGSetConfig(JPEG_SET_ENCODE_QUALITY,
                    JPG_QUALITY_LEVEL_2)) != JPEG_OK) {
       perror("Unable to set encoding quality");
       goto cleanup;
    }




    // Run the encoding.
    ret = SsbSipJPEGEncodeExe(eh, NULL, JPEG_USE_HW_SCALER);
    if( ret != JPEG_OK ) {
       perror("Encode failed");
       goto cleanup;
    }


    // Get the address of the encoded file.
    jpeg_output = SsbSipJPEGGetEncodeOutBuf(eh, &file_size);
    if(jpeg_output == NULL) {
        perror("Unable to get encoded file after JPEG compression");
        ret = -1;
        goto cleanup;
    }


    // Open the output file.
    file_output = fopen(file_name, "w");
    if( !file_output ) {
        perror("Unable to open output file for writing");
        ret = -1;
        goto cleanup;
    }


    // Write the data to the output file.
    if(debug_level >= 1)
        fprintf(stderr, "Writing output file (%ld bytes)\n", file_size);
    fwrite(jpeg_output, 1, file_size, file_output);
    fclose(file_output);





    // Clean up.
cleanup:
    if(eh) {
        SsbSipJPEGEncodeDeInit(eh);
        eh = 0;
    }

    return 0;
}



