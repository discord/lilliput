#ifndef LILLIPUT_PNG_HPP
#define LILLIPUT_PNG_HPP

#include <stddef.h>
#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// PNG encoding constants
#define L_PNG_COMPRESSION CV_IMWRITE_PNG_COMPRESSION

// Encoder handle
typedef struct png_encoder_struct* png_encoder;

// Error codes (reuse OpenCV error codes for consistency)
#define L_PNG_SUCCESS 0
#define L_PNG_ERROR_INVALID_CHANNEL_COUNT 1
#define L_PNG_ERROR_NULL_MATRIX 2
#define L_PNG_ERROR_INVALID_DIMENSIONS 3
#define L_PNG_ERROR_BUFFER_TOO_SMALL 4
#define L_PNG_ERROR_INVALID_ARG 5
#define L_PNG_ERROR_UNKNOWN 6


// Encoder defaults
#define L_PNG_DEFAULT_COMPRESSION 6

// Creates a PNG encoder with the given output buffer and optional ICC profile
png_encoder png_encoder_create(void* dst,
                               size_t dst_len,
                               const void* icc_data,
                               size_t icc_len);

// Encodes image data to PNG format
// src_data: raw pixel data (BGR or BGRA format)
// width, height: image dimensions
// channels: 3 for BGR, 4 for BGRA
// stride: bytes per row
// opt: encoding options array (pairs of key, value)
// opt_len: length of opt array
// out_size: output parameter for encoded size
int png_encoder_encode(png_encoder e,
                       const void* src_data,
                       int width,
                       int height,
                       int channels,
                       size_t stride,
                       const int* opt,
                       size_t opt_len,
                       size_t* out_size);

// Releases encoder resources
void png_encoder_release(png_encoder e);

#ifdef __cplusplus
}
#endif

#endif
