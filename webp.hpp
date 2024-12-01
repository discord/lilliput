#ifndef LILLIPUT_WEBP_HPP
#define LILLIPUT_WEBP_HPP

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct webp_decoder_struct* webp_decoder;
typedef struct webp_encoder_struct* webp_encoder;

webp_decoder webp_decoder_create(const opencv_mat buf);
int webp_decoder_get_width(const webp_decoder d);
int webp_decoder_get_height(const webp_decoder d);
int webp_decoder_get_pixel_type(const webp_decoder d);
int webp_decoder_get_num_frames(const webp_decoder d);
int webp_decoder_get_total_duration(const webp_decoder d);
int webp_decoder_get_prev_frame_delay(const webp_decoder d);
int webp_decoder_get_prev_frame_dispose(const webp_decoder d);
int webp_decoder_get_prev_frame_blend(const webp_decoder d);
int webp_decoder_get_prev_frame_x_offset(const webp_decoder d);
int webp_decoder_get_prev_frame_y_offset(const webp_decoder d);
bool webp_decoder_get_prev_frame_has_alpha(const webp_decoder d);
uint32_t webp_decoder_get_bg_color(const webp_decoder d);
uint32_t webp_decoder_get_loop_count(const webp_decoder d);
size_t webp_decoder_get_icc(const webp_decoder d, void* buf, size_t buf_len);
void webp_decoder_release(webp_decoder d);
bool webp_decoder_decode(webp_decoder d, opencv_mat mat);

webp_encoder webp_encoder_create(void* buf, size_t buf_len, const void* icc, size_t icc_len, uint32_t bgcolor, int loop_count);
size_t webp_encoder_write(webp_encoder e, const opencv_mat src, const int* opt, size_t opt_len, int delay, int blend, int dispose, int x_offset, int y_offset);
void webp_encoder_release(webp_encoder e);
size_t webp_encoder_flush(webp_encoder e);
void webp_decoder_advance_frame(webp_decoder d);
int webp_decoder_has_more_frames(webp_decoder d);

#ifdef __cplusplus
}
#endif

#endif
