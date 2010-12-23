// $Id$
// test_dev_shimd.cpp - test device shim daemon
// Copyright (C) 2010 Chumby Industries. All rights reserved
// This basically takes over one or more /dev entries specified on the command line
// (e.g. input/event0 switch) and replaces them with FIFO entries

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <linux/input.h>

#define VER_STR "0.17"

#define MAX_OWNED	16
const char *g_owned_devices[MAX_OWNED];
int g_owned_device_handles[MAX_OWNED];
int g_owned_fifo_handles[MAX_OWNED];
int g_owned_device_blocksize[MAX_OWNED];
int g_owned_device_flags[MAX_OWNED];
unsigned char g_lastbyte[MAX_OWNED];
unsigned long g_blocks_read[MAX_OWNED];
unsigned long g_blocks_written[MAX_OWNED];
unsigned long g_dupes_skipped[MAX_OWNED];
int g_owned_device_count = 0;
volatile int g_running = 1;
int g_dbgLevel = 0;

// Flag bits in g_owned_device_flags[]
#define DFLAG_LOG	0x01	// Log when debug level >= 1
#define DFLAG_LAST	0x02	// Attempt large block read but take only last byte
#define DFLAG_KILLDUPES	0x04	// Pass on byte only when different from previous byte read
#define DFLAG_HIDDEV	0x08	// Device returns HID structure with valid struct timeval

// Path of command input FIFO
#define MAX_OPEN_CMDPIPES	16
#define CMD_INPUT_FIFO	"/tmp/.shimd-cmdin"
FILE * g_cmdInputFifo = NULL;
char *g_cmdFifoOutput[MAX_OPEN_CMDPIPES]; // Name of output for a command input handle
FILE * g_cmdFifoOutputFile[MAX_OPEN_CMDPIPES]; // Output FILE for command
char *g_cmdFifoCapture[MAX_OPEN_CMDPIPES]; // Pathname of output capture
int g_cmdFifoCaptureHandle[MAX_OPEN_CMDPIPES]; // Output handle for capture
char *g_cmdFifoPlayback[MAX_OPEN_CMDPIPES]; // Pathname of playback file
int g_cmdFifoPlaybackHandle[MAX_OPEN_CMDPIPES]; // Input handle for playback
unsigned long long g_captureStart[MAX_OPEN_CMDPIPES]; // struct timeval for start or resume of capture
unsigned long long g_playbackStart[MAX_OPEN_CMDPIPES]; // struct timeval for start or resume of playback
unsigned long long g_capturePause[MAX_OPEN_CMDPIPES]; // struct timeval for time when capture was last paused
unsigned long long g_playbackPause[MAX_OPEN_CMDPIPES]; // struct timeval for time when playback was last paused
unsigned char g_captureActive[MAX_OPEN_CMDPIPES]; // capture active (!0) or paused (0)
unsigned char g_playbackActive[MAX_OPEN_CMDPIPES]; // playback active (!0) or paused (0)

// hiddev-specific part of playback structure
typedef struct _hiddev_playback_t
{
	unsigned short type;
	unsigned short code;
	long value;
} HIDDEV_PLAYBACK;

// Playback structure
typedef struct _playback_t
{
	unsigned long long offset; // Time offset in microseconds
	int devid; // Index into g_owned_devices[]
	union {
		unsigned long ul[2];
		unsigned short us[4];
		unsigned char uc[8];
		HIDDEV_PLAYBACK hid;
	};
} PLAYBACK;

// Playback arrays and current index
PLAYBACK *g_playback[MAX_OPEN_CMDPIPES];
int g_playbackCount[MAX_OPEN_CMDPIPES];
int g_playbackIndex[MAX_OPEN_CMDPIPES];

#define CAPSTART_SET(n,ull)	(g_captureStart[n]=ull)
#define PLAYSTART_SET(n,ull)	(g_playbackStart[n]=ull)
#define CAPPAUSETIME_SET(n,ull)	(g_capturePause[n]=ull)
#define PLAYPAUSETIME_SET(n,ull)	(g_playbackPause[n]=ull)

// Convert struct timeval (returned by gettimeofday) to unsigned long long in microseconds
unsigned long long tv_to_ull( struct timeval const *ptv )
{
	unsigned long long r = ptv->tv_sec * 1000000LL;
	r += ptv->tv_usec;
	return r;
}

// Convert unsigned long long in microseconds back to struct timeval
void ull_to_tv( unsigned long long us, struct timeval *ptv )
{
	ptv->tv_usec = (us % 1000000L);
	ptv->tv_sec =  (us / 1000000L);
}

// Get time of day and return as an unsigned long long in microseconds
unsigned long long timeofday_ull()
{
	struct timeval tv;
	gettimeofday( &tv, NULL );
	return tv_to_ull( &tv );
}

// Initialize command FIFO structures
void init_cmd_fifos()
{
	int n;
	for (n = 0; n < MAX_OPEN_CMDPIPES; n++)
	{
		g_cmdFifoOutput[n] = NULL;
		g_cmdFifoOutputFile[n] = NULL;
		g_cmdFifoCapture[n] = NULL;
		g_cmdFifoCaptureHandle[n] = -1;
		g_cmdFifoPlayback[n] = NULL;
		g_cmdFifoPlaybackHandle[n] = -1;
		g_captureStart[n] = 0LL;
		g_playbackStart[n] = 0LL;
		g_capturePause[n] = 0LL;
		g_playbackPause[n] = 0LL;
		g_captureActive[n] = 0;
		g_playbackActive[n] = 0;
		g_playback[n] = NULL;
		g_playbackCount[n] = 0;
		g_playbackIndex[n] = 0;
	}
}

