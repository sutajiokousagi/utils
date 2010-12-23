// proc_kmsg_cat.cpp - gets lines from /proc/kmsg and spits out
// gettimeofday() before each line

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

int main( int argc, char *argv[] )
{
	FILE *f = fopen( "/proc/kmsg", "r" );
	if (!f)
	{
		fprintf( stderr, "%s: could not open /proc/kmsg, errno=%d\n", argv[0], errno );
		return -1;
	}

	fprintf( stderr, "%s reading /proc/kmsg - ctrl-C to abort...\n", argv[0] );
	struct timeval tv;
	char buff[16384];
	unsigned long serial = 0;
	// This will block
	while (fgets( buff, sizeof(buff)-1, f ))
	{
		buff[sizeof(buff)-1] = '\0';
		char *nl = strrchr( buff, '\n' );
		if (nl) *nl = '\0';
		gettimeofday( &tv, NULL );
		printf( "[%lu.%06lu] [%lu] %s\n", tv.tv_sec, tv.tv_usec, serial++, buff );
		fflush( stdout );
	}

	fclose( f );

	return 0;
}
