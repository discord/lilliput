#include "avcodec.hpp"

#include <cstdio>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>

#include "icc_profiles/rec2020_profile.h"
#include "icc_profiles/rec601_ntsc_profile.h"
#include "icc_profiles/rec601_pal_profile.h"
#include "icc_profiles/rec709_profile.h"
#include "icc_profiles/srgb_profile.h"

#ifdef __cplusplus
}
#endif

/* converts a decoded AVFrame (YUV) into a BGRA cv::Mat at the mat's dimensions.
 * this does four things in a single sws_scale pass:
 *   1. pixel format conversion — decoded frames are YUV (e.g. YUV420P), but
 *      cv::Mat and downstream encoders (JPEG/WebP) expect BGRA
 *   2. scaling — resizes from source dimensions (e.g. 1920x1080) to the
 *      thumbnail dimensions already set on the output mat
 *   3. colorspace mapping — selects the correct YUV-to-RGB conversion matrix
 *      based on the source video's color standard (BT.709 for HD, BT.601 for
 *      SD, BT.2020 for HDR, etc.) so colors don't shift
 *   4. stride alignment — pads output rows to 32-byte boundaries, which
 *      opencv and SIMD operations expect for performance
 * returns true on success. */
static bool scale_yuv_frame_to_bgra_mat(AVFrame* frame, opencv_mat output_mat)
{
    auto cvMat = static_cast<cv::Mat*>(output_mat);
    if (!cvMat) {
        fprintf(stderr, "scale_yuv_frame_to_bgra_mat: output_mat is null\n");
        return false;
    }

    /* stride alignment: pad row width to next 32-byte boundary */
    int stepSize = 4 * cvMat->cols;
    if (cvMat->cols % 32 != 0) {
        int width = cvMat->cols + 32 - (cvMat->cols % 32);
        stepSize = 4 * width;
    }
    if (!opencv_mat_set_row_stride(output_mat, stepSize)) {
        fprintf(stderr, "scale_yuv_frame_to_bgra_mat: failed to set row stride\n");
        return false;
    }

    /* set up sws context for format conversion + scaling */
    struct SwsContext* sws = sws_getContext(
        frame->width, frame->height, (AVPixelFormat)(frame->format), /* source */
        cvMat->cols, cvMat->rows, AV_PIX_FMT_BGRA,                  /* destination */
        SWS_BILINEAR, NULL, NULL, NULL);

    if (!sws) {
        fprintf(stderr, "scale_yuv_frame_to_bgra_mat: sws_getContext failed\n");
        return false;
    }

    /* pick the correct YUV color matrix for this video's standard */
    int colorspace;
    switch (frame->colorspace) {
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        colorspace = SWS_CS_BT2020;
        break;
    case AVCOL_SPC_BT470BG:
        colorspace = SWS_CS_ITU601;
        break;
    case AVCOL_SPC_SMPTE170M:
        colorspace = SWS_CS_SMPTE170M;
        break;
    case AVCOL_SPC_SMPTE240M:
        colorspace = SWS_CS_SMPTE240M;
        break;
    default:
        colorspace = SWS_CS_ITU709;
        break;
    }
    const int* inv_table = sws_getCoefficients(colorspace);
    int srcRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    const int* table = sws_getCoefficients(SWS_CS_DEFAULT);
    if (sws_setColorspaceDetails(sws, inv_table, srcRange, table, 1, 0, 1 << 16, 1 << 16) < 0) {
        fprintf(stderr, "scale_yuv_frame_to_bgra_mat: sws_setColorspaceDetails failed\n");
        sws_freeContext(sws);
        return false;
    }

    int dstLinesizes[4];
    if (av_image_fill_linesizes(dstLinesizes, AV_PIX_FMT_BGRA, stepSize / 4) < 0) {
        fprintf(stderr, "scale_yuv_frame_to_bgra_mat: av_image_fill_linesizes failed\n");
        sws_freeContext(sws);
        return false;
    }
    uint8_t* dstData[4] = {cvMat->data, NULL, NULL, NULL};

    int ret = sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesizes);
    sws_freeContext(sws);
    if (ret < 0) {
        fprintf(stderr, "scale_yuv_frame_to_bgra_mat: sws_scale failed\n");
        return false;
    }
    return true;
}

