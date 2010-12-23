// $Id$
// progress_updater.cpp - wrapper program to perform various update tasks
// and update a progress thermometer in proportion to actual data processed

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <dirent.h>
#include <errno.h>
#include <signal.h>

#include "fbtext.h"

#define VER_STR "0.27"
#define HELP_MSG "Syntax: %s <options>\n\
where <options> consists of one or more of:\n\
--help	Display this message\n\
--phase={1|2}	(required) Current phase\n\
--rfs=<devnode>	(required) Block device for root filesystem to be updated, e.g. /dev/mmcblk0p3\n\
--kernel={krnA|krnB}	(required) Code to substitute for block or blockz writes where code=kernel\n\
--dir=<update_dir>		(required) Directory to search for cfg files\n\
--blockdev=<devnode>	(default=/dev/mmcblk0p1) Device node to pass to config_util as --dev= for block writes\n\
--rfstag={/etc/rfsA|/etc/rfsB} Tag to add to rfs after extracting via touch\n\
"

// Input options
int g_phase = 0;
char g_rfs[256] = {0};
char g_rfstag[256] = {0};
char g_kernel[64] = {0};
char g_dir[1024] = {0};
// Default arg to pass to config_util as --dev=
char g_blockdev[1024] = "/dev/mmcblk0p1";
// Temporary mount point for device specified by --rfs=
char g_rfs_mount[256] = {0};
int g_rfs_mount_refs = 0;

// Error messages
char g_parse_error[1024];

// Turn off progress display
int g_silent = 0;

// Cumulative progress in bytes
unsigned long long g_cumulative = 0;

// Total in bytes for this phase
unsigned long long g_phaseTotal;

// FBWIDTH and FBHEIGHT are passed in at compile time
// g_width and g_height are initialized from environment or /psp/video_res in fbtext_init()

// Ironforge (w=320 h=240) used a thermometer at
//      top             left    bottom  right           height  width
//      144             21              161             289                     17              268
//      60%             6.5625% 67.083% 90.3125%        7.083%  83.75%

// Thermometer margin offsets
#define THERMO_LEFT_MARGIN_PCT  0.065625
#define THERMO_TOP_MARGIN_PCT   0.60
#define THERMO_BOTTOM_MARGIN_PCT 0.670833333
#define THERMO_TOP2_MARGIN_PCT  0.70
#define THERMO_BOTTOM2_MARGIN_PCT 0.770833333
#define THERMO_RIGHT_MARGIN_PCT 0.903125
#define THERMO_HEIGHT_PCT       0.07083333
#define THERMO_WIDTH_PCT        0.8375
static int g_leftOff, g_topOff[2], g_bottomOff[2], g_rightOff, lastBarValue[2];
void updateThermometerOffsets()
{
        g_leftOff = lastBarValue[0] = lastBarValue[1] = (int)(g_width * THERMO_LEFT_MARGIN_PCT);
        g_topOff[0] = (int)(g_height * THERMO_TOP_MARGIN_PCT);
        g_topOff[1] = (int)(g_height * THERMO_TOP2_MARGIN_PCT);
        g_bottomOff[0] = (int)(g_height * THERMO_BOTTOM_MARGIN_PCT);
        g_bottomOff[1] = (int)(g_height * THERMO_BOTTOM2_MARGIN_PCT);
        g_rightOff = (int)(g_width * THERMO_RIGHT_MARGIN_PCT);
        fprintf( stderr, "left/right = %d,%d; top/bottom[0] = %d,%d; top/bottom[1] = %d,%d\n", g_leftOff, g_rightOff,
			g_topOff[0], g_bottomOff[0], g_topOff[1], g_bottomOff[1] );
}

enum {
        PROGRESS_NORMAL=0,
        PROGRESS_BAD,
        PROGRESS_LOADING
};

