/*
 * The fast rotation process goes like this:
    1) Open the file.
    2) Call the rotator on it.
    3) Open the output file.
    4) Write the file to it.
    5) Profit!
 */

static char *default_infile   = "input.jpg";
static char *default_outfile  = "output.jpg";
unsigned char debug_level     = 0;
unsigned char save_images     = 0;

#define MAX_WIDTH  1600
#define MAX_HEIGHT 1200


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

#ifdef USE_LIB_EXIF
#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>
#endif

#include "jpeg.h"
#include "rotator.h"
#include "postprocessor.h"
#include "image.h"


static void save_image(struct Image *image, char *description) {
    FILE *out = fopen(description, "w");
    if(!out) {
        perror("Warning: Unable to open binary to save data");
        return;
    }
    int bytes = image->width*image->height*image->bpp;
    if(debug_level>=1)
        fprintf(stderr, "Writing image (%dx%dx%d) to %s (%d bytes)\n", 
                image->width, image->height, image->bpp, description, bytes);
    fwrite(image->data, bytes, 1, out);
    fclose(out);
}


int rescale_jpeg(char *input, char *output,
                 int width, int height, int rotation, char use_exif) {

#ifdef USE_LIB_EXIF
    ExifLoader *loader       = exif_loader_new();
    ExifData   *ed           = NULL;
    ExifEntry  *ee           = NULL;
#endif
    struct Rotator *rotator  = rotator_init(rotator_alloc());
    struct Jpeg    *reader   = jpeg_init(jpeg_alloc());
    struct PostProcessor *pp = postprocessor_init(postprocessor_alloc());
    struct Image   *image;
    int             ret      = 0;

    if(debug_level >= 2)
        fprintf(stderr, "Entered rescale_jpeg(%p, %p, %d, %d, %d, %d)\n",
                input, output, width, height, rotation, use_exif);

#ifndef USE_LIB_EXIF
    if(use_exif)
        fprintf(stderr, "Warning: EXIF image specified, "
                         "but code copmiled without USE_LIB_EXIF\n");
#else
    // Open the file and pull out the exif data.
    exif_loader_write_file(loader, input);
    ed = exif_loader_get_data(loader);
    exif_loader_unref(loader); loader=NULL;

    // See if there's a thumbnail.
    if( use_exif && !ed->data ) {
        fprintf(stderr, "Image \"%s\" doesn't contain a thumbnail\n", input);
        ret = -1;
        goto cleanup;
    }

    // Look for an orientation tag.
    if( (ee = exif_content_get_entry(ed->ifd[EXIF_IFD_0],    0x0112))
     || (ee = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], 0x0112)) ) {
        int e_rotation = exif_get_short(ee->data, exif_data_get_byte_order(ed));
        // Convert exif rotation to degrees.
        // Note this completely disregards flipping.
        switch(e_rotation) {
            default:
            case 1:
                rotation+=0;
                break;
            case 6:
                rotation+=90;
                break;
            case 3:
                rotation+=180;
                break;
            case 8:
                rotation+=270;
                break;
        }
    }
    while(rotation>=0 && rotation>=360) rotation -= 360;


    // Read the image in from the thumbnail data, change it to RGB, and
    // rotate it according to the EXIF data.
    if( use_exif ) {
        ret = jpeg_read_ram(reader, (char *)ed->data, ed->size, 1);
    }
    else
