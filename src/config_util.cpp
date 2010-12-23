// $Id$
// config_util.cpp - Utility for manipulating config area on eSD-boot platforms
// Copyright (C) 2009 Chumby Industries, Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#include "esd_config_area.h"

#define UTIL_VER	"0.23"

// Global values set by command line
const char *g_cmd = "disp"; // Display current config block (--cmd=) (--dev)
// For other commands and options see help text
const char *g_partition_device = "/dev/mmcblk0p1"; // Device or filename (--dev=)
const char *g_configname = NULL; // Current configname from environment or --configname=
const char *g_build_version = NULL; // Current build version from --build_ver=
const char *g_mbr_source = "/dev/mmcblk0"; // mbr source device or file (--mbr=)
const char *g_block = NULL; // block name (--block=)
int g_force = 0; // If 1, force write to device regardless of existing valid config block
int g_pad = 0; // If 1, pad output of create command to full size of config area
unsigned int g_mbr_length = 4 * 512; // Length of MBR + padding, in bytes
int g_active_flag = -1; // Value to use for putactive or putflags
int g_update_flag = -1; // Value to use for putupdate or putflags
char g_sw_ver[64] = {0}; // value to write to last_update
int g_seek = ESD_CONFIG_AREA_PART1_OFFSET; // Initial seek offset

// Block definition structure to use
typedef struct _tmp_block_def {
	unsigned int offset;	// Offset within partition (calculated)
	const char *srcfile;	// Source file
	unsigned int length;	// Length (specified)
	char name[5];			// Name plus terminating null
	unsigned char ver_data[4]; // Optional version data
} tmp_block_def;
unsigned int g_last_offset = ESD_CONFIG_AREA_PART1_OFFSET + ESD_CONFIG_AREA_LENGTH; // First available address after config area
int g_block_def_count = 0;
tmp_block_def g_blocks[64];

const char *help_text = "Syntax:\n\
config_util [--cmd={create|get|put|getblock|putblock|getactive|putactive|getupdate|putupdate|putflags|getversion|putversion}] <options>\n\
Default command is disp\n\
Commands and required options:\n\
	create	(--configname --mbr --build_ver)\n\
		Create a config area and write to stdout\n\
	init (--configname --mbr --build_ver --dev)\n\
		Same as create but writes config area to device\n\
	get (--dev)\n\
		Write current config area to stdout\n\
	put (--dev)\n\
		Read config area from stdin and write to device\n\
	getblock (--dev --block)\n\
		Get specified block and write to stdout\n\
	putblock (--dev --block)\n\
		Read specified block from stdin and write to device. Special block name reserved writes 1st 48k of dev;\n\
		special block name config is equivalent to put\n\
	getactive (--dev)\n\
		Get active flag (0 for rfsA, 1 for rfsB)\n\
	putactive (--dev --activeflag)\n\
		Write specified active partition value\n\
	getupdate (--dev)\n\
		Get \"update active\" flag (1 for update in progress)\n\
	putupdate (--dev --updateflag)\n\
		Write specified update active flag value\n\
	putflags (--dev --activeflag --updateflag)\n\
		Write both active flag and update flag\n\
	getversion (--dev)\n\
		Get current last update version\n\
	putversion (--dev --build_ver)\n\
		Write specified version to last update version\n\
	getmbr (--dev --mbr)\n\
		Get current backup MBR and write to stdout\n\
	putmbr (--dev --mbr)\n\
		Write backup MBR to config area\n\
	getfactory (--dev)\n\
		Get factory data and write to stdout\n\
	putfactory (--dev)\n\
		Read factory data from stdin and write to config area\n\
Options (default in parens):\n\
	--configname=<name> (CONFIGNAME from environment)\n\
		CONFIGNAME value to use\n\
	--dev=<device or file> (/dev/mmcblk0p1)\n\
		Device name or filename for image of partition 1 including config area\n\
	--build_ver=<version string>\n\
		Build version to use (e.g. 1.7.1892)\n\
	--mbr=<mbr source> (/dev/mmcblk0)\n\
		File or device containing MBR to use\n\
	--block=<block name>\n\
		Block name to read or write\n\
	--blockdef=<source file>,<size>,<name>[,<ver1>,<ver2>,<ver3>,<ver4>]\n\
		Block definition to add when defining config area using --create\n\
	--force\n\
		With put or init command, skip validation of existing config area\n\
	--pad\n\
		With create command, pad output to full size of config area\n\
	--noseek\n\
		Do not attempt to seek to offset 0xc000 on device for start of config area\n\
	--mbr_length=<bytes> (2048)\n\
		Length of MBR plus padding sectors, in bytes. Default is\n\
		MBR + 3 padding sectors = 4 sectors * 512 bytes = 2k\n\
	--activeflag=<value>\n\
		Value to write for putactive or putflags command (0 or 1)\n\
	--updateflag=<value>\n\
		Value to write for putupdate or putflags commands (0 or 1)\n\
	--help\n\
		Display this text\n\
";
// Initialize an empty config area structure
void init_config( config_area *ca )
{
	memset( ca, 0, sizeof(config_area) );
	unsigned char v[4] = {ESD_CONFIG_AREA_VER};
	memcpy( ca->sig, "Cfg*", 4 );
	memcpy( ca->area_version, v, sizeof(v) );
	ca->active_index[0] = 0;
	ca->updating[0] = 0;
	ca->p1_offset = g_mbr_length;
	//strcpy(ca->last_update,"1.7.1892");
	//strcpy(ca->configname,"silvermoon_sd");
	// Mark table as empty
	ca->block_table[0].offset = 0xffffffff;
}

