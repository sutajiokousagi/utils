/*
 * $Id$ hid_keys.cpp - map EVT_ABS on a HID device to keys
**/

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
#include <errno.h>
#include <time.h>
#include <signal.h>

#include <linux/hiddev.h>
#include <linux/input.h>


#define VER_STR "0.40"

enum _keydef_types {
	KDTYPE_KEY = 1,
	KDTYPE_ABSXY = 3,
	KDTYPE_BYCODE = 30
};
typedef struct _keydef {
	char type;
	int xmin;
	int xmax;
	int ymin;
	int ymax;
	const char *s;
	int rept;
	int rept_delay;
} KEYDEF;

int g_keydef_count = 0;
int g_debug = 0;
int g_quit = 0;
KEYDEF g_keys[1024];
#define MAX_KEYDEFS (sizeof(g_keys)/sizeof(g_keys[0]))

// Seconds to wait on input before sending ping string
int g_ping_delay = 0;
const char *g_ping_str = NULL;

// Last string sent
static char g_last_sent[1024] = {0};

// Send a string to stdout
void
send_str( const char *s )
{
	puts( s );
	strcpy( g_last_sent, s );
	fflush( stdout );
}

// Record last string sent (before exiting)
void
record_last_sent( void )
{
	if (g_last_sent[0])
	{
		FILE *f = fopen( "hid_keys_last_sent", "w" );
		if (f)
		{
			fprintf( f, "%s\n", g_last_sent );
			fclose( f );
			g_last_sent[0] = '\0';
		}
	}

}

// Reset file containing last string sent (on startup)
void
clear_last_sent( void )
{
	unlink( "hid_keys_last_sent" );
	g_last_sent[0] = '\0';
}

// Translate an x/y up event into a key
bool
translate_event( int x, int y, int force )
{
	int n;
	static int last_sent = -1;
	for (n = 0; n < g_keydef_count; n++)
	{
		if (g_keys[n].type != KDTYPE_ABSXY) continue;
		if (x >= g_keys[n].xmin &&
			x <= g_keys[n].xmax &&
			y >= g_keys[n].ymin &&
			y <= g_keys[n].ymax)
		{
			if (n == last_sent && !force)
			{
				return true;
			}
			last_sent = n;
			int r;
			if (g_debug) fprintf( stderr, "x=%d y=%d matched %d\n", x, y, n );
			for (r = 0; r < g_keys[n].rept; r++)
			{
				if (r) sleep( g_keys[n].rept_delay );
				send_str( g_keys[n].s );
			}
			return true;
		}
	}
	return false;
}

// Translate a key event into a string to send
bool
translate_key( int code, int value, int lastx, int lasty )
{
	int n;
	for (n = 0; n < g_keydef_count; n++)
	{
		if (g_keys[n].type != KDTYPE_KEY) continue;
		// Code, value, flags, unused
		if (g_keys[n].xmin != code) continue;
		if (g_keys[n].xmax != value) continue;
		if (g_keys[n].s && g_keys[n].s[0])
		{
			send_str( g_keys[n].s );
		}
		if (g_keys[n].ymin & 0x01)
		{
			fprintf( stderr, "Quit requested by key def %d\n", n );
			g_quit = 1;
		}
		return true;
	}
	return false;
}

// Translate a position code event into a string to send
bool
translate_code( int code, int value, int lastx, int lasty )
{
	int n;
	static int last_sent = -1;
	for (n = 0; n < g_keydef_count; n++)
	{
		if (g_keys[n].type != KDTYPE_BYCODE) continue;
		// code, flags, min, max
		if (g_keys[n].xmin != code) continue;
		unsigned int flags = g_keys[n].xmax;
		if (value < g_keys[n].ymin || value > g_keys[n].ymax) continue;
		// Bit 0 is no repeat
		if ((flags & 0x01) && n == last_sent)
		{
			return true;
		}
		last_sent = n;
		send_str( g_keys[n].s );
		return true;
	}
	return false;
}