extern AVInputFormat ff_mov_demuxer;
extern AVInputFormat ff_matroska_demuxer;
extern AVInputFormat ff_mp3_demuxer;
extern AVInputFormat ff_flac_demuxer;
extern AVInputFormat ff_wav_demuxer;
extern AVInputFormat ff_aac_demuxer;
extern AVInputFormat ff_ogg_demuxer;
extern AVCodec ff_h264_decoder;
extern AVCodec ff_hevc_decoder;
extern AVCodec ff_mpeg4_decoder;
extern AVCodec ff_vp9_decoder;
extern AVCodec ff_vp8_decoder;
extern AVCodec ff_mp3_decoder;
extern AVCodec ff_flac_decoder;
extern AVCodec ff_aac_decoder;
extern AVCodec ff_vorbis_decoder;

void avcodec_init()
{
    av_log_set_level(AV_LOG_ERROR);
}

struct avcodec_decoder_struct {
    const cv::Mat* mat;
    ptrdiff_t read_index;
    AVFormatContext* container;
    AVCodecContext* codec;
    AVIOContext* avio;
    int video_stream_index;
};

static int avcodec_decoder_read_callback(void* d_void, uint8_t* buf, int buf_size)
{
    avcodec_decoder d = static_cast<avcodec_decoder>(d_void);
    size_t buf_len = d->mat->total() - d->read_index;
    size_t read_len = (buf_len > buf_size) ? buf_size : buf_len;
    if (read_len == 0) {
        return AVERROR_EOF;
    }
    memmove(buf, d->mat->data + d->read_index, read_len);
    d->read_index += read_len;
    return read_len;
}

static int64_t avcodec_decoder_seek_callback(void* d_void, int64_t offset, int whence)
{
    avcodec_decoder d = static_cast<avcodec_decoder>(d_void);
    uint8_t* to;
    switch (whence) {
    case SEEK_SET:
        to = d->mat->data + offset;
        break;
    case SEEK_CUR:
        to = d->mat->data + d->read_index + offset;
        break;
    case SEEK_END:
        to = d->mat->data + d->mat->total() + offset;
        break;
    case AVSEEK_SIZE:
        return d->mat->total();
    default:
        return -1;
    }
    if (to < d->mat->data) {
        return -1;
    }
    if (to >= (d->mat->data + d->mat->total())) {
        return -1;
    }
    d->read_index = (to - d->mat->data);
    return 0;
}

static bool avcodec_decoder_is_audio(const avcodec_decoder d)
{
    if (!d->container) {
        return false;
    }
    if (d->container->iformat == &ff_mp3_demuxer) {
        return true;
    }
    if (d->container->iformat == &ff_flac_demuxer) {
        return true;
    }
    if (d->container->iformat == &ff_wav_demuxer) {
        return true;
    }
    if (d->container->iformat == &ff_aac_demuxer) {
        return true;
    }
    if (d->container->iformat == &ff_ogg_demuxer) {
        return true;
    }
    return false;
}

bool avcodec_decoder_is_streamable(const opencv_mat mat)
{
    const int64_t probeBytesLimit = 32 * 1024; // Define the probe limit
    const size_t atomHeaderSize = 8;
    int64_t bytesRead = 0;
    const cv::Mat* buf = static_cast<const cv::Mat*>(mat);
    size_t bufSize = buf->total();
    size_t peekSize = MIN(bufSize, probeBytesLimit);

    while (bytesRead + atomHeaderSize <= peekSize) {
        // Read atom size and type
        uint32_t atomSize = (buf->data[bytesRead] << 24) | (buf->data[bytesRead + 1] << 16) |
          (buf->data[bytesRead + 2] << 8) | buf->data[bytesRead + 3];

        // Validate atom size
        if (atomSize < atomHeaderSize || atomSize + bytesRead > bufSize) {
            break;
        }

        // Read atom type
        char atomType[4];
        memcpy(atomType, &buf->data[bytesRead + 4], 4);

        // Check for 'moov' and 'mdat' atoms using byte comparison
        if (memcmp(atomType, "moov", 4) == 0) {
            return true;
        }
        if (memcmp(atomType, "mdat", 4) == 0) {
            return false;
        }

        // Move to the next atom position
        bytesRead += atomSize; // Atom size includes the header size
    }

    return false;
}

