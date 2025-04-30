#ifndef LILLIPUT_AVIF_HPP
#define LILLIPUT_AVIF_HPP

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

//----------------------
// Enums & Constants
//----------------------
enum AvifBlendMode { AVIF_BLEND_ALPHA = 0, AVIF_BLEND_NONE = 1 };

enum AvifDisposeMode { AVIF_DISPOSE_NONE = 0, AVIF_DISPOSE_BACKGROUND = 1 };

enum AvifEncoderOptions { AVIF_QUALITY = 1, AVIF_SPEED = 2 };

//----------------------
// Type Definitions
//----------------------
typedef struct avif_decoder_struct* avif_decoder;
typedef struct avif_encoder_struct* avif_encoder;

//----------------------
// Decoder Management
//----------------------
avif_decoder avif_decoder_create(const opencv_mat buf, const bool tone_mapping_enabled);
void avif_decoder_release(avif_decoder d);

//----------------------
// Decoder Properties
//----------------------
int avif_decoder_get_width(const avif_decoder d);
int avif_decoder_get_height(const avif_decoder d);
int avif_decoder_get_pixel_type(const avif_decoder d);
int avif_decoder_get_num_frames(const avif_decoder d);
uint32_t avif_decoder_get_duration(const avif_decoder d);
uint32_t avif_decoder_get_loop_count(const avif_decoder d);
size_t avif_decoder_get_icc(const avif_decoder d, void* buf, size_t buf_len);
size_t avif_decoder_get_color_xmp(const avif_decoder d, void* buf, size_t buf_len);
uint32_t avif_decoder_get_bg_color(const avif_decoder d);
int avif_decoder_get_total_duration(const avif_decoder d);

//----------------------
// Frame Properties
//----------------------
int avif_decoder_get_frame_duration(const avif_decoder d);
int avif_decoder_get_frame_dispose(const avif_decoder d);
int avif_decoder_get_frame_blend(const avif_decoder d);
int avif_decoder_get_frame_x_offset(const avif_decoder d);
int avif_decoder_get_frame_y_offset(const avif_decoder d);

//----------------------
// Frame Operations
//----------------------
bool avif_decoder_decode(avif_decoder d, opencv_mat mat);
int avif_decoder_has_more_frames(avif_decoder d);

//----------------------
// Encoder Management
//----------------------
avif_encoder avif_encoder_create(void* buf,
                                 size_t buf_len,
                                 const void* icc,
                                 size_t icc_len,
                                 const void* color_xmp,
                                 size_t color_xmp_len,
                                 int loop_count);
void avif_encoder_release(avif_encoder e);

//----------------------
// Encoder Operations
//----------------------
size_t avif_encoder_write(avif_encoder e,
                          const opencv_mat src,
                          const int* opt,
                          size_t opt_len,
                          int delay_ms,
                          int blend,
                          int dispose);
size_t avif_encoder_flush(avif_encoder e);

#ifdef __cplusplus
}
#endif

#endif