// Issue error to stderr and to cmd pipe output
void IssueError( int cmdPipeIndex, const char *msg, ... )
{
	va_list args;
	va_start( args, msg );
	vfprintf( stderr, msg, args );
	fflush( stderr );
	if (g_cmdFifoOutputFile[cmdPipeIndex])
	{
		vfprintf( g_cmdFifoOutputFile[cmdPipeIndex], msg, args );
		fflush( g_cmdFifoOutputFile[cmdPipeIndex] );
	}
	va_end( args );
}

// Issue non-error results to cmd pipe output
void IssueResponse( int cmdPipeIndex, const char *msg, ... )
{
	va_list args;
	va_start( args, msg );
	if (g_cmdFifoOutputFile[cmdPipeIndex])
	{
		vfprintf( g_cmdFifoOutputFile[cmdPipeIndex], msg, args );
		fflush( g_cmdFifoOutputFile[cmdPipeIndex] );
	}
	else
	{
		fprintf( stderr, "Error: cannot write to output [%d]. Message to be written:\n", cmdPipeIndex );
		vfprintf( stderr, msg, args );
		fflush( stderr );
	}
	va_end( args );
}


// Check for valid command with output pipe
bool isValidCmdIndex( int id )
{
	if (id < 0 || id >= MAX_OPEN_CMDPIPES)
	{
		fprintf( stderr, "Error: invalid cmd index [%d]\n", id );
		return false;
	}
	if (g_cmdFifoOutputFile[id] == NULL)
	{
		fprintf( stderr, "Error: no output for [%d]\n", id );
		return false;
	}
	return true;
}

// Find device by name. Return -1 if not found
int find_device( const char *devname )
{
	int n;
	for (n = 0; n < g_owned_device_count; n++)
	{
		if (!strcasecmp( devname, g_owned_devices[n] ))
		{
			return n;
		}
	}
	return -1;
}

// Read playback data. Return number of entries read or -1 if failed
int read_playback_data( int cmdid, int fh )
{
	if (!isValidCmdIndex( cmdid ))
	{
		return -1;
	}
	FILE *f = fdopen( fh, "r" );
	if (!f)
	{
		IssueError( cmdid, "ERROR [%d] fdopen(%d,'r') failed (errno=%d)\n", cmdid, fh, errno );
		return -1;
	}
	// Handle will be closed later
	g_playbackCount[cmdid] = 0;
	g_playbackIndex[cmdid] = 0;
	// Read all lines into memory first then parse
	#define MAX_LINE_EXTENTS	64
	int extent_size = 1000; // size of next extent in lines
	int extent_sizes[MAX_LINE_EXTENTS]; // sizes of extents in lines
	char **extents[MAX_LINE_EXTENTS]; // Actual extents
	int extent;
	char buff[1024];
	bool reached_eof = false;
	for (extent = 0; extent < MAX_LINE_EXTENTS && !reached_eof; extent++)
	{
		extent_sizes[extent] = extent_size;
		extents[extent] = (char **)alloca( sizeof(char*) * extent_size );
		int line = 0;
		reached_eof = true;
		while (fgets( buff, sizeof(buff), f ))
		{
			char *eol = strchr( buff, '\n' );
			if (eol) *eol = '\0';
			extents[extent][line++] = strdupa( buff );
			if (line >= extent_size)
			{
				reached_eof = false;
				break;
			}
		}
		g_playbackCount[cmdid] += line;
		if (extent_size < 4000)
		{
			// Use more lines in next extent
			extent_size *= 2;
		}
	}
	if (!reached_eof)
	{
		IssueError( cmdid, "WARNING [%d] too many lines in playback input (gave up after %d)\n", cmdid, g_playbackCount[cmdid] );
	}
	// Allocate entries
	g_playback[cmdid] = (PLAYBACK *)malloc( sizeof(PLAYBACK) * g_playbackCount[cmdid] );
	if (!g_playback[cmdid])
	{
		IssueError( cmdid, "ERROR [%d] allocation failed for %lu bytes\n", cmdid, sizeof(PLAYBACK) * g_playbackCount[cmdid] );
		return -1;
	}
	int final_count = 0;
	int input_line = 0;
	int extent_count = extent;
	for (extent = 0; extent < extent_count; extent++)
	{
		int line;
		for (line = 0; line < extent_sizes[extent] && line < g_playbackCount[cmdid]; line++, input_line++)
		{
			char devname[256];
			int devid;
			unsigned long long time_offset;
			int remainder;
			int actual_scanned = sscanf( extents[extent][line], "%s %Lu %n", &devname, &time_offset, &remainder );
			if (actual_scanned >= 2)
			{
				devid = find_device( devname );
				if (devid < 0)
				{
					IssueError( cmdid, "WARNING [%d] ignoring playback line %d: device %s not found\n",
						cmdid, input_line, devname );
				}
				else
				{
					g_playback[cmdid][final_count].offset = time_offset;
					g_playback[cmdid][final_count].devid = devid;
					// Read remaining data based on flags
					if (g_owned_device_flags[devid] & DFLAG_HIDDEV)
					{
						sscanf( &extents[extent][line][remainder], "%hu %hu %ld",
							&g_playback[cmdid][final_count].hid.type,
							&g_playback[cmdid][final_count].hid.code,
							&g_playback[cmdid][final_count].hid.value );
					}
					else
					{
						// FIXME add support for accel
						switch (g_owned_device_blocksize[devid])
						{
							case 1:
							case 2:
								sscanf( &extents[extent][line][remainder], "%hu",
									&g_playback[cmdid][final_count].us[0]
									);
								break;
							case 4:
								sscanf( &extents[extent][line][remainder], "%hu %hu",
									&g_playback[cmdid][final_count].us[0],
									&g_playback[cmdid][final_count].us[1]
									);
								break;
							case 6:
								sscanf( &extents[extent][line][remainder], "%hu %hu %hu",
									&g_playback[cmdid][final_count].us[0],
									&g_playback[cmdid][final_count].us[1],
									&g_playback[cmdid][final_count].us[2]
									);
								break;
							case 8:
								sscanf( &extents[extent][line][remainder], "%hu %hu %hu %hu",
									&g_playback[cmdid][final_count].us[0],
									&g_playback[cmdid][final_count].us[1],
									&g_playback[cmdid][final_count].us[2],
									&g_playback[cmdid][final_count].us[3]
									);
								break;
							default:
								IssueError( cmdid, "WARNING [%d] unsupported block size %d for input line %d\n",
									cmdid, g_owned_device_blocksize[devid], input_line );
								break;
						}
					}
					final_count++;
				}
			}
			else
			{
				fprintf( stderr, "Error [%d] reading %s (x %d line %d) - got only %d/2 required tokens\n", cmdid, extents[extent][line], extent, line, actual_scanned );
			}
		}
	}
	// Set final count - we may have skipped some entries
	g_playbackCount[cmdid] = final_count;
	return final_count;
}