avcodec_decoder avcodec_decoder_create(const opencv_mat buf, const bool hevc_enabled, const bool av1_enabled)
{
    avcodec_decoder d = new struct avcodec_decoder_struct();
    memset(d, 0, sizeof(struct avcodec_decoder_struct));
    d->mat = static_cast<const cv::Mat*>(buf);

    d->container = avformat_alloc_context();
    if (!d->container) {
        avcodec_decoder_release(d);
        return NULL;
    }

    d->avio = avio_alloc_context(
      NULL, 0, 0, d, avcodec_decoder_read_callback, NULL, avcodec_decoder_seek_callback);
    if (!d->avio) {
        avcodec_decoder_release(d);
        return NULL;
    }
    d->container->pb = d->avio;

    int res = avformat_open_input(&d->container, NULL, NULL, NULL);
    if (res < 0) {
        avformat_free_context(d->container);
        d->container = NULL;
        avcodec_decoder_release(d);
        return NULL;
    }

    // perform a quick search for the video stream index in the container
    AVCodecParameters* codec_params = NULL;
    for (int i = 0; i < d->container->nb_streams; i++) {
        if (d->container->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            codec_params = d->container->streams[i]->codecpar;
            d->video_stream_index = i;
            break;
        }
    }

    // call avformat_find_stream_info only if no header was found (i.e. mpeg-ts),
    // or if the duration, width, or height are unknown.
    // this is an expensive operation that could involve frame decoding, perform judiciously.
    bool isAudioOnly = avcodec_decoder_is_audio(d);
    if ((!isAudioOnly &&
         (!codec_params || codec_params->width <= 0 || codec_params->height <= 0)) ||
        d->container->duration <= 0) {
        res = avformat_find_stream_info(d->container, NULL);
        if (res < 0) {
            avcodec_decoder_release(d);
            return NULL;
        }

        if (isAudioOnly) {
            // in this case, quit out fast since we won't be decoding this anyway
            // (audio is metadata-only)
            return d;
        }

        // repeat the search for the video stream index
        if (!codec_params) {
            for (int i = 0; i < d->container->nb_streams; i++) {
                if (d->container->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                    codec_params = d->container->streams[i]->codecpar;
                    d->video_stream_index = i;
                    break;
                }
            }
            if (!codec_params) {
                avcodec_decoder_release(d);
                return NULL;
            }
        }
    }

    const AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        avcodec_decoder_release(d);
        return NULL;
    }

    if (codec->id == AV_CODEC_ID_HEVC && !hevc_enabled) {
        avcodec_decoder_release(d);
        return NULL;
    }

    if (codec->id == AV_CODEC_ID_AV1 && !av1_enabled) {
        avcodec_decoder_release(d);
        return NULL;
    }

    d->codec = avcodec_alloc_context3(codec);

    res = avcodec_parameters_to_context(d->codec, codec_params);
    if (res < 0) {
        avcodec_decoder_release(d);
        return NULL;
    }

    res = avcodec_open2(d->codec, codec, NULL);
    if (res < 0) {
        avcodec_decoder_release(d);
        return NULL;
    }

    return d;
}

const uint8_t* avcodec_get_icc_profile(int color_primaries, size_t& profile_size)
{
    switch (color_primaries) {
    case AVCOL_PRI_BT2020:
        profile_size = sizeof(rec2020_profile);
        return rec2020_profile;
    case AVCOL_PRI_BT470BG: // BT.601 PAL
        profile_size = sizeof(rec601_pal_profile);
        return rec601_pal_profile;
    case AVCOL_PRI_SMPTE170M: // BT.601 NTSC
        profile_size = sizeof(rec601_ntsc_profile);
        return rec601_ntsc_profile;
    default:
        // Default to sRGB profile
        profile_size = sizeof(srgb_profile);
        return srgb_profile;
    }
}

int avcodec_decoder_get_icc(const avcodec_decoder d, void* dest, size_t dest_len)
{
    size_t profile_size;

    if (!d || !d->codec) {
        return -1;
    }
    
    const uint8_t* profile_data = avcodec_get_icc_profile(d->codec->color_primaries, profile_size);

    if (profile_size > dest_len) {
        return -1; // Destination buffer is too small
    }

    std::memcpy(dest, profile_data, profile_size);
    return static_cast<int>(profile_size);
}

int avcodec_decoder_get_width(const avcodec_decoder d)
{
    if (d->codec) {
        AVStream* st = d->container->streams[d->video_stream_index];
        if (st->sample_aspect_ratio.num > 0 && st->sample_aspect_ratio.den > 0 &&
            st->sample_aspect_ratio.num > st->sample_aspect_ratio.den) {
            return (int64_t)d->codec->width * st->sample_aspect_ratio.num /
              st->sample_aspect_ratio.den;
        }
        return d->codec->width;
    }
    return 0;
}

