#ifndef LILLIPUT_WEBPMUX_HPP
#define LILLIPUT_WEBPMUX_HPP

#include <webp/mux.h>
#include <webp/demux.h>
#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

WebPAnimDecoder* webpmux_create_decoder(void* buf, size_t buf_len, void* info);
WebPAnimEncoder* webpmux_create_encoder(int width, int height);
int webpmux_decoder_read_data(WebPAnimDecoder* dec, opencv_mat mat);
int webpmux_decoder_skip_frame(WebPAnimDecoder* dec);
int webpmux_encoder_add_frame(WebPAnimEncoder* enc, opencv_mat mat, int timestamp, int quality);
size_t webpmux_encoder_write(WebPAnimEncoder* enc, void* buf, size_t size, int timestamp);

#ifdef __cplusplus
}
#endif

#endif