#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


struct accelReadDataV1 {
  unsigned int version;
  unsigned int timestamp;
  unsigned int inst[3];  // x, y, z
  unsigned int avg[3];
  unsigned int impact[3]; // values for the last impact peak acceleration
  unsigned int impactTime;
  unsigned int impactHint;
};

struct accelReadDataV3 {
  struct accelReadDataV1 v1;
  unsigned int gRange;    // g range in milli-g's
};

enum {
    ACCEL_NO_TAP         = 0x00,
    ACCEL_SINGLE_TAP     = 0x01,
    ACCEL_DOUBLE_TAP     = 0x02,
    ACCEL_DNE            = 0x03  // in Kionix doc, not sure what it means???
};

enum {
    ACCEL_FU             = 0x01,        // face-up
    ACCEL_FD             = 0x02,        // face-down
    ACCEL_UP             = 0x04,        // up
    ACCEL_DO             = 0x08,        // down
    ACCEL_RI             = 0x10,        // right
    ACCEL_LE             = 0x20         // left
};

enum {
    ACCEL_ZP             = ACCEL_FU,    // Z+
    ACCEL_ZN             = ACCEL_FD,    // Z-
    ACCEL_YP             = ACCEL_UP,    // Y+
    ACCEL_YN             = ACCEL_DO,    // Y-
    ACCEL_XP             = ACCEL_RI,    // X+
    ACCEL_XN             = ACCEL_LE     // X-
};

enum {
    ACCEL_FACE_UP_TAP    = ACCEL_ZP,
    ACCEL_FACE_DOWN_TAP  = ACCEL_ZN,
    ACCEL_UP_TAP         = ACCEL_YP,
    ACCEL_DOWN_TAP       = ACCEL_YN,
    ACCEL_RIGHT_TAP      = ACCEL_XP,
    ACCEL_LEFT_TAP       = ACCEL_XN
};

enum {
    ACCEL_FACE_UP_TILT   = ACCEL_FU,
    ACCEL_FACE_DOWN_TILT = ACCEL_FD,
    ACCEL_UP_TILT        = ACCEL_UP,
    ACCEL_DOWN_TILT      = ACCEL_DO,
    ACCEL_RIGHT_TILT     = ACCEL_RI,
    ACCEL_LEFT_TILT      = ACCEL_LE
};

// TILT event -- note tilt_pos_cur/tilt_pos_pre will be zero for no event
//
struct accel_tilt {
    unsigned long timestamp;    // event time in jiffies
    unsigned char tilt_pos_cur; // current ACCEL_FACE_UP_TILT .. ACCEL_LEFT_TILT
    unsigned char tilt_pos_pre; // previous ACCEL_FACE_UP_TILT ...
};

// TAP event -- note tap_event will be ACCEL_NO_TAP for no event
//
struct accel_tap {
    unsigned long timestamp;    // event time in jiffies
    unsigned char tap_event;    // ACCEL_NO_TAP .. ACCEL_DNE
    unsigned char tap_axis;     // ACCEL_FACE_UP_TAP .. ACCEL_LEFT_TAP
};

struct accelReadDataV5 {
    struct accelReadDataV3  v3;
    struct accel_tilt       tilt;
    struct accel_tap        tap;
};


// Additional data present if version >= 3
typedef struct _v3data_t {
	unsigned int gRange;
} v3data_t;

typedef struct accel {
    union {
        struct {
            unsigned int version;
            unsigned int timestamp;
            unsigned int inst[3];
            unsigned int avg[3];
            unsigned int impact[3];
            unsigned int impactTime;
            unsigned int impactHint;
            v3data_t v3;
        } v1;
        struct accelReadDataV5 v5;
    } u;
	// End data

	enum accel_constants { SIGNED_BIAS = 2048, FULL_RANGE = 4095, HALF_RANGE = 2047 };
	unsigned int GetGRange() const { if (u.v1.version < 3) return 3000; else return u.v1.v3.gRange; }
	// Instantaneous value (x=0,y=1,z=2) in milliG's
	int GetInst_mG(int xyz) const;
	int GetAvg_mG(int xyz) const;
	int GetImpact_mG(int xyz) const;
	int Get_mG(unsigned int u) const;
} accel_t;