int avcodec_decoder_get_height(const avcodec_decoder d)
{
    if (d->codec) {
        AVStream* st = d->container->streams[d->video_stream_index];
        if (st->sample_aspect_ratio.num > 0 && st->sample_aspect_ratio.den > 0 &&
            st->sample_aspect_ratio.den > st->sample_aspect_ratio.num) {
            return (int64_t)d->codec->height * st->sample_aspect_ratio.den /
              st->sample_aspect_ratio.num;
        }
        return d->codec->height;
    }
    return 0;
}

int avcodec_decoder_get_orientation(const avcodec_decoder d)
{
    if (!d->container) {
        return CV_IMAGE_ORIENTATION_TL;
    }
    if (!d->codec) {
        return CV_IMAGE_ORIENTATION_TL;
    }
    CVImageOrientation orientation = CV_IMAGE_ORIENTATION_TL;
    AVDictionaryEntry* tag =
      av_dict_get(d->container->streams[d->video_stream_index]->metadata, "rotate", NULL, 0);
    int rotation = 0;
    if (tag) {
        rotation = atoi(tag->value);
    }
    else {
        uint8_t* displaymatrix = NULL;
        const AVPacketSideData* sd = NULL;

        // access side data from codecpar instead of directly from the stream
        AVCodecParameters* codecpar = d->container->streams[d->video_stream_index]->codecpar;
        for (int i = 0; i < codecpar->nb_coded_side_data; i++) {
            if (codecpar->coded_side_data[i].type == AV_PKT_DATA_DISPLAYMATRIX) {
                sd = &codecpar->coded_side_data[i];
                break;
            }
        }

        displaymatrix = sd ? sd->data : NULL;
        if (displaymatrix) {
            rotation = (360 - (int)(av_display_rotation_get((const int32_t*)displaymatrix))) % 360;
        }
    }
    switch (rotation) {
    case 90:
        orientation = CV_IMAGE_ORIENTATION_RT;
        break;
    case 180:
        orientation = CV_IMAGE_ORIENTATION_BR;
        break;
    case 270:
        orientation = CV_IMAGE_ORIENTATION_LB;
        break;
    }
    return orientation;
}

float avcodec_decoder_get_duration(const avcodec_decoder d)
{
    if (d->container) {
        return d->container->duration / (float)(AV_TIME_BASE);
    }
    return 0;
}

const char* avcodec_decoder_get_description(const avcodec_decoder d)
{
    if (d->container) {
        if (d->container->iformat == &ff_mov_demuxer) {
            return "MOV";
        }
        if (d->container->iformat == &ff_matroska_demuxer) {
            return "WEBM";
        }
        if (d->container->iformat == &ff_mp3_demuxer) {
            return "MP3";
        }
        if (d->container->iformat == &ff_flac_demuxer) {
            return "FLAC";
        }
        if (d->container->iformat == &ff_wav_demuxer) {
            return "WAV";
        }
        if (d->container->iformat == &ff_aac_demuxer) {
            return "AAC";
        }
        if (d->container->iformat == &ff_ogg_demuxer) {
            return "OGG";
        }
    }
    return "";
}

const char* avcodec_decoder_get_video_codec(const avcodec_decoder d)
{
    if (!d || !d->codec) {
        return "Unknown";
    }
    
    switch (d->codec->codec_id) {
    case AV_CODEC_ID_H264:
        return "H264";
    case AV_CODEC_ID_HEVC:
        return "HEVC";
    case AV_CODEC_ID_AV1:
        return "AV1";
    case AV_CODEC_ID_VP8:
        return "VP8";
    case AV_CODEC_ID_VP9:
        return "VP9";
    case AV_CODEC_ID_MPEG4:
        return "MPEG4";
    default:
        return "Unknown";
    }
}

const char* avcodec_decoder_get_audio_codec(const avcodec_decoder d)
{
    if (!d || !d->container) {
        return "Unknown";
    }
    
    for (unsigned int i = 0; i < d->container->nb_streams; i++) {
        AVStream* stream = d->container->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            switch (stream->codecpar->codec_id) {
            case AV_CODEC_ID_AAC:
                return "AAC";
            case AV_CODEC_ID_MP3:
                return "MP3";
            case AV_CODEC_ID_FLAC:
                return "FLAC";
            case AV_CODEC_ID_VORBIS:
                return "Vorbis";
            case AV_CODEC_ID_OPUS:
                return "Opus";
            default:
                return "Unknown";
            }
        }
    }
    
    return "Unknown";
}

