#ifndef C_GIFLIB_WRAPPER_H
#define C_GIFLIB_WRAPPER_H

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct giflib_decoder_struct *giflib_decoder;
typedef struct giflib_encoder_struct *giflib_encoder;

giflib_decoder giflib_decoder_create(const opencv_mat buf);
int giflib_get_decoder_width(const giflib_decoder d);
int giflib_get_decoder_height(const giflib_decoder d);
int giflib_get_decoder_num_frames(const giflib_decoder d);
void giflib_decoder_release(giflib_decoder d);
bool giflib_decoder_slurp(giflib_decoder d);
bool giflib_decoder_decode(giflib_decoder d, int frame_index, opencv_mat mat);

giflib_encoder giflib_encoder_create(vec buf, const giflib_decoder d);
bool giflib_encoder_init(giflib_encoder e, int width, int height);
bool giflib_encoder_encode_frame(giflib_encoder e, int frame_index, const opencv_mat frame);
bool giflib_encoder_spew(giflib_encoder e);
void giflib_encoder_release(giflib_encoder e);

#ifdef __cplusplus
}
#endif

#endif