int g_bias = 2048;
size_t g_size = sizeof(struct accelReadDataV1); // Assume v1
bool g_mg = false;
bool g_tt = false;
int g_delta = 1;
int g_version = 1;


// Dump a single entry to stdout
void
Dump( int iteration, accel_t const *d )
{
	char tsbuff[128];
	char impbuff[128];
	/**
#define TIMEFMT_SHORT "%d%b%Y %D"
#define TIMEFMT_SHORT "%T"
#define TIMEFMT_LONG "%a %d%b%Y %T"
#define TIMEFMT_LONG "%a %T"
	struct tm *ltime;
	time_t now = d->timestamp;
	ltime = localtime( &now );
	strftime( tsbuff, sizeof(tsbuff), TIMEFMT_LONG, ltime );
	now = d->impactTime;
	ltime = localtime( &now );
	strftime( impbuff, sizeof(impbuff), TIMEFMT_SHORT, ltime );
	**/
	// timestamp and impactTime are in 10ms ticks
	sprintf( tsbuff, "%d", d->u.v1.timestamp / 100 );
	sprintf( impbuff, "T-%ds", (d->u.v1.timestamp - d->u.v1.impactTime) / 100 );
	char extra[256] = {0};

	sprintf( extra, " hint=%08lx", d->u.v1.impactHint );
	if (d->u.v1.version >= 3)
	{
		sprintf( &extra[strlen(extra)], " gRange=%d", d->u.v1.v3.gRange );
	}

	if (g_mg)
	{
		printf( "[%5d] %s inst=(%d,%d,%d)mG  \r",
			iteration,
			tsbuff,
			d->GetInst_mG(0),
			d->GetInst_mG(1),
			d->GetInst_mG(2) );
	}
	else
	{
        if (!g_tt) {
            printf( "[%d]v%d %s inst=(%d,%d,%d)raw,(%d,%d,%d)mg; avg=(%d,%d,%d) imp=(%d,%d,%d)@%s%s\n",
                    iteration,
                    d->u.v1.version,
                    tsbuff,
                    d->u.v1.inst[0]-g_bias, d->u.v1.inst[1]-g_bias, d->u.v1.inst[2]-g_bias,
                    d->GetInst_mG(0), d->GetInst_mG(1), d->GetInst_mG(2),
                    d->u.v1.avg[0]-g_bias, d->u.v1.avg[1]-g_bias, d->u.v1.avg[2]-g_bias,
                    d->u.v1.impact[0]-g_bias, d->u.v1.impact[1]-g_bias, d->u.v1.impact[2]-g_bias,
                    impbuff,
                        extra );
        }

        if (d->u.v1.version >= 5) {
            if (d->u.v5.tilt.tilt_pos_cur || d->u.v5.tilt.tilt_pos_pre) {
                sprintf( impbuff, "T-%ds", (d->u.v1.timestamp - d->u.v5.tilt.timestamp) / 100 );
                printf( "[%d]v%d %s tilt=(%02x)pre (%02x)cur@%s\n",
                        iteration, d->u.v1.version, tsbuff, 
                        d->u.v5.tilt.tilt_pos_pre, d->u.v5.tilt.tilt_pos_cur, 
                        impbuff);
            }

            if (d->u.v5.tap.tap_event != ACCEL_NO_TAP) {
                sprintf( impbuff, "T-%ds", (d->u.v1.timestamp - d->u.v5.tap.timestamp) / 100 );
                printf( "[%d]v%d %s tap=(%02x)event (%02x)axis@%s\n",
                        iteration, d->u.v1.version, tsbuff, 
                        d->u.v5.tap.tap_event, d->u.v5.tap.tap_axis, 
                        impbuff);
            }
        }
	}
}

int accel_t::GetInst_mG(int xyz) const
{
	return Get_mG(u.v1.inst[xyz]);
}

int accel_t::GetAvg_mG(int xyz) const
{
	return Get_mG(u.v1.avg[xyz]);
}

int accel_t::GetImpact_mG(int xyz) const
{
	return Get_mG(u.v1.impact[xyz]);
}

