/*
 * $Id$ usbkbd.cpp - ioctl test program for usb kbd devices
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

#include <linux/hiddev.h>
#include <linux/input.h>


#define VER_STR "0.29"

int
KbTest1( const char *deviceName )
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
  printf("hiddev driver version is %d.%d.%d\n",
	 version >> 16, (version >> 8) & 0xff, version & 0xff);

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
  printf("vendor 0x%04hx product 0x%04hx version 0x%04hx ",
          device_info.vendor, device_info.product, device_info.version);
  printf("has %i application%s ", device_info.num_applications,
         (device_info.num_applications==1?"":"s"));
  printf("and is on bus: %d devnum: %d ifnum: %d\n",
         device_info.busnum, device_info.devnum, device_info.ifnum);


	// Example 3
  /* suck out some device information */
  // Already done for example 2
  //ioctl(fd, HIDIOCGDEVINFO, &device_info);

 /* Now that we have the number of applications (in the
  * device_info.num_applications field),
  * we can retrieve them using the HIDIOCAPPLICATION ioctl()
  * applications are indexed from 0..{num_applications-1}
  */
  int appl;
  for (yalv = 0; yalv < device_info.num_applications; yalv++) {
    appl = ioctl(fd, HIDIOCAPPLICATION, yalv);
    if (appl > 0) {
	printf("Application %i is 0x%x ", yalv, appl);
	/* The magic values come from various usage table specs */
	switch ( appl >> 16)
	    {
	    case 0x01 :
		printf("(Generic Desktop Page)\n");
		break;
	    case 0x0c :
		printf("(Consumer Product Page)\n");
		break;
	    case 0x80 :
		printf("(USB Monitor Page)\n");
		break;
	    case 0x81 :
		printf("(USB Enumerated Values Page)\n");
		break;
	    case 0x82 :
		printf("(VESA Virtual Controls Page)\n");
		break;
	    case 0x83 :
		printf("(Reserved Monitor Page)\n");
		break;
	    case 0x84 :
		printf("(Power Device Page)\n");
		break;
	    case 0x85 :
		printf("(Battery System Page)\n");
		break;
	    case 0x86 :
	    case 0x87 :
		printf("(Reserved Power Device Page)\n");
		break;
	    default :
		printf("(Unknown page - needs to be added)\n");
	    }
    }
  }

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

  printf("Supported event types:\n");
  for (yalv = 0; yalv < EV_MAX; yalv++) {
      if (test_bit(yalv, evtype_bitmask)) {
	  /* this means that the bit is set in the event types list */
	  printf("  Event type 0x%02x ", yalv);
	  switch ( yalv)
	      {
			case EV_SYN :
				printf( " (SYN - event separator)\n" );
				break;
	      case EV_KEY :
		  printf(" (Keys or Buttons)\n");
		  break;
	      case EV_ABS :
		  printf(" (Absolute Axes)\n");
		  break;
	      case EV_LED :
		  printf(" (LEDs)\n");
		  break;
	      case EV_REP :
		  printf(" (Repeat)\n");
		  break;
	      default:
		  printf(" (Unknown event type: 0x%04hx)\n", yalv);
	      }
	    }
	}


	int inCount = 0;
    size_t read_bytes;  /* how many bytes were read */
    struct input_event ev[64]; /* the events (up to 64 at once) */
	static const char *kc[] = {
		// 0-0f (0-15)
		"?", "[Esc]", "1", "2",   "3", "4", "5", "6",   "7", "8", "9", "0",   "-", "=", "[Bksp]", "[Tab]",
		// 10-1f (16-31)
		"Q", "W", "E", "R",   "T", "Y", "U", "I",   "O", "P", "[", "]",   "[Enter]\n", "[LCtrl]", "A", "S",
		// 20-2f (32-47)
		"D", "F", "G", "H",   "J", "K", "L", ";",   "'", "`", "[LShift]", "\\",   "Z", "X", "C", "V",
		// 30-3f (48-63)
		"B", "N", "M", "<",   ">", "/", "[RShift]", "[*Num]",   "[LAlt]", "[Space]", "[CapsLock]", "[F1]",   "[F2]", "[F3]", "[F4]", "[F5]",
		// 40-4f (64-79)
		"[F6]", "[F7]", "[F8]", "[F9]",   "[F10]", "[NumLock]", "[ScrlLock]", "[HomeNum]",   "[UpNum]", "[PgUpNum]", "[-Num]", "[LeftNum]",   "[Num5]", "[RightNum]", "[+Num]", "[EndNum]",
		// 50-5f (80-95)
		"[DownNum]", "[PgDnNum]", "[InsNum]", "[DelNum]",   "?", "?", "?", "[F11]",   "[F12]", "?", "?", "?",   "?", "?", "?", "?",
		// 60-6f (96-111)
		"[EnterNum]", "[RCtrl]", "[/Num]", "[PrtScr]",   "[RAlt]", "?", "[Home]", "[Up]",   "[PgUp]", "[Left]", "[Right]", "[End]",   "[Down]", "[PgDn]", "[Ins]", "[Del]",
		// 70-7f (112-127)
		"?", "[Mute]", "[Vol-]", "[Vol+]",   "[Power]", "?", "?", "[Break]",   "?", "?", "?", "?",   "?", "[Win]", "?", "[Click]",
		// 80-8f
		"?", "?", "?", "?",   "?", "?", "?", "?",   "[Search]", "?", "?", "?",   "[Calc]", "?", "?", "?",
		// 90-9f
		"[Term??]", "?", "?", "?",   "?", "?", "?", "?",   "?", "?", "?", "[Email]",   "[NewFolder]", "?", "[MediaReverse]", "[MediaFwd]",
		// a0-af
		"?", "?", "?", "[MediaEnd]",   "[Pause/Play]", "[MediaStart]", "[MediaStop]", "?",   "?", "?", "?", "?",   "[Web]", "?", "?", "?"
	};
	#define KC_SIZE (sizeof(kc)/sizeof(kc[0]))
	#define KS_SHIFT 0x01
	#define KS_CTRL 0x02
	#define KS_ALT 0x04
	static unsigned char ks[] = {
		// 0-f
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0,
		// 10-1f
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, KS_CTRL, 0, 0,
		// 20
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, KS_SHIFT, 0, 			0, 0, 0, 0,
		// 30
		0, 0, 0, 0,			0, 0, KS_SHIFT, 0,				KS_ALT, 0, 0, 0, 			0, 0, 0, 0,
		// 40
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0,
		// 50
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0,
		// 60
		0, KS_CTRL, 0, 0,			KS_ALT, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0,
		// 70, 80, 90, a0
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0,
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0,
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0,
		0, 0, 0, 0,			0, 0, 0, 0,				0, 0, 0, 0, 			0, 0, 0, 0
	};
	#define KS_SIZE (sizeof(ks)/sizeof(ks[0]))
	unsigned char curState = 0;

	printf( "If you have a keyboard attached with usbkbd.ko loaded, type stuff on the keyboard.\n\
End with Ctrl-Alt-Del or the keyboard Power button (only for event2)\n\
v" VER_STR "\n" );

	bool exitRequested = false;
   while (!exitRequested)
	{
		inCount++;
		int
	read_bytes = read(fd, ev, sizeof(struct input_event) * 64);

	if (read_bytes < (int) sizeof(struct input_event)) {
	    perror("evtest: short read");
	    return 1;
	}

	for (yalv = 0; yalv < (int) (read_bytes / sizeof(struct input_event)); yalv++)
	    {
	    	// type==1 for key event
	    	// value==1 for key press, 0 for release
	    	// codes are mapped in kc and ks above
	    	/**
		printf("[%ld.%06ld] type %d, code %02x, value %d\n",
		       ev[yalv].time.tv_sec, ev[yalv].time.tv_usec, ev[yalv].type,
		       ev[yalv].code, ev[yalv].value);
		     **/
	    	if (ev[yalv].type == 0) continue;
	    	if (ev[yalv].type != 1 || ev[yalv].value > 2 || ev[yalv].code > KS_SIZE)
	    	{
	    	//
		printf("Event: time %ld.%06ld, type %d, code %02x, value %d\n",
		       ev[yalv].time.tv_sec, ev[yalv].time.tv_usec, ev[yalv].type,
		       ev[yalv].code, ev[yalv].value);
		       continue;
		    }
		    if (ev[yalv].value == 2)
		    {
		    	// Key repeat event
		    	continue;
		    }
			// Check for state changes
			int iCode = ev[yalv].code;
			unsigned char newState = ks[iCode];
			if (newState)
			{
				unsigned char prevState = curState;
				curState &= ~newState;
				curState |= (newState & (ev[yalv].value*0xffff));
				if (curState != prevState)
				{
					//printf( "[%x->%x-%x-%x]<%s%s>", newState, prevState, prevState & ~newState, curState, ev[yalv].value ? "" : "/", kc[iCode] );
					printf( "<%s%s>", ev[yalv].value ? "" : "/", kc[iCode] );
				}
			}
			else if (ev[yalv].value == 1) // For now, we're not handling held keys or release events
			{
				if (kc[iCode][0] == '?')
				{
					printf( "%s{%02x}", kc[iCode], iCode );
				}
				else
				{
					printf( "%s", kc[iCode] );
				}
				// Check for Del with Ctrl+Alt
				if (curState == (KS_CTRL|KS_ALT) && (iCode == 0x6f || iCode == 0x53))
				{
					exitRequested = true;
					break;
				}
				// Check for power button
				if (iCode == 0x74)
				{
					exitRequested = true;
					break;
				}
			}
			fflush( stdout );
	    }
	}


	printf( "End keyboard test\n" );


	close( fd );

	return 0;
}

