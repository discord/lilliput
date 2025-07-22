#include "avcodec.hpp"

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


//--------------------------------
// Types and Structures
//--------------------------------

struct avcodec_decoder_struct {
    const cv::Mat* mat;
    ptrdiff_t read_index;
    AVFormatContext* container;
    AVCodecContext* codec;
    AVIOContext* avio;
    int video_stream_index;
};

//--------------------------------
// Functions
//--------------------------------

/**
 * Initializes the avcodec library.
 */
void avcodec_init()
{
    av_log_set_level(AV_LOG_ERROR);
}


/**
 * Reads data from the input buffer.
 * @param d_void The avcodec_decoder_struct pointer.
 * @param buf The buffer to read data into.
 * @param buf_size The size of the buffer.
 * @return The number of bytes read, or AVERROR_EOF if the end of the file is reached.
 */
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

/**
 * Seeks to a specific position in the input buffer.
 * @param d_void The avcodec_decoder_struct pointer.
 * @param offset The offset to seek to.
 * @param whence The origin of the seek.
 * @return 0 if the seek was successful, or -1 if the seek failed.
 */
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

/**
 * Checks if the input buffer is an audio stream.
 * @param d The avcodec_decoder_struct pointer.
 * @return True if the input buffer is an audio stream, false otherwise.
 */
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

/**
 * Checks if the input buffer is a streamable video.
 * @param mat The input OpenCV matrix.
 * @return True if the input buffer is a streamable video, false otherwise.
 */
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

/**
 * Creates an avcodec decoder for the given OpenCV matrix.
 * @param buf The input OpenCV matrix containing video/audio data.
 * @param hevc_enabled Whether HEVC decoding is enabled.
 * @param av1_enabled Whether AV1 decoding is enabled.
 * @return A pointer to the created avcodec_decoder_struct, or nullptr if creation failed.
 */
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

/**
 * Gets the ICC profile for the given color primaries.
 * @param color_primaries The color primaries.
 * @param profile_size The size of the ICC profile.
 * @return A pointer to the ICC profile.
 */
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

/**
 * Gets the ICC profile for the given avcodec_decoder.
 * @param d The avcodec_decoder.
 * @param dest The destination buffer.
 * @param dest_len The size of the destination buffer.
 * @return The number of bytes written to the destination buffer, or -1 if the destination buffer is too small.
 */
int avcodec_decoder_get_icc(const avcodec_decoder d, void* dest, size_t dest_len)
{
    size_t profile_size;
    const uint8_t* profile_data = avcodec_get_icc_profile(d->codec->color_primaries, profile_size);

    if (profile_size > dest_len) {
        return -1; // Destination buffer is too small
    }

    std::memcpy(dest, profile_data, profile_size);
    return static_cast<int>(profile_size);
}

/**
 * Gets the width of the video stream.
 * @param d The avcodec_decoder.
 * @return The width of the video stream.
 */
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

/**
 * Gets the height of the video stream.
 * @param d The avcodec_decoder.
 * @return The height of the video stream.
 */
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

/**
 * Gets the orientation of the video stream.
 * @param d The avcodec_decoder.
 * @return The orientation of the video stream.
 */
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

/**
 * Gets the duration of the video stream.
 * @param d The avcodec_decoder.
 * @return The duration of the video stream.
 */
float avcodec_decoder_get_duration(const avcodec_decoder d)
{
    if (d->container) {
        return d->container->duration / (float)(AV_TIME_BASE);
    }
    return 0;
}

/**
 * Gets the description of the video stream.
 * @param d The avcodec_decoder.
 * @return The description of the video stream.
 */
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

/**
 * Gets the video codec of the video stream.
 * @param d The avcodec_decoder.
 * @return The video codec of the video stream.
 */
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

/**
 * Gets the audio codec of the video stream.
 * @param d The avcodec_decoder.
 * @return The audio codec of the video stream.
 */
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

/**
 * Checks if the video stream has subtitles.
 * @param d The avcodec_decoder.
 * @return True if the video stream has subtitles, false otherwise.
 */
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

/**
 * Copies a frame from the video stream to an OpenCV matrix.
 * @param d The avcodec_decoder.
 * @param mat The OpenCV matrix to copy the frame to.
 * @param frame The frame to copy.
 * @return The result of the copy operation.
 */
static int avcodec_decoder_copy_frame(const avcodec_decoder d, opencv_mat mat, AVFrame* frame)
{
    if (!d || !d->codec || !d->codec->codec || !mat || !frame) {
        return -1;
    }
    
    auto cvMat = static_cast<cv::Mat*>(mat);
    if (!cvMat) {
        return -1;
    }

    int res = avcodec_receive_frame(d->codec, frame);
    if (res >= 0) {
        // Calculate the step size based on the cv::Mat's width
        int stepSize =
          4 * cvMat->cols; // Assuming the cv::Mat is in BGRA format, which has 4 channels
        if (cvMat->cols % 32 != 0) {
            int width = cvMat->cols + 32 - (cvMat->cols % 32);
            stepSize = 4 * width;
        }
        if (!opencv_mat_set_row_stride(mat, stepSize)) {
            return -1;
        }

        // Create SwsContext for converting the frame format and scaling
        struct SwsContext* sws =
          sws_getContext(frame->width,
                         frame->height,
                         (AVPixelFormat)(frame->format), // Source dimensions and format
                         cvMat->cols,
                         cvMat->rows,
                         AV_PIX_FMT_BGRA, // Destination dimensions and format
                         SWS_BILINEAR,    // Specify the scaling algorithm; you can choose another
                                          // according to your needs
                         NULL,
                         NULL,
                         NULL);

        // Configure colorspace
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

        // Configure color range
        int srcRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;

        // Configure YUV conversion table
        const int* table = sws_getCoefficients(SWS_CS_DEFAULT);

        sws_setColorspaceDetails(sws, inv_table, srcRange, table, 1, 0, 1 << 16, 1 << 16);

        // The linesizes and data pointers for the destination
        int dstLinesizes[4];
        av_image_fill_linesizes(dstLinesizes, AV_PIX_FMT_BGRA, stepSize / 4);
        uint8_t* dstData[4] = {cvMat->data, NULL, NULL, NULL};

        // Perform the scaling and format conversion
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesizes);

        // Free the SwsContext
        sws_freeContext(sws);
    }

    return res;
}

/**
 * Decodes a packet from the video stream.
 * @param d The avcodec_decoder.
 * @param mat The OpenCV matrix to copy the frame to.
 * @param packet The packet to decode.
 * @return The result of the decode operation.
 */
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

/**
 * Decodes a video stream into an OpenCV matrix.
 * @param d The avcodec_decoder.
 * @param mat The OpenCV matrix to copy the frame to.
 * @return True if the decode operation was successful, false otherwise.
 */
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

/**
 * Releases the resources allocated for the avcodec_decoder_struct.
 * @param d The avcodec_decoder.
 */
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