int accel_t::Get_mG(unsigned int u) const
{
	// Convert to signed value
	int s = u - accel_t::SIGNED_BIAS;
	// Scale to milliGs
	int r = GetGRange();
	int res = s * r;
	res /= accel_t::HALF_RANGE;
	//printf( "\nraw u=%u s=%d range=%d res=%d\n", u, s, r, res );
	return res;
}

int
SyntaxError( int argc, char *argv[], const char *msg )
{
	printf( "Syntax error: %s\n", msg );
	printf( "syntax: %s count [--mg] [--bias=<n>] [--tt] < /dev/accel\n", argv[0] );
	printf( "count is 0 to loop infinitely or number of iterations to read\n" );
	printf( "options:\n\
  --mg  Only display instantaneous values in milliGs\n\
  --bias=<n>	Set bias to specified value (default=2048)\n\
  --delta=<n>	Minimum delta to display change (default=1)\n\
  --tt  Only display tap and tilt events\n\
" );
	return 1;
}

int
main( int argc, char *argv[] )
{
	printf( "accelcat v0.18\n" );
	if (argc < 2)
	{
		return SyntaxError( argc, argv, "No count specified" );;
	}

	int count = -1;
	int n;
	for (n = 1; n < argc; n++)
	{
		if (!strcmp( argv[n], "--tt" ))
		{
			g_tt = true;
		}
		else if (!strcmp( argv[n], "--mg" ))
		{
			g_mg = true;
		}
		else if (!strncmp( argv[n], "--bias=", 7 ))
		{
			g_bias = atoi( &argv[n][7] );
		}
		else if (!strncmp( argv[n], "--delta=", 8 ))
		{
			g_delta = atoi( &argv[n][8] );
		}
		else
		{
			count = atoi( argv[n] );
		}
	}
	if (count < 0)
	{
		return SyntaxError( argc, argv, "Count missing from options or invalid" );
	}
	accel_t d;
    bool tt = false;
	int lasti0=-1, lasti1=-1, lasti2=-1;
	int records_read = 0;
	int records_displayed = 0;
	for (n = 0; !count || records_displayed < count; n++)
	{
		memset( &d, 0, sizeof(d) );
		size_t bytesread = fread( &d, 1, g_size, stdin );
		if (bytesread != g_size)
		{
			printf( "Expected %d got %d bytes\n", g_size, bytesread );
		}
		// First time through interpret and display version
		if (n == 0)
		{
			if (d.u.v1.version >= 3)
			{
				g_size = d.u.v1.version == 3? sizeof(struct accelReadDataV3) : 
                                              sizeof(struct accelReadDataV5);
				// Synchronize input
				fread( &d.u.v1.v3, 1, sizeof(d.u.v1.v3), stdin );
                if (d.u.v1.version == 5) {
                    fread(&d.u.v5.tilt, 1, sizeof(d.u.v5.tilt), stdin);
                    fread(&d.u.v5.tap, 1, sizeof(d.u.v5.tap), stdin);
                }
			}
			printf( "Version = %08lx (%02x %02x %02x %02x) gRange=%d\n",
					d.u.v1.version, (int)(d.u.v1.version>>24),
					(int)((d.u.v1.version>>16)&0xff),
					(int)((d.u.v1.version>>8)&0xff),
					(int)(d.u.v1.version&0xff),
					d.GetGRange() );
		}
		records_read++;
        tt = d.u.v1.version == 5 &&
             (d.u.v5.tap.tap_event != ACCEL_NO_TAP ||
              d.u.v5.tilt.tilt_pos_cur || d.u.v5.tilt.tilt_pos_pre);
        if (g_tt && tt) {
            Dump( n, &d );
            records_displayed++;
        } else
		if (    tt
            ||  abs( d.u.v1.inst[0] - lasti0 ) >= g_delta
			||	abs( d.u.v1.inst[1] - lasti1 ) >= g_delta
			||	abs( d.u.v1.inst[2] - lasti2 ) >= g_delta )
		{
			Dump( n, &d );
			lasti0 = d.u.v1.inst[0];
			lasti1 = d.u.v1.inst[1];
			lasti2 = d.u.v1.inst[2];
			records_displayed++;
		}
	}
	printf( "\nDone - %d/%d records displayed.\n", records_displayed, records_read );

	return 0;
}

