#include <stdio.h>

#include "opencv.hpp"
#include "opencv_giflib.hpp"

int main() {
    // 400 MB
    size_t buf_size = 1024 * 1024 * 400;
    uint8_t *buf = (uint8_t*)(malloc(buf_size));

    size_t nread;
    size_t total_read = 0;
    do {
        nread = fread(buf + total_read, sizeof(char), 4096, stdin);
        total_read += nread;
        if (total_read >= buf_size) {
            return 1;
        }
    } while (nread > 0);

    opencv_mat mat = opencv_mat_create_from_data(total_read, 1, CV_8U, buf, total_read);
    if (!mat) {
        return 5;
    }
    /*
    opencv_decoder dec = opencv_decoder_create(mat);
    if (!dec) {
        return 2;
    }
    if (!opencv_decoder_set_source(dec, mat)) {
        return 3;
    }
    if (!opencv_decoder_read_header(dec)) {
        return 4;
    }

    int width =  opencv_decoder_get_width(dec);
    int height = opencv_decoder_get_height(dec);
    int type = opencv_decoder_get_pixel_type(dec);

    if (width > 8192) {
        return 9;
    }

    if (height > 8192) {
        return 10;
    }
    */
    giflib_decoder dec = giflib_decoder_create(mat);
    if (!dec) {
        return 2;
    }

    int width = giflib_get_decoder_width(dec);
    int height = giflib_get_decoder_height(dec);

    if (width > 8192) {
        return 9;
    }

    if (height > 8192) {
        return 10;
    }

    if (!giflib_decoder_slurp(dec)) {
        return 3;
    }

    uint8_t *pixel_buf = (uint8_t*)(malloc(width * height * 4));
    opencv_mat pixel_mat = opencv_mat_create_from_data(width, height, CV_8UC4, pixel_buf, width * height * 4);
    if (!pixel_mat) {
        return 6;
    }

    /*
    if (!opencv_decoder_read_data(dec, pixel_mat)) {
        return 8;
    }
    */

    if (!giflib_decoder_decode(dec, 0, pixel_mat)) {
            return 8;
    }

    opencv_mat_release(pixel_mat);
    /*
    opencv_decoder_release(dec);
    */
    giflib_decoder_release(dec);
    opencv_mat_release(mat);

    free(pixel_buf);
    free(buf);

    return 0;
}
