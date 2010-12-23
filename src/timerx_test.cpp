// $Id$
// timerx_test.cpp - test resolution of timerx

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <math.h>

// Get value from device handle or CLOCK_MONOTONIC / CLOCK_REALTIME
// if handle is < 0
unsigned int read_timer( int h )
{
	unsigned int v;
	if (h > 0)
	{
		if (read( h, &v, sizeof(v) ) < sizeof(v))
		{
			fprintf( stderr, "Error: failed to read %d bytes from handle %d\n", sizeof(v), h );
			exit( -1 );
		}
	}
	else
	{
		struct timespec ts;
		if (clock_gettime( h==-1 ? CLOCK_MONOTONIC : CLOCK_REALTIME, &ts ))
		{
			fprintf( stderr, "Error: failed to read time using clock_gettime( %s, ... )\n", h==-1 ? "CLOCK_MONOTONIC" : "CLOCK_REALTIME" );
			exit( -1 );
		}
		// Convert to a U32 value in milliseconds
		unsigned long long u;
		u = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
		// Truncate high order bits
		return (unsigned long)(u & 0xffffffff);
	}
	return v;
}

// Test specified timer, return total samples read
int
TestTimer( const char *device, int test_duration )
{
	printf( "Collecting data for %s - this will take about %ds\n", device, test_duration );

	int isdev = strncmp( device, "clock", 4 );
	int h;
	if (isdev)
	{
		h = open( device, O_RDONLY );
		if (h < 0)
		{
			fprintf( stderr, "Failed to open %s: errno=%d (%s)\n", device, errno, strerror(errno) );
			return -1;
		}
	}
	else
	{
		if (!strcmp( device, "clockm" ))
		{
			h = -1;
		}
		else if (!strcmp( device, "clockr" ))
		{
			h = -2;
		}
		else
		{
			fprintf( stderr, "Unrecognized device %s - expected clockm or clockr\n", device );
			return -1;
		}
		printf( "Reading data using clock_gettime( %s )\n",
			h==-1 ? "CLOCK_MONOTONIC" : "CLOCK_REALTIME" );
	}

	unsigned int firstValue = 0xffffffff;
	unsigned int lastValue;
	lastValue = read_timer( h );
	printf( "Got initial value %u (%x)\n", lastValue, lastValue );
	unsigned int value;
	struct timeval tv;
	struct timeval starttv;
	gettimeofday( &starttv, NULL );
	bool done = false;
	bool first = true;
#define MAX_DELTAS	20000
	unsigned int timerDeltas[MAX_DELTAS];
	unsigned int seconds[MAX_DELTAS];
	unsigned int useconds[MAX_DELTAS];
	int sampleCount = 0;
	while (!done)
	{
		value = read_timer( h );
		if (first || value != lastValue)
		{
			if (first)
			{
				firstValue = value;
			}
			// If not first time, get time difference
			else
			{
				if (sampleCount >= MAX_DELTAS)
				{
					fprintf( stderr, "Too many samples (%d/%d), exiting...\n", sampleCount, MAX_DELTAS );
					done = true;
					break;
				}
				timerDeltas[sampleCount] = value - lastValue;
				gettimeofday( &tv, NULL );
				seconds[sampleCount] = tv.tv_sec;
				useconds[sampleCount] = tv.tv_usec;
				if (value < lastValue)
				{
					fprintf( stderr, "ERROR: backward travel on sample %d (was %lu now %lu) at %lu.%06lu\n", 
						sampleCount, lastValue, value, tv.tv_sec - starttv.tv_sec, tv.tv_usec );
				}
				sampleCount++;
				if (tv.tv_sec - starttv.tv_sec >= test_duration)
				{
					printf( "%d seconds elapsed, exiting sample read\n", tv.tv_sec - starttv.tv_sec );
					done = true;
					break;
				}
				if (tv.tv_sec < starttv.tv_sec)
				{
					printf( "!!! wrap occurred, exiting sample read \n" );
					done = true;
					break;
				}
			}
			lastValue = value;
			first = false;
		}	
	}
	if (isdev)
	{
		close( h );
	}
	printf( "Read %d samples from %s - analyzing...\n", sampleCount, device );

	unsigned int min_delta = 0xffffffff;
		int min_idx;
	unsigned int max_delta = 0;
		int max_idx;
	long long elapsed_beats = lastValue - firstValue;
	if (elapsed_beats < 0)
	{
		elapsed_beats += 0x100000000LL;
	}
	double sum = 0.0;
	double mean;
	double timerDeltaVariance = 0.0;
	int n;
	// Pass 1: calculate mean
	for (n = 0; n < sampleCount; n++)
	{
		sum += timerDeltas[n];
		if (timerDeltas[n] < min_delta)
		{
			min_idx = n;
			min_delta = timerDeltas[n];
		}
		if (timerDeltas[n] > max_delta)
		{
			max_idx = n;
			max_delta = timerDeltas[n];
		}
	}
	mean = sum / sampleCount;
	// Pass 2: calculate variance sum and standard deviation
	for (n = 0; n < sampleCount; n++)
	{
		double var = timerDeltas[n] - mean;
		timerDeltaVariance += var * var;
	}
	double stdDev = sqrt( timerDeltaVariance / sampleCount );

	printf( "-------- Results for %s ----------\n", device );
	printf( "Min delta: reading #%d/%d at %lu.%06lus %6lu\n", min_idx, sampleCount, seconds[min_idx] - starttv.tv_sec, useconds[min_idx], min_delta );
	printf( "Max delta: reading #%d/%d at %lu.%06lus %6lu\n", max_idx, sampleCount, seconds[max_idx] - starttv.tv_sec, useconds[max_idx], max_delta );
	printf( "Elapsed time: %lds\n", seconds[sampleCount-1] - starttv.tv_sec );
	printf( "Elapsed ticks: %lld\n", elapsed_beats );
	printf( "Mean beats/sec: %9.6f\n", (double)elapsed_beats / (seconds[sampleCount-1] - starttv.tv_sec) );
	printf( "Mean delta: %9.3f\n", mean );
	printf( "Standard deviation: %9.6f\n", stdDev );
	printf( "\n" );

	return sampleCount;

}

const char rev[] = "$Rev: 41675$";
int main( int argc, char *argv[] )
{
	char revcopy[64];
	strcpy( revcopy, &rev[6] );
	strtok( revcopy, "$" );
	printf( "%s v0.15 rev %s\n", argv[0], revcopy );

	struct timespec ts;
	int res = clock_getres( CLOCK_MONOTONIC, &ts );
	printf( "clock_getres( CLOCK_MONOTONIC ) returns %d, resolution = %lu.%09lu\n", res, ts.tv_sec, ts.tv_nsec );
	res = clock_gettime( CLOCK_MONOTONIC, &ts );
	printf( "clock_gettime( CLOCK_MONOTONIC ) returns %d, time = %lu.%09lu\n", res, ts.tv_sec, ts.tv_nsec );
	res = clock_getres( CLOCK_REALTIME, &ts );
	printf( "clock_getres( CLOCK_REALTIME ) returns %d, resolution = %lu.%09lu\n", res, ts.tv_sec, ts.tv_nsec );
	res = clock_gettime( CLOCK_REALTIME, &ts );
	printf( "clock_gettime( CLOCK_REALTIME ) returns %d, time = %lu.%09lu\n", res, ts.tv_sec, ts.tv_nsec );
	
	TestTimer( "/dev/timerx", 60 );
	TestTimer( "/dev/timerm", 60 );
	TestTimer( "clockm", 60 );
	TestTimer( "clockr", 60 );
	
	return 0;
}
