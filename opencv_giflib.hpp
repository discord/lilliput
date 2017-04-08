#ifndef C_GIFLIB_WRAPPER_H
#define C_GIFLIB_WRAPPER_H

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct giflib_decoder_struct *giflib_Decoder;

giflib_Decoder giflib_createDecoder(const opencv_Mat buf);
int giflib_get_decoder_width(const giflib_Decoder d);
int giflib_get_decoder_height(const giflib_Decoder d);
int giflib_get_decoder_num_frames(const giflib_Decoder d);
void giflib_decoder_release(giflib_Decoder d);
bool giflib_decoder_slurp(giflib_Decoder d);
bool giflib_decoder_decode(giflib_Decoder d, int frame_index, opencv_Mat mat);

#ifdef __cplusplus
}
#endif

#endif
