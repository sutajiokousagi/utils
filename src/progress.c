#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/fcntl.h>
#include <sys/stat.h>

#define BUFFER_SIZE 8192


// Thermometer margin offsets
#define THERMO_LEFT_MARGIN_PCT  0.065625
#define THERMO_TOP_MARGIN_PCT   0.60
#define THERMO_BOTTOM_MARGIN_PCT 0.670833333
#define THERMO_TOP2_MARGIN_PCT  0.70
#define THERMO_BOTTOM2_MARGIN_PCT 0.770833333
#define THERMO_RIGHT_MARGIN_PCT 0.903125
#define THERMO_HEIGHT_PCT       0.07083333
#define THERMO_WIDTH_PCT        0.8375

#ifdef CNPLATFORM_yume
struct fb_var_screeninfo fb_var;
struct fb_fix_screeninfo fb_fix;
char * fb_base_addr = NULL;
int fb_write_off = 0;
long int screensize = 0, pagesize;
#endif


struct color {
    int r, g, b;
};


// The coordinate system used for the progress bar system is with (0, 0)
// located at the top-left, and (1, 1) located at the bottom-right.
struct progress_bar {
    float        x_offset;
    float        y;             // Values expressed in percent-of-screen.
    float        width, height; // Values expressed in percent-of-screen.
    struct color color;
    float        percentage;
    int          fb_width, fb_height, fb_bpp;
    int          fb_num;
    union {
        uint8_t  *fb;
        uint8_t  *fb_8;
        uint16_t *fb_16;
        uint32_t *fb_32;
    };
    int          fd;
};


struct color color_chumby_blue = {
    .r = 0,
    .g = 71,
    .b = 182,
};



//      
// bottom and right are *not* in the rectangle, so top==bottom and/or left==right results in nothing
//      
void my_fillrect(struct progress_bar *pb,
                 unsigned int top,    unsigned int left,
                 unsigned int bottom, unsigned int right,
                 unsigned int r  ,unsigned int g, unsigned int b) {           
    unsigned int y;
    uint16_t color16 = ((r&0xf8)<<8) + ((g&0xfc)<<3) + ((b&0xf8)>>3);
    uint32_t color32 = (r<<16) | (g<<8) | b;

    // Sanity check the values
    if (top>=bottom || left>=right)
        return; 

    if(right>pb->fb_width)
        right = pb->fb_width;

    if (bottom>pb->fb_height)
        bottom = pb->fb_height;

    if(pb->fb_bpp == 16) {
        for(y=top; y<bottom; y++) {   
            uint16_t *line = &pb->fb_16[y*pb->fb_width+left];
            unsigned width = right-left;
            while(width--)
                *line++ = color16;
        }
    }
    else {
        for(y=top; y<bottom; y++) {   
            uint32_t *line = &pb->fb_32[y*pb->fb_width+left];
            unsigned width = right-left;
            while(width--)
                *line++ = color32;
        }
    }
}


int progress_bar_init(struct progress_bar *pb,
                       float x_offset,          float y,
                       float width,             float height,
                       struct color color) {
    char fb_dev[16];
    struct fb_var_screeninfo info;

    pb->percentage = 0;
    pb->x_offset   = x_offset;
    pb->y          = y;
    pb->width      = width;
    pb->height     = height;
    pb->color      = color;
    pb->fb_num     = 0;

    snprintf(fb_dev, sizeof(fb_dev), "/dev/fb%d", pb->fb_num);
    if((pb->fd = open(fb_dev, O_RDWR)) < 0) {
        perror("Can't open framebuffer");
        return 1;
    }

#ifdef CNPLATFORM_yume
    // Get fixed screen information
    if (ioctl(pb->fd, FBIOGET_FSCREENINFO, &fb_fix)) {
            printf("Error reading fb fixed information.\n");
            exit(1);
    }

    // Get variable screen information
    if (ioctl(pb->fd, FBIOGET_VSCREENINFO, &fb_var)) {
            printf("Error reading fb variable information.\n");
            exit(1);
    }

    pb->fb_width  = fb_var.xres;
    pb->fb_height = fb_var.yres;
    pb->fb_bpp    = fb_var.bits_per_pixel;

    screensize = fb_var.xres * fb_var.yres * fb_var.bits_per_pixel / 8;

    pagesize = sysconf(_SC_PAGESIZE);
    //printf("%dx%d, %dbpp, size in KBytes=%ld, pagesize=%ldK\n", fb_var.xres, fb_var.yres, fb_var.bits_per_pixel,
    //      screensize / 1024, pagesize / 1024 );

    /* fix #6351 comment26 e.m. 2006oct20 */
    pb->fb = (unsigned short *)mmap(NULL , screensize+pagesize, PROT_READ | PROT_WRITE, MAP_SHARED, pb->fd, 0);

    if (pb->fb == (unsigned short *)-1) {
            printf("error mapping fb\n");
            exit(1);
    }

    /* temporary fix for 5159, mapping is paged aligned */
    if (fb_fix.smem_start & (pagesize-1)) {
            pb->fb += (fb_fix.smem_start & (pagesize-1));
            //fprintf(stderr, "Fix alignment 0x%08lx -> %p, x=%d, y=%d\n",
            //      fb_fix.smem_start, pb->fb, cur_x, cur_y);
    }
#else

    // Dynamically grab screen info
    if(ioctl(pb->fd, FBIOGET_VSCREENINFO, &info) == -1) {
        perror("Unable to get screen size");
        close(pb->fd);
        return 1;
    }
    pb->fb_width  = info.xres;
    pb->fb_height = info.yres;
    pb->fb_bpp    = info.bits_per_pixel;

    pb->fb = (uint8_t *)mmap(0, pb->fb_width*pb->fb_height*(pb->fb_bpp/8),
                                    PROT_READ | PROT_WRITE, MAP_SHARED,
                                    pb->fd, 0);
    if(pb->fb==(uint8_t *)-1) {
        perror("Can't mmap framebuffer");
        close(pb->fd);
        return 1;
    }
#endif

    return 0;
}

