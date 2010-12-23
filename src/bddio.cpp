// $Id$
// bddio - block I/O replacement for dd
// Copyright (C) 2010 Chumby Industries, Inc. All rights reserved
// This is a replacement for some of the functionality of dd
// It adds some features like inter-block write delays and inter-erase
// block write delays.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#define VER_STR "0.17"

typedef struct _parameter {
	const char *name;
	int value;
} Parameter;

typedef struct _sparameter {
	const char *name;
	char *value;
} SParameter;

#define DEFPARAM(name,default_val) \
	Parameter p_##name = { #name, default_val }
#define DEFSPARAM(name,default_val) \
	SParameter p_##name = { #name, default_val }

DEFSPARAM( read, NULL );
DEFSPARAM( write, NULL );

DEFPARAM( blocksize, 0 );
DEFPARAM( eblocksize, 0 );
DEFPARAM( count, -1 );
DEFPARAM( skip, 0 );
DEFPARAM( seek, 0 );
DEFPARAM( blockwdelay, 0 );
DEFPARAM( eblockwdelay, 10 );
DEFPARAM( verbose, 0 );
DEFPARAM( tailpadding, 0 );

Parameter * iParm_list[] = {
	&p_blocksize,
	&p_eblocksize,
	&p_count,
	&p_skip,
	&p_seek,
	&p_blockwdelay,
	&p_eblockwdelay,
	&p_tailpadding
};
#define IPARM_COUNT (sizeof(iParm_list)/sizeof(iParm_list[0]))

SParameter * sParm_list[] = {
	&p_read,
	&p_write
};
#define SPARM_COUNT (sizeof(sParm_list)/sizeof(sParm_list[0]))

