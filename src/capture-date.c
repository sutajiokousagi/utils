/*
 * The fast rotation process goes like this:
    1) Open the file.
    2) Call the rotator on it.
    3) Open the output file.
    4) Write the file to it.
    5) Profit!
 */

unsigned char debug_level     = 0;

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

#include <time.h>


#include <libexif/exif-data.h>
#include <libexif/exif-loader.h>




// Reports the file's modification (or creation) date.  Useful as a
// fallback for files that have no exif data.
int report_mdate(char *input) {
    struct stat status;
    if(stat(input, &status)) {
        perror("Unable to get status of file");
        return -1;
    }
    printf("%s:%d\n", input, status.st_mtime);
    return 0;
}

int report_exif_date(char *input) {

    ExifLoader *loader       = exif_loader_new();
    ExifData   *ed           = NULL;
    ExifEntry  *ee           = NULL;
    int         ret          = 0;

    if(debug_level >= 2)
        fprintf(stderr, "Entered report_exif_date(\"%s\")\n", input);


    // Open the file and pull out the exif data.
    exif_loader_write_file(loader, input);
    ed  = exif_loader_get_data(loader);
    exif_loader_unref(loader); loader=NULL;

    // See if there's exif data at all.
    if( !ed->data ) {
        if(debug_level>=1)
            fprintf(stderr, ">>>%s: Doesn't contain exif data\n", input);
        ret = report_mdate(input);
        goto cleanup;
    }

    // Look for an orientation tag.
    if( (ee = exif_content_get_entry(ed->ifd[EXIF_IFD_0],    0x0132))
     || (ee = exif_content_get_entry(ed->ifd[EXIF_IFD_EXIF], 0x0132)) ) {
        struct tm tm;
        time_t t;
        char entry[20];
        memcpy(entry, ee->data, 19);
        // Convert from "YYYY:MM:DD hh:mm:ss" to time_t

        entry[4]='\0';
        entry[7]='\0';
        entry[10]='\0';
        entry[13]='\0';
        entry[16]='\0';
        entry[19]='\0';

        tm.tm_year = strtol(entry+0,  NULL, 0)-1900;
        tm.tm_mon  = strtol(entry+5,  NULL, 0)-1;
        tm.tm_mday = strtol(entry+8,  NULL, 0);
        tm.tm_hour = strtol(entry+11, NULL, 0);
        tm.tm_min  = strtol(entry+14, NULL, 0);
        tm.tm_sec  = strtol(entry+17, NULL, 0);

        t = mktime(&tm);

        printf("%s:%d\n", input, t);
    }
    else {
        ret = report_mdate(input);
        if(debug_level >= 1)
            fprintf(stderr, ">>> %s: No date field found\n", input);
    }


cleanup:
    if(ed) {
        exif_data_unref(ed);
        ed = NULL;
    }

    if(debug_level >= 1)
        fprintf(stderr, "Returning from report_exif_date: %d\n", ret);
    return ret;
}



void print_help(char *progname) {
    printf("Usage:\n"
            "%s [-d] file1.jpg file2.jpg ...\n"
            "\t-d  Enable extra debugging information\n"
            "\t-h  This help message\n"
            "",
            progname);
    return;
}



int main(int argc, char **argv) {
    int   ch;



    if( argc > 1 ) {
        while ((ch = getopt(argc, argv, "i:hd?")) != -1) {
            switch (ch) {
                case 'd':
                    debug_level++;    // Debugging is good!
                    break;

                default:
                case '?':
                case 'h':
                    print_help(argv[0]);
                    return 1;
                    break;
            }
        }
    }
    else {
        print_help(argv[0]);
        return 1;
    }
    argc -= optind;
    argv += optind;

    int i;
    for(i=0; i<argc; i++)
        report_exif_date(argv[i]);
    return 0;
}