// Read MBR from source into backup copy
// Return -1 if failed to open, 1 if successful, 0 if failed to read entire mbr
int
read_mbr( const char *mbr_source, config_area *ca )
{
	int fd;
	fd = open( mbr_source, O_RDONLY );
	if (fd < 0)
	{
		fprintf( stderr, "open(%s) failed errno=%d (%s)\n", mbr_source, errno, strerror(errno) );
		return -1;
	}
	int bytes_read = read( fd, ca->mbr_backup, sizeof(ca->mbr_backup) );
	close( fd );
	if (bytes_read != sizeof(ca->mbr_backup))
	{
		fprintf( stderr, "read from %s for %d bytes failed, read only %d bytes\n", mbr_source, sizeof(ca->mbr_backup), bytes_read );
	}
	return (bytes_read == sizeof(ca->mbr_backup)) ? 1 : 0;
}

// Fail with error message
void
die( const char *msg )
{
	fprintf( stderr, "Error: %s\n", msg );
	exit( -1 );
}

// Check for required option and fail if not present
void
check_required( const char *cmd_option, const char *option_value, const char *cmd_name )
{
	char die_msg[256];
	if (option_value != NULL && *option_value)
	{
		return;
	}
	sprintf( die_msg, "--%s required for %s command - cannot continue", cmd_option, cmd_name );
	die( die_msg );
}

void
check_required( const char *cmd_option, int option_value, const char *cmd_name )
{
	char die_msg[256];
	if (option_value != -1)
	{
		return;
	}
	sprintf( die_msg, "--%s required for %s command - cannot continue", cmd_option, cmd_name );
	die( die_msg );
}

// Common utility functions. Most of these simply die via exit(-1) on any serious error

// Get offsets within structure
#define STRUCT_OFFSET(m)	static int offset_##m(void) { config_area a; return (int)(((unsigned char *)&a.m) - (unsigned char*)&a); }
#define STRUCT_OFFSET_ARRAY(m)	static int offset_##m(void) { config_area a; return (int)(((unsigned char *)&a.m[0]) - (unsigned char *)&a); }

STRUCT_OFFSET_ARRAY(active_index);
STRUCT_OFFSET_ARRAY(updating);
STRUCT_OFFSET_ARRAY(last_update);
STRUCT_OFFSET(p1_offset);
STRUCT_OFFSET_ARRAY(factory_data);
STRUCT_OFFSET_ARRAY(configname);
STRUCT_OFFSET_ARRAY(mbr_backup);
STRUCT_OFFSET_ARRAY(block_table);