// Free playback data
void free_playback_data( int cmdid )
{
	if (!isValidCmdIndex( cmdid ))
	{
		return;
	}
	int n;
	if (g_playback[cmdid])
	{
		free( g_playback[cmdid] );
		g_playback[cmdid] = NULL;
	}
	g_playbackCount[cmdid] = 0;
	g_playbackIndex[cmdid] = 0;
}


// Close a single command FIFO
void close_cmd_fifo( int n )
{
	if (g_cmdFifoOutput[n])
	{
		free( g_cmdFifoOutput[n] );
		g_cmdFifoOutput[n] = NULL;
	}
	if (g_cmdFifoOutputFile[n])
	{
		fclose( g_cmdFifoOutputFile[n] );
		g_cmdFifoOutputFile[n] = NULL;
	}
	if (g_cmdFifoCapture[n])
	{
		free( g_cmdFifoCapture[n] );
		g_cmdFifoCapture[n] = NULL;
	}
	if (g_cmdFifoCaptureHandle[n] >= 0)
	{
		close( g_cmdFifoCaptureHandle[n] );
		g_cmdFifoCaptureHandle[n] = -1;
	}
	if (g_cmdFifoPlayback[n])
	{
		free( g_cmdFifoPlayback[n] );
		g_cmdFifoPlayback[n] = NULL;
	}
	if (g_cmdFifoPlaybackHandle[n] >= 0)
	{
		close( g_cmdFifoPlaybackHandle[n] );
		g_cmdFifoPlaybackHandle[n] = -1;
	}
	free_playback_data( n );
}

// Close command FIFO structures
void close_cmd_fifos()
{
	int n;
	for (n = 0; n < MAX_OPEN_CMDPIPES; n++)
	{
		close_cmd_fifo( n );
	}
}

// Start capture. Return 0 if successful or -1 if failed
int start_capture( int id, const char *path )
{
	if (!isValidCmdIndex( id ))
	{
		return -1;
	}
	if (g_cmdFifoCaptureHandle[id] >= 0)
	{
		IssueError( id, "ERROR [%d] capture to %s is already active, close before reopening\n",
			id, g_cmdFifoCapture[id] );
		return -1;
	}
	if ((g_cmdFifoCaptureHandle[id] = open( path, O_WRONLY | O_CREAT | O_TRUNC, 0644 )) < 0)
	{
		IssueError( id, "ERROR [%d] capture to %s failed, errno=%d (%s)\n",
			id, path, errno, strerror(errno) );
		return -1;
	}
	g_cmdFifoCapture[id] = strdup( path );
	CAPSTART_SET(id, timeofday_ull());
	g_captureActive[id] = 1;
	return 0;
}

// Pause or resume capture.
int pause_resume_capture( int id, int resume )
{
	if (!isValidCmdIndex( id ))
	{
		return -1;
	}
	if (g_cmdFifoCaptureHandle[id] < 0)
	{
		IssueError( id, "ERROR [%d] capture is not active\n", id );
		return -1;
	}
	if ((resume ^ g_captureActive[id]) == 0)
	{
		IssueError( id, "ERROR [%d] ignoring %s (no change)\n", id, resume ? "resume" : "pause" );
		return -1;
	}
	// If pausing, save time of pause
	if (!resume)
	{
		CAPPAUSETIME_SET(id, timeofday_ull());
	}
	// If resuming, add length of pause to offset
	else
	{
		unsigned long long u = timeofday_ull();
		u -= g_capturePause[id];
		g_captureStart[id] += u;
	}
	g_captureActive[id] = resume;
	return 0;
}

