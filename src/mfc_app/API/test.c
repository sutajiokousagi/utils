#include "line_buf_test.h"
#include "ring_buf_test.h"
#include "encoder_test.h"
#include "display_test.h"


int main(int argc, char **argv)
{
	//Test_H263_Decoder_Line_Buffer(argc, argv);
	//Test_H264_Decoder_Line_Buffer(argc, argv);
	//Test_MPEG4_Decoder_Line_Buffer(argc, argv);
	//Test_Decoder_Ring_Buffer(argc, argv);
	Test_Display(argc, argv);
	//Test_H264_Encoder(argc, argv);
	//Test_MPEG4_Encoder(argc, argv);	
	//Test_H263_Encoder(argc, argv);
	
	return 0;
}


