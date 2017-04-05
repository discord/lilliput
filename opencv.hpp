#ifndef C_WRAPPER_H
#define C_WRAPPER_H

#include <stdbool.h>
#include <stddef.h>
#include <opencv2/core/fast_math.hpp>
#include <opencv2/core/core_c.h>
#include <opencv2/imgproc/types_c.h>
#include <opencv2/imgcodecs/imgcodecs_c.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *opencv_Mat;
typedef void *opencv_Decoder;
typedef void *opencv_Encoder;
typedef void *vec;

vec vec_create();
void vec_destroy(vec v);
size_t vec_copy(const vec v, void *buf, size_t buf_len);
size_t vec_size(const vec v);

int opencv_type_depth(int type);
int opencv_type_channels(int type);

opencv_Decoder opencv_createDecoder(const opencv_Mat buf);
const char *opencv_decoder_get_description(const opencv_Decoder d);
void opencv_decoder_release(opencv_Decoder d);
bool opencv_decoder_set_source(opencv_Decoder d, const opencv_Mat buf);
bool opencv_decoder_read_header(const opencv_Decoder d);
int opencv_decoder_get_width(const opencv_Decoder d);
int opencv_decoder_get_height(const opencv_Decoder d);
int opencv_decoder_get_pixel_type(const opencv_Decoder d);
bool opencv_decoder_read_data(const opencv_Decoder d, opencv_Mat dst);

opencv_Mat opencv_createMat(int width, int height, int type);
opencv_Mat opencv_createMatFromData(int width, int height, int type, void *data, size_t data_len);
void opencv_mat_release(opencv_Mat mat);
void opencv_resize(const opencv_Mat src, opencv_Mat dst, int width, int height, int interpolation);
opencv_Mat opencv_crop(opencv_Mat src, int x, int y, int width, int height);

opencv_Encoder opencv_createEncoder(const char *ext);
void opencv_encoder_release(opencv_Encoder e);
bool opencv_encoder_set_destination(opencv_Encoder e, vec dst);
bool opencv_encoder_write(opencv_Encoder e, const opencv_Mat src, const int *opt, size_t opt_len);

#ifdef __cplusplus
}
#endif

#endif
