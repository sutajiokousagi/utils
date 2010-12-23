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

#define gp_system_dir_delim '/'



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


    CHECK (gp_abilities_list_new  (&al));
    CHECK (gp_abilities_list_load (al, NULL));

    count = gp_abilities_list_count (al);
    if (count < 0) {
        printf("gp_abilities_list_count error: %d\n", count);
        return(1);
    }
    else if (count == 0) {
        /* Copied from gphoto2-abilities-list.c gp_abilities_list_load() */
        const char *camlib_env = getenv(CAMLIBDIR_ENV);
        const char *camlibs = (camlib_env != NULL)?camlib_env:CAMLIBS;

        printf("no camera drivers (camlibs) found in camlib dir:\n"
               "    CAMLIBS='%s', default='%s', used=%s\n",
               camlib_env?camlib_env:"(null)", CAMLIBS,
               (camlib_env!=NULL)?"CAMLIBS":"default");
        return(1);
    }


    /* For each camera in the list, add a text snippet to the 
     * output file. */
    for (i = 0; i < count; i++) {
        CameraAbilities abilities;
        const char *camlib_basename;
        CHECK (gp_abilities_list_get_abilities (al, i, &abilities));
        camlib_basename = basename(abilities.library);

        /* Don't print out empty vendor IDs.  Empty product IDs might be
         * okay, though. */
        if( !abilities.usb_vendor )
            continue;

        printf("%04x:%04x\t# %s\n",
               abilities.usb_vendor,
               abilities.usb_product,
               abilities.model
               );
    }

    CHECK (gp_abilities_list_free (al));
    return (0);
}

