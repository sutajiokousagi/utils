// $Id$
// Yume floating point library test
// Copyright (C) 2010 Chumby Industries, Inc. All rights reserved

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

// Dump a single double value
void dump( double d )
{
	unsigned long long *pll = (unsigned long long*)&d;
	unsigned char *pc = (unsigned char *)&d;
	int posInf = isinf(d);
	int negInf = 0;
	if (posInf && signbit(d))
	{
		negInf = posInf;
		posInf = 0;
	}
	printf( "[%c]NaN [%c]+inf [%c]-inf %f %e %16llx %02x%02x%02x%02x ...\n",
		isnan(d) ? 'Y' : ' ',
		posInf ? 'Y' : ' ',
		negInf ? 'Y' : ' ',
		d,
		d,
		*pll,
		pc[0], pc[1], pc[2], pc[3] );
}

int main( int argc, char *argv[] )
{
	printf( "%s v0.10\n", argv[0] );
	double d;
	float f;
	unsigned long *pl;
	const char *leadFmt = "%-20s: ";
	printf( "sizeof(float)=%d, sizeof(double)=%d\n", sizeof(f), sizeof(d) );
	printf( leadFmt, "Uninitialized" );
	dump( d );
	printf( leadFmt, "NaN" );
	d = NAN;
	dump( d );
	printf( leadFmt, "NaN + 1" );
	d += 1;
	dump( d );
	printf( leadFmt, "NaN * 2" );
	d = NAN * 2;
	dump( d );
	d = INFINITY;
	printf( leadFmt, "+Infinity" );
	dump( d );
	printf( leadFmt, "+Infinity - 20000" );
	d -= 20000;
	dump( d );
	d = -INFINITY;
	printf( leadFmt, "-Infinity" );
	dump( d );
	printf( leadFmt, "-Infinity + +Infinity" );
	d += INFINITY;
	dump( d );
	d = M_PI;
	printf( leadFmt, "PI" );
	dump( d );
	d *= d;
	printf( leadFmt, "PI**2" );
	dump( d );
	printf( "Test completed\n" );
	return 0;
}

