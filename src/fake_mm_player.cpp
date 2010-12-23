// $Id$
// fake_mm_player - fake multimedia player
// Takes a container file and pretends to do something with it

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

int g_state = 1; // 1 = playing, 0 = paused, -1 = killed

void mySignalHandler( int sigNum );

// Signal handler. Currently we handle SIGINT and by default, all signals are handled once only.
// We need to set SIGINT to SIG_IGN after the first press
void mySignalHandler( int sigNum )
{
	switch (sigNum)
	{
		case SIGINT:
		case SIGTERM:
				// Make sure the next one doesn't terminate
				signal( sigNum, SIG_IGN );
				// Request exit
				g_state = -1;
				break;
		case SIGUSR1:
			g_state = 0; // Pause
			// reassert
			signal( sigNum, mySignalHandler );
			break;
		case SIGUSR2:
			g_state = 1; // Resume
			// reassert
			signal( sigNum, mySignalHandler );
			break;
		default:
			printf( "ERROR - unhandled signal %d\n", sigNum );
			break;
	}
}

int
main( int argc, char *argv[] )
{
	if (argc < 2)
	{
		printf( "ERROR - no filename passed to %s\n", argv[0] );
		return -1;
	}

	// Trap SIGTERM and SIGINT to exit
	signal( SIGTERM, mySignalHandler );
	signal( SIGINT, mySignalHandler );

	// Allow SIGUSR1 to pause
	signal( SIGUSR1, mySignalHandler );

	// Allow SIGUSR2 to resume
	signal( SIGUSR2, mySignalHandler );

	system( "imgtool --fill=180,220,100 > /dev/null" );

	char fbuff[8192];
	sprintf( fbuff, "fbwrite \"\nStarting %s\n\n\"", argv[1] );
	system( fbuff );

	printf( "START %s\n", argv[1] );
	fflush( stdout );
	printf( "PID %d\n", getpid() );
	fflush( stdout );

	int n;
	for (n = 0; n < 30; n++)
	{
		sleep( 1 );
		if (g_state == 0)
		{
			strcpy( &fbuff[strlen(fbuff)-1], "Pause \"" );
			system( fbuff );
			printf( "PAUSED\n" );
			fflush( stdout );
			while (g_state == 0)
			{
				sleep( 1 );
			}
			strcpy( &fbuff[strlen(fbuff)-1], "Resume \"" );
			system( fbuff );
			printf( "RESUMED\n" );
			fflush( stdout );
		}
		else if (g_state < 0)
		{
			strcpy( &fbuff[strlen(fbuff)-1], "\nKILLED! \"" );
			system( fbuff );
			printf( "TERMINATED\n" );
			fflush( stdout );
			break;
		}
		else
		{
			strcpy( &fbuff[strlen(fbuff)-1], ".\"" );
			system( fbuff );
		}
	}

	printf( "END %s\n", argv[1] );

	if (g_state == 1)
	{
		strcpy( &fbuff[strlen(fbuff)-1], "\n\nDone\"" );
		system( fbuff );
	}

	return 0;
}