// Capture a record, which should be either a single byte or a hiddev 16-byte record
int capture( int devid, int cmdid, unsigned char *data )
{
	// Return quietly if not active
	if (!isValidCmdIndex( cmdid ))
	{
		return -1;
	}
	if (!g_captureActive[cmdid])
	{
		return -1;
	}
	// Do we need to capture time of day?
	char buff[1024];
	int buff_len;
	if (!(g_owned_device_flags[devid] & DFLAG_HIDDEV))
	{
		buff_len = sprintf( buff, "%s %Lu %u\n",
			g_owned_devices[devid],
			timeofday_ull() - g_captureStart[cmdid],
			*data );
	}
	else
	{
		struct input_event *pie = (struct input_event *)data;
		unsigned long long ull = tv_to_ull( &pie->time );
		static unsigned long long tod_ull = 0LL;
		if (!tod_ull)
		{
			tod_ull = timeofday_ull();
			fprintf( stderr, "First device time %Lu - gettimeofday() returns %Lu, diff %Ld\n",
				ull, tod_ull, tod_ull - ull );
			fflush( stderr );
			tod_ull -= ull;
		}
		// Apply difference between gettimeofday and device time
		// We should probably be using clock_gettime() with CLOCK_MONOTONIC
		ull += tod_ull;
		buff_len = sprintf( buff, "%s %Lu %u %u %lu\n", 
			g_owned_devices[devid], 
			ull - g_captureStart[cmdid],
			pie->type, pie->code, pie->value );
	}
	int bytes_written = write( g_cmdFifoCaptureHandle[cmdid], buff, buff_len );
	return (bytes_written == buff_len) ? 0 : -1;
}

// Save record to any capturing command ids. Return number captured or -1 if error
int capture_all( int devid, unsigned char *data )
{
	int n;
	int error_count = 0;
	int total = 0;
	for (n = 0; n < MAX_OPEN_CMDPIPES; n++)
	{
		if (g_captureActive[n])
		{
			if (capture( devid, n, data ) < 0)
			{
				error_count++;
			}
			total++;
		}
	}
	return (error_count ? error_count : total);
}

// End capture
int end_capture( int id )
{
	if (!isValidCmdIndex( id ))
	{
		return -1;
	}
	if (g_cmdFifoCaptureHandle[id] < 0)
	{
		IssueError( id, "ERROR [%d] capture is not active\n", id );
		return -1;
	}
	close( g_cmdFifoCaptureHandle[id] );
	g_cmdFifoCaptureHandle[id] = -1;
	if (g_cmdFifoCapture[id])
	{
		free( g_cmdFifoCapture[id] );
		g_cmdFifoCapture[id] = NULL;
	}
	return 0;
}

// Start playback from file. Return 0 if successful or -1 if failed
int start_playback( int id, const char *path )
{
	if (!isValidCmdIndex( id ))
	{
		return -1;
	}
	if (g_cmdFifoPlaybackHandle[id] >= 0)
	{
		IssueError( id, "ERROR [%d] playback from %s is already active, close before reopening\n",
			id, g_cmdFifoPlayback[id] );
		return -1;
	}
	if ((g_cmdFifoPlaybackHandle[id] = open( path, O_RDONLY)) < 0)
	{
		IssueError( id, "ERROR [%d] playback from %s failed, errno=%d (%s)\n",
			id, path, errno, strerror(errno) );
		return -1;
	}
	int entries_read = read_playback_data( id, g_cmdFifoPlaybackHandle[id] );
	if (entries_read < 0)
	{
		close( g_cmdFifoPlaybackHandle[id] );
		g_cmdFifoPlaybackHandle[id] = -1;
		return -1;
	}
	g_cmdFifoPlayback[id] = strdup( path );
	PLAYSTART_SET(id, timeofday_ull());
	g_playbackActive[id] = 1;
	IssueResponse( id, "OK [%d] started playback from %s (%d entries)\n",
		id, path, entries_read );
	return 0;
}

// Pause or resume playback
int pause_resume_playback( int id, int resume )
{
	if (!isValidCmdIndex( id ))
	{
		return -1;
	}
	if (g_cmdFifoPlaybackHandle[id] < 0)
	{
		IssueError( id, "ERROR [%d] playback is not active\n", id );
		return -1;
	}
	if ((resume ^ g_playbackActive[id]) == 0)
	{
		IssueError( id, "ERROR [%d] ignoring %s (no change)\n", id, resume ? "resume" : "pause" );
		return -1;
	}
	// If pausing, save time of pause
	if (!resume)
	{
		PLAYPAUSETIME_SET(id, timeofday_ull());
	}
	// If resuming, add length of pause to offset
	else
	{
		unsigned long long u = timeofday_ull();
		u -= g_playbackPause[id];
		g_playbackStart[id] += u;
	}
	g_playbackActive[id] = resume;
	return 0;
}

// Abort playback
int end_playback( int id )
{
	if (!isValidCmdIndex( id ))
	{
		return -1;
	}
	g_playbackActive[id] = 0;
	if (g_cmdFifoPlaybackHandle[id] < 0)
	{
		IssueError( id, "ERROR [%d] playback is not active\n", id );
		return -1;
	}
	close( g_cmdFifoPlaybackHandle[id] );
	g_cmdFifoPlaybackHandle[id] = -1;
	if (g_cmdFifoPlayback[id])
	{
		free( g_cmdFifoPlayback[id] );
		g_cmdFifoPlayback[id] = NULL;
	}
	free_playback_data( id );
	return 0;
}