int
read_events( const char *deviceName )
{

	// From http://www.frogmouth.net/hid-doco/c514.html
	// Example 1 and common init
  int fd = -1;
  int version;
    int yalv;           /* loop counter */

  if ((fd = open(deviceName, O_RDONLY)) < 0) {
    perror("hiddev open");
    return 1;
  }

  /* ioctl() accesses the underlying driver */
  ioctl(fd, HIDIOCGVERSION, &version);

  /* the HIDIOCGVERSION ioctl() returns an int */
  /* so we unpack it and display it */
  //fprintf( stderr, "hiddev driver version is %d.%d.%d\n",
	// version >> 16, (version >> 8) & 0xff, version & 0xff);

	// Example 2
  struct hiddev_devinfo device_info;

	memset( &device_info, 0, sizeof(device_info) );

 /* suck out some device information */
  ioctl(fd, HIDIOCGDEVINFO, &device_info);

  /* the HIDIOCGDEVINFO ioctl() returns hiddev_devinfo
   * structure - see <linux/hiddev.h>
   * So we work through the various elements, displaying
   * each of them
   */
  //fprintf( stderr, "vendor 0x%04hx product 0x%04hx version 0x%04hx ",
  //        device_info.vendor, device_info.product, device_info.version);
  //fprintf( stderr, "has %i application%s ", device_info.num_applications,
  //       (device_info.num_applications==1?"":"s"));
  //fprintf( stderr, "and is on bus: %d devnum: %d ifnum: %d\n",
  //       device_info.busnum, device_info.devnum, device_info.ifnum);


	// Example 4
/* this macro is used to tell if "bit" is set in "array"
 * it selects a byte from the array, and does a boolean AND
 * operation with a byte that only has the relevant bit set.
 * eg. to check for the 12th bit, we do (array[1] & 1<<4)
 */
#define test_bit(bit, array)    (array[bit/8] & (1<<(bit%8)))

  uint8_t evtype_bitmask[EV_MAX/8 + 1];

  memset(evtype_bitmask, 0, sizeof(evtype_bitmask));
  if (ioctl(fd, EVIOCGBIT(0, EV_MAX), evtype_bitmask) < 0) {
      perror("evdev ioctl");
  }

  //fprintf( stderr, "Supported event types:\n" );
  bool hasAbs = false;
  for (yalv = 0; yalv < EV_MAX; yalv++) {
      if (test_bit(yalv, evtype_bitmask)) {
	  /* this means that the bit is set in the event types list */
	  //fprintf( stderr, "  Event type 0x%02x ", yalv );
	  switch ( yalv)
	      {
	      	/****
			case EV_SYN :
				fprintf( stderr, " (SYN - event separator)\n" );
				break;
	      case EV_KEY :
		  fprintf( stderr, " (Keys or Buttons)\n");
		  break;
		  ****/
	      case EV_ABS :
			//fprintf( stderr, " (Absolute Axes)\n");
			hasAbs = true;
			break;
			/****
	      case EV_LED :
		  fprintf( stderr, " (LEDs)\n");
		  break;
	      case EV_REP :
		  fprintf( stderr, " (Repeat)\n");
		  break;
	      default:
		  fprintf( stderr, " (Unknown event type: 0x%04hx)\n", yalv);
		  ****/
	      }
	    }
	}


	int inCount = 0;
    size_t read_bytes;  /* how many bytes were read */
    struct input_event ev[64]; /* the events (up to 64 at once) */
	bool exitRequested = false;
	// Get EV_SYN events
	int lastX = 0;
	int lastY = 0;
	// Ignore EV_SYN unless both lastX and lastY have been received
	int receivedXY = 0; // 0x01 = left, 0x02 = right
	// Keep track of ping counter when no events received
	int ping_start = 0;
   while (!exitRequested && !g_quit)
	{
		inCount++;
		// Check for stdout ready
		// Wait up to 1s for input
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        fd_set readfds, writefds;
        int result;
        FD_ZERO(&readfds);
        FD_SET(fd,&readfds);
        bool inputReady = (result = select(fd+1,&readfds,NULL,NULL,&timeout)) > 0;
        FD_ZERO( &writefds );
        FD_SET( fileno(stdout), &writefds );
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        // We'll never get a failure here
        if (select( fileno(stdout) + 1, NULL, &writefds, NULL, &timeout ) <= 0)
        {
			fprintf( stderr, "stdout no longer accepting output - exiting\n" );
			break;
        }
        if (!inputReady)
        {
        	// Keep waiting for input
        	ping_start++;
        	if (ping_start == g_ping_delay && g_ping_str != NULL)
        	{
        		ping_start = 0;
        		//fprintf( stderr, "Sending ping %s\n", g_ping_str );
        		send_str( g_ping_str );
        	}
        	// Debug only
        	/*****
        	else
        	{
        		fprintf( stderr, "No input ready\n" );
        	}
        	*****/
        	continue;
        }
        ping_start = 0;
		int
	read_bytes = read(fd, ev, sizeof(struct input_event) * 64);

	if (read_bytes < (int) sizeof(struct input_event)) {
	    perror("evtest: short read");
	    return 1;
	}

	for (yalv = 0; yalv < (int) (read_bytes / sizeof(struct input_event)); yalv++)
	    {
		if (g_quit) break;
		static bool hasPress = false;
	    	// type==1 for key event
	    	// value==1 for key press, 0 for release
	    	// codes are mapped in kc and ks above
	    	if (ev[yalv].type != EV_ABS &&
			ev[yalv].type != EV_KEY &&
			ev[yalv].type != EV_SYN )
	    	{
	    		continue;
	    	}
		// Handle SYN events for non-press devices
		if (ev[yalv].type == EV_SYN)
		{
			if (!hasPress && (receivedXY & 0x03) == 0x03)
			{
				translate_event( lastX, lastY, 0 );
			}
			continue;
		}
		if (ev[yalv].type == EV_ABS)
		{
	    	// code=0 is y
	    	// code=1 is x
	    	// code=0x18 is press/release state
	    	switch (ev[yalv].code)
	    	{
	    		case 0:
					lastY = ev[yalv].value;
					receivedXY |= 0x02;
					//translate_event( lastX, lastY, 0 );
					continue;
	    		case 1:
					lastX = ev[yalv].value;
					receivedXY |= 0x01;
					//translate_event( lastX, lastY, 0 );
					continue;
	    		case 0x18:
					hasPress = true;
					if (ev[yalv].value == 0)
					{
						if (!translate_event( lastX, lastY, 1 ))
						{
							fprintf( stderr, "up at %d,%d not translated\n", lastX, lastY );
							fflush( stderr );
						}
					}
					continue;
			default:
				if (translate_code( ev[yalv].code, ev[yalv].value, lastX, lastY ))
				{
					continue;
				}
				break;
	    	}
		}
		else if (ev[yalv].type == EV_KEY)
		{
			if (translate_key( ev[yalv].code, ev[yalv].value, lastX, lastY ))
			{
				continue;
			}
		}

		if (g_debug)
		{
			fprintf( stderr, "[%ld.%06ld] type %d, code %02x, value %d\n",
		       		ev[yalv].time.tv_sec, ev[yalv].time.tv_usec, ev[yalv].type,
		       		ev[yalv].code, ev[yalv].value);
			fflush( stderr );
		}

	    }
	}

	close( fd );

	return 0;
}

