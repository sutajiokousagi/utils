// $Id$
// fake_touchscreen.cpp - Fake touchscreen events
// henry@chumby.com

#include <stdlib.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <asm/types.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/hiddev.h>
#include <linux/input.h>

int main( int argc, char *argv[] )
{
	fprintf( stderr, "%s v0.10 - writing fake touchscreen events 1x/sec to stdout\n", argv[0] );
	struct input_event ev;
	ev.time.tv_usec = 0;
	ev.time.tv_sec = 0;
	// EV_SYN is used to mark the end of separate x and y records sent for an ABS event
	ev.type = EV_SYN;
	ev.code = 0;
	ev.value = 0;
	while (1)
	{
		write( 1, &ev, sizeof(ev) );
		ev.time.tv_sec++;
		sleep( 1 );
	}
}