// We support two progress bars; the top is the current file, bottom is total
void updateProgressBar( int perc, int color, int index )
{
        if (!g_silent)
        {
                int barValue;
                int r, g, b;
                if (perc > 100) perc = 100;
                barValue = (int)( g_width * THERMO_LEFT_MARGIN_PCT + g_width * THERMO_WIDTH_PCT * ( perc * 0.01 ) );
                //fprintf( stderr, "p=%d index=%d, bv=%d\n", perc, index, barValue );
                // color is either 0 for normal progress (blue), 1 for bad (red) or 2 for loading (light blue)
                switch (color)
                {
                        case PROGRESS_NORMAL:   // Normal
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
                fbtext_fillrect( g_topOff[index], lastBarValue[index], g_bottomOff[index], barValue, r, g, b );
                lastBarValue[index] = barValue;
        }
}

void clearProgressBar( int noErase, int index )
{
        if (!g_silent)
        {
                if (!noErase)
                {
                        fbtext_eraserect( g_topOff[index] - 2, g_leftOff - 2, g_bottomOff[index] + 2, g_rightOff + 2 );
                }
                lastBarValue[index] = g_leftOff;
        }
}

// Class to represent a single cfg file
class CfgFile
{
public:
	CfgFile()
	{
		src = NULL;
		md5 = NULL;
		calc_md5 = NULL;
		root = NULL;
		preuntar = NULL;
		priority = 0;
		Reset();
	};

	~CfgFile()
	{
		assign_str( &src, NULL );
		assign_str( &md5, NULL );
		assign_str( &calc_md5, NULL );
		assign_str( &root, NULL );
		assign_str( &preuntar, NULL );
	};

	void Reset()
	{
		assign_str( &src, NULL );
		assign_str( &md5, NULL );
		assign_str( &calc_md5, NULL );
		assign_str( &root, NULL );
		assign_str( &preuntar, NULL );
		type[0] = '\0';
		phase_bits = 0;
		size = -1LL;
		code[0] = '\0';
		priority = 0;
	};

	char *src;
	char type[64];
	unsigned int phase_bits;
	char *md5;
	char *calc_md5;
	long long size;
	char code[64];
	char *root;
	char *preuntar;
	int priority;

	// Validate object for completeness.
	bool IsValid( char *errmsg )
	{
		*errmsg = '\0';
		if (!type[0]) strcat( errmsg, "type " );
		if (size<=0LL) strcat( errmsg, "size " );
		if (!src || !*src) strcat( errmsg, "src " );
		if (!md5 || !*md5) strcat( errmsg, "md5 " );
		if (*errmsg)
		{
			strcat( errmsg, "<- missing" );
			return false;
		}
		if (access( src, 0 ))
		{
			strcpy( errmsg, "cannot access src" );
			return false;
		}
		// Type-specific validations
		if (!strncmp( type, "block", 5 )) // block and blockz
		{
			if (!code[0])
			{
				sprintf( errmsg, "type %s requires code", type );
				return false;
			}
		}
		else if (!strcmp( type, "tarball" ))
		{
			if (!root[0])
			{
				sprintf( errmsg, "type %s requires root", type );
				return false;
			}
			// If rfs was specified, we've already substituted temp mount point
			if (access( root, 00 ))
			{
				sprintf( errmsg, "resolved root dir %s not accessible", root );
				return false;
			}
		}
		return true;
	};

	// Parse option and value. If invalid returns false and writes error message
	bool Parse( const char *option, const char *value, char *errmsg )
	{
		if (!strcmp( option, "src" ))
		{
			assign_str( &src, value );
		}
		else if (!strcmp( option, "type" ))
		{
			strncpy( type, value, sizeof(type) );
		}
		else if (!strcmp( option, "phase" ))
		{
			phase_bits = atoi( value );
		}
		else if (!strcmp( option, "size" ))
		{
			size = atoll( value );
		}
		else if (!strcmp( option, "code" ))
		{
			strncpy( code, value, sizeof(code) );
			// Substitute kernel from cmd line (krnA, krnB) for "kernel"
			if (!strcmp( code, "kernel" ))
			{
				strcpy( code, g_kernel );
			}
		}
		else if (!strcmp( option, "root" ))
		{
			const char *rootdir = value;
			// Substitute root mount point used for rfs from command line if rfs
			if (!strcmp( rootdir, "rfs" ))
			{
				rootdir = g_rfs_mount;
				g_rfs_mount_refs++;
			}
			assign_str( &root, rootdir );
		}
		else if (!strcmp( option, "md5" ))
		{
			assign_str( &md5, value );
		}
		else if (!strcmp( option, "preuntar" ))
		{
			assign_str( &preuntar, value );
		}
		else if (!strcmp( option, "priority" ))
		{
			priority = atoi( value );
		}
		else
		{
			sprintf( errmsg, "Unhandled option in file '%s'", option );
			return false;
		}
		return true;
	};

	// Calculate md5 checksum and compare with stored checksum. false if failed to open or mismatched
	bool CalcMD5( char *errmsg )
	{
		char cmdline[1024];
		sprintf( cmdline, "md5sum %s", src );
		FILE *md5sum = popen( cmdline, "r" );
		*errmsg = '\0';
		if (!md5sum)
		{
			sprintf( errmsg, "Could not open %s", cmdline );
			return false;
		}
		assign_str( &calc_md5, NULL );
		while (fgets( cmdline, sizeof(cmdline), md5sum ))
		{
			if (calc_md5 == NULL)
			{
				char *s = strtok( cmdline, " \t\r\n" );
				assign_str( &calc_md5, s );
			}
		}
		pclose( md5sum );
		if (calc_md5 == NULL)
		{
			sprintf( errmsg, "md5sum %s did not return", src );
			return false;
		}
		if (strcmp( calc_md5, md5 ))
		{
			sprintf( errmsg, "md5=%s, md5sum returned %s - mismatch!", md5, calc_md5 );
			return false;
		}
		return true;
	};

	// Perform actual processing
	bool Process( char *errmsg )
	{
		*errmsg = '\0';
		if (!strcmp( type, "tarball" ))
		{
			return ProcessTarball( errmsg );
		}
		if (!strncmp( type, "block", 5 ))
		{
			return ProcessBlock( errmsg );
		}
		sprintf( errmsg, "%s: Unhandled type %s", __FUNCTION__, type );
		return false;
	};

private:
	// Processing for tarball
	bool ProcessTarball( char *errmsg )
	{
//#define VERBOSE_TAR
//#define TAR_VIA_FIFO
#ifndef VERBOSE_TAR
		FILE *tar_in = fopen( src, "r" );
		if (!tar_in)
		{
			sprintf( errmsg, "Cannot open %s - errno=%d (%s)", src, errno, strerror(errno) );
			return false;
		}
#endif
		if (chdir( root ))
		{
			sprintf( errmsg, "Cannot chdir(%s) - errno=%d (%s)", root, errno, strerror(errno) );
#ifndef VERBOSE_TAR
			fclose( tar_in );
#endif
			return false;
		}
		// If preuntar specified, run it and just report exit code
		if (preuntar)
		{
			int preuntar_exit = system( preuntar );
			if (preuntar_exit < 0)
			{
				fprintf( stderr, "Warning: pre-untar cmd '%s' failed, errno=%d (%s)\n", preuntar, errno, strerror(errno) );
			}
			else if (preuntar_exit)
			{
				fprintf( stderr, "Warning: pre-untar cmd '%s' returned exit code %d\n", preuntar, preuntar_exit );
			}
		}
#if !defined(VERBOSE_TAR) && defined(TAR_VIA_FIFO)
		fprintf( stderr, "Creating fifo /tmp/tar.in\n" );
		if (mkfifo( "/tmp/tar.in", 0644 ))
		{
			sprintf( errmsg, "Failed to create fifo, errno=%d (%s)", errno, strerror(errno) );
			chdir( g_dir );
			fclose( tar_in );
			return false;
		}
#endif
		fflush( stdout );
		fflush( stderr );
		fflush( stdin );
#ifdef VERBOSE_TAR
		char pipe_cmd[512];
		sprintf( pipe_cmd, "tar xzvf %s/%s 2>&1", g_dir, src );
		FILE *tar_out = popen( pipe_cmd, "r" );
		if (!tar_out)
		{
			sprintf( errmsg, "Cannot open pipe \"%s\" for output: errno=%d (%s)", pipe_cmd, errno, strerror(errno) );
			chdir( g_dir );
			return false;
		}
#else
#if defined(TAR_VIA_FIFO)
		FILE *tar_out2 = popen( "tar xzf /tmp/tar.in 2>/tmp/tar.err", "w" );
		if (!tar_out2)
		{
			sprintf( errmsg, "Cannot open tar xz for output in %s - errno=%d (%s)", root, errno, strerror(errno) );
			chdir( g_dir );
			fclose( tar_in );
			return false;
		}
		FILE *tar_out = fopen( "/tmp/tar.in", "w" );
		if (!tar_out)
		{
			sprintf( errmsg, "Cannot open named pipe - errno=%d (%s)", errno, strerror(errno) );
			chdir( g_dir );
			fclose( tar_in );
			pclose( tar_out2 );
			return false;
		}
#else
		FILE *tar_out = popen( "tar xzf - 2>/tmp/tar.err", "w" );
		if (!tar_out)
		{
			sprintf( errmsg, "Cannot open tar xz for output in %s - errno=%d (%s)", root, errno, strerror(errno) );
			chdir( g_dir );
			fclose( tar_in );
			return false;
		}
#endif
#endif
		char block_buff[1024];
		int exit_code;
#ifdef VERBOSE_TAR
		int lines_read = 0;
		// We have to approximate the conversion between files extracted and bytes read
		// 2330 files currently with 39.4mb data boils down to about 17.8k per file average.
		while (fgets( block_buff, sizeof(block_buff), tar_out ))
		{
			lines_read++;
			if (lines_read % 100 == 0)
			{
				fprintf( stderr, "%s", block_buff );
				int prog_estimate = (lines_read > 2500) ? 2500 : lines_read;
				if (prog_estimate < 2500)
				{
					updateProgressBar( (unsigned)((lines_read * 100LL) / 2500), PROGRESS_NORMAL, 0 );
					updateProgressBar( (unsigned)(((g_cumulative + size * lines_read / 2500) * 100LL) / g_phaseTotal), PROGRESS_NORMAL, 1 );
				}
				sync();
			}
		}
		g_cumulative += size;
		updateProgressBar( 100, PROGRESS_NORMAL, 0 );
		updateProgressBar( (unsigned)((g_cumulative * 100LL) / g_phaseTotal), PROGRESS_NORMAL, 1 );
		fprintf( stderr, "Extracted %d files, last file was %s", lines_read, block_buff );
		exit_code = pclose( tar_out );
#else
		int bytes_read;
		unsigned long long total_bytes_read = 0;
		memset( block_buff, 0, sizeof(block_buff) );
		for ( ;
			(bytes_read = fread( block_buff, sizeof(block_buff[0]), sizeof(block_buff)/sizeof(block_buff[0]), tar_in )) > 0;
			total_bytes_read += bytes_read)
		{
			if (total_bytes_read != 0 &&
				(total_bytes_read % (8 * 1024 * 1024) == 0 /* || (total_bytes_read % (1024 * 1024) == 0 && total_bytes_read > 32 * 1024 * 1024)*/))
			{
				fprintf( stderr, "%dMB\n", total_bytes_read / (1024 * 1024) );
				fflush( tar_out );
				fflush( stdout );
				/****
				int n;
				for (n = 0; n < 5; n++)
				{
					sleep( 5 );
					sync();
					system( "cat /proc/meminfo | awk '/^[BMC][uea][fmc][fFh][ere][red]/ {print;}'" );
				}
				***/
			}
			bool lastBlock = (bytes_read < sizeof(block_buff));
			/****
			if (lastBlock || total_bytes_read >= 41356706 - 32768)
			{
				fflush( stdout );
				system( "cat /proc/meminfo | awk '/^[BM][ue][fm][fF][er][re]/ {print;}'" );
				fprintf( stderr, "%s block - %u bytes read, total so far %u, final total = %u %02x%02x%02x%02x %02x%02x%02x%02x\n",
					lastBlock ? "Last" : "Penultimate", bytes_read, total_bytes_read, bytes_read + total_bytes_read,
					 block_buff[0], block_buff[1], block_buff[2], block_buff[3],
					 block_buff[4], block_buff[5], block_buff[6], block_buff[7] );
				fflush( stderr );
			}
			****/
			if (fwrite( block_buff, sizeof(block_buff[0]), bytes_read/sizeof(block_buff[0]), tar_out ) < bytes_read)
			{
				sprintf( errmsg, "Failed writing to tar xz pipe at byte offset %llu - errno=%d (%s)", total_bytes_read, errno, strerror(errno) );
				fprintf( stderr, "Returned to %s: %s\n", g_dir, errmsg );
				fflush( stderr );
				chdir( g_dir );
				fclose( tar_in );
#ifdef TAR_VIA_FIFO
				fclose( tar_out );
				pclose( tar_out2 );
#else
				pclose( tar_out );
#endif
				return false;
			}
			if (lastBlock)
			{
				fprintf( stderr, "Wrote last block, size = %llu, phase total = %llu\n", size, g_phaseTotal );
				fflush( stderr );
			}
			g_cumulative += bytes_read;
			updateProgressBar( (unsigned)((total_bytes_read * 100LL) / size), PROGRESS_NORMAL, 0 );
			updateProgressBar( (unsigned)((g_cumulative * 100LL) / g_phaseTotal), PROGRESS_NORMAL, 1 );
			memset( block_buff, 0, sizeof(block_buff) );
		}
#ifdef TAR_VIA_FIFO
		fclose( tar_out );
		fprintf( stderr, "Flushing pipe\n" );
		fflush( stderr );
		fflush( tar_out );
#endif
		fclose( tar_in );
		fprintf( stderr, "Wrote a total of %llu bytes (%uK) to pipe\n", total_bytes_read, total_bytes_read / 1024 );
		fflush( stderr );
		sync();
#ifdef TAR_VIA_FIFO
		exit_code = pclose( tar_out2 );
		unlink( "/tmp/tar.in" );
#else
		exit_code = pclose( tar_out );
#endif
#endif
		if (chdir( g_dir ))
		{
			sprintf( errmsg, "Failed to change dir back to %s, errno=%d (%s)", g_dir, errno, strerror(errno) );
			return false;
		}
		if (exit_code != 0)
		{
			sprintf( errmsg, "tar xz returned with exit code %d", exit_code );
			return false;
		}
		// If extracted to root and rfs tag specified, touch it now
		if (!strcmp( root, g_rfs_mount ) && g_rfstag[0])
		{
			sprintf( block_buff, "touch %s%s", root, g_rfstag );
			int touch_res = system( block_buff );
			fprintf( stderr, "%s returned %d\n", block_buff, touch_res );
		}
		return true;
	};

	// Processing for block / blockz
	bool ProcessBlock( char *errmsg )
	{
		FILE *block_in;
		bool isGzip = (strcmp( type, "blockz" ) == 0);
		char block_buff[1024];
		if (isGzip)
		{
			sprintf( block_buff, "zcat %s", src );
			fprintf( stderr, "%s | ", block_buff );
			block_in = popen( block_buff, "r" );
		}
		else
		{
			block_in = fopen( src, "r" );
		}
		if (!block_in)
		{
			sprintf( errmsg, "failed to open %ssource %s, errno=%d (%s)", isGzip ? "gzip " : "", src, errno, strerror(errno) );
			return false;
		}
		FILE *block_out;
		if (!strcmp( code, "config" ))
		{
			sprintf( block_buff, "config_util --dev=%s --cmd=put", g_blockdev );
		}
		else
		{
			sprintf( block_buff, "config_util --block=%s --dev=%s --cmd=putblock", code, g_blockdev );
		}
		fprintf( stderr, "%s%s%s\n", block_buff, isGzip ? " < " : "", isGzip ? src : "" );
		fflush( stderr );
		block_out = popen( block_buff, "w" );
		if (!block_out)
		{
			sprintf( errmsg, "failed to open %s for output, errno=%d (%s)", block_buff, errno, strerror(errno) );
			if (isGzip)
			{
				pclose( block_in );
			}
			else
			{
				fclose( block_in );
			}
			return false;
		}
		int bytes_read;
		unsigned int total_bytes_read = 0;
		fflush( stderr );
		for ( ;
			(bytes_read = fread( block_buff, sizeof(block_buff[0]), sizeof(block_buff)/sizeof(block_buff[0]), block_in )) > 0;
			total_bytes_read += bytes_read)
		{
			fwrite( block_buff, sizeof(block_buff[0]), bytes_read/sizeof(block_buff[0]), block_out );
			g_cumulative += bytes_read;
			updateProgressBar( (unsigned)((total_bytes_read * 100LL) / size), PROGRESS_NORMAL, 0 );
			updateProgressBar( (unsigned)((g_cumulative * 100LL) / g_phaseTotal), PROGRESS_NORMAL, 1 );
		}
		int blockStatus = pclose( block_out );
		fprintf( stderr, "read total %uK, config_util status = %d\n", total_bytes_read / 1024, blockStatus );
		if (isGzip)
		{
			int exit_status = pclose( block_in );
			if (exit_status)
			{
				sprintf( errmsg, "zcat exited with status %d", exit_status );
				return false;
			}
		}
		else
		{
			fclose( block_in );
		}
		if (blockStatus)
		{
			sprintf( errmsg, "config_util exited with status %d", blockStatus );
			return false;
		}
		return true;
	};

	void assign_str( char **s, const char *value )
	{
		if (*s)
		{
			free( *s );
			*s = NULL;
		}
		if (value)
		{
			*s = strdup( value );
		}
	};
};

#define MAX_CFG_FILES	128
CfgFile g_cfgs[MAX_CFG_FILES];
int g_cfgIndex[MAX_CFG_FILES];
int g_cfg_count = 0;

void assert_arg( const char *option, const char *value )
{
	if (value && *value)
	{
		return;
	}
	fprintf( stderr, "Error: --%s option requires a value argument.\n" HELP_MSG, option, "progress_updater" );
	exit( -1 );
}

bool matches( const char *option_match, const char *option, int option_length )
{
	if (option_length != strlen( option_match ))
	{
		return false;
	}
	return (strncmp( option_match, option, option_length ) == 0);
}

// Parse cfg file. true if OK
bool parse_cfg( const char *path )
{
	g_parse_error[0] = '\0';
	if (g_cfg_count >= MAX_CFG_FILES)
	{
		sprintf( g_parse_error, "Max cfg file count (%d) exceeded", g_cfg_count );
		return false;
	}
	CfgFile *cfg = &g_cfgs[g_cfg_count];
	cfg->Reset();
	FILE *f = fopen( path, "r" );
	if (!f)
	{
		sprintf( g_parse_error, "Could not open file (errno=%d, %s)", errno, strerror(errno) );
		return false;
	}
	char buff[1024];
	int line_no = 0;
	while (fgets( buff, sizeof(buff), f ))
	{
		char *option, *value;
		option = strtok( buff, "=\r\n" );
		value = strtok( NULL, "\r\n" );
		line_no++;
		// Ignore empty lines
		if (!option) continue;
		if (!*option) continue;
		// Ignore comments
		if (*option == '#') continue;
		// value is always required
		if (!value || !*value)
		{
			sprintf( g_parse_error, "Line %d (%s) has no value", line_no, option );
			fclose( f );
			return false;
		}
		if (!cfg->Parse( option, value, g_parse_error ))
		{
			fclose( f );
			return false;
		}
	}
	fclose( f );
	// Validate cfg object
	if (!cfg->IsValid( g_parse_error ))
	{
		return false;
	}
	g_cfg_count++;
	return true;
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
				fprintf( stderr, "SIGPIPE in progress_updater - terminating\n" );
				exit( -1 );
				break;
			default:
				fprintf( stderr, "Unhandled sigaction %d in progress_updater\n", signum );
				exit( -1 );
				break;
        }
}

