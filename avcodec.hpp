#ifndef LILLIPUT_AVCODEC_HPP
#define LILLIPUT_AVCODEC_HPP

#include "opencv.hpp"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct avcodec_decoder_struct* avcodec_decoder;

void avcodec_init();

avcodec_decoder avcodec_decoder_create(const opencv_mat buf, const bool hevc_enabled, const bool av1_enabled);
void avcodec_decoder_release(avcodec_decoder d);
int avcodec_decoder_get_width(const avcodec_decoder d);
int avcodec_decoder_get_height(const avcodec_decoder d);
int avcodec_decoder_get_orientation(const avcodec_decoder d);
float avcodec_decoder_get_duration(const avcodec_decoder d);
bool avcodec_decoder_decode(const avcodec_decoder d, opencv_mat mat);
bool avcodec_decoder_is_streamable(const opencv_mat buf);
bool avcodec_decoder_has_subtitles(const avcodec_decoder d);
const char* avcodec_decoder_get_description(const avcodec_decoder d);
const char* avcodec_decoder_get_video_codec(const avcodec_decoder d);
const char* avcodec_decoder_get_audio_codec(const avcodec_decoder d);
int avcodec_decoder_get_icc(const avcodec_decoder d, void* dest, size_t dest_len);

// Seek to a timestamp (in seconds) and decode a single video frame into mat.
// Returns true on success, false on failure.
bool avcodec_decoder_seek_and_decode(const avcodec_decoder d, float timestamp_sec, opencv_mat mat);

#ifdef __cplusplus
}
#endif

#endif
