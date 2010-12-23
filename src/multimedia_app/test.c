#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display_test.h"
#include "display_4_windows.h"
#include "display_optimization1.h"
#include "display_optimization2.h"
#include "cam_encoder_test.h"
#include "cam_enc_dec_test.h"
#include "cam_dec_preview.h"
#include "capture.h"
#include "jpeg_display.h"

#define VER_STR "0.10"

static void print_menu(void);


int main(int argc, char **argv)
{
	int	num;

	// If file(s) are specified, play according to extension
	int argnum;
	int files_played = 0;

	printf( "%s v%s\n", argv[0], VER_STR );

	for (argnum = 1; argnum < argc; argnum++)
	{
		// Ignore all --options. We may have something like
		// mm_app movie.264 --width=480 --height=270
		if (!strncmp( argv[argnum], "--", 2 ))
		{
			continue;
		}
		if (!strcasecmp( argv[argnum], "capture" ))
		{
			printf( "Capturing video\n" );
			Test_Capture(argc, argv);
			// We're in batch play mode - exit before menu
			files_played++;
			continue;
		}
		if (!strcasecmp( argv[argnum], "record" ))
		{
			char *fake_args[] = {"dummy", "Cam_encoding_640x480-1.264"};
			Test_Cam_Encoding( argc, argv );
			// We're in batch play mode - exit before menu
			files_played++;
			printf( "Trying to play captured video\n" );
			Test_Display_H264(2, fake_args);
			printf( "Trying to play jpeg\n" );
			Test_Jpeg_Display( 0, fake_args );
			continue;
		}
		if (!strcasecmp( argv[argnum], "jpeg" ))
		{
			Test_Jpeg_Display(argc, argv);
			if (argnum < argc) argnum++;
			files_played++;
			continue;
		}
		char *ext = strrchr( argv[argnum], '.' );
		if (!ext)
		{
			printf( "No file extension found in %s\n", argv[argnum] );
			continue;
		}
		if (!strcasecmp( ext, ".264" ))
		{
			Test_Display_H264(argc, argv);
		}
		else if (!strcasecmp( ext, ".m4v" ))
		{
			Test_Display_MPEG4(argc, argv);
		}
		else if (!strcasecmp( ext, ".263" ))
		{
			Test_Display_H263(argc, argv);
		}
		else if (!strcasecmp( ext, ".rcv" ))
		{
			Test_Display_VC1(argc, argv);
		}
		else
		{
			printf( "Unhandled extension %s in %s - recognized extensions are .264, .m4v, .263 and .vc1\n",
				ext, argv[argnum] );
			continue;
		}
		files_played++;
	}

	// If we played files in batch mode, exit now
	if (files_played > 0)
	{
		printf( "batch mode: played %d files, exiting now...\n", files_played );
		return 0;
	}

	while(1)
	{
		system("clear");
		print_menu();

		scanf("%d", &num);
		fflush(stdin);


		switch(num)
		{
			case 1:
				Test_Display_H264(argc, argv);
				break;
			case 2:
				Test_Display_MPEG4(argc, argv);
				break;
			case 3:
				Test_Display_H263(argc, argv);
				break;
			case 4:
				Test_Display_VC1(argc, argv);
				break;
			case 5:
				Test_Display_4_Windows(argc, argv);
				break;
			case 6:
				Test_Display_Optimization1(argc, argv);
				break;
			case 7:
				Test_Display_Optimization2(argc, argv);
				break;
			case 8:
				Test_Cam_Encoding(argc, argv);
				break;
			case 9:
				Test_Cam_Dec_Preview(argc, argv);
				break;
			case 10:
				Test_Cam_Enc_Dec(argc, argv);
				break;
			case 11:
				Test_Capture(argc, argv);
				break;
			case 12:
				Test_Jpeg_Display(argc, argv);
				break;
			case 13:
				exit(0);
				break;
			default:
				printf("Number is wrong\n");
				exit(0);
				break;
		}
	}


	return 0;
}

static void print_menu(void)
{
	printf("========= S3C6400/6410 Demo Application ==========\n");
	printf("=                                                =\n");
	printf("=  1.   H.264 display                            =\n");
	printf("=  2.   MPEG4 display                            =\n");
	printf("=  3.   H.263 display                            =\n");
	printf("=  4.   VC-1  display                            =\n");
	printf("=  5.   4-windows display                        =\n");
	printf("=  6.   Display using local path                 =\n");
	printf("=  7.   Display using double buffering           =\n");
	printf("=  8.   Camera preview & MFC encoding            =\n");
	printf("=  9.   MFC decoding & Camera preview            =\n");
	printf("=  10.  Camera preview & MFC encoding/decoding   =\n");
	printf("=  11.  Camera input and JPEG encoding           =\n");
	printf("=  12.  JPEG decoding and display                =\n");
	printf("=  13.  Exit                                     =\n");
	printf("=                                                =\n");
	printf("==================================================\n");
	printf("Select number --> ");
}