// Open and validate the sig then seek to specified offset. Return handle
static int
open_valid( int offset )
{
	int fd = open( g_partition_device, O_RDWR );
	if (fd == -1)
	{
		fprintf( stderr, "Failed to open %s r/w - errno=%d (%s)\n", g_partition_device, errno, strerror(errno) );
		exit( -1 );
	}
	config_area a;
	if (g_seek != 0)
	{
		if (lseek( fd, g_seek, SEEK_SET ) == -1)
		{
			fprintf( stderr, "Failed to seek to offset 0x%x on %s - errno=%d (%s)\n", g_seek, g_partition_device, errno, strerror(errno) );
			close( fd );
			exit( -1 );
		}
	}
	if (read( fd, &a.sig[0], sizeof(a.sig) ) != sizeof(a.sig))
	{
		fprintf( stderr, "Failed to read %d bytes from %s, errno=%d\n", sizeof(a.sig), g_partition_device, errno );
		close( fd );
		exit( -1 );
	}
	if (a.sig[0] != 'C' || a.sig[1] != 'f' || a.sig[2] != 'g' || a.sig[3] != '*')
	{
		fprintf( stderr, "Signature mismatch, expected Cfg* got %c%c%c%c\n",
			a.sig[0], a.sig[1], a.sig[2], a.sig[3] );
		close( fd );
		exit( -1 );
	}
	if (offset != sizeof(a.sig))
	{
		if (lseek( fd, offset - sizeof(a.sig), SEEK_CUR ) == -1)
		{
			fprintf( stderr, "Seek failure, errno=%d (%s)\n", errno, strerror(errno) );
			close( fd );
			exit( -1 );
		}
	}
	return fd;
}

// Open, check signature, and find specified block, then seek to start offset. Return handle and set length
static int
open_block( const char *block_code, int& block_length )
{
	// Return valid file handle or die
	int fd = open_valid( offset_block_table() );
	// Check for block code "reserved", which means 48k bytes at offset 0
	if (!strcmp( block_code, "reserved" ))
	{
		lseek( fd, 0, SEEK_SET );
		block_length = ESD_CONFIG_AREA_PART1_OFFSET;
		return fd;
	}
	// Find matching entry
	int nBlock;
	block_def bd;
	for (nBlock = 0; nBlock < 64; nBlock++)
	{
		int bytes_read;
		if ((bytes_read = read( fd, &bd, sizeof(bd) )) != sizeof(bd))
		{
			fprintf( stderr, "read error at block %d, %d bytes requested, %d bytes read\n", nBlock, sizeof(bd), bytes_read );
			close( fd );
			exit( -1 );
		}
		// Check for match
		if (!memcmp( bd.n.name, block_code, sizeof(bd.n.name) ))
		{
			// Seek to offset
			block_length = bd.length;
			if (lseek( fd, bd.offset, SEEK_SET ) != bd.offset)
			{
				fprintf( stderr, "found %s at block %d with length %u but seek to offset %u failed, errno=%d (%s)\n",
					block_code, nBlock, bd.length, bd.offset, errno, strerror(errno) );
				close( fd );
				exit( -1 );
			}
			// Success, return handle
			return fd;
		}
	}
	// not found
	fprintf( stderr, "did not find block %s after reading %d blocks\n", block_code, nBlock );
	close( fd );
	exit( -1 );
}

