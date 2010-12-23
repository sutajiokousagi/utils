// $Id$
//
// aplaypipe.cpp - aplay blocking pipe manager
// Copyright (C) 2010 Chumby Industries. All rights reserved.
//
// This is for use with --SoundOutputPipe in flashplayer. Basically
// it forces all aplay output to the ALSA xfer_align size (2048
// frames or 8192 bytes on silvermoon)
//

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <signal.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>

// Internals
#include <libio.h>

#include "alsa/asoundlib.h"

#define VER_STR "1.12"

// Copied from flashplayer
unsigned long SI_GetRawTime()
{
    static bool firstTime = true;
    struct timespec ts;
    if (clock_gettime( CLOCK_MONOTONIC, &ts ))
    {
        syslog( LOG_ERR, "Error: failed to read time using clock_gettime( %s, ... )", "CLOCK_MONOTONIC" );
        return 0;
    }
    // Convert to a U32 value in milliseconds
    unsigned long long u;
    u = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    if (firstTime)
    {
        firstTime = false;
        int res = clock_getres( CLOCK_MONOTONIC, &ts );
        syslog( LOG_INFO, "clock_getres( CLOCK_MONOTONIC ) returns %d, resolution = %lu.%09lu, initial value = %lu\n",
            res, ts.tv_sec, ts.tv_nsec, (unsigned long)(u & 0xffffffff) );
    }

    // Truncate high order bits
    return (unsigned long)(u & 0xffffffff);
}

int g_quit = 0;
// Kill all child processes
int g_killQuit = 0;
int g_dbg = 0;
FILE *g_fDbgOut = NULL;
FILE *g_fPcmOut = NULL;
FILE *g_fPcmIn = NULL;
int g_deadInputCheck = 0;

// Set when SIGINT received based on console
// If we terminate immediately on SIGINT before flashplayer has a chance to shut us down,
// flashplayer will get stuck and may not receive a SIGPIPE.
volatile time_t g_terminateRequested = 0;

#define READ 0
#define WRITE 1

#define USE_SHMEM

#ifdef USE_SHMEM
int g_shmh = -1;
#pragma pack(4)
typedef struct tagShmem
{
	pid_t aplaypipe_pid;
	char ver[16];
	// Timestamp value of last flush request
	unsigned long flush_requested;
	char _unused[1024-sizeof(long)-sizeof(char[16])-sizeof(pid_t)];
} Shmem;
Shmem *g_pshmem = NULL;
#endif

// ALSA parameters
static char *device = "default"; /* was:"plughw:0,0";*/                     /* playback device */
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;    /* sample format */
static unsigned int rate = 44100;                       /* stream rate */
static unsigned int channels = 2;                       /* count of channels */
static unsigned int buffer_time = 500000;               /* ring buffer length in us */
static unsigned int period_time = 100000;               /* period time in us */
//static double freq = 440;                               /* sinusoidal wave frequency in Hz */
static int verbose = 0;                                 /* verbose flag */
static int resample = 1;                                /* enable alsa-lib resampling */
static int period_event = 0;                            /* produce poll event after each period */

static snd_pcm_sframes_t buffer_size;
static snd_pcm_sframes_t period_size;
static snd_output_t *output = NULL;

// Signal mask for use with sigprocmask()
sigset_t g_signalMask, g_signalUnblockMask, g_oldSignalMask;
// Previous signal handler for SIGCHLD
struct sigaction old_segv;

// Forward declaration for re-instating signal handler
void aplaypipeSignalHandler( int sigNum );

// Signal handler. Currently we handle SIGINT and by default, all signals are handled once only.
// We need to set SIGINT to SIG_IGN after the first press. Note that SIGCHLD is handled via sigaction
void aplaypipeSignalHandler( int sigNum )
{
	static int terminateRequestCount = 0;
	switch (sigNum)
	{
		case SIGINT:
		case SIGTERM:
			// Make sure the next one doesn't terminate
			signal( sigNum, SIG_IGN );
			terminateRequestCount++;
			// If it's the second request, enable default handling on the third
			if (terminateRequestCount > 1)
			{
				syslog( LOG_INFO, "%s(%d) - got request %d, next request will default to normal behavior\n", __FUNCTION__, sigNum, terminateRequestCount );
				signal( SIGINT, SIG_DFL );
			}
			else
			{
				syslog( LOG_INFO, "%s(%d) - quit requested\n", __FUNCTION__, sigNum );
				if (sigNum == SIGINT)
				{
					syslog( LOG_INFO, "deferring quit due to SIGINT\n" );
					g_terminateRequested = time( NULL );
				}
				else
				{
					g_quit = 1;
				}
				signal( SIGINT, aplaypipeSignalHandler );
			}
			break;

		case SIGPIPE:
			syslog( LOG_ERR, "%s() - got SIGPIPE, exiting\n", __FUNCTION__ );
			signal( SIGPIPE, SIG_DFL );
			g_quit++;
			break;

		case SIGUSR1:
			syslog( LOG_DEBUG, "%s() - got SIGUSR1, bumping debug level", __FUNCTION__ );
			g_dbg++;
			break;

		default:
			syslog( LOG_ERR, "%s() - got unknown signal %d\n", __FUNCTION__, sigNum );
			break;
	}
}

// Timestamp for debug output
char *strTimestamp()
{
	static char dt[128];
	time_t now = time( NULL );
	struct tm *tmNow = localtime( &now );
	strftime( dt, sizeof(dt), "%y-%m-%d %H:%M:%S", tmNow );
	return dt;
}

// Get a single long and advance buffer
static long ExtractLong( char *&buff, long &buffLen )
{
	long r /*= *((long*)buff)*/;
	// The above will not work the way you'd expect
	memcpy( &r, buff, sizeof(r) );
	buff += sizeof(long);
	buffLen -= sizeof(long);
	return r;
}

// Set close on exec flag in file descriptor.
// We do this because we want this file to be closed across any execve() calls we make...
int set_cloexec_flag(int desc, int value) {
	int oldflags = fcntl(desc,F_GETFD,0);
	if (oldflags<0)
		return oldflags;
	if (value!=0)
		oldflags |= FD_CLOEXEC;
	else
		oldflags &= ~FD_CLOEXEC;
	return fcntl(desc,F_SETFD,oldflags);
}


void aplaypipeSigAction( int signum, siginfo_t *siginfo, void *data )
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
				if (g_dbg) syslog( LOG_DEBUG, "%s SIGCHLD pid=%u checking 0-%d", __FUNCTION__, childPid, childPid );
				// Flag exit check
				g_deadInputCheck++;
			}
			else
			{
				syslog( LOG_ERR, "%s SIGCHLD pid=%u unhandled code %u", __FUNCTION__, siginfo->si_pid, siginfo->si_code );
			}
			break;

		case SIGSEGV:
			syslog( LOG_ERR, "%s() got SIGSEGV, exiting", __FUNCTION__ );
			syslog( LOG_ERR, "siginfo.si_errno=%d, si_code=%d (%s)", siginfo->si_errno, siginfo->si_code,
                siginfo->si_code==SEGV_MAPERR ? "SEGV_MAPERR" : "SEGV_ACCERR" );
	            // valid for  SIGILL, SIGFPE, SIGSEGV, and SIGBUS
			syslog( LOG_ERR, "offending address is 0x%08lx, file handle %d, context 0x%08lx",
                (unsigned long)siginfo->si_addr, siginfo->si_fd, (unsigned long)data );
			exit( -1 );
			break;

		default:
			syslog( LOG_ERR, "%s - unknown signal %d", __FUNCTION__, signum );
	}
}


