// $Id$
// tscat.cpp - dump /dev/ts0
// Copyright (C) 2008 Chumby Industries, Inc. All rights reserved.
// henry@chumby.com

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>

#define VER_STR "0.16"

volatile int g_quit = 0;

struct ts_event {
	unsigned short pressure;
	unsigned short x;
	unsigned short y;
	unsigned short pad;
};

int fbWidth = 800;
int fbHeight = 600;

// Signal handler
void sigHandler( int sigNum );
void sigHandler( int sigNum )
{
	switch (sigNum)
	{
		case SIGINT:
		case SIGTERM:
			printf( "Setting quit flag for signal %d - touch screen to exit\n", sigNum );
			g_quit++;
			if (g_quit < 5)
			{
				signal( sigNum, sigHandler );
			}
			else if (g_quit == 5)
			{
				printf( "Leaving signal handler to default - next SIGINT will abort\n" );
			}
			break;
		default:
			printf( "Unknown signal %d not handled\n", sigNum );
			break;
	}
	return;
}

// ts offset and range conversion
int tsXOff = 45;
int tsYOff = 45;
int tsXRange = 270 - tsXOff;
int tsYRange = 190 - tsYOff;
int tsXFlip = 0;
int tsYFlip = 0;
void GetTSSettings()
{
	// Read from /psp/ts_settings
	FILE *f = fopen( "/psp/ts_settings", "r" );
	if (!f)
	{
		printf( "Error: could not open /psp/ts_settings (errno=%d) (%s)\n", errno, strerror(errno) );
		return;
	}
	fscanf( f, "%d,%d,%d,%d\n", &tsXOff, &tsXRange, &tsYOff, &tsYRange );
	fscanf( f, "%d,%d\n", &tsXFlip, &tsYFlip );
	fclose( f );
	printf( "Read settings: x,y offset = %d,%d; range = %d,%d; flip = %d,%d\n",
		tsXOff, tsYOff,
		tsXRange, tsYRange,
		tsXFlip, tsYFlip );
}

// RGB 565
unsigned short fbColor =
	(0x1f << 10)
	| (0x7 << 5)
	| (0xe);
// Write to head, read from tail
int tailHead = 0;
int tailTail = 0;
int tailCount = 0;
unsigned short tail[] = {
	0, 0, 0, 0, 0
};
#define TAILMAX (sizeof(tail)/sizeof(tail[0]))
unsigned short tailX[TAILMAX];
unsigned short tailY[TAILMAX];

// Frame buffer handle
int fbHandle;
// Memory mapped frame buffer
unsigned short *fb = NULL;

// Blast value into pixel
void _SetPixel( int x, int y, unsigned short value )
{
	fb[y*fbWidth+x] = value;
}

// Get pixel contents
unsigned short _GetPixel( int x, int y )
{
	return fb[y*fbWidth+x];
}

// Push pixel contents to tail queue
void PushPixel( int x, int y )
{
}

// Pop pixel contents from tail queue
void PopPixel()
{
}


// Set pixel to fbColor
void SetPixel( int x, int y )
{
	if (x < 0 || y < 0 || x >= fbWidth || y >= fbHeight)
	{
		return;
	}
	if (tailHead == tailTail && tailCount > 0)
	{
		// Restore end
		PopPixel();
	}
	_SetPixel( x, y, fbColor );
}

