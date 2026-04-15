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

typedef struct {
    int64_t timestamp_us;
    int64_t byte_offset;
    int32_t size;
} avcodec_keyframe_entry;

int avcodec_decoder_get_keyframe_count(const avcodec_decoder d);
int avcodec_decoder_get_keyframes(
    const avcodec_decoder d,
    avcodec_keyframe_entry* out_entries,
    int max_entries
);
int avcodec_decoder_get_codec_id(const avcodec_decoder d);
int avcodec_decoder_get_extradata(
    const avcodec_decoder d,
    void* dest,
    size_t dest_len
);
bool avcodec_decode_raw_keyframe(
    int codec_id,
    const void* extradata,
    int extradata_size,
    int source_width,
    int source_height,
    const void* chunk_data,
    int chunk_size,
    opencv_mat output_mat
);

#ifdef __cplusplus
}
#endif

#endif