bool avcodec_decoder_has_subtitles(const avcodec_decoder d)
{
    for (unsigned int i = 0; i < d->container->nb_streams; i++) {
        AVStream* stream = d->container->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
            return true;
        }
    }
    return false;
}

static int avcodec_decoder_copy_frame(const avcodec_decoder d, opencv_mat mat, AVFrame* frame)
{
    if (!d || !d->codec || !d->codec->codec || !mat || !frame) {
        return -1;
    }
    
    int res = avcodec_receive_frame(d->codec, frame);
    if (res >= 0) {
        if (!scale_yuv_frame_to_bgra_mat(frame, mat)) {
            return -1;
        }
    }

    return res;
}

static int avcodec_decoder_decode_packet(const avcodec_decoder d, opencv_mat mat, AVPacket* packet)
{
    int res = avcodec_send_packet(d->codec, packet);
    if (res < 0) {
        return res;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return -1;
    }

    res = avcodec_decoder_copy_frame(d, mat, frame);
    av_frame_free(&frame);

    return res;
}

bool avcodec_decoder_decode(const avcodec_decoder d, opencv_mat mat)
{
    if (!d || !d->container || !d->codec || !mat) {
        return false;
    }
    AVPacket packet;
    bool done = false;
    bool success = false;
    while (!done) {
        int res = av_read_frame(d->container, &packet);
        if (res < 0) {
            return false;
        }
        if (packet.stream_index == d->video_stream_index) {
            res = avcodec_decoder_decode_packet(d, mat, &packet);
            if (res >= 0) {
                success = true;
                done = true;
            }
            else if (res != AVERROR(EAGAIN) && res != AVERROR_INVALIDDATA) {
                done = true;
            }
        }
        av_packet_unref(&packet);
    }
    return success;
}

void avcodec_decoder_release(avcodec_decoder d)
{
    if (d->codec) {
        avcodec_free_context(&d->codec);
    }

    if (d->container) {
        avformat_close_input(&d->container);
    }

    if (d->avio) {
        avio_flush(d->avio);
        av_free(d->avio->buffer);
        av_free(d->avio);
    }

    delete d;
}

/* ── spritesheet support ──────────────────────────────────────────── */

/* returns the number of keyframes (I-frames) in the video stream's index.
 * the index is populated by libavformat when it parses the moov atom during
 * avcodec_decoder_create(). returns -1 on error. */
int avcodec_decoder_get_keyframe_count(const avcodec_decoder d)
{
    if (!d || !d->container) {
        return -1;
    }

    if (d->video_stream_index < 0 || d->video_stream_index >= (int)d->container->nb_streams) {
        return -1;
    }

    AVStream* st = d->container->streams[d->video_stream_index];
    int total = avformat_index_get_entries_count(st);
    int keyframe_count = 0;

    for (int i = 0; i < total; i++) {
        const AVIndexEntry* e = avformat_index_get_entry(st, i);
        if (e && (e->flags & AVINDEX_KEYFRAME)) {
            keyframe_count++;
        }
    }

    return keyframe_count;
}

/* copies keyframe index entries from the moov atom into the caller-provided array.
 * each entry contains a timestamp, absolute byte offset into mdat, and sample size —
 * enough to issue an HTTP range request for the raw compressed keyframe data.
 * returns the number of entries written, or -1 on error. */
int avcodec_decoder_get_keyframes(
    const avcodec_decoder d,
    avcodec_keyframe_entry* out_entries,
    int max_entries)
{
    if (!d || !d->container || !out_entries || max_entries <= 0) {
        return -1;
    }

    if (d->video_stream_index < 0 || d->video_stream_index >= (int)d->container->nb_streams) {
        return -1;
    }

    AVStream* st = d->container->streams[d->video_stream_index];
    AVRational time_base = st->time_base;
    int total = avformat_index_get_entries_count(st);
    int count = 0;

    for (int i = 0; i < total && count < max_entries; i++) {
        const AVIndexEntry* e = avformat_index_get_entry(st, i);
        if (e && (e->flags & AVINDEX_KEYFRAME)) {
            // h264 streams with B-frames can have a negative initial decode
            // timestamp due to composition time offsets (the decoder needs to decode
            // future reference frames before presenting the first frame, so the first
            // decode timestamp is shifted negative so that the first presentation
            // timestamp lands at 0).
            // clamp to 0 since negative timestamps are meaningless for spritesheet
            // consumers (interval selection, WebVTT generation, tile layout).
            int64_t ts = av_rescale_q(e->timestamp, time_base, (AVRational){1, 1000000});
            out_entries[count].timestamp_us = ts < 0 ? 0 : ts;
            out_entries[count].byte_offset = e->pos;
            out_entries[count].size = e->size;
            count++;
        }
    }

    return count;
}