int main( int argc, char *argv[] )
{
  unsigned min_x = 10000;
  unsigned min_y = 10000;
  unsigned max_x = 0;
  unsigned max_y = 0;
  char *tsdev = "/dev/input/ts0";
  char *fbdev = "/dev/fb0";
  char sView[256] = ",x,y,rawx,rawy,minx,miny,maxx,maxy,";
  char helpMsg[] = "Syntax: %s [--fb=fbdev] [--ts=tsdev] [--scrw=800] [--scrh=600] [--view=x,y,rawx,rawy,minx,miny,maxx,maxy]\n";
  int n;
  for (n = 1; n < argc; n++)
  {
  	if (!strncmp( argv[n], "--fb=", 5 ))
  	{
  		fbdev = &argv[n][5];
  	}
  	else if (!strncmp( argv[n], "--ts=", 5 ))
  	{
  		tsdev = &argv[n][5];
  	}
  	else if (!strncmp( argv[n], "--scrw=", 7 ))
  	{
  		fbWidth = atoi( &argv[n][7] );
  	}
  	else if (!strncmp( argv[n], "--scrh=", 7 ))
  	{
  		fbHeight = atoi( &argv[n][7] );
  	}
  	else if (!strcmp( argv[n], "--help" ))
  	{
  		printf( helpMsg, argv[0] );
  		return -1;
  	}
	else if (!strncmp( argv[n], "--view=", 7 ))
	{
		strcpy( &sView[1], &argv[n][7] );
		strcat( sView, "," );
	}
  	else
  	{
  		printf( "Unrecognized option %s\n", argv[n] );
  		printf( helpMsg, argv[0] );
  		return -1;
  	}
  }
  printf( "tscat v" VER_STR " - reading from %s - fbColor=%x - ctrl-C to end\n", tsdev, fbColor );
  GetTSSettings();
  int fh = open( tsdev, O_RDONLY );
  if (fh < 1)
  {
	printf( "Error: cannot open %s (errno=%d): %s\n", tsdev, errno, strerror(errno) );
	return -1;
  }
  fbHandle = open( fbdev, O_RDWR );
  if (fbHandle < 0)
  {
  	printf( "Error: cannot open %s (errno=%d): %s\n", fbdev, errno, strerror(errno) );
  	return -1;
  }
  fb = (unsigned short*)mmap( 0,
				fbHeight * fbWidth * sizeof(short),
				PROT_READ | PROT_WRITE,
				MAP_SHARED,
				fbHandle,
				0);
  if (fb == NULL)
  {
  	printf( "Error: mmap() failed (errno=%d): %s\n", errno, strerror(errno) );
  	return -1;
  }
  memset( fb, 0, fbHeight * fbWidth * sizeof(short) );

  struct ts_event e;
  int bytesRead;

  signal( SIGINT, sigHandler );

  // Use range divide method to generate a statistical map of raw results on each axis
  // Divide 4000 pixel x axis into 100 parts - same with 3000 pixel y axis. Thus
  // we have 10000 40 x 30 unit blocks mapped individually per axis.
  int xRange[100];
  int yRange[100];
  int xRange_count = 100;
  int yRange_count = 100;
  int xRange_divisor = 4000 / xRange_count;
  int yRange_divisor = 3000 / yRange_count;

  memset( xRange, 0, sizeof(xRange) );
  memset( yRange, 0, sizeof(yRange) );

  while ((bytesRead = read( fh, &e, sizeof(e) )) >= sizeof(e) && !g_quit)
  {
  	// Update raw minima / maxima
  	if (e.x < min_x) min_x = e.x;
  	if (e.y < min_y) min_y = e.y;
  	if (e.x > max_x) max_x = e.x;
  	if (e.y > max_y) max_y = e.y;
  	// Determine indices into stats
  	int unitX = (e.x / xRange_divisor) % xRange_count;
  	int unitY = (e.y / yRange_divisor) % yRange_count;
  	xRange[unitX]++;
  	yRange[unitY]++;
  	int x, y;
  	x = (e.x - tsXOff) * fbWidth / tsXRange;
  	y = (e.y - tsYOff) * fbHeight / tsYRange;
	int fieldsDisplayed = 0;
  	printf( "x,y = (%u,%u) {%d,%d} pressure = %d min=%u,%u max=%u,%u pad=%u (0x%x)\n",
  		e.x, e.y, x, y, e.pressure, min_x, min_y, max_x, max_y, e.pad, e.pad );
	if (fieldsDisplayed > 0)
	{
		putchar( '\n' );
	}
  	SetPixel( tsXFlip ? fbWidth-x : x, tsYFlip ? fbHeight-y : y );
  }
  if (g_quit)
  {
  	printf( "Quit requested\n" );
  }
  else
  {
	printf( "eof or error (errno=%d)\n", errno );
  }
  close( fh );

  printf( "x raw min, max, range = {%d,%d,%d}\n", min_x, max_x, max_x - min_x );
  printf( "y raw min, max, range = {%d,%d,%d}\n", min_y, max_y, max_y - min_y );

  // Analyze frequency
  int nz_count;
  int axis_sum, axis_min, axis_max;
  axis_sum = 0;
  axis_min = 100000;
  axis_max = 0;
  for (n = 0, nz_count = 0; n < xRange_count; n++)
  {
  	if (xRange[n] == 0) continue;
  	axis_sum += xRange[n];
  	nz_count++;
  	if (xRange[n] < axis_min) axis_min = xRange[n];
  	if (xRange[n] > axis_max) axis_max = xRange[n];
  }
  if (nz_count > 0)
  {
  	double dAvg = axis_sum / (double)nz_count;
  	printf( "x axis has %d nz entries, min=%d, max=%d, avg=%.1f\n",
		nz_count, axis_min, axis_max, dAvg );
	// Find min and max with count at least 20% of avg
	int avg_min = (int)(dAvg / 5);
	int new_min = -1;
	int new_max = -1;
	if (avg_min > 1)
	{
		for (n = 0; n < xRange_count; n++)
		{
			if (xRange[n] < avg_min) continue;
			if (new_min < 0) new_min = n;
			new_max = n;
		}
		if (new_min >= 0 && new_max >= 0)
		{
			min_x = new_min * xRange_divisor;
			max_x = new_max * xRange_divisor;
			printf( "Got new xmin = %d, xmax = %d\n", min_x, max_x );
		}
	}
  }
  axis_sum = 0;
  axis_min = 100000;
  axis_max = 0;
  for (n = 0, nz_count = 0; n < yRange_count; n++)
  {
  	if (yRange[n] == 0) continue;
  	axis_sum += yRange[n];
  	nz_count++;
  	if (yRange[n] < axis_min) axis_min = yRange[n];
  	if (yRange[n] > axis_max) axis_max = yRange[n];
  }
  if (nz_count > 0)
  {
  	double dAvg = axis_sum / (double)nz_count;
  	printf( "y axis has %d nz entries, min=%d, max=%d, avg=%.1f\n",
		nz_count, axis_min, axis_max, dAvg );
	// Find min and max with count at least 20% of avg
	int avg_min = (int)(dAvg / 5);
	int new_min = -1;
	int new_max = -1;
	if (avg_min > 1)
	{
		for (n = 0; n < yRange_count; n++)
		{
			if (yRange[n] < avg_min) continue;
			if (new_min < 0) new_min = n;
			new_max = n;
		}
		if (new_min >= 0 && new_max >= 0)
		{
			min_y = new_min * yRange_divisor;
			max_y = new_max * yRange_divisor;
			printf( "Got new ymin = %d, ymax = %d\n", min_y, max_y );
		}
	}
  }

  printf( "try:\necho -n \"%d,%d,%d,%d\" > /psp/ts_settings\n", min_x, max_x-min_x, min_y, max_y-min_y );
  printf( "y inverted:\necho -n \"%d,%d,%d,%d\" > /psp/ts_settings\n", min_x, max_x-min_x, max_y, -(max_y - min_y) );
  return 0;
}