#endif
        ret = jpeg_read_file(reader, input, 0);
    if(ret) {
        fprintf(stderr, "Failed to read JPEG file\n");
        goto cleanup;
    }

    image = (struct Image *)reader;

    if(save_images)
        save_image(image, "1-jpeg.bin");


    /*
    if((ret = postprocessor_change_colorspace(pp, 
                    (struct Image *)reader, RGB16))) {
        fprintf(stderr, "Failed to change colorspace\n");
        goto cleanup;
    }
    if(save_images)
        save_image(image, "2-pp.bin");
    */


    if((ret = rotator_rotate(rotator, (struct Image *) reader, rotation))) {
        fprintf(stderr, "Failed to rotate\n");
        goto cleanup;
    }
    if(save_images)
        save_image(image, "3-rotator.bin");


    // Determine what the new dimensions should be.
    int alt_height = image->height * width / image->width;
    if (alt_height>height) {
        width  = image->width * height / image->height;
    } else {
        height = alt_height;
    }


    // padding (because the hardware JPEG codec needs to have images be
    // multiples of the given dimensions.)
    while(width%16) {
        if(debug_level>=2)
            fprintf(stderr, "Increasing width to pad to 16px: %d->%d\n",
                    width, width+(16-(width&0x0f)));
        width+=(16-(width&0x0f));
    }
    while(height%8) {
        if(debug_level>=2)
            fprintf(stderr, "Increasing height to pad to 8px: %d->%d\n",
                    height, height+(8-(height&0x07)));
        height += (8-(height&0x07));
    }
    

    // Ensure the padding doesn't go over the size limit.
    while(width  > MAX_WIDTH) {
        if(debug_level>=1)
            fprintf(stderr, "Decreasing width by 16 to fit in %dpx: %d->%d\n",
                    MAX_WIDTH, width, width-16);
        width  -= 16;
    }
    while(height > MAX_HEIGHT) {
        if(debug_level>=1)
            fprintf(stderr, "Decreasing height by 8 to fit in %dpx: %d->%d\n",
                    MAX_HEIGHT, height, height-8);
        height -= 8;
    }


    // Sometimes, it has trouble scaling images to very small sizes.  Take
    // the scaling in steps.
    if(image->width/width > 4) {
        if((ret = postprocessor_scale_and_change_colorspace(pp, 
                    (struct Image *) reader, width*4, height*4, YCBYCR))) {
            fprintf(stderr, "Failed to prescale image and convert colorspace\n");
            goto cleanup;
        }
    }


    if((ret = postprocessor_scale_and_change_colorspace(pp, 
                (struct Image *) reader, width, height, YCBYCR))) {
        fprintf(stderr, "Failed to scale image and convert colorspace\n");
        goto cleanup;
    }
    if(save_images)
        save_image(image, "4-pp.bin");


    if((ret = jpeg_write(reader, output))) {
        fprintf(stderr, "Failed to write image\n");
        goto cleanup;
    }


cleanup:
#ifdef USE_LIB_EXIF
    if(ed) {
        exif_data_unref(ed);
        ed = NULL;
    }
#endif

    if(reader) {
        if(debug_level>=1)
            fprintf(stderr, "Freeing JPEG reader/writer...\n");
        jpeg_free(reader);
    }
    if(pp) {
        if(debug_level >= 1)
            fprintf(stderr, "Freeing postprocessor driver...\n");
        postprocessor_free(pp);
    }
    if(rotator) {
        if(debug_level >= 1)
            fprintf(stderr, "Freeing rotator driver...\n");
        rotator_free(rotator);
    }

    if(debug_level >= 1)
        fprintf(stderr, "Returning from rescale_jpeg: %d\n", ret);
    return ret;
}



int main(int argc, char **argv) {
    char *infile   = default_infile;
    char *outfile  = default_outfile;
    int   default_width=800, default_height=600;
    int   ch;
    long  rotation = 0;
    char  use_exif = 0;


    if(getenv("VIDEO_X_RES"))
        default_width  = strtol(getenv("VIDEO_X_RES"), NULL, 0);
    if(getenv("VIDEO_Y_RES"))
        default_height = strtol(getenv("VIDEO_Y_RES"), NULL, 0);


    if( argc > 1 ) {
        while ((ch = getopt(argc, argv, "i:o:r:x:y:thds")) != -1) {
            switch (ch) {
                case 'd':
                    debug_level++;    // Debugging is good!
                    break;

                case 'i':
                    infile = optarg;
                    break;

                case 'o':
                    outfile = optarg;
                    break;

                case 'r':
                    rotation = strtol(optarg, NULL, 0);
                    break;

                case 't':
                    use_exif = 1;
                    break;

                case 's':
                    save_images = 1;
                    break;

                case 'x':
                    default_width = strtol(optarg, NULL, 0);
                    break;

                case 'y':
                    default_height = strtol(optarg, NULL, 0);
                    break;

                default:
                case '?':
                case 'h':
                    printf("Usage:\n"
        "%s [-i infile] [-o outfile] [-x width] [-y height]\n"
        "\t-i  File to extract the thumbnail from (default: %s)\n"
        "\t-d  Enable extra debugging information\n"
        "\t-o  File to write the scaled thumbnail to (default: %s)\n"
        "\t-r  Rotation amount, in 90-degree increments (default: 0)\n"
#ifdef USE_LIB_EXIF
        "\t-t  Take the exif thumbnail rather than the whole image\n"
#endif
        "\t-s  Save binary images for each step of the process\n"
        "\t-x  Width of the scaled jpeg (default: %d)\n"
        "\t-y  Height of the scaled jpeg (default: %d)\n"
        "\t-h  This help message\n"
        "",
                argv[0], default_infile, default_outfile,
                default_width, default_height);
                        exit(0);
                        break;
            }
        }
    }
    argc -= optind;
    argv += optind;


    return rescale_jpeg(infile, outfile,
                        default_width, default_height, rotation, use_exif);
}
