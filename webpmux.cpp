#include "webpmux.hpp"
#include <webp/encode.h>

extern "C" {

WebPAnimDecoder* webpmux_create_decoder(void* buf, size_t buf_len, void* info)
{
    auto i = static_cast<WebPAnimInfo*>(info);
    WebPData data = {static_cast<const uint8_t*>(buf), buf_len};
    WebPAnimDecoderOptions dec_options;
    WebPAnimDecoderOptionsInit(&dec_options);
    dec_options.color_mode = MODE_BGRA;
    WebPAnimDecoder* dec = WebPAnimDecoderNew(&data, &dec_options);
    if (dec == NULL)
        return NULL;
    WebPAnimDecoderGetInfo(dec, i);
    return dec;
}

WebPAnimEncoder* webpmux_create_encoder(int width, int height)
{
    WebPAnimEncoderOptions enc_options;
    WebPAnimEncoderOptionsInit(&enc_options);
    return WebPAnimEncoderNew(width, height, &enc_options);
}

int webpmux_decoder_read_data(WebPAnimDecoder* dec, opencv_mat mat)
{
    auto m = static_cast<cv::Mat*>(mat);
    int timestamp = 0;
    uint8_t* buf = NULL;
    int res = WebPAnimDecoderGetNext(dec, &buf, &timestamp);
    if (!res)
        return -1;
    cv::Mat img(cv::Size(m->cols, m->rows), CV_8UC4, buf);
    img.copyTo(*m);
    return timestamp;
}

int webpmux_decoder_skip_frame(WebPAnimDecoder* dec)
{
    int timestamp = 0;
    uint8_t* buf = NULL;
    return WebPAnimDecoderGetNext(dec, &buf, &timestamp);
}

int webpmux_encoder_add_frame(WebPAnimEncoder* enc, opencv_mat mat, int timestamp, int quality)
{
    // TODO: Determine whether it's necessary to extract lossless flag
    // Unfortunately, it appears to be non-trivial to determine whether a single
    // frame in an animation was encoded losslessly, so it may be better to let
    // the encoder decide for the sake of time, at the possible cost of intended
    // quality. (We're already doing transformations anyway, so there's a loss
    // of quality inherent to this process.)

    auto m = static_cast<cv::Mat*>(mat);
    WebPConfig config;
    WebPConfigInit(&config);
    config.lossless = false;
    config.quality = quality;
    WebPPicture frame;
    WebPPictureInit(&frame);
    frame.width = m->cols;
    frame.height = m->rows;
    frame.use_argb = true;
    frame.argb = (uint32_t*)m->data;
    frame.argb_stride = m->cols;
    if (!WebPAnimEncoderAdd(enc, &frame, timestamp, &config)) {
        // printf("failed to encode: %s\n", WebPAnimEncoderGetError(enc));
        return frame.error_code;
    }
    return 0;
}

size_t webpmux_encoder_write(WebPAnimEncoder* enc, void* buf, size_t size, int timestamp)
{
    WebPAnimEncoderAdd(enc, NULL, timestamp, NULL);
    WebPData webp_data;
    WebPDataInit(&webp_data);
    if (!WebPAnimEncoderAssemble(enc, &webp_data)) {
        // printf("failed to write: %s\n", WebPAnimEncoderGetError(enc));
        WebPAnimEncoderDelete(enc);
        return 0;
    }
    size_t total = size < webp_data.size ? size : webp_data.size;
    memcpy(buf, webp_data.bytes, total);
    WebPAnimEncoderDelete(enc);
    return total;
}

size_t webpmux_encode_single_frame(WebPAnimEncoder* enc, opencv_mat mat, int quality, void* buf, size_t size)
{
    auto m = static_cast<cv::Mat*>(mat);
    WebPConfig config;
    WebPConfigPreset(&config, WEBP_PRESET_PHOTO, quality);
    WebPPicture frame;
    WebPPictureInit(&frame);
    frame.width = m->cols;
    frame.height = m->rows;
    frame.use_argb = true;
    frame.argb = (uint32_t*)m->data;
    frame.argb_stride = m->cols;
    WebPMemoryWriter writer;
    WebPMemoryWriterInit(&writer);
    writer.mem = (uint8_t*)buf;
    writer.max_size = size;
    frame.writer = WebPMemoryWrite;
    frame.custom_ptr = &writer;
    if (!WebPEncode(&config, &frame)) {
        return 0;
    }
    return size < writer.size ? size : writer.size;
}

}