// Inject playback events
// Close playback if completed
int playback( int cmdid, unsigned long long tod_ull )
{
	if (!isValidCmdIndex( cmdid ))
	{
		return -1;
	}
	if (g_cmdFifoPlaybackHandle[cmdid] < 0 || !g_playbackActive[cmdid])
	{
		IssueError( cmdid, "ERROR [%d] playback is not active (handle=%d active=%d)\n", cmdid, g_cmdFifoPlaybackHandle[cmdid], g_playbackActive[cmdid] );
		return -1;
	}
	int events_injected = 0;
	// Get current offset
	unsigned long long current_offset_ull = tod_ull - g_playbackStart[cmdid];
	while (g_playbackIndex[cmdid] < g_playbackCount[cmdid])
	{
		// Is it time for this event yet?
		if (g_playback[cmdid][g_playbackIndex[cmdid]].offset > current_offset_ull)
		{
			return events_injected;
		}
		// Send this event
		int current_index = g_playbackIndex[cmdid]++;
		int devid = g_playback[cmdid][current_index].devid;
		int blocksize = g_owned_device_blocksize[devid];
		if (g_owned_device_flags[devid] & DFLAG_HIDDEV)
		{
			struct timeval tv;
			ull_to_tv( g_playback[cmdid][current_index].offset + g_playbackStart[cmdid], &tv );
			write( g_owned_fifo_handles[devid], &tv, sizeof(tv) );
			write( g_owned_fifo_handles[devid], &g_playback[cmdid][current_index].hid, sizeof(HIDDEV_PLAYBACK) );
		}
		else
		{
			write( g_owned_fifo_handles[devid], &g_playback[cmdid][current_index].uc[0], g_owned_device_blocksize[devid] );
		}
		events_injected++;
	}
	// Reached end
	IssueResponse( cmdid, "OK [%d] completed playback\n", cmdid );
	end_playback( cmdid );
	return events_injected;
}

/****
 * Command input syntax
noop
 * do nothing - sent from child process to wake up pipe open
quit
 * exit immediately
dbg <lvl>
 * set specified debug level
open path
 * open path for output, returns unique id 0-15 on first line of output file if successful
 * id must prefix all other commands
<id> close
 * close path and invalidate id
<id> capture output-path [device_list]
 * start capture to output-path. If optional device list is specified,
 * only the specified devices (as on the command line, switch, input/event0, etc) will be
 * captured, otherwise all devices will be captured
<id> pausecap
<id> resumecap
 * pause and resume capture
<id> endcap
 * closes capture
<id> playback input-path
 * start playback of input-path
<id> pauseplay
<id> resumeplay
 * pause and resume playback
<id> endplay
 * abort playback
*****/
// Process a command in a single NL-terminated input line
int process_cmd( const char *line )
{
	int id;
	int token_count;
	char cmd[256];
	char path[1024];
	int remainder;
	// open takes a path, all others are prefixed by the index
	if (!strncmp( line, "open", 4 ))
	{
		token_count = sscanf( line, "%s %s", cmd, path );
		// Find unused entry
		for (id = 0; id < MAX_OPEN_CMDPIPES; id++)
		{
			if (g_cmdFifoOutput[id] == NULL)
			{
				if (g_cmdFifoOutputFile[id] = fopen( path, "w" ))
				{
					g_cmdFifoOutput[id] = strdup( path );
					fprintf( g_cmdFifoOutputFile[id], "%d\n", id );
					fflush( g_cmdFifoOutputFile[id] );
					return 0;
				}
				else
				{
					fprintf( stderr, "Failed to open %s in response to request (errno=%d [%s]): %s\n", 
						path, errno, strerror(errno), line );
					return -1;
				}
			}
		}
		FILE *tmp = fopen( path, "w" );
		if (tmp)
		{
			fprintf( tmp, "%d\nMax open cmd pipes (%d) exceeded\n", -1, MAX_OPEN_CMDPIPES );
			fclose( tmp );
		}
		fprintf( stderr, "No free cmd pipes for request %s", line );
		return -1;
	}
	else if (!strncmp( line, "noop", 4 ))
	{
		return 0;
	}
	else if (!strncmp( line, "quit", 4 ))
	{
		g_running = 0;
		return 0;
	}
	else if (!strncmp( line, "dbg", 3 ))
	{
		g_dbgLevel = atoi( &line[4] );
		return 0;
	}
	else
	{
		token_count = sscanf( line, "%d %s %n", &id, cmd, &remainder );
		if (token_count < 1)
		{
			fprintf( stderr, "Insufficient args on command pipe in %s", line );
			return -1;
		}
		if (id < 0 || id >= MAX_OPEN_CMDPIPES)
		{
			fprintf( stderr, "Invalid index %d specified\n", id );
			return -1;
		}
		if (g_cmdFifoOutputFile[id] == NULL)
		{
			fprintf( stderr, "Output for index %d (%s) already closed\n", id, g_cmdFifoOutput[id] );
			return -1;
		}
		// Now we can actually write results to g_cmdFifoOutputFile[id]
		// Skip leading whitespace
		remainder += strspn( &line[remainder], " \t\r\n" );
		char *szCmdArgs = strdupa( &line[remainder] );
		char *szCmdArgsNL = strchr( szCmdArgs, '\n' );
		if (szCmdArgsNL)
		{
			*szCmdArgsNL = '\0';
		}
		// Process commands with no other arguments required
		if (!strcasecmp( cmd, "close" ))
		{
			IssueResponse( id, "OK [%d] closing\n", id );
			close_cmd_fifo( id );
			return 0;
		}
		if (!strcasecmp( cmd, "endcap" ))
		{
			IssueResponse( id, "OK [%d] ending capture\n", id );
			end_capture( id );
			return 0;
		}
		if (!strcasecmp( cmd, "endplay" ))
		{
			IssueResponse( id, "OK [%d] aborting playback\n", id );
			end_playback( id );
			return 0;
		}
		if (!strcasecmp( cmd, "pausecap" ))
		{
			if (!pause_resume_capture( id, 0 ))
			{
				IssueResponse( id, "OK [%d] pausing capture\n", id );
				return 0;
			}
			return -1;
		}
		if (!strcasecmp( cmd, "pauseplay" ))
		{
			if (!pause_resume_playback( id, 0 ))
			{
				IssueResponse( id, "OK [%d] pausing playback\n", id );
				return 0;
			}
			return -1;
		}
		if (!strcasecmp( cmd, "resumecap" ))
		{
			if (!pause_resume_capture( id, 1 ))
			{
				IssueResponse( id, "OK [%d] resuming capture\n", id );
				return 0;
			}
			return -1;
		}
		if (!strcasecmp( cmd, "resumeplay" ))
		{
			if (!pause_resume_playback( id, 1 ))
			{
				IssueResponse( id, "OK [%d] resuming playback\n", id );
				return 0;
			}
			return -1;
		}
		// At least one argument is required
		if (!*szCmdArgs)
		{
			IssueError( id, "ERROR [%d] insufficient arguments for %s\n", id, cmd );
			return -1;
		}
		if (!strcasecmp( cmd, "capture" ))
		{
			if (start_capture( id, szCmdArgs ) < 0)
			{
				IssueError( id, "ERROR [%d] failed to start capture to %s\n", id, szCmdArgs );
				return -1;
			}
			IssueResponse( id, "OK [%d] capturing to %s\n", id, szCmdArgs );
			return 0;
		}
		if (!strcasecmp( cmd, "playback" ))
		{
			if (start_playback( id, szCmdArgs ) < 0)
			{
				IssueError( id, "ERROR [%d] failed to open %s for playback\n", id, szCmdArgs );
				return -1;
			}
			IssueResponse( id, "OK [%d] started playback of %s\n", id, szCmdArgs );
			return 0;
		}
		// Unrecognized
		IssueError( id, "ERROR [%d] unrecognized command %s\n", id, cmd );
	}
	return -1;
}

