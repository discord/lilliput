#ifndef LILLIPUT_JPEG_HPP
#define LILLIPUT_JPEG_HPP

#include <stddef.h>
#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

// JPEG encoding constants
#define L_JPEG_QUALITY CV_IMWRITE_JPEG_QUALITY
#define L_JPEG_PROGRESSIVE CV_IMWRITE_JPEG_PROGRESSIVE

// Encoder handle
typedef struct jpeg_encoder_struct* jpeg_encoder;

// Error codes
#define L_JPEG_SUCCESS 0
#define L_JPEG_ERROR_INVALID_CHANNEL_COUNT 1
#define L_JPEG_ERROR_NULL_MATRIX 2
#define L_JPEG_ERROR_INVALID_DIMENSIONS 3
#define L_JPEG_ERROR_BUFFER_TOO_SMALL 4
#define L_JPEG_ERROR_INVALID_ARG 5
#define L_JPEG_ERROR_UNKNOWN 6

// Encoder defaults
#define L_JPEG_DEFAULT_QUALITY 95
#define L_JPEG_DEFAULT_PROGRESSIVE 0
#define L_JPEG_DEFAULT_OPTIMIZE 0

// Creates a JPEG encoder with the given output buffer and optional ICC profile
jpeg_encoder jpeg_encoder_create(void* dst,
                                 size_t dst_len,
                                 const void* icc_data,
                                 size_t icc_len);

// Encodes image data to JPEG format
// src_data: raw pixel data (BGR or BGRA format)
// width, height: image dimensions
// channels: 3 for BGR, 4 for BGRA
// stride: bytes per row
// opt: encoding options array (pairs of key, value)
// opt_len: length of opt array
// out_size: output parameter for encoded size
int jpeg_encoder_encode(jpeg_encoder e,
                        const void* src_data,
                        int width,
                        int height,
                        int channels,
                        size_t stride,
                        const int* opt,
                        size_t opt_len,
                        size_t* out_size);

// Releases encoder resources
void jpeg_encoder_release(jpeg_encoder e);

#ifdef __cplusplus
}
#endif

#endif