int
main( int argc, char *argv[] )
{
	printf( "usbkbd test v" VER_STR "\n" );
	if (argc < 2)
	{
		printf( "Syntax:\n%s device\n\twhere device is something like /dev/input/event0\n", argv[0] );
		return -1;
	}
	if (access( argv[1], 0 ) != 0)
	{
		int devMajor = 13;
		int devMinor = 65; // Default for input/event1
		// Get major:minor from /sys/class/input/event<n>
		if (!strncmp( argv[1], "/dev", 4 ))
		{
			char sysdev[128];
			sprintf( sysdev, "/sys/class%s/dev", &argv[1][4] );
			FILE *sysdev_f = fopen( sysdev, "r" );
			if (sysdev_f)
			{
				int nMajor, nMinor;
				char majmin[256];
				if (fgets( majmin, sizeof(majmin), sysdev_f ))
				{
					char *sMajor = strtok( majmin, ":\r\n" );
					char *sMinor = strtok( NULL, "\r\n" );
					if (sMajor != NULL && sMinor != NULL)
					{
						nMajor = atoi( sMajor );
						nMinor = atoi( sMinor );
						if (nMajor > 0 && nMinor > 0)
						{
							devMajor = nMajor;
							devMinor = nMinor;
						}
						else printf( "Major:minor of %d:%d invalid\n", nMajor, nMinor );
					}
					else printf( "Failed to get major and minor\n" );
				}
				else printf( "Failed to read from %s\n", sysdev );
			}
			else printf( "Unable to open %s\n", sysdev );

		}
		printf( "Creating device node %s\n", argv[1] );
		char buff[1024];
		sprintf( buff, "mknod %s c %d %d", argv[1], devMajor, devMinor );
		printf( "%s\n", buff );
		system( buff );
	}
	return KbTest1( argv[1] );
}