// Command implementations
static int
cmd_create()
{
	config_area ca;
	init_config( &ca );
	strcpy(ca.last_update, g_build_version);
	strcpy(ca.configname, g_configname);
	read_mbr( g_mbr_source, &ca );
	// Add blocks
	int block;
	for (block = 0; block < g_block_def_count; block++)
	{
		// Open file and get size, validate <= .length
		ca.block_table[block].offset = g_blocks[block].offset;
		ca.block_table[block].length = g_blocks[block].length;
		memcpy( ca.block_table[block].block_ver, g_blocks[block].ver_data, 4 );
		memcpy( ca.block_table[block].n.name, g_blocks[block].name, 4 );
		int fd = open( g_blocks[block].srcfile, O_RDONLY );
		if (fd < 0)
		{
			fprintf( stderr, "Cannot open %s for validation: errno=%d (%s)\n", g_blocks[block].srcfile, errno, strerror(errno) );
			exit( -1 );
		}
		long actual_size = lseek( fd, 0L, SEEK_END );
		close( fd );
		if (actual_size < 0L)
		{
			fprintf( stderr, "Cannot get length of %s: errno=%d (%s)\n", g_blocks[block].srcfile, errno, strerror(errno) );
			exit( -1 );
		}
		if (actual_size > g_blocks[block].length)
		{
			fprintf( stderr, "Block[%d] definition invalid: length %u specified but file %s is %u bytes\n",
				block, g_blocks[block].length, g_blocks[block].srcfile, actual_size );
			exit( -1 );
		}
	}
	// Terminate block list
	if (g_block_def_count < 64)
	{
		ca.block_table[g_block_def_count].offset = 0xffffffff;
	}
	// Dump the whole thing to stdout
	fflush( stderr );
	fflush( stdout );
	write( 1, &ca, sizeof(ca) );
	// Pad to full length if required
	if (g_pad)
	{
		int padlength = ESD_CONFIG_AREA_LENGTH - sizeof(ca);
		if (padlength > 0)
		{
			char padblock[1024];
			memset( padblock, 0, sizeof(padblock) );
			while (padlength > 0)
			{
				int passlength = (padlength > sizeof(padblock)) ? sizeof(padblock) : padlength;
				write( 1, padblock, passlength );
				padlength -= passlength;
			}
		}
	}

	// Success
	return 0;
}

static int
cmd_putactive()
{
	int fd = open_valid( offset_active_index() );
	config_area a;
	memset( &a.active_index[0], 0, sizeof(a.active_index) );
	a.active_index[0] = g_active_flag;
	if (write( fd, &a.active_index[0], sizeof(a.active_index) ) != sizeof(a.active_index))
	{
		fprintf( stderr, "Failed to write active index\n" );
		exit( -1 );
	}
	fprintf( stderr, "Set active partition index to %d\n", g_active_flag );
	close( fd );
	return 0;
}

static int
cmd_getactive()
{
	int fd = open_valid( offset_active_index() );
	config_area a;
	if (read( fd, &a.active_index[0], sizeof(a.active_index) ) != sizeof(a.active_index))
	{
		fprintf( stderr, "Failed to read active index\n" );
		exit( -1 );
	}
	printf( "%d\n", a.active_index[0] );
	close( fd );
	return 0;
}

static int
cmd_putupdate()
{
	int fd = open_valid( offset_updating() );
	config_area a;
	memset( &a.updating[0], 0, sizeof(a.updating) );
	a.updating[0] = g_update_flag;
	if (write( fd, &a.updating[0], sizeof(a.updating) ) != sizeof(a.updating))
	{
		fprintf( stderr, "Failed to write update active flag\n" );
		exit( -1 );
	}
	fprintf( stderr, "Set update active status to %d\n", g_update_flag );
	close( fd );
	return 0;
}

static int
cmd_getupdate()
{
	int fd = open_valid( offset_updating() );
	config_area a;
	if (read( fd, &a.updating[0], sizeof(a.updating) ) != sizeof(a.updating))
	{
		fprintf( stderr, "Failed to read update active flag\n" );
		exit( -1 );
	}
	printf( "%d\n", a.updating[0] );
	close( fd );
	return 0;
}

static int
cmd_putflags()
{
	int fd = open_valid( offset_active_index() );
	config_area a;
	memset( &a, 0, sizeof(a) );
	a.updating[0] = g_update_flag;
	a.active_index[0] = g_active_flag;
	if (write( fd, &a.active_index[0], sizeof(a.active_index) ) != sizeof(a.active_index))
	{
		fprintf( stderr, "Failed to write active index\n" );
		exit( -1 );
	}
	lseek( fd, offset_updating(), SEEK_SET );
	if (write( fd, &a.updating[0], sizeof(a.updating) ) != sizeof(a.updating))
	{
		fprintf( stderr, "Failed to write update active flag\n" );
		exit( -1 );
	}
	fprintf( stderr, "Set update active status to %d, active partition to %d\n", g_update_flag, g_active_flag );
	close( fd );
	return 0;
}

static int
cmd_getversion()
{
	int fd = open_valid( offset_last_update() );
	config_area a;
	if (read( fd, &a.last_update[0], sizeof(a.last_update) ) != sizeof(a.last_update))
	{
		fprintf( stderr, "Failed to read last update version\n" );
		exit( -1 );
	}
	printf( "%s\n", a.last_update );
	close( fd );
	return 0;
}

