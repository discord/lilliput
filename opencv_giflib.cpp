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
    GraphicsControlBlock FirstGCB;
    DGifSavedExtensionToGCB(d->gif, 0, &FirstGCB);
    int first_transparency_index = FirstGCB.TransparentColor;
    uint8_t bg_red, bg_green, bg_blue, bg_alpha;
    if (d->gif->SBackGroundColor == first_transparency_index) {
        bg_red = bg_green = bg_blue = bg_alpha = 0;
    } else {
        bg_red = d->gif->SColorMap->Colors[d->gif->SBackGroundColor].Red;
        bg_green = d->gif->SColorMap->Colors[d->gif->SBackGroundColor].Green;
        bg_blue = d->gif->SColorMap->Colors[d->gif->SBackGroundColor].Blue;
        bg_alpha = 255;
    }

    GraphicsControlBlock GCB;
    DGifSavedExtensionToGCB(d->gif, frame_index, &GCB);
    int transparency_index = GCB.TransparentColor;

    SavedImage im = d->gif->SavedImages[frame_index];

    int frame_left = im.ImageDesc.Left;
    int frame_top = im.ImageDesc.Top;
    int frame_width = im.ImageDesc.Width;
    int frame_height = im.ImageDesc.Height;

    int buf_width = cvMat->cols;
    int buf_height = cvMat->rows;

    if (frame_left < 0) {
        return false;
    }

    if (frame_top < 0) {
        return false;
    }

    if (frame_width < 0) {
        return false;
    }

    if (frame_height < 0) {
        return false;
    }

    if (frame_left + frame_width > buf_width) {
        return false;
    }

    if (frame_top + frame_height > buf_height) {
        return false;
    }

    ColorMapObject *globalColorMap = d->gif->SColorMap;
    ColorMapObject *frameColorMap = im.ImageDesc.ColorMap;
    ColorMapObject *colorMap = frameColorMap ? frameColorMap : globalColorMap;

    if (!colorMap) {
        return false;
    }

    if (frame_index == 0) {
        // first frame -- draw the background
        for (size_t y = 0; y < buf_height; y++) {
            uint8_t *dst = cvMat->data + y * cvMat->step;
            for (size_t x = 0; x < buf_width; x++) {
                *dst++ = bg_blue;
                *dst++ = bg_green;
                *dst++ = bg_red;
                *dst++ = bg_alpha;
            }
        }
    }

    if (frame_index > 0) {
        int previous_frame_index = frame_index - 1;
        GraphicsControlBlock prevGCB;
        DGifSavedExtensionToGCB(d->gif, previous_frame_index, &prevGCB);
        int prev_disposal = prevGCB.DisposalMode;
        if (prev_disposal == DISPOSE_BACKGROUND) {
            // draw over the previous frame with the BG color
            // TODO should we do bounds checking here?
            SavedImage prevIM = d->gif->SavedImages[previous_frame_index];
            int prev_frame_left = prevIM.ImageDesc.Left;
            int prev_frame_top = prevIM.ImageDesc.Top;
            int prev_frame_width = prevIM.ImageDesc.Width;
            int prev_frame_height = prevIM.ImageDesc.Height;
            for (int y = prev_frame_top; y < prev_frame_top + prev_frame_left; y++) {
                uint8_t *dst = cvMat->data + y * cvMat->step;
                for (int x = prev_frame_left; x < prev_frame_left + prev_frame_width; x++) {
                    *dst++ = bg_blue;
                    *dst++ = bg_green;
                    *dst++ = bg_red;
                    *dst++ = bg_alpha;
                }
            }
        } else if (prev_disposal == DISPOSE_PREVIOUS) {
            // TODO or maybe not to do
            // should we at least log this happened so that we know this exists?
            // tldr this crazy method requires you to walk back across all previous
            //    frames until you reach one with DISPOSAL_DO_NOT
            //    and "undraw them", most likely would be done by building a temp
            //    buffer when first one is encountered
        }
    }

    // TODO handle interlaced gifs?

    int bit_index = 0;
    for (int y = frame_top; y < frame_top + frame_height; y++) {
        uint8_t *dst = cvMat->data + y * cvMat->step;
        for (int x = frame_left; x < frame_left + frame_width; x++) {
            GifByteType palette_index = im.RasterBits[bit_index++];
            if (palette_index == transparency_index) {
                // TODO: don't hardcode 4 channels (8UC4) here
                dst += 4;
                continue;
            }
            *dst++ = colorMap->Colors[palette_index].Blue;
            *dst++ = colorMap->Colors[palette_index].Green;
            *dst++ = colorMap->Colors[palette_index].Red;
            *dst++ = 255;
        }
    }

    return true;
}

