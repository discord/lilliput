#include "avcodec.hpp"

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

#ifdef __cplusplus
}
#endif

extern AVInputFormat ff_mov_demuxer;
extern AVInputFormat ff_matroska_demuxer;
extern AVCodec ff_h264_decoder;
extern AVCodec ff_mpeg4_decoder;
extern AVCodec ff_vp9_decoder;


void avcodec_init() {
    av_register_input_format(&ff_mov_demuxer);
    av_register_input_format(&ff_matroska_demuxer);

    avcodec_register(&ff_h264_decoder);
    avcodec_register(&ff_mpeg4_decoder);
    avcodec_register(&ff_vp9_decoder);
}

struct avcodec_decoder_struct {
    const cv::Mat *mat;
    ptrdiff_t read_index;
    const char *description;
    AVFormatContext *container;
    AVCodecContext *codec;
    uint8_t *av_buf;
    AVIOContext *avio;
};

static int avcodec_decoder_read_callback(void *d_void, uint8_t *buf, int buf_size) {
    avcodec_decoder d = static_cast<avcodec_decoder>(d_void);
    size_t buf_len = d->mat->total() - d->read_index;
    size_t read_len = (buf_len > buf_size) ? buf_size : buf_len;
    memmove(buf, d->mat->data + d->read_index, read_len);
    d->read_index += read_len;
    return read_len;
}

static int64_t avcodec_decoder_seek_callback(void *d_void, int64_t offset, int whence) {
    avcodec_decoder d = static_cast<avcodec_decoder>(d_void);
    uint8_t *to;
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

avcodec_decoder avcodec_decoder_create(const opencv_mat buf) {
    avcodec_decoder d = new struct avcodec_decoder_struct();
    memset(d, 0, sizeof(struct avcodec_decoder_struct));
    d->mat = static_cast<const cv::Mat *>(buf);

    d->container = avformat_alloc_context();
    if (!d->container) {
        avcodec_decoder_release(d);
        return NULL;
    }

    size_t av_buf_sz = 4096;
    d->av_buf = (uint8_t*)(av_malloc(av_buf_sz));
    if (!d->av_buf) {
        avcodec_decoder_release(d);
        return NULL;
    }

    d->avio = avio_alloc_context(d->av_buf, av_buf_sz, 1, d, avcodec_decoder_read_callback,
                                 NULL, avcodec_decoder_seek_callback);
    if (!d->avio) {
        avcodec_decoder_release(d);
        return NULL;
    }
    d->container->pb = d->avio;

    int res = avformat_find_stream_info(d->container, NULL);
    if (res < 0) {
        avcodec_decoder_release(d);
        return NULL;
    }

    d->codec = NULL;
    for (int i = 0; i < d->container->nb_streams; i++) {
        if (d->container->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            d->codec = d->container->streams[i]->codec;
            break;
        }
    }
    if (!d->codec) {
        avcodec_decoder_release(d);
        return NULL;
    }

    // build a string description
    // use this format:
    // $container_name + '/' + $codec name + NUL
    const size_t max_container_name_len = 32;
    const size_t max_codec_name_len = 32;
    // add a char for '/' and a char for NUL
    char *description = (char*)(malloc(max_container_name_len + max_codec_name_len + 1 + 1));
    if (description) {
        avcodec_decoder_release(d);
        return NULL;
    }
    const char *container_name = d->container->iformat->name;
    const char *codec_name = avcodec_get_name(d->codec->codec_id);
    size_t container_name_len = strlen(container_name);
    size_t codec_name_len = strlen(codec_name);
    container_name_len = (container_name_len > max_container_name_len) ? max_container_name_len : container_name_len;
    codec_name_len = (codec_name_len > max_codec_name_len) ? max_codec_name_len : codec_name_len;
    memcpy(description, container_name, container_name_len);
    description[container_name_len] = '/';
    memcpy(description + container_name_len + 1, codec_name, codec_name_len);
    description[container_name_len + 1 + codec_name_len] = 0;
    d->description = description;

    return d;
}

int avcodec_decoder_get_width(const avcodec_decoder d) {
    return d->codec->width;
}

int avcodec_decoder_get_height(const avcodec_decoder d) {
    return d->codec->height;
}

const char *avcodec_decoder_get_description(const avcodec_decoder d) {
    return d->description;
}

bool avcodec_decoder_decode(const avcodec_decoder d, opencv_mat mat) {
    auto cvMat = static_cast<cv::Mat *>(mat);
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        return false;
    }

    bool success = false;
    int ret = avcodec_receive_frame(d->codec, frame);
    if (ret >= 0) {
        success = true;
        uint8_t *dst = cvMat->data;
        memcpy(dst, frame->data, frame->width * frame->height * 4);
    }

    av_frame_free(&frame);
    return success;
}

void avcodec_decoder_release(avcodec_decoder d) {
    if (d->avio) {
        avio_close(d->avio);
    }

    if (d->av_buf) {
        av_free(d->av_buf);
    }

    if (d->container) {
        d->container->pb = NULL;
        avformat_free_context(d->container);
    }

    if (d->description) {
        char *desc = (char *)(d->description);
        free(desc);
    }

    delete d;
}
