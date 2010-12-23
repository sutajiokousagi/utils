/*
 *  badblock.c
 *
 * Ken Steele
 * Copyright (c) Chumby Industries, 2007-8
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
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <asm/types.h>
#include <mtd/mtd-user.h>

// Define to use Ken's method for bad block detection - may be ironforge-specific?
//#define ALTERNATE_BB_DETECT

static mtd_info_t meminfo;

#define NAND_PAGE_SIZE  meminfo.writesize //512   // in bytes
#define NAND_BLOCK_SIZE meminfo.erasesize //16384 // in bytes

//
// returns offset to the begining of the current NAND block
//
int getCurrentBlockOffset( unsigned long offset )
{
	// hgroover: was 0xffffc000 = 16kb blocks
	return ( offset & ~(meminfo.erasesize-1) );
}

void blockMarkBad( int* fd, int offset, int goodFlag )
{
    unsigned char oobbuf[64] = {0};
    struct mtd_oob_buf oob	 = {0, meminfo.oobsize, oobbuf};

	if (meminfo.writesize == 2048)
	{
		if (goodFlag)
		{
			printf( "Cannot attempt recovery on large block NAND\n" );
			return;
		}
		printf( "Large block NAND - marking 0x%x bad via ioctl\n", offset );
		loff_t bad_addr = offset & ~(meminfo.erasesize - 1);
		printf("Marking block at %08lx (page-aligned) bad\n", (long)bad_addr);
		if (ioctl(*fd, MEMSETBADBLOCK, &bad_addr)) {
			perror("MEMSETBADBLOCK");
			/* But continue anyway */
		}
		return;
	}

    memset( oobbuf, 0xA5, sizeof(oobbuf) );

    // read OOB data
    oob.start = getCurrentBlockOffset( offset );
	if( ioctl( *fd, MEMREADOOB, &oob ) != 0 )
	{
		perror( "ioctl(MEMREADOOB)" );
        exit( __LINE__ );
	}
    //printf( "After ioctl(MEMREADOOB): oob.start=%x, oob.length=%x\n", oob.start, oob.length);

    printf( "Read:  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            oobbuf[0],  oobbuf[1],  oobbuf[2],  oobbuf[3], oobbuf[4],  oobbuf[5],
            oobbuf[6],  oobbuf[7],  oobbuf[8],  oobbuf[9], oobbuf[10], oobbuf[11],
            oobbuf[12], oobbuf[13], oobbuf[14], oobbuf[15] );

	if( 1 == goodFlag )
	{
	   oobbuf[5] = 0xFF;
	}
	else
	{
	   oobbuf[5] = 0x00; // set bad block byte to 0
	}

	// write OOB data
    oob.start = getCurrentBlockOffset( offset );
    oob.length = meminfo.oobsize;
	if( ioctl( *fd, MEMWRITEOOB, &oob ) != 0 )
	{
		perror( "ioctl(MEMWRITEOOB)" );
        exit( __LINE__ );
	}
    //printf( "After ioctl(MEMWRITEOOB): oob.start=%x, oob.length=%x\n", oob.start, oob.length);

    printf( "Wrote: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            oobbuf[0],  oobbuf[1],  oobbuf[2],  oobbuf[3], oobbuf[4],  oobbuf[5],
            oobbuf[6],  oobbuf[7],  oobbuf[8],  oobbuf[9], oobbuf[10], oobbuf[11],
            oobbuf[12], oobbuf[13], oobbuf[14], oobbuf[15] );

    memset( oobbuf, 0xA5, sizeof(oobbuf) );

    // read OOB data
    oob.start = getCurrentBlockOffset( offset );
    oob.length = meminfo.oobsize;
	if( ioctl( *fd, MEMREADOOB, &oob ) != 0 )
	{
		perror( "ioctl(MEMREADOOB)" );
        exit( __LINE__ );
	}

    printf( "Read:  %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            oobbuf[0],  oobbuf[1],  oobbuf[2],  oobbuf[3], oobbuf[4],  oobbuf[5],
            oobbuf[6],  oobbuf[7],  oobbuf[8],  oobbuf[9], oobbuf[10], oobbuf[11],
            oobbuf[12], oobbuf[13], oobbuf[14], oobbuf[15] );
}


int main( int argc, char **argv )
{
	unsigned long offset = 0;

    int fd;
    int c = 0;
    int currentBlock = 0;
    int totalBlocks = 0;
    int goodFlag = 0;

    char* mtdName = NULL;

    if( argc < 4 )
    {
        printf( "usage: %s -m <mtdname> -s <start address> [-c]\n       -c marks block clean\n       ( example: %s -m /dev/mtd7 -s 0x00 )\n", argv[0], argv[0] );
        exit( 1 );
    }

    while( ( c = getopt( argc, argv, "cm:s:n:f:be" ) ) != EOF )
    {
        switch( c )
        {
            case 'c':
                goodFlag = 1;
                break;
            case 'm':
                mtdName = optarg;
                break;
            case 's':
                offset = strtoul( optarg, NULL, 0 );
                break;
            default:
                exit(0);
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
		printf( "Unknown flash (not normal NAND)\n" );
		close( fd );
		exit( 1 );
	}

    blockMarkBad( &fd, offset, goodFlag );

	close( fd );

	return 0;
}