void ChumbySigAction( int signum, siginfo_t *siginfo, void *data )
{
        // Get context - linux-specific
        ucontext_t *context = (ucontext_t*)data;
        switch (signum)
        {
        	case SIGCHLD:
                if (siginfo->si_code == CLD_EXITED
                        || siginfo->si_code == CLD_KILLED
                        || siginfo->si_code == CLD_DUMPED
                        )
                {
                        // Get pid of child process
                        pid_t childPid = siginfo->si_pid;
                        fprintf( stderr, "SIGCHLD: child process %u exited, code = %d\n", childPid, siginfo->si_code );
                }
                else
                {
                        fprintf( stderr, "SIGCHLD pid=%u unhandled code %u\n", siginfo->si_pid, siginfo->si_code );
                }
				break;
			case SIGPIPE:
				fprintf( stderr, "SIGPIPE - terminating\n" );
				record_last_sent();
				exit( -1 );
				break;
			default:
				fprintf( stderr, "Unhandled sigaction %d\n", signum );
				break;
        }
}

int
main( int argc, char *argv[] )
{
	fprintf( stderr, "hid_keys v" VER_STR "\n" );
	if (argc < 3)
	{
		fprintf( stderr, "Syntax:\n%s device keymap\n\twhere device is something like /dev/input/event0\n", argv[0] );
		return -1;
	}
	// Read keymap
	FILE *fKeymap = fopen( argv[2], "r" );
	if (!fKeymap)
	{
		fprintf( stderr, "Failed to open %s (errno=%d: %s)\n", argv[2], errno, strerror(errno) );
		return -1;
	}
	// xmin	xmax	ymin	ymax	string
	char buff[1024];
	while (fgets( buff, sizeof(buff), fKeymap ))
	{
		char *t1, *t2, *t3, *t4, *t5;
		char kdtype = KDTYPE_ABSXY;
		t1 = strtok( buff, " \t\r\n" );
		if (t1 && !strncmp( t1, "t=", 2 ))
		{
			kdtype = atoi( &t1[2] );
			t1 = strtok( NULL, " \t\r\n" );
		}
		t2 = strtok( NULL, " \t\r\n" );
		t3 = strtok( NULL, " \t\r\n" );
		t4 = strtok( NULL, " \t\r\n" );
		// This may contain spaces
		t5 = strtok( NULL, "\t\r\n" );
		if (!t5) continue;
		// Optional
		char *t6 = strtok( NULL, " \t\r\n" );
		char *t7 = strtok( NULL, " \t\r\n" );
		if (*t1 == '#') continue;
		if (!strcmp( t1, "ping" ))
		{
			if (!t7) continue;
			g_ping_delay = atoi( t7 );
			g_ping_str = strdup( t5 );
			continue;
		}
		if (g_keydef_count >= MAX_KEYDEFS)
		{
			fprintf( stderr, "Too many keydefs - maximum (%d) exceeded\n", MAX_KEYDEFS );
			return -1;
		}
		g_keys[g_keydef_count].type = kdtype;
		g_keys[g_keydef_count].xmin = atoi(t1);
		g_keys[g_keydef_count].xmax = atoi(t2);
		g_keys[g_keydef_count].ymin = atoi(t3);
		g_keys[g_keydef_count].ymax = atoi(t4);
		// Optional repeat count and repeat delay may exist
		g_keys[g_keydef_count].s = strdup(t5);
		g_keys[g_keydef_count].rept = 1;
		g_keys[g_keydef_count].rept_delay = 1;
		if (t6) g_keys[g_keydef_count].rept = atoi( t6 );
		if (t7) g_keys[g_keydef_count].rept_delay = atoi( t7 );
		g_keydef_count++;
	}
	fclose( fKeymap );
	if (g_keydef_count == 0)
	{
		fprintf( stderr, "No key defs loaded from %s, cannot continue\n", argv[2] );
		return -1;
	}

        // Set up signal handlers based on Linux-specific extensions to POSIX sigaction
        struct sigaction segv_handler;
        memset( &segv_handler, 0, sizeof(segv_handler) );
        segv_handler.sa_sigaction = ChumbySigAction;
        segv_handler.sa_flags = SA_SIGINFO;
        struct sigaction old_segv;
        sigaction( SIGSEGV, &segv_handler, &old_segv );
        sigaction( SIGILL, &segv_handler, &old_segv );
        sigaction( SIGFPE, &segv_handler, &old_segv );
        sigaction( SIGBUS, &segv_handler, &old_segv );
        sigaction( SIGABRT, &segv_handler, &old_segv );
        sigaction( SIGALRM, &segv_handler, &old_segv );
        sigaction( SIGPIPE, &segv_handler, &old_segv );

        // Handling SIGCHLD potentially gets flashplayer into a state where it can only be killed
        // by kill -9. Most likely this is due to some conflict with glibc.
        //sigaction( SIGCHLD, &segv_handler, &old_segv );

	int r = read_events( argv[1] );

	// Write last sent
	record_last_sent();

	return r;
}
