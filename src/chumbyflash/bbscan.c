/*
 *  bbscan.c
 *
 * Ken Steele
 * Copyright (c) Chumby Industries, 2007
 *
 * bbscan is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * bbscan is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with bbscan; if not, write to the Free Software
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

// Define to use Ken's method for bad block detection - may be ironforge-specific?
//#define ALTERNATE_BB_DETECT

static mtd_info_t meminfo;

#define NAND_PAGE_SIZE  meminfo.writesize //512   // in bytes
#define NAND_BLOCK_SIZE meminfo.erasesize //16384 // in bytes

unsigned long* badBlocks;
int            bbCounter = 0;

unsigned char readbuf[2048]; // Max page size for large block NAND
unsigned char oobbuf[64]; // Max oob buffer size supported

unsigned long startOffset = 0;

void reportBadBlocks( char* mtdName )
{
    FILE* fp = fopen( "/tmp/badblocks", "a" );
    fprintf( fp, "%s:%d,", mtdName, bbCounter );
    fclose( fp );
}

//
// returns offset to the begining of the current NAND block
//
int getCurrentBlockOffset( unsigned long offset )
{
	// hgroover: was 0xffffc000 = 16kb blocks
	return ( offset & ~(meminfo.erasesize-1) );
}

void badBlockScan( int* fd, unsigned long offset, int totalBlocks )
{
    int i                    = 0;
    struct mtd_oob_buf oob	 = {0, meminfo.oobsize, oobbuf};
    unsigned long long blockstart = 1;

    printf( "Scanning for bad blocks..\n" );
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
		blockstart = offset & (~meminfo.erasesize + 1);
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

            badBlocks[i] = 1;
            ++bbCounter;
        }

        offset += NAND_BLOCK_SIZE;
    }
    printf( "Bad block scan complete.\n" );
    //fbtext_printf( "Bad block scan complete.\n" );
}


int main( int argc, char **argv )
{
	unsigned long offset   = 0;

    int i                  = 0;
    int c                  = 0;
	int cnt                = 0;
    int fd;
	int ifd                = 0;
	int totalBlocks        = 0;
    int currentBlock       = 0;
    int eb                 = 0;
    int pageWritten        = 0;
    int bytesWritten       = 0;
    int bbScan             = 0;
    int currentBlockNumber = 0;

    char* mtdName     = NULL;
    char* fileName    = NULL;

	struct mtd_oob_buf oob = { 0, 16, oobbuf };

    if( argc < 2 )
    {
        printf( "usage: %s -m <mtdname>\n", argv[0] );
        exit( 1 );
    }

	fbtext_init();

    while( ( c = getopt( argc, argv, "m:" ) ) != EOF )
    {
        switch( c )
        {
            case 'm':
                mtdName = optarg;
                break;
            default:
				printf( "%s: Unknown option -%c\n", argv[0], c );
                exit(0);
        }
    }

    if( NULL != fileName )
    {
        if( ( ifd = open( fileName, O_RDONLY ) ) == -1 )
        {
            perror( "open input file" );
            close( ifd );
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

    totalBlocks = (int)(meminfo.size / NAND_BLOCK_SIZE);

	// Make sure device page sizes are valid
	if (!(meminfo.oobsize == 16 && meminfo.writesize == 512) &&
		!(meminfo.oobsize == 8 && meminfo.writesize == 256) &&
		//!(meminfo.oobsize == 32 && meminfo.writesize == 1024) &&
		!(meminfo.oobsize == 64 && meminfo.writesize == 2048))
	{
		printf( "Unknown flash (not normal NAND): oobsize=%u, writesize=%u\n", meminfo.oobsize, meminfo.writesize );
		close( fd );
		exit( 1 );
	}

	oob.length = meminfo.oobsize;
	if (meminfo.writesize == 2048)
	{
		unsigned int sizeKM = meminfo.size / 1024;
		char *sizeText = "kB";
		if (sizeKM % 1024 == 0)
		{
			sizeKM /= 1024;
			sizeText = "mB";
		}
		// Note that the local include has oobblock where the current kernel defines writesize
		printf( "Large block NAND (2k page) size = %u%s (%u pages, %u blocks) erasesize = %u\n",
			sizeKM, sizeText, meminfo.size / 2048, meminfo.size / 65536, meminfo.erasesize );
	}

	// allocate and initialize memory for badBlocks array
    badBlocks = (unsigned long*)malloc( sizeof( unsigned long ) * totalBlocks );
    for( i = 0; i < totalBlocks; ++i )
    {
        badBlocks[i] = 0;
    }

    fbtext_clear();
    printf( "Scanning %s @ [0x%08x] for bad blocks\n", mtdName, offset, totalBlocks );
    //fbtext_printf( "Scanning %s @ [0x%08x] for bad blocks\n", mtdName, offset, totalBlocks );

    badBlockScan( &fd, offset, totalBlocks );

	close( fd );
	close( ifd );

	free( badBlocks );

    fbtext_clear();

    reportBadBlocks( mtdName );

	return 0;
}
