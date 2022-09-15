#ifndef LILLIPUT_GIFLIB_HPP
#define LILLIPUT_GIFLIB_HPP

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct apng_decoder_struct* apng_decoder;
typedef struct apng_encoder_struct* apng_encoder;

typedef enum {
    apng_decoder_have_next_frame,
    apng_decoder_eof,
    apng_decoder_error,
} apng_decoder_frame_state;

apng_decoder apng_decoder_create(const opencv_mat buf);
int apng_decoder_get_width(const apng_decoder d);
int apng_decoder_get_height(const apng_decoder d);
int apng_decoder_get_num_frames(const apng_decoder d);
int apng_decoder_get_frame_width(const apng_decoder d);
int apng_decoder_get_frame_height(const apng_decoder d);
int apng_decoder_get_prev_frame_delay_num(const apng_decoder d);
int apng_decoder_get_prev_frame_delay_den(const apng_decoder d);
void apng_decoder_release(apng_decoder d);
apng_decoder_frame_state apng_decoder_decode_frame_header(apng_decoder d);
bool apng_decoder_decode_frame(apng_decoder d, opencv_mat mat);
apng_decoder_frame_state apng_decoder_skip_frame(apng_decoder d);

apng_encoder apng_encoder_create(void* buf, size_t buf_len);
bool apng_encoder_init(apng_encoder e, int width, int height, int num_frames);
bool apng_encoder_encode_frame(apng_encoder e, const opencv_mat frame, int ms);
bool apng_encoder_flush(apng_encoder e);
void apng_encoder_release(apng_encoder e);
int apng_encoder_get_output_length(apng_encoder e);

#ifdef __cplusplus
}
#endif

#endif