// Taken from ALSA example http://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2pcm_8c-example.html

static int set_hwparams(snd_pcm_t *handle,
                        snd_pcm_hw_params_t *params,
                        snd_pcm_access_t access)
{
        unsigned int rrate;
        snd_pcm_uframes_t size;
        int err, dir;

        /* choose all parameters */
        err = snd_pcm_hw_params_any(handle, params);
        if (err < 0) {
                printf("Broken configuration for playback: no configurations available: %s\n", snd_strerror(err));
                return err;
        }
        /* set hardware resampling */
        err = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
        if (err < 0) {
                printf("Resampling setup failed for playback: %s\n", snd_strerror(err));
                return err;
        }
        /* set the interleaved read/write format */
        err = snd_pcm_hw_params_set_access(handle, params, access);
        if (err < 0) {
                printf("Access type not available for playback: %s\n", snd_strerror(err));
                return err;
        }
        /* set the sample format */
        err = snd_pcm_hw_params_set_format(handle, params, format);
        if (err < 0) {
                printf("Sample format not available for playback: %s\n", snd_strerror(err));
                return err;
        }
        /* set the count of channels */
        err = snd_pcm_hw_params_set_channels(handle, params, channels);
        if (err < 0) {
                printf("Channels count (%i) not available for playbacks: %s\n", channels, snd_strerror(err));
                return err;
        }
        /* set the stream rate */
        rrate = rate;
        err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
        if (err < 0) {
                printf("Rate %iHz not available for playback: %s\n", rate, snd_strerror(err));
                return err;
        }
        if (rrate != rate) {
                printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
                return -EINVAL;
        }
        /* set the buffer time */
        err = snd_pcm_hw_params_set_buffer_time_near(handle, params, &buffer_time, &dir);
        if (err < 0) {
                printf("Unable to set buffer time %i for playback: %s\n", buffer_time, snd_strerror(err));
                return err;
        }
        err = snd_pcm_hw_params_get_buffer_size(params, &size);
        if (err < 0) {
                printf("Unable to get buffer size for playback: %s\n", snd_strerror(err));
                return err;
        }
        buffer_size = size;
        /* set the period time */
        err = snd_pcm_hw_params_set_period_time_near(handle, params, &period_time, &dir);
        if (err < 0) {
                printf("Unable to set period time %i for playback: %s\n", period_time, snd_strerror(err));
                return err;
        }
        err = snd_pcm_hw_params_get_period_size(params, &size, &dir);
        if (err < 0) {
                printf("Unable to get period size for playback: %s\n", snd_strerror(err));
                return err;
        }
        period_size = size;
        /* write the parameters to device */
        err = snd_pcm_hw_params(handle, params);
        if (err < 0) {
                printf("Unable to set hw params for playback: %s\n", snd_strerror(err));
                return err;
        }
        return 0;
}

static int set_swparams(snd_pcm_t *handle, snd_pcm_sw_params_t *swparams)
{
        int err;

        /* get the current swparams */
        err = snd_pcm_sw_params_current(handle, swparams);
        if (err < 0) {
                printf("Unable to determine current swparams for playback: %s\n", snd_strerror(err));
                return err;
        }
        /* start the transfer when the buffer is almost full: */
        /* (buffer_size / avail_min) * avail_min */
        err = snd_pcm_sw_params_set_start_threshold(handle, swparams, (buffer_size / period_size) * period_size);
        if (err < 0) {
                printf("Unable to set start threshold mode for playback: %s\n", snd_strerror(err));
                return err;
        }
        /* allow the transfer when at least period_size samples can be processed */
        /* or disable this mechanism when period event is enabled (aka interrupt like style processing) */
        err = snd_pcm_sw_params_set_avail_min(handle, swparams, period_event ? buffer_size : period_size);
        if (err < 0) {
                printf("Unable to set avail min for playback: %s\n", snd_strerror(err));
                return err;
        }
        /* enable period events when requested */
	/*** Available in later ALSA version ***
        if (period_event) {
                err = snd_pcm_sw_params_set_period_event(handle, swparams, 1);
                if (err < 0) {
                        printf("Unable to set period event: %s\n", snd_strerror(err));
                        return err;
                }
        }
	****************************************/
        /* write the parameters to the playback device */
        err = snd_pcm_sw_params(handle, swparams);
        if (err < 0) {
                printf("Unable to set sw params for playback: %s\n", snd_strerror(err));
                return err;
        }
        return 0;
}

/*
 *   Underrun and suspend recovery
 */

static int xrun_recovery(snd_pcm_t *handle, int err)
{
        if (verbose)
                printf("stream recovery\n");
        if (err == -EPIPE) {    /* under-run */
                err = snd_pcm_prepare(handle);
                if (err < 0)
                        printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
                return 0;
        } else if (err == -ESTRPIPE) {
                while ((err = snd_pcm_resume(handle)) == -EAGAIN)
                        sleep(1);       /* wait until the suspend flag is released */
                if (err < 0) {
                        err = snd_pcm_prepare(handle);
                        if (err < 0)
                                printf("Can't recovery from suspend, prepare failed: %s\n", snd_strerror(err));
                }
                return 0;
        }
        return err;
}

// Define option IDs and options
typedef enum {
OPT_INVALID=0,
	OPT_HELP=1,
	OPT_DEBUG,
	OPT_PCMOUT,
	OPT_PCMIN,
	OPT_TAILFILLMIN,
	OPT_INPUTSLEEPTIME,
	OPT_EMPTYTAILMIN,
	OPT_POSTTAILFILLSILENCESTART,
	OPT_POSTTAILFILLSILENCEDURATION,
	OPT_BLOCKFACTOR,
#ifdef USE_SHMEM
	OPT_SHMEMSEGNAME,
#endif
	OPT_CBSMAX,
	OPT_MAXSILENCEWRITETIME,
	OPT_MAXSILENCEOUTPUTSLEEPTIME,
	OPT_EMPTYINPUTSILENCESTART,
	OPT_TAILFILLAFTERTRAILINGSILENCE,
	OPT_TRAILINGSILENCETRIGGER,
	OPT_IGNOREFLUSH,
	OPT_FLUSHEXPIRY,
	OPT_ALSADIRECT,
	OPT_ALSADEVICE,
	OPT_ALSABLOCKING,
	OPT_SILENCERATE,
OPT_END
} OptionID;

