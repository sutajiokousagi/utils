// $Id$
// findsyms.cpp - find kernel syms closest to an address
// Copyright (C) 2008 Chumby Industries, Inc. All rights reserved.

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int main( int argc, char *argv[] )
{
	unsigned int addr;
	if (argc < 2)
	{
		printf( "Syntax: %s addr\n\twhere addr is a hex address\n", argv[0] );
		return -1;
	}

	if (sscanf( argv[1], "%x", &addr ) < 1)
	{
		printf( "Could not scan hex address from %s - don't include 0x\n", argv[1] );
		return -1;
	}

	printf( "Checking /proc/kallsyms for %08x\n", addr );

	FILE *fpSyms = fopen( "/proc/kallsyms", "r" );
	if (fpSyms == NULL)
	{
		printf( "Error: could not open /proc/kallsyms, errno = %d\n", errno );
		return -1;
	}

	char line[1024];
#define MAX_CANDIDATES 4
	char closest[MAX_CANDIDATES][1024];
	int closest_dist[MAX_CANDIDATES];
	int closest_count = 0;
	int total_lines = 0;

	while (fgets( line, sizeof(line), fpSyms ))
	{
		total_lines++;
		line[sizeof(line)-1] = '\0';
		char *eol = strpbrk( line, "\r\n" );
		if (eol) *eol = '\0';
		unsigned int sym_addr;
		if (sscanf( line, "%08x ", &sym_addr ) < 1)
		{
			continue;
		}
		int dist;
		if (sym_addr == 0)
		{
			continue;
		}
		if (sym_addr < addr)
		{
			dist = addr - sym_addr;
		}
		else
		{
			dist = -(sym_addr - addr);
		}
		int n;
		bool replaced = false;
		for (n = 0; n < closest_count; n++)
		{
			if (abs(dist) < abs(closest_dist[n]))
			{
				strcpy( closest[n], line );
				closest_dist[n] = dist;
				replaced = true;
				break;
			}
		}
		if (!replaced && n < MAX_CANDIDATES)
		{
			strcpy( closest[n], line );
			closest_dist[n] = dist;
			closest_count++;
		}
	}

	fclose( fpSyms );

	printf( "total lines = %d\n", total_lines );
	printf( "%d closest addresses:\n", closest_count );

	// Grade by address
	int grade_vec[MAX_CANDIDATES];
	int n;
	for (n = 0; n < MAX_CANDIDATES; n++)
	{
		// Set grade vector to identity
		grade_vec[n] = n;
	}

	bool done = false;
	while (!done)
	{
		done = true;
		for (n = 0; n < closest_count-1; n++)
		{
		}
	}

	printf( "Distance Address  Type, name\n" );
	for (n = 0; n < closest_count; n++)
	{
		printf( "%8d %s\n", closest_dist[grade_vec[n]], closest[grade_vec[n]] );
	}

	return 0;
}

