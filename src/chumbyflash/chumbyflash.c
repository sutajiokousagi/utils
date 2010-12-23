/*
 *  chumbyflash.c
 *
 * Ken Steele
 * Copyright (c) Chumby Industries, 2007-2009
 *
 * chumbyflash is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * chumbyflash is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with chumbyflash; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <asm/types.h>
#include <mtd/mtd-user.h>

#include "getopt.h"
#include "fbtext.h"

#define VER_STR "1.48"
#define REV_STR "$Rev::      $"
#define MAKE_VER(buff) sprintf(buff, VER_STR " r%-5.5s", REV_STR + 7 )

// Needs to be initialized in main()
char g_ver[128];

// Define to use Ken's method for bad block detection - may be ironforge-specific?
// More likely, specific to small-block NAND, also possibly specific to the NAND
// flash controller on the imx21
//#define ALTERNATE_BB_DETECT

static mtd_info_t meminfo;

#define NAND_PAGE_SIZE  meminfo.writesize //512   // in bytes
#define NAND_BLOCK_SIZE meminfo.erasesize //16384 // in bytes

// Microseconds to wait between erase blocks when writing
#define INTER_BLOCK_USECS	(g_inter_block_msecs*1000)
// Microseconds to wait between pages when writing
#define INTER_PAGE_USECS	(g_inter_page_msecs*1000)

unsigned int g_inter_block_msecs = 20;
unsigned int g_inter_page_msecs = 2;
int g_totalBlocks        = 0;

// Map of bad blocks from start of partition (not from g_startOffset)
unsigned long* badBlocks;
int            bbCounter = 0;

unsigned char *writebuf = NULL;
unsigned char *readbuf = NULL;
unsigned char *oobbuf = NULL;

// Offset from start of partition as optionally specified by -s
// We skip an offset based on the sum of previous bad blocks in all preceding
// partitions, and need to ensure that the space from the start of the
// partition up to the offset is erased. The boot loader depends on 0xff skipping.
unsigned long g_startOffset = 0;

// Silent mode means no thermometer updates
int g_silent = 0;

// Log summary actions to /tmp/chumbyflash.log
void log_actions( const char *fmt, ... )
{
	FILE *fLog = fopen( "/tmp/chumbyflash.log", "a" );
	va_list args;
	if (!fLog)
	{
		return;
	}
	va_start( args, fmt );
	vfprintf( fLog, fmt, args );
	fclose( fLog );
	va_end( args );
}

void reportBadBlocks( char* mtdName )
{
    FILE* fp = fopen( "/tmp/badblocks", "a" );
    fprintf( fp, "%s:%d\n", mtdName, bbCounter );
    fclose( fp );
}

// Report newly discovered bad blocks
void reportNewBadBlocks( char *mtdName, int known_bad_blocks, int found_bad_blocks )
{
	FILE *fp = fopen( "/tmp/new_badblocks", "a" );
	fprintf( fp, "%s:%d\n", mtdName, found_bad_blocks );
	fclose( fp );
}

// WIDTH and HEIGHT are screen dimensions passed in at compile time
// g_width and g_height are initialized from environment or /psp/video_res in fbtext_init()

// Ironforge (w=320 h=240) used a thermometer at
//	top		left	bottom	right		height	width
//	144		21		161		289			17		268
//	60%		6.5625%	67.083%	90.3125%	7.083%	83.75%

// Thermometer margin offsets
#define THERMO_LEFT_MARGIN_PCT	0.065625
#define THERMO_TOP_MARGIN_PCT	0.60
#define THERMO_BOTTOM_MARGIN_PCT 0.670833333
#define THERMO_RIGHT_MARGIN_PCT	0.903125
#define THERMO_HEIGHT_PCT	0.07083333
#define THERMO_WIDTH_PCT	0.8375
static int g_leftOff, g_topOff, g_bottomOff, g_rightOff, lastBarValue;
void updateThermometerOffsets()
{
	g_leftOff = lastBarValue = (int)(g_width * THERMO_LEFT_MARGIN_PCT);
	g_topOff = (int)(g_height * THERMO_TOP_MARGIN_PCT);
	g_bottomOff = (int)(g_height * THERMO_BOTTOM_MARGIN_PCT);
	g_rightOff = (int)(g_width * THERMO_RIGHT_MARGIN_PCT);
}

enum {
	PROGRESS_NORMAL=0,
	PROGRESS_BAD,
	PROGRESS_LOADING
};
void updateProgressBar( int perc, int color )
{
	if (!g_silent)
	{
		int barValue;
		int r, g, b;
		if (perc > 100) perc = 100;
		barValue = (int)( g_width * THERMO_LEFT_MARGIN_PCT + g_width * THERMO_WIDTH_PCT * ( perc * 0.01 ) );
		// color is either 0 for normal progress (blue), 1 for bad (red) or 2 for loading (light blue)
		switch (color)
		{
			case PROGRESS_NORMAL:	// Normal
			default:
				r = 0;
				g = 71;
				b = 182;
				break;
			case PROGRESS_BAD: // Bad
				r = 153;
				g = 51;
				b = 9;
				break;
			case PROGRESS_LOADING: // Loading
				r = 0;
				g = 121;
				b = 241;
				break;
		}
		fbtext_fillrect( g_topOff, lastBarValue, g_bottomOff, barValue, r, g, b );
		lastBarValue = barValue;
	}
}

void clearProgressBar( int noErase )
{
	if (!g_silent)
	{
		if (!noErase)
		{
			fbtext_eraserect( g_topOff - 2, g_leftOff - 2, g_bottomOff + 2, g_rightOff + 2 );
		}
		lastBarValue = g_leftOff;
	}
}

void DEBUG( char* szString )
{
    printf( "DEBUG: %s\n", szString );
    fbtext_printf( "DEBUG: %s\n", szString );
}

void printHex( unsigned char* buffer, int length )
{
   int i = 0;

    for( i = 0; i < length; ++i )
    {
        printf( "%2x ", buffer[i] );
        if( i % 16 == 0 )
        {
            printf( "\n" );
        }
    }
    printf( "\n" );
}

int verifyWrite( int* fd, unsigned char* readbuffer, unsigned char* writebuffer, int length )
{
    int i       = 0;
    int compare = 0;

    pread( *fd, readbuffer, length, 0 );

    //printHex( readbuf, cnt );
    //printHex( writebuffer, cnt );

    for( i = 0; i < length; ++i )
    {
        printf( "%x:%x ", readbuf[i], writebuf[i] );
        compare = memcmp( readbuf + i, writebuf + i, 1 );
        if( 0 != compare )
        {
            return compare;
        }
    }

    return 0;
}

//
// returns offset to the begining of the current NAND erase block
//
int getCurrentBlockOffset( unsigned long offset )
{
	// hgroover: was 0xffffc000 = 16kb blocks
    return ( offset & ~(meminfo.erasesize-1) );
}


//
// returns numeric value of the current NAND block number
// from the specified g_startOffset. This is a relative block number
// and cannot be used to index the bad block lookup table.
//
int getCurrentBlockNumber( unsigned long offset )
{
	if (offset < g_startOffset)
	{
		printf( "\n%s ERROR: will return negative value, offset = %lx, g_startOffset = %lx\n", __FUNCTION__, offset, g_startOffset );
	}
    return ( getCurrentBlockOffset( offset ) / NAND_BLOCK_SIZE ) - ( getCurrentBlockOffset( g_startOffset ) / NAND_BLOCK_SIZE );
}

// Get absolute block number from start of partition
int getAbsoluteBlockNumber( unsigned long offset )
{
	return getCurrentBlockOffset( offset ) / NAND_BLOCK_SIZE;
}


// 1 if offset is in a bad block. Note that the bad block lookup table is absolute
// with respect to the start of the partition.
int isBadBlock( unsigned long offset )
{
	int idx = getAbsoluteBlockNumber( offset );
	if (idx >= g_totalBlocks)
	{
		printf( "\n%s ERROR: invalid index %d total blocks %d for offset 0x%lx\n", __FUNCTION__, idx, g_totalBlocks, offset );
		return 1;
	}
	return badBlocks[idx];
}

// Mark block bad in lookup table. Note that bad block lookup table is absolute
// with respect to the start of the partition.
void setBadBlock( unsigned long offset )
{
	int idx = getCurrentBlockOffset( offset ) / NAND_BLOCK_SIZE;
	if (idx >= g_totalBlocks)
	{
		printf( "\n%s ERROR: invalid index %d total blocks %d for offset 0x%lx\n", __FUNCTION__, idx, g_totalBlocks, offset );
		return;
	}
	badBlocks[idx] = 1;
}

//
// returns offset to the next NAND block
//
int getNextBlockOffset( unsigned long offset )
{
    return ( getCurrentBlockOffset( offset ) + NAND_BLOCK_SIZE );
}


//
// returns an offset to the next NAND block that is not
// marked bad
//
int getNextCleanBlock( unsigned long offset )
{
    offset = getNextBlockOffset( offset );

    if( !isBadBlock( offset ) )
    {
        return offset;
    }
    else
    {
        while( isBadBlock( offset ) )
        {
            printf( "DEBUG: block %d is ALSO BAD, skipping to next block.\n", getAbsoluteBlockNumber( offset ) );
            //fbtext_printf( "DEBUG: block %d is ALSO BAD, skipping to next block.\n", getCurrentBlockNumber( offset ) );

            offset = getNextBlockOffset( offset );
        }
        return offset;
    }
}


// Perform bad block scan starting at offset, with adjOffset being the first block we'll write
// to. Return the updated adjOffset (which may have additional bad blocks found before it).
unsigned long badBlockScan( int* fd, unsigned long offset, unsigned long adjOffset, int totalBlocks )
{
    int i                    = 0;
    struct mtd_oob_buf oob	 = {0, meminfo.oobsize, oobbuf};
    int increment            = 1;
    unsigned long long blockstart = 1;

    // if the adjusted offset is 0, that means there are no bad blocks prior
    // to this partition, so do NOT increment the adjusted offset.
    // The blocks between offset and adjOffset (if they re different) would
    // be bad blocks NOT included in the -s offset passed in at startup,
    // and thus need to reflect in the write offset we use
    if( 0 == adjOffset )
    {
        increment = 0;
    }

	printf( "Scanning for bad blocks (%d blocks X %dkb @ %lx adj %lx inc %d)..\n",
		totalBlocks, meminfo.erasesize>>10, offset, adjOffset, increment );
    //fbtext_printf( "Scanning for bad blocks..\n" );
    for( i = 0; i < totalBlocks; ++i )
    {
#ifdef ALTERNATE_BB_DETECT
        oob.start = getCurrentBlockOffset( offset );

		// Read OOB data and exit on failure
		if( ioctl( *fd, MEMREADOOB, &oob ) != 0 )
		{
			perror( "ioctl(MEMREADOOB)" );
		}

        if( oobbuf[5] != 0xFF )
#else
		int badblock;
		blockstart = offset & ~(meminfo.erasesize - 1);
		if ((badblock = ioctl(*fd, MEMGETBADBLOCK, &blockstart)) < 0) {
			perror("ioctl(MEMGETBADBLOCK)");
			break;
		}
		if (badblock)
#endif
        {
            // found a bad block
            printf( "Block %d [0x%08x] is bad.\n", i, offset );
            //fbtext_printf( "Block %d [0x%08x] is bad.\n", i, offset );

            /* printf( "Block %d OOB: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                    i, oobbuf[0],  oobbuf[1],  oobbuf[2],  oobbuf[3], oobbuf[4],  oobbuf[5],
                       oobbuf[6],  oobbuf[7],  oobbuf[8],  oobbuf[9], oobbuf[10], oobbuf[11],
                       oobbuf[12], oobbuf[13], oobbuf[14], oobbuf[15] );
            */

            if( ( i <= getAbsoluteBlockNumber( adjOffset ) ) && ( increment ) )
            {
                printf( "incrementing from page [0x%08x] to next aligned block", adjOffset );
                adjOffset = getCurrentBlockOffset( adjOffset + NAND_BLOCK_SIZE );
                printf( " at [0x%08x]\n", adjOffset );
				log_actions( "bad block %d @0x%lx new adj 0x%lx\n", i, offset, adjOffset );
            }
            else
            {
				log_actions( "bad block %d @0x%lx adj 0x%lx\n", i, offset, adjOffset );
            }

            badBlocks[i] = 1;
            ++bbCounter;
        }

        offset += NAND_BLOCK_SIZE;
    }
    printf( "Bad block scan complete, total = %d.\n" );
    log_actions( "%d bad blocks\n", bbCounter );
    //fbtext_printf( "Bad block scan complete.\n" );

    return adjOffset;
}