// signal handler
void ChumbySigAction( int signum, siginfo_t *siginfo, void *data )
{
        // Get context - linux-specific
        ucontext_t *context = (ucontext_t*)data;
        switch (signum)
        {
        	case SIGPIPE:
			fprintf( stderr, "SIGPIPE: caught, aborting\n" );
			g_running = 0;
			break;
		case SIGINT:
		case SIGTERM:
			fprintf( stderr, "SIGINT / SIGTERM (%d): exiting\n", signum );
			g_running = 0;
			break;
		case SIGSEGV:
			fprintf( stderr, "SIGSEGV: exiting immediately\n" );
			g_running = 0;
			exit( -1 );
			break;
		default:
			fprintf( stderr, "Unhandled signal %d\n", signum );
			break;
	}
}

int main( int argc, char *argv[] )
{
	printf( "%s v" VER_STR "\n", argv[0] );

	int n;
	int cmds_processed = 0;
	int cmds_failed = 0;
	for (n = 1; n < argc; n++)
	{
		if (argv[n][0] == '-')
		{
			// Process options
		}
		else
		{
			if (g_owned_device_count >= MAX_OWNED)
			{
				fprintf( stderr, "Max number of devices (%d) exceeded\n", MAX_OWNED );
				return -1;
			}
			g_owned_devices[g_owned_device_count++] = argv[n];
		}
	}

	if (g_owned_device_count < 1)
	{
		fprintf( stderr, "No devices specified\n" );
		return -1;
	}

	printf( "Checking %d devices for current node status...\n", g_owned_device_count );
	int devnodes_failed = 0;
	int exist_failed = 0;
	struct stat file_info;
	for (n = 0; n < g_owned_device_count; n++)
	{
		char devPath[256];
		sprintf( devPath, "/dev/%s", g_owned_devices[n] );
		printf( "  [%s] ", devPath );
		if (stat( devPath, &file_info ))
		{
			printf( "- not found\n" );
			fprintf( stderr, "stat(%s) failed, errno=%d (%s)\n", devPath, errno, strerror(errno) );
			exist_failed++;
			continue;
		}
		if (S_ISCHR( file_info.st_mode ))
		{
			printf( "- char device (OK)\n" );
		}
		else
		{
			printf( "- not a char device (FAILED)\n" );
			devnodes_failed++;
		}
	}

	if (exist_failed || devnodes_failed)
	{
		fprintf( stderr, "%d errors detected\n", exist_failed + devnodes_failed );
		return -1;
	}

	const char *emergency_script = "/tmp/test_dev_shimd_recover.sh";
	printf( "Creating emergency restore script %s\n", emergency_script );
	FILE *fEmergency = fopen( emergency_script, "w" );
	if (!fEmergency)
	{
		fprintf( stderr, "Failed to create %s\n", emergency_script );
		return -1;
	}
	fprintf( fEmergency, "#!/bin/sh\n" );
	fprintf( fEmergency, "# Written by %s\n", argv[0] );
	for (n = 0; n < g_owned_device_count; n++)
	{
		fprintf( fEmergency, "if [ -e /dev/%s.org ]; then\n", g_owned_devices[n] );
		fprintf( fEmergency, " rm -f /dev/%s; mv /dev/%s.org /dev/%s\n", g_owned_devices[n], g_owned_devices[n], g_owned_devices[n] );
		fprintf( fEmergency, "else\n" );
		fprintf( fEmergency, " echo \"/dev/%s.org not found\"\n", g_owned_devices[n] );
		fprintf( fEmergency, "fi\n" );
	}
	fprintf( fEmergency, "# End script\n" );
	fclose( fEmergency );
	chmod( emergency_script, 0755 );

	printf( "Handling signals\n" );
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
        sigaction( SIGINT, &segv_handler, &old_segv );
        sigaction( SIGTERM, &segv_handler, &old_segv );

	printf( "Creating command input fifo %s\n", CMD_INPUT_FIFO );
	unlink( CMD_INPUT_FIFO );
	mkfifo( CMD_INPUT_FIFO, 0777 );
	fflush( stdout );
	pid_t cmdChild = fork();
	if (cmdChild)
	{
		// This will block until someone begins writing
		// We'll do that from the child process
		printf( "Opening command input pipe - child process %u should wake us up...\n", cmdChild );
		fflush( stdout );
		g_cmdInputFifo = fopen( CMD_INPUT_FIFO, "r" );
		if (!g_cmdInputFifo)
		{
			fprintf( stderr, "Failed to open %s r/o (errno=%d [%s])\n", CMD_INPUT_FIFO, errno, strerror(errno) );
			return -1;
		}
		// Allow the child process time to exit
		usleep( 400000L );
	}
	else
	{
		usleep( 250000L );
		printf( "Issuing noop command from child process %u to wake up parent\n", getpid() );
		FILE *wtmp = fopen( CMD_INPUT_FIFO, "w" );
		if (wtmp)
		{
			fprintf( wtmp, "noop\n" );
			fclose( wtmp );
		}
		else
		{
			fprintf( stderr, "Failed to open command pipe, errno=%d\n", errno );
		}
		exit( 0 );
	}

	init_cmd_fifos();

	printf( "Creating FIFO entries and renaming dev entries to .org\n" );
	fflush( stdout );
	for (n = 0; n < g_owned_device_count; n++)
	{
		char devPath[256];
		char orgPath[256];
		sprintf( devPath, "/dev/%s", g_owned_devices[n] );
		sprintf( orgPath, "/dev/%s.org", g_owned_devices[n] );
		unlink( orgPath );
		printf( " %s -> %s", devPath, orgPath );
		rename( devPath, orgPath );
		printf( ", created FIFO %s\n", devPath );
		mkfifo( devPath, 0644 );
	}

	printf( "Opening device inputs and FIFO outputs\n" );
	int max_rfd = 0;
	int max_wfd = 0;
	for (n = 0; n < g_owned_device_count; n++)
	{
		char fifoPath[256];
		char devPath[256];
		sprintf( fifoPath, "/dev/%s", g_owned_devices[n] );
		sprintf( devPath, "/dev/%s.org", g_owned_devices[n] );
		g_blocks_read[n] = 0;
		g_blocks_written[n] = 0;
		g_dupes_skipped[n] = 0;
		g_lastbyte[n] = 0xff;
		if (!strcasecmp( g_owned_devices[n], "switch" ))
		{
			g_owned_device_blocksize[n] = 1;
			g_owned_device_flags[n] = DFLAG_LAST | DFLAG_KILLDUPES;
		}
		// else if (!strcasecmp( g_owned_devices[n], "accel" ))
		//{
		//}
		else
		{
			g_owned_device_blocksize[n] = 16;
			g_owned_device_flags[n] = DFLAG_LOG;
			if (!strncmp( g_owned_devices[n], "input/event", 11 ))
			{
				g_owned_device_flags[n] |= DFLAG_HIDDEV;
			}
		}
		if ((g_owned_device_handles[n] = open( devPath, O_RDONLY )) < 0)
		{
			fprintf( stderr, "Failed to open %s r/o - errno=%d (%s)\n", devPath, errno, strerror(errno) );
		}
		else 
		{
			printf( " opened %s - handle = %d, block size=%d bytes\n", devPath, g_owned_device_handles[n], g_owned_device_blocksize[n] );
			if (g_owned_device_handles[n] > max_rfd)
			{
				max_rfd = g_owned_device_handles[n];
			}
		}
		if ((g_owned_fifo_handles[n] = open( fifoPath, O_WRONLY )) < 0)
		{
			fprintf( stderr, "Failed to open %s w/o - errno=%d (%s)\n", fifoPath, errno, strerror(errno) );
		}
		else if (g_owned_fifo_handles[n] > max_wfd)
		{
			max_wfd = g_owned_fifo_handles[n];
		}
	}

	printf( "Processing input with %d read handles\n", max_rfd + 1 );
	fflush( stdout );
	fflush( stderr );
	struct timeval t;
	fd_set rfds;
	fd_set xfds;
	unsigned long iterations = 0;
	while (g_running)
	{
		iterations++;
		t.tv_sec = 0;
		t.tv_usec = 0;
		FD_ZERO( &rfds );
		FD_ZERO( &xfds );
		for (n = 0; n < g_owned_device_count; n++)
		{
			if (g_owned_device_handles[n] >= 0)
			{
				FD_SET( g_owned_device_handles[n], &rfds );
			}
			if (g_owned_fifo_handles[n] >= 0)
			{
				FD_SET( g_owned_fifo_handles[n], &xfds );
			}
		}
		FD_SET( fileno( g_cmdInputFifo ), &rfds );
		// Check for read ready and exceptions on the write pipes
		int bits_ready = select( max_rfd + 1, &rfds, NULL, &xfds, &t );
		// Check for playback
		int cmdid;
		for (cmdid = 0; cmdid < MAX_OPEN_CMDPIPES; cmdid++)
		{
			if (!g_playbackActive[cmdid])
			{
				continue;
			}
			playback( cmdid, timeofday_ull() );
		}
		if (bits_ready < 1)
		{
			continue;
		}
		// Process commands first
		if (FD_ISSET( fileno( g_cmdInputFifo ), &rfds ))
		{
			char lineBuff[1024];
			if (fgets( lineBuff, sizeof(lineBuff), g_cmdInputFifo ))
			{
				int lineRes = process_cmd( lineBuff );
				cmds_processed++;
				if (lineRes < 0)
				{
					cmds_failed++;
				}
			}
		}

		// If any exceptions occurred on output fifo handles, abort
		// We don't seem to get these when the program at the other end closes,
		// perhaps if it is killed by a signal such as SIGKILL...
		// Process inputs
		for (n = 0; n < g_owned_device_count; n++)
		{
			if (g_owned_fifo_handles[n] >= 0 && FD_ISSET( g_owned_fifo_handles[n], &xfds ))
			{
				fprintf( stderr, "Exception on handle %d (%s) - aborting\n",
					n, g_owned_devices[n] );
				g_running = 0;
				break;
			}
			if (g_owned_device_handles[n] >= 0 && FD_ISSET( g_owned_device_handles[n], &rfds ))
			{
				unsigned char buff[1024];
				int bytes_to_read = g_owned_device_blocksize[n];
				int min_bytes_to_read = bytes_to_read;
				int bytes_read;
				int offset = 0;
				if (g_owned_device_flags[n] & DFLAG_LAST)
				{
					bytes_to_read = sizeof(buff);
				}
				bytes_read = read( g_owned_device_handles[n], buff, bytes_to_read );
				if (bytes_read >= min_bytes_to_read )
				{
					g_blocks_read[n] += (bytes_read / min_bytes_to_read);
					if (g_owned_device_flags[n] & DFLAG_LAST)
					{
						offset = bytes_read - min_bytes_to_read;
					}
					if (g_owned_device_flags[n] & DFLAG_KILLDUPES)
					{
						if (buff[offset] == g_lastbyte[n])
						{
							g_dupes_skipped[n]++;
							continue;
						}
						else
						{
							g_lastbyte[n] = buff[offset];
						}
					}
					// Capture if recording in progress
					capture_all( n, &buff[offset] );
					if (write( g_owned_fifo_handles[n], &buff[offset], g_owned_device_blocksize[n] ) != g_owned_device_blocksize[n] )
					{
						fprintf( stderr, "Write failed on %s\n", g_owned_devices[n] );
						fflush( stderr );
					}
					else 
					{
						g_blocks_written[n]++;
						if (g_dbgLevel > 0 && (g_owned_device_flags[n] & DFLAG_LOG))
						{
							fprintf( stderr, "wrote %d bytes for %s: %02x %02x %02x %02x...\n", g_owned_device_blocksize[n], g_owned_devices[n], buff[0], buff[1], buff[2], buff[3] );
							fflush( stderr );
						}
					}
				}
				else
				{
					fprintf( stderr, "Short read on %s\n", g_owned_devices[n] );
					fflush( stderr );
				}
			}
		}
	}

	printf( "Closing FIFO outputs after %lu iterations\n", iterations );
	for (n = 0; n < g_owned_device_count; n++)
	{
		printf( " [%d] [%s] handle=%d flags 0x%04x blocks read=%lu written=%lu skipped=%lu\n",
			n, g_owned_devices[n], g_owned_device_handles[n], g_owned_device_flags[n], 
			g_blocks_read[n], g_blocks_written[n], g_dupes_skipped[n] );
		if (g_owned_device_handles[n] >= 0)
		{
			close( g_owned_device_handles[n] );
			g_owned_device_handles[n] = -1;
		}
		if (g_owned_fifo_handles[n] >= 0)
		{
			close( g_owned_fifo_handles[n] );
			g_owned_fifo_handles[n] = -1;
		}
	}

	printf( "Restoring original device entries\n" );
	for (n = 0; n < g_owned_device_count; n++)
	{
		char devPath[256];
		char orgPath[256];
		sprintf( devPath, "/dev/%s", g_owned_devices[n] );
		sprintf( orgPath, "/dev/%s.org", g_owned_devices[n] );
		unlink( devPath );
		printf( " %s -> %s\n", orgPath, devPath );
		rename( orgPath, devPath );
	}

	printf( "Closing command input; %d/%d cmds failed\n", cmds_failed, cmds_processed );
	fclose( g_cmdInputFifo );

	printf( "Closing outputs\n" );
	close_cmd_fifos();

	return 0;
}