static int
cmd_putversion()
{
	int fd = open_valid( offset_last_update() );
	config_area a;
	memset( &a.last_update[0], 0, sizeof(a.last_update) );
	strcpy( a.last_update, g_sw_ver );
	if (write( fd, &a.last_update[0], sizeof(a.last_update) ) != sizeof(a.last_update))
	{
		fprintf( stderr, "Failed to write last update version\n" );
		exit( -1 );
	}
	fprintf( stderr, "Set last update version to %s\n", g_sw_ver );
	close( fd );
	return 0;

}

static int
cmd_getfactory()
{
	int fd = open_valid( offset_factory_data() );
	config_area a;
	if (read( fd, &a.factory_data[0], sizeof(a.factory_data) ) != sizeof(a.factory_data))
	{
		fprintf( stderr, "Failed to read factory data\n" );
		exit( -1 );
	}
	printf( "%s\n", a.factory_data );
	close( fd );
	return 0;
}

static int
cmd_putfactory()
{
	int fd = open_valid( offset_factory_data() );
	config_area a;
	memset( &a.factory_data[0], 0, sizeof(a.factory_data) );
	int bytes_read = read( 0, &a.factory_data[0], sizeof(a.factory_data) );
	if (write( fd, &a.factory_data[0], sizeof(a.factory_data) ) != sizeof(a.factory_data))
	{
		fprintf( stderr, "Failed to write factory data - %d bytes read from stdin\n", bytes_read );
		exit( -1 );
	}
	fprintf( stderr, "Read %d factory data bytes from stdin, wrote %d to config\n", bytes_read, sizeof(a.factory_data) );
	close( fd );
	return 0;
}

static int
cmd_getblock()
{
	int blockLength;
	// Open with read offset at start of block or die
	int fd = open_block( g_block, blockLength );
	int total_bytes = 0;
	int bytes_read, bytes_to_read;
	char block_buff[1024];
	fflush( stdout );
	for ( ; total_bytes < blockLength; total_bytes += bytes_read)
	{
		bytes_to_read = sizeof(block_buff);
		if (blockLength - total_bytes < bytes_read)
		{
			bytes_to_read = blockLength - total_bytes;
		}
		bytes_read = read( fd, block_buff, bytes_to_read );
		if (bytes_read < bytes_to_read)
		{
			close( fd );
			fprintf( stderr, "Short read on %s - requested %d got %d bytes\n", g_block, bytes_to_read, bytes_read );
			exit( -1 );
		}
		write( 1, block_buff, bytes_read );
	}
	close( fd );
	return 0;
}

static int
cmd_putblock()
{
	int blockLength;
	// Open with write offset at start of block or die
	int fd = open_block( g_block, blockLength );
	int total_bytes = 0;
	int msgDisplayed = 0;
	int bytes_read, bytes_to_read;
	char block_buff[1024];
	for ( ; total_bytes < blockLength; total_bytes += bytes_read)
	{
		bytes_to_read = sizeof(block_buff);
		if (blockLength - total_bytes < bytes_read)
		{
			bytes_to_read = blockLength - total_bytes;
		}
		bytes_read = read( 0, block_buff, bytes_to_read );
		if (bytes_read > 0)
		{
			int bytes_written = write( fd, block_buff, bytes_read );
			if (bytes_written != bytes_read)
			{
				fprintf( stderr, "Write failed at offset %d - tried to write %d bytes, only %d bytes written (errno=%d/%s)",
					total_bytes, bytes_read, bytes_written, errno, strerror(errno) );
				close( fd );
				exit( -1 );
			}
		}
		if (bytes_read < 0)
		{
			total_bytes += bytes_read;
			fprintf( stderr, "Error reading block: %d.  Finished write with total of %d bytes written (block size %d)\n", bytes_read, total_bytes, blockLength );
			msgDisplayed++;
			break;
		}
		if (!bytes_read)
		{
			total_bytes += bytes_read;
			fprintf( stderr, "Finished write with total of %d bytes written (block size %d)\n", total_bytes, blockLength );
			msgDisplayed++;
			break;
		}
	}
	close( fd );
	if (!msgDisplayed)
	{
		fprintf( stderr, "Write completed, total %d bytes written\n", total_bytes );
	}
	return 0;
}