typedef struct _optionDef {
	OptionID id;	///< Unique identifier
	int required_args; ///< Required number of args to follow
	const char *text; ///< Case-insensitive option text
	const char *helptext; ///< Help text to be displayed
} OptionDef;

OptionDef opts[] = {
	{	OPT_HELP,	0,	"help",		"Display command option help" 	},
	{	OPT_DEBUG,	1,	"Debug", 	"Specify location of debug log file"	},
	{	OPT_PCMOUT,	1,	"PCMOut",	"Write all PCM data as sent to aplay (with silence padding) to raw file" },
	{	OPT_PCMIN,	1,	"PCMIn",	"Write all PCM data as received on stdin to this file"	},
	{	OPT_TAILFILLMIN, 1,	"TailFillMin",	"(400) Minimum ms of inactivity before we tail-fill with silence" },
	{	OPT_INPUTSLEEPTIME, 1,	"InputSleepTime", "(5) Sleep time for input in ms" },
	{	OPT_EMPTYTAILMIN,	1,	"EmptyTailMin",	"(60) Minimum ms of timeout before an empty read is tail-filled and sent to the buffer" },
	{	OPT_POSTTAILFILLSILENCESTART,	1, "PostTailFillSilenceStart",	"(200) Post-tail fill silence start delay in ms\n\
After a block was written with tail fill and we get no input for this amount of time,\n\
write silence until post-tail fill silence end delay" },
	{	OPT_POSTTAILFILLSILENCEDURATION, 1,	"PostTailFillSilenceDuration", "(120) Post-tail fill silence duration in ms\n\
Rate will be limited to one block per input_sleep_time ms" },
	{	OPT_BLOCKFACTOR,	1,	"BlockFactor", "(8192) Blocking factor for buffer in bytes\n\
Pass buffer on to aplay in units of this amount. If full amount not received,\n\
fill tail with silence. Parent process (writer) should use select() to check for\n\
when we're ready to read data, and can assume that we'll always be able to read\n\
an entire buffer." },
#ifdef USE_SHMEM
	{	OPT_SHMEMSEGNAME,	1,	"ShmSegName", "(/aplaypipe-shm) Shared memory segment name to use for synch" },
#endif
	{	OPT_CBSMAX,		1,	"CBSMax", "(16) Maximum consecutive blocks of silence to send" },
	{	OPT_MAXSILENCEWRITETIME, 1,	"MaxSilenceWriteTime", "(10) Maximum time in ms to allow for silence fill writes" },
	{	OPT_MAXSILENCEOUTPUTSLEEPTIME, 1, "MaxSilenceOutputSleepTime", "(0) Maximum time in ms to wait for output ready when writing silence to aplay" },
	{	OPT_EMPTYINPUTSILENCESTART, 1, "EmptyInputSilenceStart", "(500) Time in ms when no input is ready before writing silence to aplay" },
	{	OPT_TAILFILLAFTERTRAILINGSILENCE, 1, "TailFillAfterTrailingSilence", "(50) Minimum ms of inactivity before tail-filling when last packet had trailing silence" },
	{	OPT_TRAILINGSILENCETRIGGER, 1, "TrailingSilenceTrigger", "(128) Number of bytes of trailing silence required to trigger TailFillAfterTrailingSilence" },
	{	OPT_IGNOREFLUSH, 1, "IgnoreFlush", "(0) Ignore flush requests through shared memory" },
	{	OPT_FLUSHEXPIRY, 1, "FlushExpiry", "(200) Flush request expiry in ms" },
	{	OPT_ALSADIRECT,	1, "ALSADirect", "(1) 0=pipe to aplay; 1=ALSA directly; 2=ALSA with ring buffer; 4=use callback" },
	{	OPT_ALSADEVICE, 1, "ALSADevice", "(default) ALSA device to use (if ALSADirect>0)" },
	{	OPT_ALSABLOCKING, 1, "ALSABlocking", "(1) ALSA is opened in blocking mode" },
	{	OPT_SILENCERATE, 1, "SilenceRate", "(60) Maximum delay between buffer writes (in ms) - use to maintain spacing between short sounds, limited by CBSMax" },
};
#define OPTS_COUNT	(sizeof(opts)/sizeof(opts[0]))

// Find option in list by sequential search. Returns index into opts[] and fills in argument or -1 if not found
int FindOption( const char *argv[], int& argn, int argc, char *OptionArgument )
{
	int n;
	if (strncmp( argv[argn], "--", 2 ))
	{
		fprintf( stderr, "%s() - missing option leadin -- for %s\n", __FUNCTION__, argv[argn] );
		return -1;
	}
	const char *arg = &argv[argn][2];
	*OptionArgument = '\0';
	for (n = 0; n < OPTS_COUNT; n++)
	{
		int OptionLength = strlen( opts[n].text );
		if (strncasecmp( arg, opts[n].text, OptionLength ))
		{
			continue;
		}
		// Check for arg. Currently only 0 or 1 args supported
		if (opts[n].required_args > 0)
		{
			// Support both --option=arg and --option arg
			const char *ArgSource = &arg[OptionLength];
			if (*ArgSource == '=')
			{
				ArgSource++;
				strcpy( OptionArgument, ArgSource );
				if (!*ArgSource)
				{
					fprintf( stderr, "%s() - missing required argument to %s=\n", __FUNCTION__, opts[n].text );
					return -1;
				}
			}
			else if (argn < argc)
			{
				argn++;
				ArgSource = argv[argn];
				if (!*ArgSource)
				{
					fprintf( stderr, "%s() - missing required argument to %s\n", __FUNCTION__, opts[n].text );
					return -1;
				}
			}
			else
			{
				fprintf( stderr, "%s() - missing required argument to %s\n", __FUNCTION__, opts[n].text );
				return -1;
			}
		}
		return n;
	}
	return -1;
}

// Return bytes of trailing silence
int TrailingSilenceBytes( void *p, unsigned int len )
{
	if (len < 4)
	{
		return 0;
	}
	unsigned long *lp = (unsigned long*)p;
	// Convert to length in uint32
	len >>= 2;
	/*****
	if (g_dbg && g_fDbgOut)
	{
		fprintf( g_fDbgOut, "tsb 0x%08lx len=%u %08lx ... %08lx\n",
			lp, len, *lp, lp[len-1] );
	}
	*****/
	int count = 0;
	for (lp += (len-1); len > 0; len--, lp--)
	{
		if (*lp) break;
		count++;
	}
	// Return count in bytes
	return count << 2;
}

