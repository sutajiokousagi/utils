// $Id$

#include <stdio.h>
#include "fbtext.h"

#define VERSION_STR "0.12"

int main( int argc, char** argv )
{
	int show_help = 0;
	int read_stdin = 0;
	int red_val = 0, green_val = 0, blue_val = 255;
	int start_x = 0, start_y = 0;
	const char *s = NULL;
	if( argc < 2 )
	{
		show_help = 1;
	}
	else
	{
		if (!strcmp( argv[1], "--help" ))
		{
			show_help = 1;
		}
		else
		{
			int n = 1;
			while (n < argc && !strncmp( argv[n], "--", 2 ))
			{

				if (!strncmp( argv[n], "--color=", 8 ))
				{
					sscanf( &argv[n][8], "%d,%d,%d", &red_val, &green_val, &blue_val );
				}
				else if (!strncmp( argv[n], "--pos=", 6 ))
				{
					int x, y;
					sscanf( &argv[n][6], "%d,%d", &x, &y );
					start_x = x;
					start_y = y;
				}
				else
				{
					fprintf( stderr, "Unrecognized option %s\n", argv[n] );
					show_help = 1;
				}
				n++;
			}
			
			if (n >= argc)
			{
				show_help = 1;
			}
			else
			{
				s = argv[n];
				if (!strcmp( argv[n], "-" ))
				{
					read_stdin = 1;
				}
			}
		}

	}

	if (show_help)
	{
		printf( "%s v" VERSION_STR "; syntax: %s [options] <string>\n", argv[0], argv[0] );
		printf( "<string> may be - to read from stdin\n" );
		printf( "options may be one or more of:\n" );
		printf( " --help	Display this message\n" );
		printf( " --color=r,g,b	Text color (default=0,0,255)\n" );
		printf( " --pos=col,row	Display at specified character position\n" );
		exit( 0 );
	}

	//fprintf( stderr, "fbwrite v" VERSION_STR "s=[%s]\n", s );
	//fflush( stderr );

	fbtext_init();
	fbtext_clear();
	fbtext_setcolor(red_val, green_val, blue_val);
	if (start_x != 0 || start_y != 0)
	{
		fprintf( stderr, "Starting at col %d row %d\n", start_x, start_y );
	}
	fbtext_gotoxyc( start_x, start_y );
	if (read_stdin)
	{
		char buff[128];
		int line_count = 0;
		while (line_count < 25 && fgets( buff, 80, stdin ))
		{
			buff[80] = '\0';
			// FIXME handle wrapping
			fbtext_printf( "%s", buff );
			line_count++;
		}
	}
	else
	{
		fbtext_printf( "%s", s );
	}

	return 1;
}