static int
cmd_put()
{
	int fd = open( g_partition_device, O_RDWR );
	if (fd == -1)
	{
		fprintf( stderr, "Failed to open %s r/w - errno=%d (%s)\n", g_partition_device, errno, strerror(errno) );
		exit( -1 );
	}
	config_area a;
	if (g_seek != 0)
	{
		if (lseek( fd, g_seek, SEEK_SET ) == -1)
		{
			fprintf( stderr, "Failed to seek to offset 0x%x on %s - errno=%d (%s)\n", g_seek, g_partition_device, errno, strerror(errno) );
			close( fd );
			exit( -1 );
		}
	}
	// Read from stdin and validate
	int bytes_read = fread( (char*)&a, sizeof(char), sizeof(a)/sizeof(char), stdin );
	if (bytes_read <= 0)
	{
		fprintf( stderr, "Empty or failed read on put - errno=%d (%s)\n", errno, strerror(errno) );
		close( fd );
		exit( -1 );
	}
	if (a.sig[0] != 'C' || a.sig[1] != 'f' || a.sig[2] != 'g' || a.sig[3] != '*')
	{
		fprintf( stderr, "Signature mismatch, expected Cfg* got %c%c%c%c\n",
			a.sig[0], a.sig[1], a.sig[2], a.sig[3] );
		close( fd );
		exit( -1 );
	}
	int actual_written = write( fd, &a, bytes_read );
	if (actual_written != bytes_read)
	{
		fprintf( stderr, "Failed write for %d bytes to file handle %d (actual written=%d) - errno=%d (%s)\n", bytes_read, fd, actual_written, errno, strerror(errno) );
		close( fd );
		exit( -1 );
	}
	close( fd );
	fprintf( stderr, "Wrote %d bytes\n", actual_written );
	return 0;
}

static int
cmd_get()
{
	config_area a;
	int fd = open_valid( 0 );
	int bytes_read = read( fd, &a, sizeof(a) );
	if (bytes_read < 8)
	{
		fprintf( stderr, "get failed, only %d/%d bytes read - not enough for validation\n", bytes_read, sizeof(a) );
	}
	fflush( stdout );
	fflush( stderr );
	write( fileno(stdout), &a, bytes_read );
	fflush( stdout );
	return 0;
}

static int
cmd_putmbr()
{
	config_area a;
	read_mbr( g_mbr_source, &a );
	int fd = open_valid( offset_mbr_backup() );
	if (write( fd, a.mbr_backup, sizeof(a.mbr_backup) ) != sizeof(a.mbr_backup))
	{
		fprintf( stderr, "Failed to write mbr backup\n" );
		exit( -1 );
	}
	fprintf( stderr, "Updated backup copy of MBR\n" );
	close( fd );
	return 0;
}

static int
cmd_getmbr()
{
	config_area a;
	int fd = open_valid( offset_mbr_backup() );
	int bytes_read = read( fd, a.mbr_backup, sizeof(a.mbr_backup) );
	if (bytes_read < sizeof(a.mbr_backup))
	{
		fprintf( stderr, "get failed, only %d/%d bytes read\n", bytes_read, sizeof(a.mbr_backup) );
	}
	fflush( stdout );
	fflush( stderr );
	write( fileno(stdout), a.mbr_backup, bytes_read );
	fflush( stdout );
	return 0;
}