void blockMarkBad( int* fd, int offset, struct mtd_oob_buf* oob )
{
#ifdef ALTERNATE_BB_DETECT
    oob->start = getCurrentBlockOffset( offset );
    oob->length = meminfo.oobsize;
	oobbuf[5] = 0x00;

	// write OOB data
	if( ioctl( *fd, MEMWRITEOOB, oob ) != 0 )
	{
		perror( "ioctl(MEMWRITEOOB)" );
        exit( __LINE__ );
	}
/*
    printf( "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            oobbuf[0],  oobbuf[1],  oobbuf[2],  oobbuf[3], oobbuf[4],  oobbuf[5],
            oobbuf[6],  oobbuf[7],  oobbuf[8],  oobbuf[9], oobbuf[10], oobbuf[11],
            oobbuf[12], oobbuf[13], oobbuf[14], oobbuf[15] );


	// Read OOB data and exit on failure
	if( ioctl( *fd, MEMREADOOB, &noob ) != 0 )
	{
		perror( "ioctl(MEMREADOOB)" );
	}

    printf( "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            noobbuf[0],  noobbuf[1],  noobbuf[2],  noobbuf[3], noobbuf[4],  noobbuf[5],
            noobbuf[6],  noobbuf[7],  noobbuf[8],  noobbuf[9], noobbuf[10], noobbuf[11],
            noobbuf[12], noobbuf[13], noobbuf[14], noobbuf[15] );
*/
#else

	loff_t bad_addr = offset & ~(meminfo.erasesize - 1);
	printf("Marking block at %08lx (page-aligned) bad\n", (long)bad_addr);
	if (ioctl(*fd, MEMSETBADBLOCK, &bad_addr)) {
		perror("MEMSETBADBLOCK");
		/* But continue anyway */
	}

#endif
	// Update bad block table
	setBadBlock( offset );
}