int main( int argc, char *argv[] )
{
	printf( "%s v" VER_STR " $Rev$\n", argv[0] );

	int n;
	for (n = 1; n < argc; n++)
	{
		if (strncmp( argv[n], "--", 2 ))
		{
			fprintf( stderr, "Error: unhandled argument %s\n", argv[n] );
			fprintf( stderr, HELP_MSG, argv[0] );
			exit( -1 );
		}
		const char *option = &argv[n][2];
		const char *option_arg = strchr( option, '=' );
		int option_length;
		if (option_arg)
		{
			option_length = (int)(option_arg - option);
			option_arg++;
		}
		else
		{
			option_length = strlen(option);
		}
		if (matches( "phase", option, option_length ))
		{
			assert_arg( option, option_arg );
			g_phase = atoi( option_arg );
		}
		else if (matches( "rfs", option, option_length ))
		{
			assert_arg( option, option_arg );
			strncpy( g_rfs, option_arg, sizeof(g_rfs)-1 );
		}
		else if (matches( "kernel", option, option_length ))
		{
			assert_arg( option, option_arg );
			strncpy( g_kernel, option_arg, sizeof(g_kernel)-1 );
		}
		else if (matches( "dir", option, option_length ))
		{
			assert_arg( option, option_arg );
			strncpy( g_dir, option_arg, sizeof(g_dir)-1 );
		}
		else if (matches( "blockdev", option, option_length ))
		{
			assert_arg( option, option_arg );
			strncpy( g_blockdev, option_arg, sizeof(g_blockdev)-1 );
		}
		else if (matches( "rfstag", option, option_length ))
		{
			assert_arg( option, option_arg );
			strncpy( g_rfstag, option_arg, sizeof(g_rfstag)-1 );
		}
		else if (matches( "help", option, option_length ))
		{
			fprintf( stderr, HELP_MSG, argv[0] );
			return 0;
		}
		else
		{
			fprintf( stderr, "Unrecognized option --%s\n" HELP_MSG, option, argv[0] );
			return -1;
		}
	}

	// Check for required values
	if (g_phase == 0 || !g_rfs[0] || !g_kernel[0] || !g_dir[0])
	{
		fprintf( stderr, "Error: one or more of the required values are missing.\n" HELP_MSG, argv[0] );
		return -1;
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

	// Initialize fb text subsystem
	fbtext_init();

	// Set up progress bar display
	updateThermometerOffsets();

	// Clear progress bars
	clearProgressBar( 0, 0 );
	clearProgressBar( 0, 1 );

	// Make it our current dir
	chdir( g_dir );

	// Create unique mount point
	sprintf( g_rfs_mount, "/tmp/rfs.%u", getpid() );
	if (mkdir( g_rfs_mount, 0755 ))
	{
		fprintf( stderr, "Failed to create temporary mount point %s (errno=%d, %s)\n", g_rfs_mount, errno, strerror(errno) );
		return -1;
	}

	// Find and parse cfg files
	DIR *d = opendir( g_dir );
	if (!d)
	{
		fprintf( stderr, "Cannot open dir %s; errno=%d (%s)\n", g_dir, errno, strerror(errno) );
		return -1;
	}

	int files_found = 0;
	int cfg_files_found = 0;
	int cfg_files_parsed = 0;

	struct dirent *de;

	while (de = readdir( d ))
	{
		if (de->d_type != DT_REG && de->d_type != DT_LNK)
		{
			continue;
		}
		// Skip . files and treat them as invisible
		if (de->d_name[0] == '.')
		{
			continue;
		}
		files_found++;
		if (!strstr( de->d_name, ".cfg" ))
		{
			continue;
		}
		cfg_files_found++;
		char path[2048];
		sprintf( path, "%s/%s", g_dir, de->d_name );
		fprintf( stderr, "Parsing: %s", path );
		if (parse_cfg( path ))
		{
			fprintf( stderr, " [OK]\n" );
			cfg_files_parsed++;
		}
		else
		{
			fprintf( stderr, " [ERROR] %s\n", g_parse_error );
		}
	}

	closedir( d );

	fprintf( stderr, "Found %d/%d cfg files and parsed %d of them\n", cfg_files_found, files_found, cfg_files_parsed );

	if (cfg_files_found > cfg_files_parsed)
	{
		fprintf( stderr, "%d failures parsing cfg files\n", cfg_files_found - cfg_files_parsed );
		return -1;
	}

	// This isn't really correct but is reasonable - use uncompressed size
	// to calculate completion
	unsigned long long totalSize = 0LL;
	unsigned long long totalPhase = 0LL;
	int phaseCfgCount = 0;
	for (n = 0; n < g_cfg_count; n++)
	{
		totalSize += g_cfgs[n].size;
		if (g_cfgs[n].phase_bits & g_phase)
		{
			totalPhase += g_cfgs[n].size;
			phaseCfgCount++;
		}
	}

	if (g_phase == 1)
	{
		fprintf( stderr, "Performing MD5 checksum validation on %luK uncompressed data\n", totalSize / 1024L );
		fprintf( stderr, "%-24s %-8s md5\n", "source (src)", "size" );
		int md5_failures = 0;
		unsigned long cumulative_size = 0;
		for (n = 0; n < g_cfg_count; n++)
		{
			fprintf( stderr, "%-24s %8llu %s", g_cfgs[n].src, g_cfgs[n].size, g_cfgs[n].md5 );
			char md5_err[1024];
			cumulative_size += g_cfgs[n].size;
			if (g_cfgs[n].CalcMD5( md5_err ))
			{
				fprintf( stderr, " - md5 OK\n" /* " (ts=%lu cs=%lu pct=%u=%lu=%lu)", totalSize, cumulative_size,
					(unsigned)((cumulative_size * 100L) / totalSize), cumulative_size / totalSize, (100L * cumulative_size) / totalSize */ );
				updateProgressBar( (unsigned)((cumulative_size * 100LL) / totalSize), PROGRESS_NORMAL, 0 );
			}
			else
			{
				fprintf( stderr, " - ERR %s\n", md5_err );
				md5_failures++;
				updateProgressBar( (unsigned)((cumulative_size * 100LL) / totalSize), PROGRESS_BAD, 0 );
			}
		}
		if (md5_failures)
		{
			fprintf( stderr, "%d md5 validation errors\n", md5_failures );
			return -1;
		}
	}

	sleep( 1 );
	// Reset
	clearProgressBar( 0, 0 );

	// Mount rfs if any references to it
	if (g_rfs_mount_refs)
	{
		// MS_DIRSYNC is not supported
		// MS_SYNCHRONOUS slows things down by nearly an order of magnitude and doesn't solve the buffer
		// depletion problem
		fprintf( stderr, "Mounting %s on %s (%d references)\n", g_rfs, g_rfs_mount, g_rfs_mount_refs );
		if (mount( g_rfs, g_rfs_mount, "ext3", MS_NOATIME/*|MS_DIRSYNC*//*|MS_SYNCHRONOUS*/, NULL ))
		{
			fprintf( stderr, "Mount failed, errno=%d (%s)\n", errno, strerror(errno) );
			return -1;
		}
		else
		{
			fprintf( stderr, "Mounted successfully\n" );
		}
	}
	else
	{
		fprintf( stderr, "Skipping mount of %s - no references\n", g_rfs );
	}

	// Grade in descending order so highest priority is processed first
	fprintf( stderr, "Ordering %d files in descending priority order (highest first)\n", phaseCfgCount );
	for (n = 0; n < g_cfg_count; n++)
	{
		g_cfgIndex[n] = n;
	}
	bool ordered = false;
	while (!ordered)
	{
		ordered = true;
		for (n = 0; n < g_cfg_count-1; n++)
		{
			if (g_cfgs[g_cfgIndex[n]].priority < g_cfgs[g_cfgIndex[n+1]].priority)
			{
				ordered = false;
				int tmp = g_cfgIndex[n];
				g_cfgIndex[n] = g_cfgIndex[n+1];
				g_cfgIndex[n+1] = tmp;
			}
		}
	}

	// Process matching entries
	fprintf( stderr, "Processing %lluK of uncompressed data in %d files in this stage (phase %d)\n",
		totalPhase / 1024L, phaseCfgCount, g_phase );
	g_cumulative = 0;
	g_phaseTotal = totalPhase;
	for (n = 0; n < g_cfg_count; n++)
	{
		if ((g_cfgs[g_cfgIndex[n]].phase_bits & g_phase) == 0)
		{
			continue;
		}
		fprintf( stderr, "Processing %s type %s %s%s pri %d", g_cfgs[g_cfgIndex[n]].src, g_cfgs[g_cfgIndex[n]].type,
			g_cfgs[g_cfgIndex[n]].type[0] == 'b' ? "code " : "", g_cfgs[g_cfgIndex[n]].code, g_cfgs[g_cfgIndex[n]].priority );
		char processError[1024];
		if (g_cfgs[g_cfgIndex[n]].Process( processError ))
		{
			fprintf( stderr, " - OK\n" );
		}
		else
		{
			fprintf( stderr, " - ERROR %s\n", processError );
		}
		sync();
		clearProgressBar( 0, 0 );
	}

	// Unmount
	if (g_rfs_mount_refs)
	{
		fprintf( stderr, "Unmounting %s\n", g_rfs );
		sync();
		fprintf( stderr, "Sync completed\n" );
		fflush( stderr );
		if (umount( g_rfs_mount ))
		{
			fprintf( stderr, "Warning: unmount %s failed, errno=%d (%s)\n", g_rfs_mount, errno, strerror(errno) );
			return 0;
		}
		else
		{
			fprintf( stderr, "Unmount %s succeeded\n", g_rfs_mount );
			if (rmdir( g_rfs_mount ))
			{
				fprintf( stderr, "Warning: rmdir %s failed, errno=%d (%s)\n", g_rfs_mount, errno, strerror(errno) );
				return 0;
			}
		}
	}

	fprintf( stderr, "All complete\n" );

	return 0;
}