int main( int argc, char *argv[] )
{
	fprintf( stderr, "%s v" VER_STR "\n", argv[0] );
	fflush( stdout );
	int n;
	for (n = 1; n < argc; n++)
	{
		char paramName[256];
		char dash1, dash2;
		sscanf( argv[n], "%c%c%[^= ]", &dash1, &dash2, paramName );
		const char *szValue = argv[n];
		szValue += strcspn( szValue, " =\t" );
		szValue += strspn( szValue, " =\t" );
		if (!*szValue)
		{
			szValue = NULL;
		}
		int p;
		bool found = false;
		if (dash1 == '-' && dash2 == '-')
		{
			for (p = 0; p < IPARM_COUNT; p++)
			{
				if (!strcmp( iParm_list[p]->name, paramName ))
				{
					iParm_list[p]->value = atoi(szValue);
					found = true;
					break;
				}
			}
			for (p = 0; !found && p < SPARM_COUNT; p++)
			{
				if (!strcmp( sParm_list[p]->name, paramName ))
				{
					sParm_list[p]->value = strdup(szValue);
					found = true;
					break;
				}
			}
		}
		if (!found)
		{
			fprintf( stderr, "Invalid parameter %s (name=%s/value=%s)\n", argv[n], paramName, szValue );
			for (p = 0; p < IPARM_COUNT; p++)
			{
				fprintf( stderr, " --%s=<n>\n", iParm_list[p]->name );
			}
			for (p = 0; p < SPARM_COUNT; p++)
			{
				fprintf( stderr, " --%s=<s>\n", sParm_list[p]->name );
			}
			return -1;
		}
	}
	// Check for required parameter p_read or p_write
	if (p_read.value && p_write.value)
	{
		fprintf( stderr, "Error: read and write both specified\n" );
		return -1;
	}
	if (p_read.value == NULL && p_write.value == NULL)
	{
		fprintf( stderr, "Error: either read or write must be specified\n" );
		return -1;
	}

	// Check for required block size parameters
	if (p_blocksize.value <= 0 || p_eblocksize.value < p_blocksize.value)
	{
		fprintf( stderr, "Error: both blocksize and eblocksize must be specified; eblocksize must be >= blocksize\n" );
		return -1;
	}

	// Check for parameters required for read
	if (p_read.value != NULL && p_count.value == 0)
	{
		fprintf( stderr, "Error: count must be specified for read\n" );
		return -1;
	}

	// Open for read or write
	int fd;
	off_t seek_skip = 0;
	if (p_read.value)
	{
		if (p_verbose.value) fprintf( stderr, "Opening %s r/o\n", p_read.value );
		fflush( stderr );
		fd = open( p_read.value, O_RDONLY );
		if (fd < 0)
		{
			fprintf( stderr, "Error: failed to open %s r/o (errno=%d - %s)\n",
				p_read.value, errno, strerror(errno) );
			return -1;
		}
		seek_skip = p_skip.value * p_blocksize.value;
	}
	else
	{
		fd = open( p_write.value, O_RDWR | O_SYNC );
		if (fd < 0)
		{
			fprintf( stderr, "Error: failed to open %s r/w (errno=%d - %s)\n",
				p_write.value, errno, strerror(errno) );
			return -1;
		}
		seek_skip = p_seek.value * p_blocksize.value;
	}

	// Seek or skip
	if (seek_skip)
	{
		if (p_verbose.value) fprintf( stderr, "Seeking to offset %ld\n", (long)seek_skip );
		fflush( stderr );
		off_t seek_res = lseek( fd, seek_skip, SEEK_SET );
		if (seek_res != seek_skip)
		{
			fprintf( stderr, "Error: failed to seek to offset %ld (returned %ld, errno=%d - %s)\n",
				seek_skip, seek_res, errno, strerror(errno) );
			close( fd );
			return -1;
		}
	}

	// Allocate buffer and make it aligned on a 16k boundary
	unsigned char rawBuff[0xc000];
	unsigned char *buff = &rawBuff[0];
	unsigned long pageStartOffset = (unsigned long)buff;
	if (p_verbose.value) fprintf( stderr, "Original buffer start address 0x%08lx\n", pageStartOffset );
	fflush( stderr );
	pageStartOffset = (pageStartOffset + 0x3fffL) & 0xffffc000L;
	if (p_verbose.value) fprintf( stderr, "Using new address 0x%08lx\n", pageStartOffset );
	fflush( stderr );
	buff = (unsigned char *)pageStartOffset;
	memset( buff, 0xff, 0x4000 );
	if (p_verbose.value) fprintf( stderr, "Buffer starts at 0x%08lx\n", (unsigned long)buff );
	fflush( stderr );

	// If reading, read specified number of blocks
	// If writing, write all input from stdin
	int res = 0;
	bool done = false;
	unsigned long bytesRemaining = p_count.value * p_blocksize.value;
	unsigned long bytesWritten = 0L;

	while (!done)
	{
		// If reading, fill buffer
		if (p_read.value)
		{
			size_t bytesToRead = bytesRemaining;
			if (bytesRemaining > 0x4000)
			{
				bytesToRead = 0x4000;
			}
			size_t bytesRead = read( fd, buff, bytesToRead );
			if (bytesRead != bytesToRead)
			{
				if (bytesRead > 0)
				{
					write( 1, buff, bytesRead );
					bytesRemaining -= bytesRead;
				}
				fprintf( stderr, "Read failed: requested %ld bytes, read %ld, %ld remain unread (errno=%d - %s)\n",
					bytesToRead, bytesRead, bytesRemaining, errno, strerror(errno) );
				done = true;
				res = -1;
				break;
			}
			write( 1, buff, bytesRead );
			bytesRemaining -= bytesRead;
			if (bytesRemaining == 0)
			{
				if (p_verbose.value) fprintf( stderr, "Successfully read %ld bytes\n", p_blocksize.value * p_count.value );
				done = true;
				break;
			}
		}
		else
		{
			// Write one page at a time
			ssize_t bytesRead = read( 0, buff, p_blocksize.value );
			if (bytesRead < p_blocksize.value)
			{
				if (bytesRead < 0)
				{
					fprintf( stderr, "Error: could not read from stdin, errno=%d (%s)\n",
						errno, strerror(errno) );
					res = -1;
				}
				else if (bytesRead == 0)
				{
					if (p_verbose.value) fprintf( stderr, "Got end of input with %ld bytes written\n",
						bytesWritten );
				}
				else
				{
					if (bytesRead >= 512 && (bytesRead % 512) == 0)
					{
						fprintf( stderr, "Got end of input with %ld bytes written, writing remaining %ld bytes\n",
							bytesWritten, bytesRead );
						ssize_t writeCount = write( fd, buff, bytesRead );
						if (writeCount < bytesRead)
						{
							fprintf( stderr, "Error: write for %d bytes failed, errno=%d (%s)\n",
								bytesRead, errno, strerror(errno) );
							res = -1;
						}
						else
						{
							bytesWritten += writeCount;
							if (p_tailpadding.value)
							{
								fprintf( stderr, "Writing tail padding for %ld bytes\n", p_blocksize.value - bytesRead );
								memset( buff, 0xff, p_blocksize.value );
								writeCount = write( fd, buff, p_blocksize.value - bytesRead );
								if (writeCount < p_blocksize.value - bytesRead)
								{
									fprintf( stderr, "Error: tail padding write for %d bytes failed, errno=%d (%s)\n",
										p_blocksize.value - bytesRead, errno, strerror(errno) );
									res = -1;
								}
								// else do not update total bytesWritten to include tail padding
							}
						}
					}
					else
					{
						fprintf( stderr, "Got end of input with %ld bytes written - discarding remaining %ld bytes\n",
							bytesWritten, bytesRead );
					}
				}
				// If we have not written an even number of erase blocks, complain
				if (p_eblocksize.value > 0 && res == 0 && bytesWritten % p_eblocksize.value != 0)
				{
					fprintf( stderr, "** WARNING ** bytes written passes erase block size boundary by %ld bytes\n",
						(long)(bytesWritten % p_eblocksize.value) );
				}
				done = true;
				break;
			}
			ssize_t writeCount = write( fd, buff, p_blocksize.value );
			if (writeCount < p_blocksize.value)
			{
				fprintf( stderr, "Error: write for %d bytes failed, errno=%d (%s)\n",
					p_blocksize.value, errno, strerror(errno) );
				res = -1;
				done = true;
				break;
			}
			bytesWritten += writeCount;
			// Perform inter-page delay
			if (p_blockwdelay.value > 0)
			{
				usleep( p_blockwdelay.value * 1000L );
			}
			// Perform inter-erase block delay
			if (p_eblocksize.value > 0 && p_eblockwdelay.value > 0 &&
				(bytesWritten % p_eblocksize.value == 0))
			{
				usleep( p_eblockwdelay.value * 1000L );
			}
		}
	}

	close( fd );

	fflush( stderr );

	return res;
}