void eraseBlocks( int* fd, unsigned long offset, int eraseLen, int totalBlocks )
{

    int i = 0;

    erase_info_t erase;

    if (totalBlocks > 1)
    {
    	printf( "Erasing %d blocks eraseLen=%d starting at 0x%lx..\n", totalBlocks, eraseLen, offset );
    	log_actions( "erasing %d blocks eraseLen=%d start offset 0x%lx\n", totalBlocks, eraseLen, offset );
    }
    else
    {
    	log_actions( "era %d l=%d @%lx\n", totalBlocks, eraseLen, offset );
    }
    //fbtext_printf( "Erasing required blocks..\n" );
    for( i = 0; i < totalBlocks; ++i )
    {
        printf( "erasing block: %d [0x%08x]\r", i, offset );
        //fbtext_printf( "erasing block: %d [0x%08x]\r", i, offset );

		erase.start  = offset;
		erase.length = eraseLen;

        if( !isBadBlock( offset ) )
        {
            if( ioctl( *fd, MEMERASE, &erase ) != 0 )
            {
                perror("\nMTD Erase failure\n");
            }
        }
        else
        {
            printf( "\nSkipping erase of block %d because it is BAD.\n", getAbsoluteBlockNumber( offset ) );
            //fbtext_printf( "\nSkipping erase of block %d because it is BAD.\n", getCurrentBlockNumber( offset ) );
            log_actions( "era skip BAD idx %d block %d @%lx\n", i, getAbsoluteBlockNumber( offset ), offset );
        }

        offset += NAND_BLOCK_SIZE;
    }
    if (totalBlocks > 1) printf( "\nBlock erasing complete.\n" );
    //fbtext_printf( "\nBlock erasing complete.\n" );

}