int progress_bar_update(struct progress_bar *pb, float percentage, int type) {
    int   width, last_width;

    if(percentage < 0)
        percentage = 0;
    if(percentage > 1)
        percentage = 1;

    int bar_left  = (pb->fb_width - (pb->fb_width*pb->width)) / 2;
    int bar_right = pb->fb_width  - bar_left;


    // Calculate two widths: This width and the last width.  If they're
    // equal, do nothing.  Otherwise, draw from last_width+1 to width.
    last_width = (bar_right - bar_left) * pb->percentage;
    width      = (bar_right - bar_left) * percentage;

    // If we /are/ greater, then draw a box.
    if(width > last_width) {
        int top    = bar_left+last_width;
        int left   = pb->fb_height*pb->y;
        int bottom = bar_left+width;
        int right  = (pb->fb_height*pb->y + pb->fb_height*pb->height);


        top     += (pb->fb_width * pb->x_offset);
        bottom  += (pb->fb_width * pb->x_offset);

//        fprintf(stderr, "Drawing box (%d, %d) -> (%d, %d)\n",
//                top,    left,
//                bottom, right);
//        fprintf(stderr, "New width.  <<%d  %d -> %d  %d>>\n",
//                bar_left, last_width, width, bar_right-width);
        my_fillrect(pb, left, top, right, bottom,
                        pb->color.r, pb->color.g, pb->color.b);
    }
    pb->percentage = percentage;

    return 1;
}


int print_help(char *prog) {
    printf("Usage: %s -b <number_of_bytes> [-f <framebuffer_device>]\n"
           "   -b Indicates how many bytes you'll be sending through this program\n"
           "   -f If you want to draw to a framebuffer device, specify it here\n"
           "   -e write progress to /tmp/flashplayer.event to be read by the flash player\n"
           "   -r Red color to use (0-255)\n"
           "   -g Green color to use (0-255)\n"
           "   -l bLue color to use (0-255)\n"
           "   -x X-offset percentage from center (default: -0.015)\n"
           "   -y Y-offset percentage from bottom (default: 0.60)\n"
           "   -w width percentage of screen (default: 0.8375)\n"
           "   -h height percentage of screen (default: 0.07083333)\n"
           "", prog);
    return 0;
}



// Draws the progrses, either to the framebuffer (if *pb is not NULL) or to the
// console (if it is).
void draw_progress(float percentage, int write_fp_event, struct progress_bar *pb) {
    if(pb)
    {
        progress_bar_update(pb, percentage, 0);
    }
    else
    {
        fprintf(stderr, "\r%2.3f%%    ", percentage*100);
        if(write_fp_event)
        {
            char* fileName = "/tmp/flashplayer.event";
            struct stat buf;
            int i = stat( fileName, &buf );
            int ret = 0;
            FILE* fp = NULL;

            if( 100 == (percentage*100) )
            {
                // if we're at 100%, wait for the flash player to process the event and write out 100%
                while( 0 == stat( fileName, &buf ) )
                {
                    // flashplayer.event still exists, so the flash player hasn't processed the event yet
                    usleep( 100 );
                }
                // flashplayer.event got deleted, so lets send the 100% event
                if( fp = fopen( fileName, "w" ) )
                {
                    fprintf( fp, "<event type=\"Progress\" value=\"progress\" comment=\"100\"/>\n" );
                    fclose( fp );
                    ret = system( "/usr/bin/chumbyflashplayer.x -F1 >/dev/null 2>&1 &" );
                    usleep( 1000 );
                }
            } 
            else if( 0 != i )
            {
                // event file doesn't exist, so lets write to it - the flash player is responsible for
                // deleting the event file when signaled.
                if( fp = fopen( fileName, "w" ) )
                {
                    fprintf( fp, "<event type=\"Progress\" value=\"progress\" comment=\"%2.3f\"/>\n", percentage*100 );
                    fclose( fp );
                    ret = system( "/usr/bin/chumbyflashplayer.x -F1 >/dev/null 2>&1 &" ); 
                }
            }
        }
    } 
    return;
}




