#ifndef LILLIPUT_OPENCV_HPP
#define LILLIPUT_OPENCV_HPP

#include <stdbool.h>
#include <stddef.h>

#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/imgcodecs/imgcodecs_c.h>

#ifdef __cplusplus
extern "C" {
#endif

// duplicated from opencv but without a namespace
typedef enum CVImageOrientation {
    CV_IMAGE_ORIENTATION_TL = 1, ///< Horizontal (normal)
    CV_IMAGE_ORIENTATION_TR = 2, ///< Mirrored horizontal
    CV_IMAGE_ORIENTATION_BR = 3, ///< Rotate 180
    CV_IMAGE_ORIENTATION_BL = 4, ///< Mirrored vertical
    CV_IMAGE_ORIENTATION_LT = 5, ///< Mirrored horizontal & rotate 270 CW
    CV_IMAGE_ORIENTATION_RT = 6, ///< Rotate 90 CW
    CV_IMAGE_ORIENTATION_RB = 7, ///< Mirrored horizontal & rotate 90 CW
    CV_IMAGE_ORIENTATION_LB = 8  ///< Rotate 270 CW
} CVImageOrientation;

typedef void* opencv_mat;
typedef void* opencv_decoder;
typedef void* opencv_encoder;

int opencv_type_depth(int type);
int opencv_type_channels(int type);
int opencv_type_convert_depth(int type, int depth);

opencv_decoder opencv_decoder_create(const opencv_mat buf);
const char* opencv_decoder_get_description(const opencv_decoder d);
void opencv_decoder_release(opencv_decoder d);
bool opencv_decoder_set_source(opencv_decoder d, const opencv_mat buf);
bool opencv_decoder_read_header(opencv_decoder d);
int opencv_decoder_get_width(const opencv_decoder d);
int opencv_decoder_get_height(const opencv_decoder d);
int opencv_decoder_get_pixel_type(const opencv_decoder d);
int opencv_decoder_get_orientation(const opencv_decoder d);
bool opencv_decoder_read_data(opencv_decoder d, opencv_mat dst);
int opencv_copy_to_region_with_alpha(opencv_mat src,
                                     opencv_mat dst,
                                     int xOffset,
                                     int yOffset,
                                     int width,
                                     int height);
int opencv_copy_to_region(opencv_mat src,
                          opencv_mat dst,
                          int xOffset,
                          int yOffset,
                          int width,
                          int height);
void opencv_mat_set_color(opencv_mat, int red, int green, int blue, int alpha);
void opencv_mat_reset(opencv_mat mat);
int opencv_mat_clear_to_transparent(opencv_mat mat,
                                    int xOffset,
                                    int yOffset,
                                    int width,
                                    int height);

opencv_mat opencv_mat_create(int width, int height, int type);
opencv_mat opencv_mat_create_from_data(int width,
                                       int height,
                                       int type,
                                       void* data,
                                       size_t data_len);
opencv_mat opencv_mat_create_empty_from_data(int length, void* data);
bool opencv_mat_set_row_stride(opencv_mat mat, size_t stride);
void opencv_mat_release(opencv_mat mat);
void opencv_mat_resize(const opencv_mat src,
                       opencv_mat dst,
                       int width,
                       int height,
                       int interpolation);
opencv_mat opencv_mat_crop(const opencv_mat src, int x, int y, int width, int height);
void opencv_mat_orientation_transform(CVImageOrientation orientation, opencv_mat mat);
int opencv_mat_get_width(const opencv_mat mat);
int opencv_mat_get_height(const opencv_mat mat);
void* opencv_mat_get_data(const opencv_mat mat);

opencv_encoder opencv_encoder_create(const char* ext, opencv_mat dst);
void opencv_encoder_release(opencv_encoder e);
bool opencv_encoder_write(opencv_encoder e, const opencv_mat src, const int* opt, size_t opt_len);
int opencv_decoder_get_jpeg_icc(void* src, size_t src_len, void* dest, size_t dest_len);
int opencv_decoder_get_png_icc(void* src, size_t src_len, void* dest, size_t dest_len);

// Error codes
#define OPENCV_SUCCESS 0
#define OPENCV_ERROR_INVALID_CHANNEL_COUNT 1
#define OPENCV_ERROR_OUT_OF_BOUNDS 2
#define OPENCV_ERROR_NULL_MATRIX 3
#define OPENCV_ERROR_RESIZE_FAILED 4
#define OPENCV_ERROR_COPY_FAILED 5
#define OPENCV_ERROR_CONVERSION_FAILED 6
#define OPENCV_ERROR_ALPHA_BLENDING_FAILED 7
#define OPENCV_ERROR_FINAL_CONVERSION_FAILED 8
#define OPENCV_ERROR_INVALID_DIMENSIONS 9
#define OPENCV_ERROR_UNKNOWN 10

#ifdef __cplusplus
}
#endif

#endif