static int
cmd_disp()
{
	config_area a;
	int fd = open_valid( 0 );
	int bytes_read = read( fd, &a, sizeof(a) );
	if (bytes_read < 8)
	{
		fprintf( stderr, "get failed, only %d/%d bytes read - not enough for validation\n", bytes_read, sizeof(a) );
	}
	printf( "Config block read for %d bytes\n", bytes_read );
	printf( "Version:    %d,%d,%d,%d\n", a.area_version[0], a.area_version[1], a.area_version[2], a.area_version[3] );
	printf( "Active:     %d\n", a.active_index[0] );
	printf( "Updating:   %d\n", a.updating[0] );
	printf( "LastUpdate: %s\n", a.last_update );
	printf( "DevOffset:  0x%04x\n", a.p1_offset );
	printf( "Configname: %s\n", a.configname );
	// Block table entries end with offset=0xffffffff
	int n;
	for (n = 0; n < 64; n++)
	{
		if (a.block_table[n].offset == 0xffffffff)
		{
			break;
		}
		printf( "Block[%d]: %c%c%c%c @0x%08x len=%u ver=%d,%d,%d,%d\n",
			n,
			a.block_table[n].n.name[0], a.block_table[n].n.name[1], a.block_table[n].n.name[2], a.block_table[n].n.name[3],
			a.block_table[n].offset, a.block_table[n].length,
			a.block_table[n].block_ver[0], a.block_table[n].block_ver[1], a.block_table[n].block_ver[2], a.block_table[n].block_ver[3] );
	}
	printf( "BlockCount: %d\n", n );
	printf( "FactoryData:\n" );
	char temp[256];
	strncpy( temp, a.factory_data, sizeof(a.factory_data) );
	temp[sizeof(a.factory_data)] = '\0';
	char *factory_line;
	int factory_line_count = 0;
	for (factory_line = strtok( temp, "\n" ); factory_line != NULL; factory_line = strtok( NULL, "\n" ))
	{
		printf( "\t%s\n", factory_line );
		factory_line_count++;
	}
	printf( "FactoryDataCount: %d\n", factory_line_count );
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
				fprintf( stderr, "SIGPIPE in config_util - terminating\n" );
				exit( -1 );
				break;
			default:
				fprintf( stderr, "Unhandled sigaction %d in config_util\n", signum );
				exit( -1 );
				break;
        }
}

