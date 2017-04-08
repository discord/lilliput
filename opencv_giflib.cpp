#include "opencv_giflib.hpp"
#include "gif_lib.h"
#include <stdbool.h>

struct giflib_decoder_struct {
    GifFileType *gif;
    const cv::Mat *mat;
    ptrdiff_t read_index;
};

int decode_func(GifFileType *gif, GifByteType *buf, int len) {
    giflib_Decoder d = static_cast<giflib_Decoder>(gif->UserData);
    size_t buf_len = d->mat->total() - d->read_index;
    size_t read_len = (buf_len > len) ? len : buf_len;
    memmove(buf, d->mat->data + d->read_index, read_len);
    d->read_index += read_len;
    return read_len;
}

giflib_Decoder giflib_createDecoder(const opencv_Mat buf) {
    giflib_Decoder d = new struct giflib_decoder_struct();
    d->gif = NULL;
    d->mat = static_cast<const cv::Mat *>(buf);
    d->read_index = 0;

    int error = 0;
    GifFileType *gif = DGifOpen(d, decode_func, &error);
    if (error) {
        delete d;
        return NULL;
    }
    d->gif = gif;

    return d;
}

int giflib_get_decoder_width(const giflib_Decoder d) {
    return d->gif->SWidth;
}

int giflib_get_decoder_height(const giflib_Decoder d) {
    return d->gif->SHeight;
}

int giflib_get_decoder_num_frames(const giflib_Decoder d) {
    return d->gif->ImageCount;
}

void giflib_decoder_release(giflib_Decoder d) {
    int error = 0;
    DGifCloseFile(d->gif, &error);
    delete d;
}

bool giflib_decoder_slurp(giflib_Decoder d) {
    int error = DGifSlurp(d->gif);
    return error != GIF_ERROR;
}

bool giflib_decoder_decode(giflib_Decoder d, int frame_index, opencv_Mat mat) {
    cv::Mat *cvMat = static_cast<cv::Mat *>(mat);
    GraphicsControlBlock GCB;
    DGifSavedExtensionToGCB(d->gif, frame_index, &GCB);
    int transparency_index = GCB.TransparentColor;

    // XXX make sure d.Width == mat.Width && d.Height == mat.Height

    int width = cvMat->cols;
    int height = cvMat->rows;

    ColorMapObject *globalColorMap = d->gif->SColorMap;
    ColorMapObject *frameColorMap = d->gif->SavedImages[frame_index].ImageDesc.ColorMap;
    ColorMapObject *colorMap = frameColorMap ? frameColorMap : globalColorMap;
    // XXX colorMap NULL
    bool draw_background = (frame_index == 0);

    int bit_index = 0;
    for (int y = 0; y < height; y++) {
        uint8_t *dst = cvMat->data + y * cvMat->step;
        for (int x = 0; x < width; x++) {
            GifByteType palette_index = d->gif->SavedImages[frame_index].RasterBits[bit_index++];
            if (palette_index == transparency_index) {
                if (draw_background) {
                    // TODO draw_background
                    dst += 4;
                    continue;
                } else {
                    // TODO: don't hardcode 4 channels (8UC4) here
                    dst += 4;
                    continue;
                }
            }
            *dst++ = colorMap->Colors[palette_index].Blue;
            *dst++ = colorMap->Colors[palette_index].Green;
            *dst++ = colorMap->Colors[palette_index].Red;
            *dst++ = 255;
        }
    }

    return true;
}