int do_progress(int total_bytes, int draw_to_framebuffer, int write_fp_event, struct color *c, float x, float y, float width, float height) {
    struct progress_bar pb;
    struct progress_bar *progress_bar = NULL;
    int                 bytes_done = 0;
    int                 bytes_read;
    char                buffer[BUFFER_SIZE];

    if(!c)
        c = &color_chumby_blue;

    if(draw_to_framebuffer) {
        progress_bar = &pb;
        if(progress_bar_init(progress_bar,
                x,            // X-offset from center
                y,               // Y
                width,             // Width
                height,         // Height
                *c
        )) {
            fprintf(stderr, "Unable to initialize progress bar visual\n");
            draw_to_framebuffer = 0;
            progress_bar = NULL;
        }
    }


    // Fill buffer with data.
    while( ((bytes_read = read(0, buffer, sizeof(buffer))) > 0) ) {
        int bytes_left = bytes_read;
        int bytes_written;


        bytes_done += bytes_read;

        // Empty buffer out.  Keep going until it's empty.
        while(bytes_left > 0) {
            if( ((bytes_written = write(1, buffer + (bytes_read - bytes_left), bytes_left))) < 0) {
                perror("Unable to write data");
                return bytes_written;
            }

            bytes_left -= bytes_written;
        }

        // If we go over 100%, someone mislead us.  Clamp it to 100%.
        if(bytes_done > total_bytes)
            bytes_done = total_bytes;


        // Now that we've fully processed the data (read it in and then
        // written it back out), re-draw the progress bar.
        draw_progress((float)bytes_done/(float)total_bytes, write_fp_event, progress_bar);
    }

    if(bytes_read < 0) {
        perror("Unable to read data");
        return bytes_read;
    }

    return 0;
}





int main(int argc, char **argv) {
    int          ch;
    unsigned int bytes = 0;
    int          draw_to_framebuffer = 0;
    int          write_fp_event = 0;
    struct color c = color_chumby_blue;
    double        x      = -0.015;
    double        y      = 0.60;
    double        width  = 0.8375;
    double        height = 0.07083333; 

    if(argc <= 1) {
        print_help(argv[0]);
        return 1;
    }


    while((ch = getopt(argc, argv, "b:fer:g:l:x:y:w:h:")) != -1) {
        switch(ch) {
            case 'b':
                bytes = strtol(optarg, NULL, 0);
                break;

            case 'f':
                draw_to_framebuffer = 1;
                if( write_fp_event )
                {
                    printf( "syntax error: -f and -e cannot both be set\n" );
                    return print_help(argv[0]);
                } 
                break;

            case 'e':
                write_fp_event = 1;
                if( draw_to_framebuffer )
                {
                    printf( "syntax error: -e and -f cannot both be set\n" );
                    return print_help(argv[0]);
                } 
                break;

            case 'r':
                c.r = strtol(optarg, NULL, 0);
                break;

            case 'g':
                c.g = strtol(optarg, NULL, 0);
                break;

            case 'l':
                c.b = strtol(optarg, NULL, 0);
                break;

            case 'x':
                x = strtod(optarg, NULL);
                break;

            case 'y':
                y = strtod(optarg, NULL);
                break;

            case 'w':
                width = strtod(optarg, NULL);
                break;

            case 'h':
                height = strtod(optarg, NULL);
                break;

            default:
                return print_help(argv[0]);
                break;
        }
    }


    // We need some number of bytes.  Otherwise it just doesn't work.
    if(!bytes)
        return print_help(argv[0]);

    //fprintf(stderr, "x: %f\ty: %f\twidth: %f\theight: %f\n", x, y, width, height );
    return do_progress(bytes, draw_to_framebuffer, write_fp_event, &c, x, y, width, height);
}
