#ifndef LILLIPUT_WEBP_HPP
#define LILLIPUT_WEBP_HPP

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct webp_decoder_struct* webp_decoder;
typedef struct webp_encoder_struct* webp_encoder;

webp_decoder webp_decoder_create(const opencv_mat buf);
bool webp_decoder_read_header(webp_decoder d);
int webp_decoder_get_width(const webp_decoder d);
int webp_decoder_get_height(const webp_decoder d);
int webp_decoder_get_pixel_type(const webp_decoder d);
void webp_decoder_release(webp_decoder d);
bool webp_decoder_decode(webp_decoder d, opencv_mat mat);

webp_encoder webp_encoder_create(void* buf, size_t buf_len);
size_t webp_encoder_write(webp_encoder e, const opencv_mat src, const int* opt, size_t opt_len);
void webp_encoder_release(webp_encoder e);

#ifdef __cplusplus
}
#endif

#endif
