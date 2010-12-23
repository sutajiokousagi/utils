/* test-camera-list.c
 *
 * Copyright © 2001 Lutz Müller <lutz@users.sf.net>
 * Copyright © 2005 Hans Ulrich Niedermann <gp@n-dimensional.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details. 
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
//#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

#include <gphoto2/gphoto2-port-log.h>
#include <gphoto2/gphoto2-camera.h>
#include <gphoto2/gphoto2-port-portability.h>

#define CHECK(f) {int res = f; if (res < 0) {printf ("ERROR: %s\n", gp_result_as_string (res)); return (1);}}

#define CAMLIBDIR_ENV "CAMLIBS"
#define CAMLIBS "/usr/lib/libgphoto2/2.4.1"


#ifdef __GNUC__
#define __unused__ __attribute__((unused))
#else
#define __unused__
#endif


/** boolean value */
static int do_debug = 0;


/** time zero for debug log time stamps */
struct timeval glob_tv_zero = { 0, 0 };


static void
#ifdef __GNUC__
        __attribute__((__format__(printf,3,0)))
#endif
debug_func (GPLogLevel level, const char *domain, const char *format,
        va_list args, void __unused__ *data)
{
    struct timeval tv;
    long sec, usec;

    gettimeofday (&tv, NULL);
    sec = tv.tv_sec  - glob_tv_zero.tv_sec;
    usec = tv.tv_usec - glob_tv_zero.tv_usec;
    if (usec < 0) {sec--; usec += 1000000L;}
    fprintf (stderr, "%li.%06li %s(%i): ", sec, usec, domain, level);
    vfprintf (stderr, format, args);
    fputc ('\n', stderr);
}


/** C equivalent of basename(1) */
static const char *
basename (const char *pathname)
{
    char *result, *tmp;
    /* remove path part from camlib name */
    for (result=tmp=(char *)pathname; (*tmp!='\0'); tmp++) {
        if ((*tmp == gp_system_dir_delim) 
            && (*(tmp+1) != '\0')) {
            result = tmp+1;
        }
    }
    return (const char *)result;
}



/** 
 * Get list of supported cameras, walk through it, and try to see
 * if the provided USB product/vendor combination was specified.
 */
int
main (int argc, char *argv[])
{
    CameraAbilitiesList *al;
    int         i;
    int         count;
    const char *fmt_str = NULL;
    long        vendor_id, product_id;

    /* Parse the command line, looking for the USB ids */
    if( argc != 3 ) {
        fprintf(stderr, "Usage: %s [vendor_id] [product_id]\n", argv[0]);
        return 0;
    }

    vendor_id  = strtol(argv[1], NULL, 0);
    product_id = strtol(argv[2], NULL, 0);

    fprintf(stderr, "Read vendor 0x%04x product 0x%04x\n", vendor_id,
            product_id);

    if( !vendor_id || !product_id ) {
        fprintf(stderr, "Missing either the vendor or product id\n");
        return 0;
    }

    /*
    if (do_debug) {
        gettimeofday (&glob_tv_zero, NULL);
        CHECK (gp_log_add_func (GP_LOG_ALL, debug_func, NULL));
        
        gp_log (GP_LOG_DEBUG, "main", "test-camera-list start");
    }
    */


    CHECK (gp_abilities_list_new (&al));
    CHECK (gp_abilities_list_load (al, NULL));

    count = gp_abilities_list_count (al);
    if (count < 0) {
        printf("gp_abilities_list_count error: %d\n", count);
        return(0);
    }
    else if (count == 0) {
        /* Copied from gphoto2-abilities-list.c gp_abilities_list_load() */
        const char *camlib_env = getenv(CAMLIBDIR_ENV);
        const char *camlibs = (camlib_env != NULL)?camlib_env:CAMLIBS;

        printf("no camera drivers (camlibs) found in camlib dir:\n"
               "    CAMLIBS='%s', default='%s', used=%s\n",
               camlib_env?camlib_env:"(null)", CAMLIBS,
               (camlib_env!=NULL)?"CAMLIBS":"default");
        return(0);
    }


    /* For each camera in the list, add a text snippet to the 
     * output file. */
    fprintf(stderr, "Examining %d entries for 0x%04x:0x%04x\n",
            count, vendor_id, product_id);
    for (i = 0; i < count; i++) {
        CameraAbilities abilities;
        const char *camlib_basename;
        CHECK (gp_abilities_list_get_abilities (al, i, &abilities));
        camlib_basename = basename(abilities.library);

        if( abilities.usb_vendor  == vendor_id
         && abilities.usb_product == product_id ) {
            fprintf(stderr, "Found camera model \"%s\"\n", abilities.model);
            return 1;
        }
        /*
        printf(fmt_str,
               i+1, 
               camlib_basename,
               abilities.id,
               abilities.usb_vendor,
               abilities.usb_product,
               abilities.model
               );
        */
    }

    CHECK (gp_abilities_list_free (al));
    return (0);
}