int main( int argc, char **argv )
{
	unsigned long offset   = 0;
	unsigned long start_offset = 0;

    int i                  = 0;
    int c                  = 0;
	int cnt                = 0;
    int fd;
    // Default to stdin
	int ifd                = 0;
	int totalWriteBlocks   = 0;
    int currentBlock       = 0;
    int eb                 = 0;
    int pageWritten        = 0;
    int bytesWritten       = 0;
    int bbScan             = 0;
    int currentBlockNumber = 0;
    int skipWrites		   = 0;
    int mtd_bug_workaround = 0;
    int input_read_failed  = 0;
    int unpadded_mtd_write = 0;
    int validate_write     = 0;
    int start_of_block;

    char* mtdName     = NULL;
    char* fileName    = NULL;
    unsigned char *input_buff = NULL;
    int input_file_size = 0;
    int input_buff_size = 0;
    int input_buff_offset = 0;
    int known_bad_blocks = 0; // Number already known and passed in via -k
    int found_bad_blocks = 0; // Number found as a result of failed write / failed verify
    int badBlockTemp;

	struct mtd_oob_buf oob; // = { 0, 16, oobbuf };

	MAKE_VER(g_ver);
	printf( "Chumbyflash v%s\n", g_ver );

    if( argc < 4 )
    {
        printf( "usage: %s [options] -m <mtdname>\n" \
				"  where [options] is one of the following:\n" \
				"\t-s <start address> - starting offset\n" \
                "\t-f <filename> - specify file to read image from\n" \
                "\t-e            - erase blocks prior to writing\n" \
                "\t-b            - perform bad block scan\n" \
                "\t-x            - no writes\n" \
                "\t-w            - workaround mtd bug (ironforge)\n" \
                "\t-u            - unpadded data writes\n" \
                "\t-v            - validate data after writing\n"
                "\t-p <msecs>    - inter-page delay in ms (default=2)\n"
                "\t-o <msecs>    - inter-block delay in ms (default=20)\n"
                "\t-k <count>    - count of bad blocks known on entry\n"
                "\t-q			 - quiet (silent) mode - no progress updates\n"
					, argv[0] );
        exit( 1 );
    }

	fbtext_init();
	updateThermometerOffsets();

    while( ( c = getopt( argc, argv, "m:s:n:f:bexwuvp:o:k:q" ) ) != EOF )
    {
        switch( c )
        {
            case 'm':
                mtdName = optarg;
                break;
            case 's':
                offset = strtoul( optarg, NULL, 0 );
                break;
            case 'f':
                fileName = optarg;
                break;
            case 'e':
                eb = 1;
                break;
			case 'x':
				skipWrites = 1;
				break;
			case 'b':
				bbScan = 1;
				break;
			case 'w':
				mtd_bug_workaround = 1;
				break;
			case 'u':
				unpadded_mtd_write = 1;
				break;
			case 'v':
				validate_write = 1;
				break;
			case 'p':
				g_inter_page_msecs = atoi(optarg);
				break;
			case 'o':
				g_inter_block_msecs = atoi(optarg);
				break;
			case 'k':
				known_bad_blocks = atoi(optarg);
				break;
			case 'q':
				g_silent = 1;
				break;
            default:
                exit(0);
        }
    }

	start_offset = offset;
	g_startOffset = offset;

    if( NULL != fileName )
    {
        if( ( ifd = open( fileName, O_RDONLY ) ) == -1 )
        {
            perror( "open input file" );
            exit( 1 );
        }
    }

	// Open the device
	if( ( fd = open( mtdName, O_RDWR ) ) == -1 )
	{
		perror( "Failed to open MTD device\n" );
		exit( 1 );
	}

	// get MTD device capability struct
	if( ioctl( fd, MEMGETINFO, &meminfo ) != 0 )
	{
		perror( "MEMGETINFO" );
		close( fd );
		exit( 1 );
	}

    g_totalBlocks = totalWriteBlocks = (int)(meminfo.size / NAND_BLOCK_SIZE);

	// Make sure device page sizes are valid
	if (!(meminfo.oobsize == 16 && meminfo.writesize == 512) &&
		!(meminfo.oobsize == 8 && meminfo.writesize == 256) &&
		//!(meminfo.oobsize == 32 && meminfo.writesize == 1024) &&
		!(meminfo.oobsize == 64 && meminfo.writesize == 2048))
	{
		printf( "Unknown flash (not normal NAND)\n" );
		close( fd );
		exit( 1 );
	}

    fbtext_clear();
	// Allocate input buffer if validating. Validation may be done on a separate pass
	// for really large datasets (>64mb).
	if (validate_write)
	{
		// Start with 4mb
		int input_buff_chunk_size = 4 * 1024 * 1024;
		printf( "Reading data into memory from handle %d for write validation...\n", ifd );
		input_buff_size = input_buff_chunk_size;
		input_buff = (unsigned char*)malloc( input_buff_size );
		if (input_buff == NULL)
		{
			printf( "ERROR: malloc() failed for 4mb - turning off write validation: errno=%d (%s)\n", errno, strerror(errno) );
			input_buff_size = 0;
			validate_write = 0;
		}
		else
		{
			// Grow in 1mb chunks
			ssize_t bytesRead;
			int totalWritePages;
			// Show progress assuming 256k == 1% since we're reading from an input pipe
			while ((bytesRead = read( ifd, &input_buff[input_file_size], input_buff_chunk_size )) > 0)
			{
				// Grow file size
				input_file_size += bytesRead;
				// This only works when the input pipe flushes! We'll just keep reading until we
				// get 0 bytes read...
				//// If less than requested, eof
				//if (bytesRead < input_buff_chunk_size)
				//{
				//	printf( "Got EOF with %d requested, %d read\n", input_buff_chunk_size, bytesRead );
				//	break;
				//}
				// Show loading progress
				updateProgressBar( (int)(input_file_size/0x40000), PROGRESS_LOADING );
				// Allocate another 1mb chunk
				input_buff_chunk_size = 1 * 1024 * 1024;
				input_buff_size += input_buff_chunk_size;
				input_buff = (unsigned char *)realloc( input_buff, input_buff_size );
				if (input_buff == NULL)
				{
					printf( "FATAL ERROR: realloc(%d) failed, errno=%d (%s)\n", input_buff_size, errno, strerror(errno) );
					printf( "Cannot continue - retry without -v\n" );
					exit( -1 );
				}
			}
			updateProgressBar( 100, PROGRESS_LOADING );
			totalWritePages = input_file_size / meminfo.writesize + (input_file_size % meminfo.writesize == 0 ? 0 : 1);
			totalWriteBlocks = input_file_size / meminfo.erasesize + (input_file_size % meminfo.erasesize == 0 ? 0 : 1);
			printf( "Final count: %d bytes read (0x%x) (%d pages, %d blocks)\n",
				input_file_size, input_file_size, totalWritePages, totalWriteBlocks );
			// Adjust buffer size downward
			if (input_file_size + meminfo.writesize < input_buff_size)
			{
				int old_input_buff_size = input_buff_size;
				input_buff_size = (input_file_size + meminfo.writesize) & ~(meminfo.writesize - 1);
				input_buff = (unsigned char *)realloc( input_buff, input_buff_size );
				if (input_buff == NULL)
				{
					printf( "FATAL ERROR: realloc(%d) failed, errno = %d (%s) - could not reduce from %d\n",
						input_buff_size, errno, strerror(errno), old_input_buff_size );
					exit( -1 );
				}
				printf( "Final buffer size: %d (0x%x)\n", input_buff_size, input_buff_size );
			}
			// Reset position counter but don't erase
			clearProgressBar( 1 );
		}
		printf( "Final size of file read from %d is %d (%dK)\n", ifd, input_file_size, input_file_size / 1024 );
	}

	// Allocate read and write buffers
	readbuf = (unsigned char *)malloc( meminfo.writesize * sizeof(unsigned char) );
	writebuf = (unsigned char *)malloc( meminfo.writesize * sizeof(unsigned char) );
	oobbuf = (unsigned char *)malloc( meminfo.oobsize * sizeof(unsigned char) );
	oob.start = 0;
	oob.length = meminfo.oobsize;
	oob.ptr = oobbuf;

	// allocate and initialize memory for badBlocks array
    badBlocks = (unsigned long*)malloc( sizeof( unsigned long ) * g_totalBlocks );
	if (readbuf == NULL || writebuf == NULL || oobbuf == NULL || badBlocks == NULL)
	{
		printf( "malloc() failure %d/%d badBlocks %d\n", meminfo.writesize, meminfo.oobsize, g_totalBlocks );
		close( fd );
		exit( 1 );
	}

    for( i = 0; i < g_totalBlocks; ++i )
    {
        badBlocks[i] = 0;
    }

    //fbtext_printf( "Flashing %s @ [0x%08x] for %d blocks\n", mtdName, offset, g_totalBlocks );

	log_actions( "chumbyflash v%s flashing %s @ 0x%lx for %d blocks, ifs=%d\n",
		g_ver, mtdName, offset, g_totalBlocks, input_file_size );

	// Get first non-bad block. Begin scanning from start of partition.
	// Bad blocks between actual start of partition and g_startOffset need to
	// increase starting offset for writes.
    offset = badBlockScan( &fd, 0, g_startOffset, g_totalBlocks );
    start_offset = offset;
    g_startOffset = offset;

	log_actions( "new offset after bad block scan: 0x%lx\n", offset );
    if( eb )
    {
    	// This was likely never a bug since g_startOffset was always 0
    	if (g_startOffset)
    	{
			printf( "Erase of %d blocks * 0x%x bytes is starting from offset 0 rather than from specified offset 0x%lx\n",
				g_totalBlocks, meminfo.erasesize, g_startOffset );
    	}
    	else
    	{
    		printf( "Erasing %d blocks * 0x%x bytes starting from 0x%lx\n",
				g_totalBlocks, meminfo.erasesize, g_startOffset );
    	}
        eraseBlocks( &fd, 0, meminfo.erasesize, g_totalBlocks );
    }

    // Read input data from STDIN one page at a time and attempt
    // to write directly to MTD block device.  If a bad block is
    // encountered, mark the block BAD in the OOB data, increment
    // the block offset to the next clean NAND block.

	start_of_block = 1;
	badBlockTemp = 0;
	while( skipWrites == 0 )
	{
        // Read page data from file or STDIN
        memset( writebuf, 0xff, meminfo.writesize );
        if (input_buff_size)
        {
        	if (input_buff_offset + meminfo.writesize < input_file_size)
        	{
        		cnt = meminfo.writesize;
        	}
        	else
        	{
        		cnt = input_file_size - input_buff_offset;
        	}
        	if (cnt > 0)
        	{
				memcpy( writebuf, &input_buff[input_buff_offset], cnt );
				input_buff_offset += cnt;
        	}
        }
        else
        {
			cnt = read( ifd, writebuf, meminfo.writesize );
        }
        if (cnt < 0)
        {
        	printf( "ERROR (%s:%d) - read failed for %d bytes, errno = %d (%s)\n", __FILE__, __LINE__, meminfo.writesize, errno, strerror(errno) );
        	input_read_failed = 1;
        	break;
        }
        if( cnt == 0 )
        {
            // EOF
            updateProgressBar( 100, PROGRESS_NORMAL );
            usleep( 500000 );
            break;
        }
		else
		{
			int attempt = 0;
			// Default behavior is to pad to meminfo.writesize
            while( ! pageWritten )
            {
                currentBlockNumber = getAbsoluteBlockNumber( offset );

                if( !isBadBlock( offset ) )
                {
                	ssize_t writeActual;
                	ssize_t writeSize = meminfo.writesize;
                	if (unpadded_mtd_write)
                	{
                		writeSize = cnt;
                	}

					// Erase if starting
					if (start_of_block)
					{
						if (eb)
						{
							eraseBlocks( &fd, offset, meminfo.erasesize, 1 );
						}
						start_of_block = 0;
					}

                    //printf( "Writing page to block %d, page offset [0x%08x]\r", currentBlockNumber, offset );
                    //fbtext_printf( "Writing page to block %d, page offset [0x%08x]\r", currentBlockNumber, offset );
                    //if( currentBlockNumber % 2 == 0 )
                    //{
                    //    //fbtext_printf( "Writing page to block %d, page offset [0x%08x]\r", currentBlockNumber, offset );
                    //    updateProgressBar( ( (float)currentBlockNumber / (float)totalWriteBlocks ) * 100, badBlockTemp );
                    //}

                    //printf( "Writing page to block %d, page offset [0x%08x]\n", currentBlockNumber, offset );
                    if (mtd_bug_workaround)
                    {
						// dummy read to workaround MTD bug
						pread( fd, readbuf, meminfo.writesize, 0 );
                    }

					writeActual = pwrite( fd, writebuf, writeSize, offset );
                    if( writeActual != writeSize )
                    {

                        perror( "pwrite" );
                        printf( "DEBUG: Encountered potentially bad page @ [0x%08x] in block %d.  (requested write %d actual %d). Marking block BAD.\n",
							offset, currentBlockNumber, writeSize, writeActual );
                        //
                        // TODO: reenable once OOB writes are fixed in MTD
                        //
						blockMarkBad( &fd, offset, &oob );
						found_bad_blocks++;

						usleep( INTER_PAGE_USECS );

                        // Increment address counter to next block
                        offset = getNextCleanBlock( offset );
                        pageWritten = 0;
                        badBlockTemp = PROGRESS_BAD;

                        // Erase next block
                        start_of_block = 1;

						// If more than 1mb of contiguous bad blocks, give up
						attempt++;
						if (attempt > 8)
						{
							printf( "Too many consecutive bad blocks\n" );
							break;
						}
                        // exit(1);
                    }
                    else
                    {
                        // successful write
                        //DEBUG( "page written" );
                        //printf( "cnt: %d, verify: %d\n", cnt, verifyWrite( &fd, readbuf, writebuf, cnt ) );
                        //verifyWrite( fd, writebuf, cnt );

                        pageWritten = 1;
                        bytesWritten += cnt;
						usleep( INTER_PAGE_USECS );
                    }
                }
                else
                {
                    printf( "Block %d [0x%08x] is BAD, skipping\n", getAbsoluteBlockNumber( offset ), offset );
                    //fbtext_printf( "\nBlock %d [0x%08x] is BAD, skipping.\n", getCurrentBlockNumber( offset ), offset );
                    offset = getNextCleanBlock( offset );
                    badBlockTemp = PROGRESS_BAD;
                }
            }
            pageWritten = 0;
		}

        // increment offset by one page
        offset += meminfo.writesize;

		// Are we at the end of a block?
        if (offset % meminfo.erasesize == 0)
        {
        	int validate_failed = 0;
        	int validate_page_compared = 0;
        	int validate_page_failed = 0;
        	unsigned long validate_offset = offset - meminfo.erasesize;
        	unsigned long validate_offset_org = validate_offset;
        	int terminate_line = 0;
        	// Report progress to console once per 1MB
        	if (offset % (1024 * 1024) == 0)
        	{
				printf( "[0x%08x - 0x%08x] end of block %d (%dMB)", validate_offset, offset, currentBlockNumber, offset / (1024 * 1024) );
				terminate_line = 1;
        	}
        	else
        	{
        		printf( "[0x%08x - 0x%08x] written", validate_offset, offset );
        	}
        	if (validate_write)
        	{
        		printf( "; validating" );
        		printf( "                    " );
        		printf( "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b" );
        	}
        	// Needed when we don't have a LF
        	fflush( stdout );
        	usleep( INTER_BLOCK_USECS );
        	// Need to erase next block
        	start_of_block = 1;
        	// If validating writes, read and compare the block we just wrote
        	if (validate_write)
        	{
        		// validate_offset points to the start of the block in NAND we've just written
        		// offset is the current offset for the start of the next block in NAND
        		// input_buff_offset is the current offset for the start of the next set of input data to write
				int page;
				int validation_completed = 1;
				int bytes_to_compare = meminfo.erasesize;
				int bytes_compared = 0;
				int input_buff_validate_offset = input_buff_offset - meminfo.erasesize;
				int match_count = 0;
				int mismatch_count = 0;
				if (input_buff_offset > input_file_size)
				{
					// We're not validating the entire block
					bytes_to_compare = input_file_size - input_buff_validate_offset;
				}
				for (page = 0; page < meminfo.erasesize / meminfo.writesize && validation_completed; page++)
				{
					int compare_bytes = meminfo.writesize;
					if (page % 4 == 0)
					{
						printf( "." );
						fflush( stdout );
					}
					ssize_t actual_nand;
					if (compare_bytes + bytes_compared > input_file_size)
					{
						compare_bytes = input_file_size - bytes_compared;
						if (compare_bytes == 0)
						{
							break;
						}
					}
					// We always have to read from nand in writesize chunks
					actual_nand = pread( fd, readbuf, meminfo.writesize, validate_offset );
					if (actual_nand < meminfo.writesize)
					{
						printf( " Failed nand read, returned %d requested %d block %d page %d nand block %d noff %d\n",
							actual_nand, meminfo.writesize, i, page, currentBlockNumber, validate_offset );
						validation_completed = 0;
						break;
					}
					if (memcmp( &input_buff[input_buff_validate_offset],
						readbuf, compare_bytes ))
					{
						printf( " Fail %db @ blk %d page %d soff %d (0x%x) nand offs %d (0x%x)\n",
							compare_bytes, currentBlockNumber, page,
							input_buff_validate_offset, input_buff_validate_offset,
							validate_offset, validate_offset );
						mismatch_count++;
						// Mark as bad in progress bar
						badBlockTemp = PROGRESS_BAD;
						if (mismatch_count > 100)
						{
							printf( "Too many mismatches, aborting compare\n" );
							validation_completed = 0;
						}
						else if (mismatch_count < 5)
						{
							int line, byte;
							printf( "offset   ------------ source -------------   | ============ NAND =============\n" );
							for (line = 0; line < 16; line++)
							{
								printf( "%08x: ", input_buff_validate_offset + line * 12 );
								for (byte = 0; byte < 12; byte++)
								{
									printf( "%02x ", input_buff[input_buff_validate_offset + line * 12 + byte] );
								}
								printf( "| " );
								for (byte = 0; byte < 12; byte++)
								{
									printf( "%02x ", readbuf[line*12 + byte] );
								}
								printf( "\n" );
							}
						}
					}
					else
					{
						match_count++;
					}
					validate_offset += meminfo.writesize;
					input_buff_validate_offset += meminfo.writesize;
					bytes_compared += compare_bytes;
				}
				validate_page_failed = mismatch_count;
				validate_page_compared = match_count + mismatch_count;
				validate_failed = (mismatch_count > 0) ? 1 : 0;
        	}
        	// If validation attempt failed, mark this block bad and try again
        	if (validate_failed)
        	{
        		int already_marked_bad = 0;
        		unsigned long long blockstart;
				printf( "\n[0x%08x] %d/%d pages FAILED validation; ", validate_offset_org, validate_page_failed, validate_page_compared );
				// The block may already be marked bad in oob, but we didn't know about it
				// at startup otherwise we wouldn't have tried to write to it.
				found_bad_blocks++;
        		// First check to see if block is already marked bad
				blockstart = validate_offset_org;
				if ((already_marked_bad = ioctl(fd, MEMGETBADBLOCK, &blockstart)) < 0) {
					perror("ioctl(MEMGETBADBLOCK)");
					break;
				}
        		if (already_marked_bad)
        		{
        			printf( "bad block at 0x%08x is already marked bad - good job by NFC\n", validate_offset_org );
        		}
        		else
        		{
        			printf( "marking block at 0x%08x as BAD\n", validate_offset_org );
        			ioctl( fd, MEMSETBADBLOCK, &blockstart );
        		}
				setBadBlock( validate_offset_org );
        		// Rewind data we're attempting to write
        		input_buff_offset -= meminfo.erasesize;
        		// offset is already pointing to the next location in NAND
        	}
        	else
        	{
        		printf( " OK%c", terminate_line ? '\n' : '\r' );
        		fflush( stdout );
        	}
			updateProgressBar( ( (float)currentBlockNumber / (float)(totalWriteBlocks + found_bad_blocks + bbCounter)) * 100, badBlockTemp );
            badBlockTemp = PROGRESS_NORMAL;
        }
	}

	if (!skipWrites)
	{
		if (input_read_failed)
		{
			printf( "\nFlash failed due to input error\n" );
			log_actions( "flash failed due to input error\n" );
		}
		else
		{
			printf( "\nFlash complete: wrote %d bytes.\n", bytesWritten );
			log_actions( "flash complete: wrote %d bytes, found %d bad blocks\n", bytesWritten, found_bad_blocks );
			//fbtext_printf( "\nFlash complete: wrote %d bytes.\n", bytesWritten );
		}
	}

	// Close input file
	close( ifd );

	/* Close MTD device */
	close( fd );

	if (input_buff_size)
	{
		free( input_buff );
	}

	free( badBlocks );

    clearProgressBar( 0 );
    fbtext_clear();

    //reportBadBlocks( mtdName );

    // Report newly discovered bad blocks
    reportNewBadBlocks( mtdName, known_bad_blocks, found_bad_blocks );

	return 0;
}