int main( int argc, const char *argv[] )
{
	// Minimum ms of inactivity before we tail-fill
	int tail_fill_min = 400;

	// ms of inactivity before tail-filling when last input had trailing silence
	int tail_fill_after_trailing_silence = 50;

	// Minimum bytes of trailing silence to trigger tail_fill_after_trailing_silence
	int trailing_silence_trigger = 128;

	// Sleep time for input in ms
	int input_sleep_time = 5;

	// Minimum ms of timeout before an empty read is tail-filled and sent to the buffer
	int empty_tail_min = 60;

	// Post-tail fill silence start delay in ms
	// After a block was written with tail fill
	// and we get no input for this amount of time,
	// write silence until post-tail fill silence end delay
	int post_tail_fill_silence_start = 200;

	// Post-tail fill silence duration in ms
	// Rate will be limited to one block per
	// input_sleep_time ms
	int post_tail_fill_silence_duration = 120;

	// Blocking factor for buffer in bytes
	// Pass buffer on to aplay in units of this amount. If full amount not received,
	// fill tail with silence. Parent process (writer) should use select() to check for
	// when we're ready to read data, and can assume that we'll always be able to read
	// an entire buffer.
	int block_factor = 8192;

#ifdef USE_SHMEM
	// Name of shared memory segment
	char shmem_segname[1024] = "/aplaypipe-shm";
#endif

	// Maximum consecutive blocks of silence
	int cbs_max = 16;

	// Maximum time in ms allowed for silence write
	int max_silence_write_time = 10;

	// Time to wait for output ready when writing silence
	int max_silence_output_sleep_time = 0;

	// Time in ms to wait before starting silence
	int empty_input_silence_start = 500;

	// Ignore flush requests made through shared memory block
	int ignore_flush = 0;

	// Flush expiry time in ms
	int flush_expiry = 200;

	// ALSA direct bitmap options
	// 1 = pcm_writei
	// 2 = mmap (ring buffer) mode
	// 4 = async callback
	// 1 and 2 are mutually exclusive
	int ALSA_direct = 1;

	// Delay between writes in ms before we start injecting silence (up to CBSMax)
	int silence_rate = 60;

	// True if device is return from strdup() (for ALSADevice=)
	bool ALSA_device_specified = false;

	// ALSA blocking mode
	int ALSA_blocking = 1;

	// Parse options and get tail options with complete aplay command line
	char option_arg[1024];
	char aplay_cmd[1024] = "aplay --channels=2 --format=S16_LE --rate=44100 > /dev/null 2>&1";
	static char empty_str[] = "";
	static char space_str[] = " ";
	char *aplay_cmdsep = empty_str;

	int got_tail = 0;
	int n;
	for (n = 1; n < argc; n++)
	{
		if (!got_tail)
		{
			got_tail = !strcmp( argv[n], "--" );
			if (got_tail != 0)
			{
				// Skip tail separator
				continue;
			}
		}
		if (got_tail != 0)
		{
			if (*aplay_cmdsep)
			{
				strcat( aplay_cmd, aplay_cmdsep );
				strcat( aplay_cmd, argv[n] );
			}
			else
			{
				strcpy( aplay_cmd, argv[n] );
				aplay_cmdsep = space_str;
			}
			continue;
		}
		// Handle options before tail separator --
		int option_index = FindOption( argv, n, argc, option_arg );
		OptionID id = OPT_INVALID;
		if (option_index >= 0)
		{
			id = opts[option_index].id;
		}
		switch (id)
		{
			default:
			case OPT_HELP:
				{
					fprintf( stderr, "aplaypipe v%s\n", VER_STR );
					if (id != OPT_HELP)
					{
						fprintf( stderr, "Unrecognized option %s\n", argv[n] );
					}
					fprintf( stderr, "Syntax: %s [options] -- [aplay [aplay_options]]\n", argv[0] );
					fprintf( stderr, "aplay_options are passed as-is to aplay\n" );
					fprintf( stderr, "options for %s are (where default values are used they are shown in parens):\n", argv[0] );
					int i;
					for (i = 0; i < OPTS_COUNT; i++)
					{
						fprintf( stderr, "  --%s%s\t%s\n",
							opts[i].text, opts[i].required_args ? "=<value>" : "", opts[i].helptext );
					}
					return( -1 );
				}
				break;

			case OPT_DEBUG:
				g_dbg++;
				g_fDbgOut = fopen( option_arg, "w" );
				if (!g_fDbgOut)
				{
					syslog( LOG_ERR, "failed to open debug log %s", option_arg );
				}
				break;

			case OPT_PCMOUT:
				g_fPcmOut = fopen( option_arg, "w" );
				if (!g_fPcmOut)
				{
					syslog( LOG_ERR, "failed to open pcm out %s", option_arg );
				}
				break;

			case OPT_PCMIN:
				g_fPcmIn = fopen( option_arg, "w" );
				if (!g_fPcmIn)
				{
					syslog( LOG_ERR, "failed to open pcm in %s", option_arg );
				}
				break;

			case OPT_TAILFILLMIN:
				tail_fill_min = atoi( option_arg );
				syslog( LOG_INFO, "Using --tailfillmin=%d", tail_fill_min );
				break;

			case OPT_INPUTSLEEPTIME:
				input_sleep_time = atoi( option_arg );
				syslog( LOG_INFO, "using --inputsleeptime=%d", input_sleep_time );
				break;

			case OPT_EMPTYTAILMIN:
				empty_tail_min = atoi( option_arg );
				syslog( LOG_INFO, "using --emptytailmin=%d", empty_tail_min );
				break;

			case OPT_POSTTAILFILLSILENCESTART:
				post_tail_fill_silence_start = atoi( option_arg );
				syslog( LOG_INFO, "using --posttailfillsilencestart=%d", post_tail_fill_silence_start );
				break;

			case OPT_POSTTAILFILLSILENCEDURATION:
				post_tail_fill_silence_duration = atoi( option_arg );
				syslog( LOG_INFO, "using --posttailfillsilenceduration=%d", post_tail_fill_silence_duration );
				break;

			case OPT_BLOCKFACTOR:
				block_factor = atoi( option_arg );
				syslog( LOG_INFO, "using --block_factor=%d", block_factor );
				break;

#ifdef USE_SHMEM
			case OPT_SHMEMSEGNAME:
				strcpy( shmem_segname, option_arg );
				if (!strcasecmp( shmem_segname, "none" ))
				{
					shmem_segname[0] = '\0';
					syslog( LOG_INFO, "disabled shared memory - --shmemsegname=%s specified", option_arg );
				}
				else
				{
					syslog( LOG_INFO, "using --shmemsegname=%s", shmem_segname );
				}
				break;
#endif

			case OPT_CBSMAX:
				cbs_max = atoi( option_arg );
				syslog( LOG_INFO, "using --cbsmax=%d", cbs_max );
				break;

			case OPT_MAXSILENCEWRITETIME:
				max_silence_write_time = atoi( option_arg );
				syslog( LOG_INFO, "using --maxsilencewritetime=%d", max_silence_write_time );
				break;

			case OPT_MAXSILENCEOUTPUTSLEEPTIME:
				max_silence_output_sleep_time = atoi( option_arg );
				syslog( LOG_INFO, "using --maxsilenceoutputsleeptime=%d", max_silence_output_sleep_time );
				break;

			case OPT_EMPTYINPUTSILENCESTART:
				empty_input_silence_start = atoi( option_arg );
				syslog( LOG_INFO, "using --emptyinputsilencestart=%d", empty_input_silence_start );
				break;

			case OPT_TAILFILLAFTERTRAILINGSILENCE:
				tail_fill_after_trailing_silence = atoi( option_arg );
				syslog( LOG_INFO, "using --tailfillaftertrailingsilence=%d", tail_fill_after_trailing_silence );
				break;

			case OPT_TRAILINGSILENCETRIGGER:
				trailing_silence_trigger = atoi( option_arg );
				syslog( LOG_INFO, "using --trailingsilencetrigger=%d", trailing_silence_trigger );
				break;

			case OPT_IGNOREFLUSH:
				ignore_flush = atoi( option_arg );
				syslog( LOG_INFO, "using --ignoreflush=%d", ignore_flush );
				break;

			case OPT_FLUSHEXPIRY:
				flush_expiry = atoi( option_arg );
				syslog( LOG_INFO, "using --flushexpiry=%d", flush_expiry );
				break;

			case OPT_ALSADIRECT:
				ALSA_direct = atoi( option_arg );
				syslog( LOG_INFO, "using --alsadirect=%d", ALSA_direct );
				if ((ALSA_direct & 0x03) == 0x03)
				{
					fprintf( stderr, "Cannot use pcm_write1 (1) and pcm_mmap (2) together\n" );
					return( -1 );
				}
				if ((ALSA_direct & 0x03) == 0 && ALSA_direct != 0)
				{
					fprintf( stderr, "Either bit 0 or bit 1 must be specified, or use ALSADirect=0\n" );
					return( -1 );
				}
				// Temporarily, only pcm_writei is supported
				if (ALSA_direct & 0x02)
				{
					fprintf( stderr, "pcm_mmap mode is not yet supported\n" );
					return( -1 );
				}
				// And async callback will be ignored
				if (ALSA_direct & 0x04)
				{
					syslog( LOG_INFO, "async callback option is currently ignored" );
				}
				break;

			case OPT_ALSADEVICE:
				if (!option_arg[0])
				{
					fprintf( stderr, "no ALSA device specified\n" );
					return -1;
				}
				ALSA_device_specified = true;
				device = strdup( option_arg );
				fprintf( stderr, "Using ALSA device %s\n", device );
				break;

			case OPT_ALSABLOCKING:
				ALSA_blocking = atoi( option_arg );
				syslog( LOG_INFO, "using --alsablocking=%d", ALSA_blocking );
				break;

		} // switch()

	} // for all options

	// Check for illogical combinations
	if (ALSA_device_specified && !ALSA_direct)
	{
		fprintf( stderr, "Warning: ALSA device %s specified but ALSADirect=%d = option will be ignored\n", device, ALSA_direct );
	}

	openlog( "aplaypipe", (g_dbg > 0 ? LOG_PERROR : 0) | LOG_PID, LOG_USER );
	syslog( LOG_INFO, "%s v%s - reading stdin in %d-byte chunks, tail filling silence after %dms, sleep %dms",
		argv[0], VER_STR, block_factor, tail_fill_min, input_sleep_time );

	if (ALSA_direct)
	{
		syslog( LOG_INFO, "Output will go directly to ALSA %s via %s%s",
			device,
			(ALSA_direct & 0x01) ? "pcm_writei" : "pcm_mmap (ring buffer)",
			(ALSA_direct & 0x04) ? " with async callback" : "" );
	}
	else
	{
		syslog( LOG_INFO, "Output will be sent to: %s", aplay_cmd );
	}
#ifdef USE_SHMEM
	if (shmem_segname[0])
	{
		syslog( LOG_INFO, "Shared memory interface is %s", shmem_segname );
	}
	else
	{
		syslog( LOG_INFO, "Shared memory is disabled" );
	}
#endif

	// Make sure handles are closed across any exec calls we make
	set_cloexec_flag( fileno(stdin), 1 );
	set_cloexec_flag( fileno(stdout), 1 );
	set_cloexec_flag( fileno(stderr), 1 );

	// Close stdout, stderr
	//fclose( stdout );
	//fclose( stderr );

	// Set up signal handler for sigint, sigterm, sigpipe and sigsegv
	signal( SIGINT, aplaypipeSignalHandler );
	signal( SIGTERM, aplaypipeSignalHandler );
	signal( SIGPIPE, aplaypipeSignalHandler );
	signal( SIGUSR1, aplaypipeSignalHandler );

	struct sigaction chld_handler;
	memset( &chld_handler, 0, sizeof(chld_handler) );
	chld_handler.sa_sigaction = aplaypipeSigAction;
	chld_handler.sa_flags = SA_SIGINFO /*| SA_NOCLDWAIT*/;
	sigaction( SIGCHLD, &chld_handler, &old_segv );
	sigaction( SIGSEGV, &chld_handler, &old_segv );

#ifdef USE_SHMEM
	if (shmem_segname[0])
	{
		// Open and truncate
		g_shmh = shm_open( shmem_segname, O_RDWR | O_CREAT | O_TRUNC, 0666 );
		if (g_shmh < 0)
		{
			syslog( LOG_ERR, "shm_open(%s) failed errno=%d (%s)", shmem_segname, errno, strerror(errno) );
		}
		else
		{
			// Fill with zero
			ftruncate( g_shmh, sizeof(Shmem) );
			// Map it
			g_pshmem = (Shmem*) mmap( NULL, sizeof(Shmem), PROT_READ | PROT_WRITE, MAP_SHARED, g_shmh, 0 /* offset */ );
			if (g_pshmem == NULL || g_pshmem == (void*)-1)
			{
				g_pshmem = NULL;
				close( g_shmh );
				g_shmh = -1;
				shm_unlink( shmem_segname );
				syslog( LOG_ERR, "mmap failed with result %d from shm_open", g_shmh );
			}
			else
			{
				// Already initialized to 0
				g_pshmem->aplaypipe_pid = getpid();
				strncpy( g_pshmem->ver, VER_STR, sizeof(g_pshmem->ver) );
				syslog( LOG_INFO, "Successfully mapped shared memory from handle %d @0x%08lx", g_shmh, (unsigned long)g_pshmem );
			}
		}
	}
#endif

	// Set up signal mask for child termination. We'll use this around fork() calls.
	sigemptyset( &g_signalMask );
	sigaddset( &g_signalMask, SIGCHLD );

	sigemptyset( &g_signalUnblockMask );

	sigemptyset( &g_oldSignalMask );

	struct timeval timeout;
	fd_set readfds;
	fd_set writefds;

	// Parameters used for ALSA direct access
        snd_pcm_t *handle;
        int err /*, morehelp*/;
        snd_pcm_hw_params_t *hwparams;
        snd_pcm_sw_params_t *swparams;
        //int method = 0;
        signed short *samples;
        unsigned int chn;
        snd_pcm_channel_area_t *areas;


	// Open aplay for output
	FILE *fAplay = NULL;
	// Initialize sigio_set for signal blocking in ALSA output pump
    sigemptyset(&g_signalUnblockMask);
    sigaddset(&g_signalUnblockMask, SIGIO);
	// SIGSETXID is only used internally within libpthread
#ifndef SIGSETXID
#define SIGSETXID (__SIGRTMIN+1)
#endif
	// This has no effect, basically - see glibc-2.8 sources in sysdeps/unix/sysv/linux/sigprocmask.c line 58
	/****
	printf( "Adding SIGSETXID (%d) to blocking set for ALSA\n", SIGSETXID );
	sigaddset(&g_signalUnblockMask, SIGSETXID);
	****/
	if (ALSA_direct == 0)
	{
		fAplay = popen( aplay_cmd, "w" );
		g_quit = (fAplay == NULL);
	}
	else
	{
		// Initialize ALSA direct handle
		// Much of this code is ripped off from http://www.alsa-project.org/alsa-doc/alsa-lib/_2test_2pcm_8c-example.html#a49
	        snd_pcm_hw_params_alloca(&hwparams);
	        snd_pcm_sw_params_alloca(&swparams);
	        err = snd_output_stdio_attach(&output, stdout, 0);
	        if (err < 0) {
	                printf("Output failed: %s\n", snd_strerror(err));
	                return 0;
	        }
	        printf("Playback device is %s\n", device);
	        printf("Stream parameters are %iHz, %s, %i channels\n", rate, snd_pcm_format_name(format), channels);

	        if ((err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, ALSA_blocking ? 0 : SND_PCM_NONBLOCK )) < 0) {
	                printf("Playback open error: %s\n", snd_strerror(err));
	                return 0;
	        }

		// Currently only supporting one transfer method
	        if ((err = set_hwparams(handle, hwparams, /*transfer_methods[method].access*/  SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
	                printf("Setting of hwparams failed: %s\n", snd_strerror(err));
	                exit(EXIT_FAILURE);
	        }
	        if ((err = set_swparams(handle, swparams)) < 0) {
	                printf("Setting of swparams failed: %s\n", snd_strerror(err));
	                exit(EXIT_FAILURE);
	        }

	        if (g_dbg > 0)
	                snd_pcm_dump(handle, output);

	        samples = (short int*)malloc((period_size * channels * snd_pcm_format_physical_width(format)) / 8);
	        if (samples == NULL) {
	                printf("No enough memory\n");
	                exit(EXIT_FAILURE);
	        }

	        areas = (snd_pcm_channel_area_t*)calloc(channels, sizeof(snd_pcm_channel_area_t));
	        if (areas == NULL) {
	                printf("No enough memory\n");
	                exit(EXIT_FAILURE);
	        }
	        for (chn = 0; chn < channels; chn++) {
	                areas[chn].addr = samples;
	                areas[chn].first = chn * snd_pcm_format_physical_width(format);
	                areas[chn].step = channels * snd_pcm_format_physical_width(format);
		}

	}

	int buffer_offset = 0;
	int tail_fill_count = 0;
	int partial_block_count = 0;
	int full_block_count = 0;
	int silence_count = 0;
	int flush_pending = 0;
	unsigned long prevTime = SI_GetRawTime();
	unsigned long prevReadTime = prevTime;
	unsigned long prevDebugOut = 0;
	unsigned long totalBlockCount = 0;
	unsigned long pcmOutBytes = 0;
	unsigned long pcmInBytes = 0;
	unsigned long prevWriteTime = prevTime;
	int short_write_count = 0;
	int write_error_count = 0;
	int h_in = fileno(stdin);
	int h_out = 0;
	if (fAplay)
	{
		h_out = fileno( fAplay );
	}

	unsigned long input_sleep_time_us = input_sleep_time * 1000;

	// State values
	int was_last_block_tailfilled = 0;
	int was_last_block_post_tailfill_silence = 0;
	unsigned long last_tailfill_start_time = 0;
	unsigned long post_tailfill_silence_start_time = 0;
#ifdef USE_SHMEM
	// Avoid duplication of flush requests - only caller writes the value,
	// we merely read it and compare against current time
	unsigned long last_flush_requested = 0;
#endif

	// Keep track of consecutive blocks of silence.
	int consecutive_silence_count = 0;

	// Did last input block meet trailing silence test?
	int did_last_block_end_silent = 0;

	while (!g_quit)
	{
		char buff[16384];
		int n;

		// Build set of all pipes we're reading from
		FD_ZERO( &readfds );
		FD_SET( h_in, &readfds );

		int bytes_to_read = block_factor - buffer_offset;
		int bytes_read = 0;
		int is_silence = 0;

		// If nothing selected, continue waiting - timeout after {input_sleep_time}ms if no input ready
		timeout.tv_sec = 0;
		timeout.tv_usec = input_sleep_time_us;
		if (select(h_in+1, &readfds, NULL, NULL, &timeout) <= 0) {
			//if (g_dbg > 1) syslog( LOG_DEBUG, "timeout on select" );
			if (buffer_offset == 0)
			{
				// Check for silence end
				if (was_last_block_post_tailfill_silence)
				{
					long elapsed = SI_GetRawTime() - post_tailfill_silence_start_time;
					if (elapsed > 0 && elapsed < post_tail_fill_silence_duration)
					{
						is_silence = 1;
					}
					else
					{
						was_last_block_post_tailfill_silence = 0;
					}
				}
				// Check for silence start
				else if (was_last_block_tailfilled)
				{
					unsigned long now = SI_GetRawTime();
					long elapsed = now - last_tailfill_start_time;
					if (elapsed >= post_tail_fill_silence_start)
					{
						is_silence = was_last_block_post_tailfill_silence = 1;
						post_tailfill_silence_start_time = now;
					}
				}
				// Check for delay following input with trailing silence
				else if (!is_silence && did_last_block_end_silent)
				{
					unsigned long now = SI_GetRawTime();
					if (tail_fill_after_trailing_silence > 0)
					{
						long elapsed = now - prevReadTime;
						if (elapsed >= tail_fill_after_trailing_silence)
						{
							if (g_dbg && g_fDbgOut)
							{
								fprintf( g_fDbgOut, "%lu [post-lbes] elapsed = %ld\n",
									now, elapsed );
							}
							is_silence = 1;
						} // Specified time elapsed
					}
					else if (tail_fill_after_trailing_silence == 0)
					{
						is_silence = 1;
					}
					if (is_silence)
					{
						was_last_block_post_tailfill_silence = is_silence;
						post_tailfill_silence_start_time = now;
						did_last_block_end_silent = 0;
					}
				}
				// Check for pending flush
				else if (flush_pending)
				{
					unsigned long now = SI_GetRawTime();
					flush_pending = 0;
					if (g_dbg && g_fDbgOut)
					{
						fprintf( g_fDbgOut, "%lu [flush-empty] offset %lu\n",
							now, buffer_offset );
					}
					is_silence = was_last_block_post_tailfill_silence = 1;
					post_tailfill_silence_start_time = now;
				}
				// Check for empty silence start
				else if (!is_silence && consecutive_silence_count <= cbs_max)
				{
					unsigned long now = SI_GetRawTime();
					long elapsed = now - last_tailfill_start_time;
					if (prevReadTime > last_tailfill_start_time)
					{
						elapsed = now - prevReadTime;
					}
					if (elapsed >= empty_input_silence_start)
					{
						is_silence = was_last_block_post_tailfill_silence = 1;
						post_tailfill_silence_start_time = now;
					}
				}
				// Disregard any flush request - buffer is empty
				if (g_pshmem && g_pshmem->flush_requested != last_flush_requested)
				{
					if (g_dbg && g_fDbgOut)
					{
						fprintf( g_fDbgOut, "%lu [flush ignored] request time = %lu prev request = %lu silence = %d\n",
							SI_GetRawTime(), g_pshmem->flush_requested, last_flush_requested, is_silence );
					}
					last_flush_requested = g_pshmem->flush_requested;
				}
				// Fill with silence
				if (!is_silence && silence_rate > 0 && consecutive_silence_count <= cbs_max)
				{
					long now = SI_GetRawTime();
					long elapsed = now - prevWriteTime;
					// FIXME - With 8k buffer, we should be sending a buffer every 47ms
					// This should be a cumulative running average, not a simple linear
					// measurement - this is subject to false positives on video render
					if (elapsed > silence_rate)
					{
						is_silence = was_last_block_post_tailfill_silence = 1;
						if (g_dbg && g_fDbgOut)
						{
							fprintf( g_fDbgOut, "%lu [silence rate] prev write = %lu elapsed %lu\n",
								now, prevWriteTime, elapsed );
						}
					}
				}
				if (!is_silence)
				{
					continue;
				}
				else
				{
					// Prepare for common block write below
					bytes_read = block_factor;
					memset( buff, 0, block_factor );
				}
			} // post-tailfill
			// If block offset != 0, all tailfill cases will be handled below
		}
		else
		{

			// Try to read a full buffer, or read partial buffer to completion.
			// We may block on this
			bytes_to_read = block_factor - buffer_offset;
			bytes_read = read( h_in, &buff[buffer_offset], bytes_to_read );
			if (bytes_read <= 0)
			{
				if (bytes_read < 0)
				{
					syslog( LOG_ERR, "error: read returned %d errno %d", bytes_read, errno );
				}
				else
				{
					syslog( LOG_INFO, "Got EOF reading" );
					break;
				}
				g_quit = true;
				break;
			}

			// If logging data as received, write to file
			if (g_fPcmIn)
			{
				int pcm_written = fwrite( &buff[buffer_offset], sizeof(char), bytes_read, g_fPcmIn );
				if (pcm_written != bytes_read)
				{
					syslog( LOG_ERR, "failed to write to pcm input copy (errno=%d) - closing", errno );
					fclose( g_fPcmIn );
					g_fPcmIn = NULL;
				}
				else if (g_dbg && g_fDbgOut)
				{
					// Precedes write message
					fprintf( g_fDbgOut, "%lu [pcmin#%lu] %u samples\n",
						SI_GetRawTime(), pcmInBytes >> 2, pcm_written >> 2 );
				}
				pcmInBytes += pcm_written;
			}

			// Test for trailing silence
			did_last_block_end_silent = 0;
			if (trailing_silence_trigger > 0 && bytes_read >= trailing_silence_trigger)
			{
				int trailing_silence_count = TrailingSilenceBytes( &buff[buffer_offset], bytes_read );
				if (trailing_silence_count >= trailing_silence_trigger)
				{
					did_last_block_end_silent = 1;
				}
				else if (trailing_silence_count > 0 && g_dbg && g_fDbgOut)
				{
					fprintf( g_fDbgOut, "%lu [tsc=%u<thresh]\n",
						SI_GetRawTime(), trailing_silence_count );
				}
			}

		} // Read bytes

		// If not a full buffer, fill tail with silence
		unsigned long now = SI_GetRawTime();
		long elapsed = now - prevReadTime;
		if (bytes_read > 0 && !is_silence)
		{
			prevReadTime = now;
		}
		if (bytes_read < bytes_to_read)
		{
			buffer_offset += bytes_read;
			// If a flush requested, mark flush as pending for when input is empty
#ifdef USE_SHMEM
			int flush = 0;
			if (g_pshmem && g_pshmem->flush_requested != last_flush_requested)
			{
				// Expire flush requests after 200ms
				last_flush_requested = g_pshmem->flush_requested;
				if (now > last_flush_requested && now - last_flush_requested < flush_expiry)
				{
					flush = 1;
				}
				else if (g_dbg && g_fDbgOut)
				{
					fprintf( g_fDbgOut, "%lu [flush expired] elapsed = %ld read = %d request time = %lu request age = %ld\n",
						now, elapsed, bytes_read, last_flush_requested, now - last_flush_requested );
				}
			}
			if (flush && !ignore_flush)
			{
				if (g_dbg && g_fDbgOut)
				{
					fprintf( g_fDbgOut, "%lu [flush-defer] elapsed = %ld read = %d request time = %lu offset %lu flush elapsed = %ld\n",
						now, elapsed, bytes_read, last_flush_requested, buffer_offset, now - last_flush_requested );
				}
				// Pending flush will apply when no input ready
				// It will effectively short-circuit the usual delay
				flush_pending = 1;
				continue;
			}
			else
#endif
			// If we actually read something and time since last read < {tail_fill_min}ms, keep waiting
			// Disregard elapsed time if this is the first read in a buffer
			if (bytes_read > 0 && (buffer_offset == bytes_read ||
				elapsed < (did_last_block_end_silent ? tail_fill_after_trailing_silence : tail_fill_min)
						))
			{
				partial_block_count++;
				if (g_dbg && g_fDbgOut)
				{
					fprintf( g_fDbgOut, "%lu  [part] elapsed = %ld read = %d offset = %d lbes = %d\n",
						now, elapsed, bytes_read, buffer_offset, did_last_block_end_silent );
				}
				continue;
			}
			else if (bytes_read == 0 && elapsed < (did_last_block_end_silent ? tail_fill_after_trailing_silence : empty_tail_min))
			{
				if (g_dbg && g_fDbgOut)
				{
					fprintf( g_fDbgOut, "%lu  [empty] elapsed = %ld offset = %d lbes = %d\n",
						now, elapsed, buffer_offset, did_last_block_end_silent );
				}
				continue;
			}
			if (g_dbg && g_fDbgOut)
			{
				fprintf( g_fDbgOut, "%lu  [tail] elapsed = %ld read = %d offset = %d filling %d lbes = %d\n",
						now, elapsed, bytes_read, buffer_offset, block_factor - buffer_offset, did_last_block_end_silent );
			}
			tail_fill_count++;
			memset( &buff[buffer_offset], 0, block_factor - buffer_offset );
			was_last_block_tailfilled = 1;
			last_tailfill_start_time = now;
		}
		else if (!is_silence)
		{
			full_block_count++;
			was_last_block_tailfilled = 0;
		}
		else
		{
			silence_count++;
		}

		// We've reached here one of four ways:
		// 1. Full buffer read
		// 2. Partial buffer read with tail fill (write trailing 0 to fill buffer)
		// 3. Write buffer of silence based on post_tail_fill_silence_start : post_tail_fill_silence_duration
		//	window
		// 4. Partial buffer read with flush request

		// Reset partial read buffer offset
		buffer_offset = 0;

		// If writing silence, check write availability
		if (is_silence && !ALSA_direct)
		{
			FD_ZERO( &writefds );
			FD_SET( h_out, &writefds );
			timeout.tv_sec = 0;
			timeout.tv_usec = max_silence_output_sleep_time * 1000;
			if (select(h_out+1, NULL, &writefds, NULL, &timeout) <= 0)
			{
				if (g_dbg && g_fDbgOut)
				{
					// Precedes write message
					fprintf( g_fDbgOut, "%lu [write_timeout]\n",
						now );
				}
				continue;
			}
		}

		if (is_silence)
		{
			consecutive_silence_count++;
		}
		else
		{
			// Reset
			consecutive_silence_count = 0;
		}

		// Record last write time
		prevWriteTime = now;

		if (ALSA_direct)
		{
				// Block SIGIO - we did not request it but this seems to happen and causes problems with pthread_mutex
				// in libasound. Investigational experiment related to bug #5013
				sigprocmask(SIG_BLOCK, &g_signalUnblockMask, &g_oldSignalMask);

        		signed short *ptr;
	        	int err, cptr;
        	        ptr = (signed short*)buff;
        	        cptr = block_factor / 4;
        	        while (cptr > 0) {
							err = snd_pcm_hwsync( handle );
							if (err < 0)
							{
								printf( "snd_pcm_hwsync() returned %d\n", err );
							}
	                        err = snd_pcm_writei(handle, ptr, cptr);
	                        if (err == -EAGAIN)
	                                continue;
	                        if (err < 0) {
	                                if (xrun_recovery(handle, err) < 0) {
	                                        printf("Write error: %s\n", snd_strerror(err));
	                                        exit(EXIT_FAILURE);
	                                }
	                                break;  /* skip one period */
	                        }
	                        ptr += err * channels;
	                        cptr -= err;
	                }

				// Restore previous mask
				sigprocmask(SIG_SETMASK, &g_oldSignalMask, NULL);

			totalBlockCount++;
		} // ALSA
		else
		{
			// Writing via aplay
			// Write to aplay. We may block on this
			int bytes_written = write( h_out, buff, block_factor );

			if (bytes_written < block_factor)
			{
				if (bytes_written >= 0)
				{
					short_write_count++;
					syslog( LOG_ERR, "short write (%d bytes)", bytes_written );
					// Probably an overrun
					if (is_silence)
					{
						// Turn off silence pump
						was_last_block_post_tailfill_silence = 0;
						was_last_block_tailfilled = 0;
					}
				}
				else
				{
					write_error_count++;
					syslog( LOG_ERR, "failed write (%d returned, errno=%d)", bytes_written, errno );
				}
			}
			else
			{
				fflush( fAplay );
				totalBlockCount++;
			}
		} // aplay

		// Write to pcm out
		if (g_fPcmOut)
		{
			int pcmout_written = fwrite( buff, sizeof(char), block_factor, g_fPcmOut );
			if (pcmout_written != block_factor)
			{
				syslog( LOG_ERR, "failed to write to pcm output (errno=%d) - closing", errno );
				fclose( g_fPcmOut );
				g_fPcmOut = NULL;
			}
			else if (g_dbg && g_fDbgOut)
			{
				// Precedes write message
				fprintf( g_fDbgOut, "%lu [pcmout#%lu]\n",
						now, pcmOutBytes >> 2 );
			}
			pcmOutBytes += pcmout_written;
		}

		// If we've already written 2 or more silence filler blocks and time to write has gone above 10,
		// turn off the silence pump
		unsigned long postWrite = SI_GetRawTime();
		long writeElapsed = postWrite - now;
		if (consecutive_silence_count >= cbs_max || (consecutive_silence_count > 1 && writeElapsed > max_silence_write_time))
		{
			was_last_block_post_tailfill_silence = 0;
			was_last_block_tailfilled = 0;
		}

		if (g_dbg && g_fDbgOut)
		{
			long elapsedDebug = postWrite - prevDebugOut;
			//if (elapsedDebug > 10000 || elapsedDebug < 0)
			{
				fprintf( g_fDbgOut, "%lu write elapsed %ld partial %d full %d tail fill %d write %lu short %d werror %d silence %d cbs %d\n",
					postWrite, writeElapsed,
					partial_block_count, full_block_count, tail_fill_count,
					totalBlockCount, short_write_count, write_error_count,
					is_silence, consecutive_silence_count );
				prevDebugOut = postWrite;
				// Reset counters
				partial_block_count = full_block_count = tail_fill_count = 0;
				short_write_count = write_error_count = 0;
			}
		}

	} // quit flag not set

#ifdef USE_SHMEM
	if (g_shmh >= 0)
	{
		if (g_pshmem)
		{
			syslog( LOG_INFO, "detaching shared memory 0x%08lx from id %d", g_pshmem, g_shmh );
			munmap( g_pshmem, sizeof(Shmem) );
			g_pshmem = NULL;
		}
		syslog( LOG_INFO, "closing shared mem id %d", g_shmh );
		close( g_shmh );
		g_shmh = -1;
		if (shmem_segname[0])
		{
			syslog( LOG_INFO, "unlinking %s", shmem_segname );
			shm_unlink( shmem_segname );
		}
	}
#endif

	if (fAplay)
	{
		pclose( fAplay );
		fAplay = NULL;
	}

	if (g_fPcmOut)
	{
		fclose( g_fPcmOut );
		g_fPcmOut = NULL;
	}

	if (g_fPcmIn)
	{
		fclose( g_fPcmIn );
		g_fPcmIn = NULL;
	}

	if (ALSA_direct && handle)
	{
		snd_pcm_close( handle );
        handle = 0;
	}

	syslog( LOG_INFO, "read  stats: %d full blocks, %d partial blocks, %d blocks tail-filled\n",
		full_block_count, partial_block_count, tail_fill_count );
	syslog( LOG_INFO, "write stats: total blocks %lu short block writes %d failed block writes %d",
		totalBlockCount, short_write_count, write_error_count );

	if (g_fDbgOut)
	{
		fclose( g_fDbgOut );
		g_fDbgOut = NULL;
	}

	signal( SIGPIPE, SIG_IGN );

	syslog( LOG_INFO, "Exiting normally" );

	return 0;
}