/* returns the AVCodecID for the video stream, as parsed from the moov atom.
 * needed to create a standalone decoder context for raw keyframe chunks.
 * returns -1 on error. */
int avcodec_decoder_get_codec_id(const avcodec_decoder d)
{
    if (!d || !d->codec) {
        return -1;
    }
    return (int)d->codec->codec_id;
}

/* copies the codec extradata from the moov atom (e.g. SPS/PPS for h264) into dest.
 * this data is required to initialize a standalone decoder for raw keyframe chunks.
 * pass dest=NULL to query the required buffer size without copying.
 * returns bytes written, 0 if no extradata, or -1 on error. */
int avcodec_decoder_get_extradata(
    const avcodec_decoder d,
    void* dest,
    size_t dest_len)
{
    if (!d || !d->codec) {
        return -1;
    }

    int size = d->codec->extradata_size;
    if (size <= 0 || !d->codec->extradata) {
        return 0;
    }

    if (!dest) {
        return size;
    }

    if ((size_t)size > dest_len) {
        fprintf(stderr, "avcodec_decoder_get_extradata: dest buffer too small (%zu < %d)\n",
                dest_len, size);
        return -1;
    }

    memcpy(dest, d->codec->extradata, size);
    return size;
}

/* decodes a single raw keyframe chunk (from a range request) into BGRA pixels.
 * creates a temporary codec context internally — no demuxer needed, no shared
 * state, safe for parallel calls across threads.
 * codec_id, extradata, width, height come from the moov parse phase.
 * output_mat must be pre-allocated to the desired thumbnail dimensions. */
bool avcodec_decode_raw_keyframe(
    int codec_id,
    const void* extradata,
    int extradata_size,
    int source_width,
    int source_height,
    const void* chunk_data,
    int chunk_size,
    opencv_mat output_mat)
{
    if (!chunk_data || chunk_size <= 0 || !output_mat) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: invalid input (chunk_data=%p, chunk_size=%d)\n",
                chunk_data, chunk_size);
        return false;
    }

    if (extradata && extradata_size > 10 * 1024) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: extradata too large (%d bytes)\n",
                extradata_size);
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder((AVCodecID)codec_id);
    if (!codec) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: no decoder for codec_id=%d\n", codec_id);
        return false;
    }

    /* resources that need cleanup -- initialized to NULL so the
     * cleanup label can free whichever ones were allocated */
    AVCodecContext* ctx = NULL;
    AVPacket* pkt = NULL;
    AVFrame* frame = NULL;
    bool success = false;

    ctx = avcodec_alloc_context3(codec);
    if (!ctx) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: failed to alloc codec context\n");
        goto cleanup;
    }

    ctx->width = source_width;
    ctx->height = source_height;

    if (extradata && extradata_size > 0) {
        ctx->extradata = (uint8_t*)av_mallocz(extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (!ctx->extradata) {
            fprintf(stderr, "avcodec_decode_raw_keyframe: failed to alloc extradata (%d bytes)\n",
                    extradata_size);
            goto cleanup;
        }
        memcpy(ctx->extradata, extradata, extradata_size);
        ctx->extradata_size = extradata_size;
    }

    if (avcodec_open2(ctx, codec, NULL) < 0) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: avcodec_open2 failed for codec_id=%d\n",
                codec_id);
        goto cleanup;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: failed to alloc packet\n");
        goto cleanup;
    }
    pkt->data = (uint8_t*)chunk_data;
    pkt->size = chunk_size;
    pkt->flags = AV_PKT_FLAG_KEY;

    if (avcodec_send_packet(ctx, pkt) < 0) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: avcodec_send_packet failed\n");
        goto cleanup;
    }

    /* flush: signal no more packets so decoder emits the keyframe */
    avcodec_send_packet(ctx, NULL);

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: failed to alloc frame\n");
        goto cleanup;
    }

    if (avcodec_receive_frame(ctx, frame) < 0) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: avcodec_receive_frame failed\n");
        goto cleanup;
    }

    success = scale_yuv_frame_to_bgra_mat(frame, output_mat);
    if (!success) {
        fprintf(stderr, "avcodec_decode_raw_keyframe: scale_yuv_frame_to_bgra_mat failed\n");
    }

cleanup:
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    return success;
}