int
main( int argc, const char *argv[] )
{
	fprintf( stderr, "config_util v%s\n", UTIL_VER );

	// Get defaults from environment
	g_configname = getenv( "CONFIGNAME" );

	// Parse options
	int n;
	for (n = 1; n < argc; n++)
	{
		const char *equal = strchr( argv[n], '=' );
		if (strncmp( argv[n], "--", 2 ))
		{
			fprintf( stderr, "Unrecognized argument '%s' - options begin with '--'\n%s\n", argv[n], help_text );
			return -1;
		}
		const char *option = &argv[n][2];
		if (!strncmp( option, "cmd=", 4 ))
		{
			g_cmd = &equal[1];
			// We'll validate command and required options later
		}
		else if (!strncmp( option, "configname=", 11 ))
		{
			g_configname = &equal[1];
		}
		else if (!strncmp( option, "dev=", 4 ))
		{
			g_partition_device = &equal[1];
		}
		else if (!strncmp( option, "build_ver=", 10 ))
		{
			g_build_version = &equal[1];
			strcpy( g_sw_ver, g_build_version );
		}
		else if (!strncmp( option, "mbr=", 4 ))
		{
			g_mbr_source = &equal[1];
		}
		else if (!strncmp( option, "block=", 6 ))
		{
			g_block = &equal[1];
		}
		else if (!strncmp( option, "blockdef=", 9 ))
		{
			if (g_block_def_count >= 63) die( "Too many block defs (max=64)" );
			g_blocks[g_block_def_count].offset = g_last_offset;
			// Parse either 3 or 7 values
			char srcfile[256];
			unsigned int size;
			char name[128];
			unsigned int v1, v2, v3, v4;
			int items_read = sscanf( &equal[1], "%[^,],%u,%[^,],%u,%u,%u,%u", srcfile, &size, name, &v1, &v2, &v3, &v4 );
			if (items_read != 3 && items_read != 7)
			{
				fprintf( stderr, "%d components were read\n", items_read );
				die( "Either 3 or 7 components must be specified" );
			}
			g_blocks[g_block_def_count].length = size;
			g_last_offset += size;
			g_blocks[g_block_def_count].srcfile = strdup( srcfile );
			strncpy( g_blocks[g_block_def_count].name, name, 4 );
			g_blocks[g_block_def_count].name[4] = '\0';
			if (items_read == 7)
			{
				g_blocks[g_block_def_count].ver_data[0] = v1;
				g_blocks[g_block_def_count].ver_data[1] = v2;
				g_blocks[g_block_def_count].ver_data[2] = v3;
				g_blocks[g_block_def_count].ver_data[3] = v4;
			}
			else
			{
				g_blocks[g_block_def_count].ver_data[0] = 1;
				g_blocks[g_block_def_count].ver_data[1] = 0;
				g_blocks[g_block_def_count].ver_data[2] = 0;
				g_blocks[g_block_def_count].ver_data[3] = 0;
			}
			g_block_def_count++;
		}
		else if (!strcmp( option, "force" ))
		{
			g_force = 1;
		}
		else if (!strcmp( option, "pad" ))
		{
			g_pad = 1;
		}
		else if (!strncmp( option, "mbr_length=", 11 ))
		{
			g_mbr_length = atoi( &equal[1] );
		}
		else if (!strncmp( option, "activeflag=", 11 ))
		{
			g_active_flag = atoi( &equal[1] );
		}
		else if (!strncmp( option, "updateflag=", 11 ))
		{
			g_update_flag = atoi( &equal[1] );
		}
		else if (!strcmp( option, "noseek" ))
		{
			g_seek = 0;
		}
		else if (!strncmp( option, "help", 4 ))
		{
			fprintf( stderr, "%s\n", help_text );
			return 0;
		}
		else
		{
			fprintf( stderr, "Unrecognized option %s\n%s\n", argv[n], help_text );
			return -1;
		}
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

	// Validate command and corequisite options
	if (!strcmp( g_cmd, "disp" ))
	{
		check_required( "dev", g_partition_device, g_cmd );

		cmd_disp();
	}
	else if (!strcmp( g_cmd, "create" ))
	{
		check_required( "mbr", g_mbr_source, g_cmd );
		check_required( "configname", g_configname, g_cmd );
		check_required( "build_ver", g_build_version, g_cmd );

		cmd_create();
	}
	else if (!strcmp( g_cmd, "get" ))
	{
		check_required( "dev", g_partition_device, g_cmd );

		cmd_get();
	}
	else if (!strcmp( g_cmd, "put" ))
	{
		check_required( "dev", g_partition_device, g_cmd );

		cmd_put();
	}
	else if (!strcmp( g_cmd, "getblock" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		check_required( "block", g_block, g_cmd );
		cmd_getblock();
	}
	else if (!strcmp( g_cmd, "putblock" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		check_required( "block", g_block, g_cmd );
		// If config is specified, translate to a put
		if (!strcmp( g_block, "config" ))
		{
			fprintf( stderr, "Translated putblock --block=%s request to put\n", g_block );
			cmd_put();
		}
		else
		{
			cmd_putblock();
		}
	}
	else if (!strcmp( g_cmd, "getactive" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		cmd_getactive();
	}
	else if (!strcmp( g_cmd, "putactive" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		check_required( "activeflag", g_active_flag, g_cmd );
		cmd_putactive();
	}
	else if (!strcmp( g_cmd, "getupdate" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		cmd_getupdate();
	}
	else if (!strcmp( g_cmd, "putupdate" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		check_required( "updateflag", g_update_flag, g_cmd );
		cmd_putupdate();
	}
	else if (!strcmp( g_cmd, "putflags" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		check_required( "activeflag", g_active_flag, g_cmd );
		check_required( "updateflag", g_update_flag, g_cmd );
		cmd_putflags();
	}
	else if (!strcmp( g_cmd, "getversion" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		cmd_getversion();
	}
	else if (!strcmp( g_cmd, "putversion" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		check_required( "build_ver", g_sw_ver, g_cmd );
		cmd_putversion();
	}
	else if (!strcmp( g_cmd, "getmbr" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		cmd_getmbr();
	}
	else if (!strcmp( g_cmd, "putmbr" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		check_required( "mbr", g_mbr_source, g_cmd );
		cmd_putmbr();
	}
	else if (!strcmp( g_cmd, "getfactory" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		cmd_getfactory();
	}
	else if (!strcmp( g_cmd, "putfactory" ))
	{
		check_required( "dev", g_partition_device, g_cmd );
		cmd_putfactory();
	}
	else
	{
		fprintf( stderr, "Error: unrecognized command %s\n%s\n", g_cmd, help_text );
		return -1;
	}

	return 0;
}

