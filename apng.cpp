#include "apng.hpp"

#include <stdint.h>
#include <stdio.h>
#include <png.h>

struct apng_frame_header {
    uint32_t width;
    uint32_t height;
    uint32_t x_offset;
    uint32_t y_offset;
    uint16_t delay_num;
    uint16_t delay_den;
    uint8_t dispose_op;
    uint8_t blend_op;
};

struct apng_decoder_struct {
    png_structp png_ptr;
    png_infop info_ptr;
    const cv::Mat* mat;
    size_t read_pos;
    uint32_t frame_num;
    struct apng_frame_header frame_header;
    uint8_t *prev_frame;
    png_bytepp prev_rows;
};

struct apng_encoder_struct {
    png_structp png_ptr;
    png_infop info_ptr;
    uint8_t *buf;
    size_t buf_size;
    size_t write_pos;
    cv::Mat* prev_frame;
};

void user_read_data(png_structp png_ptr,
        png_bytep data, png_size_t length) {
    apng_decoder d = (apng_decoder) png_get_io_ptr(png_ptr);
    if (d->read_pos + length > d->mat->total()) {
        png_error(png_ptr, "Tried to read PNG data past end of file");
    }
    memcpy(data, &d->mat->data[d->read_pos], length);
    d->read_pos += length;
}

void user_write_data(png_structp png_ptr,
        png_bytep data, png_size_t length) {
    apng_encoder e = (apng_encoder) png_get_io_ptr(png_ptr);
    if (e->write_pos + length >= e->buf_size) {
        png_error(png_ptr, "Tried to write PNG data past end of buffer");
    }
    memcpy(&e->buf[e->write_pos], data, length);
    e->write_pos += length;
}

void user_flush_data(png_structp png_ptr) {
    // Do nothing, since this isn't actually a file
}

apng_decoder apng_decoder_create(const opencv_mat buf) {
    uint32_t width, height;
    apng_decoder d = new struct apng_decoder_struct();
    memset(d, 0, sizeof(struct apng_decoder_struct));
    d->mat = static_cast<const cv::Mat*>(buf);

    if (png_sig_cmp(d->mat->data, 0, 8) != 0)
        goto error;

    d->png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!d->png_ptr)
        goto error;

    d->info_ptr = png_create_info_struct(d->png_ptr);
    if (!d->info_ptr)
        goto error;

    if (setjmp(png_jmpbuf(d->png_ptr))) {
        goto error;
    }

    png_set_read_fn(d->png_ptr, d, user_read_data);

    png_read_info(d->png_ptr, d->info_ptr);

    // Apply transformations to image data
    png_set_expand(d->png_ptr);
    png_set_strip_16(d->png_ptr);
    png_set_gray_to_rgb(d->png_ptr);
    png_set_add_alpha(d->png_ptr, 0xFF, PNG_FILLER_AFTER);
    png_set_bgr(d->png_ptr);

    png_read_update_info(d->png_ptr, d->info_ptr);

    if(!png_get_valid(d->png_ptr, d->info_ptr, PNG_INFO_acTL))
        goto error;

    width = png_get_image_width(d->png_ptr, d->info_ptr);
    height = png_get_image_height(d->png_ptr, d->info_ptr);

    d->prev_frame = (uint8_t *) calloc(4, width * height);
    if (!d->prev_frame)
        goto error;

    d->prev_rows = (png_bytepp) calloc(sizeof (png_bytep), height);
    if (!d->prev_rows)
        goto error;

    for (int i = 0; i < height; i++) {
        d->prev_rows[i] = &d->prev_frame[i * 4 * width];
    }

    return d;

error:
    apng_decoder_release(d);
    return NULL;
}

int apng_decoder_get_width(const apng_decoder d) {
    return png_get_image_width(d->png_ptr, d->info_ptr);
}

int apng_decoder_get_height(const apng_decoder d) {
    return png_get_image_height(d->png_ptr, d->info_ptr);
}

int apng_decoder_get_num_frames(const apng_decoder d) {
    return png_get_num_frames(d->png_ptr, d->info_ptr);
}

int apng_decoder_get_frame_width(const apng_decoder d) {
    return d->frame_header.width;
}

int apng_decoder_get_frame_height(const apng_decoder d) {
    return d->frame_header.height;
}

int apng_decoder_get_prev_frame_delay_num(const apng_decoder d) {
    return d->frame_header.delay_num;
}

int apng_decoder_get_prev_frame_delay_den(const apng_decoder d) {
    return d->frame_header.delay_den;
}

void apng_decoder_release(apng_decoder d) {
    png_destroy_read_struct(&d->png_ptr, &d->info_ptr, NULL);
    free(d->prev_frame);
    free(d->prev_rows);
    delete d;
}

apng_decoder_frame_state apng_decoder_decode_frame_header(apng_decoder d) {
    if(setjmp(png_jmpbuf(d->png_ptr))) {
        return apng_decoder_error;
    }

    if (d->frame_num >= png_get_num_frames(d->png_ptr, d->info_ptr)) {
        return apng_decoder_eof;
    }

    png_read_frame_head(d->png_ptr, d->info_ptr);

    if (png_get_valid(d->png_ptr, d->info_ptr, PNG_INFO_fcTL)) {
        png_get_next_frame_fcTL(d->png_ptr, d->info_ptr,
            &d->frame_header.width, &d->frame_header.height,
            &d->frame_header.x_offset, &d->frame_header.y_offset,
            &d->frame_header.delay_num, &d->frame_header.delay_den,
            &d->frame_header.dispose_op, &d->frame_header.blend_op);
    }
    // todo: case where first frame has no fcTL and is therefore not the first frame of the image

    return apng_decoder_have_next_frame;
}

void BlendOver(unsigned char * dst, unsigned int dst_width, unsigned char ** rows_src, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
  unsigned int  i, j;
  int u, v, al;

  for (j=0; j<h; j++)
  {
    unsigned char * sp = rows_src[j];
    unsigned char * dp = dst + ((j + y) * dst_width + x)*4;

    for (i=0; i<w; i++, sp+=4, dp+=4)
    {
      if (sp[3] == 255)
        memcpy(dp, sp, 4);
      else
      if (sp[3] != 0)
      {
        if (dp[3] != 0)
        {
          u = sp[3]*255;
          v = (255-sp[3])*dp[3];
          al = u + v;
          dp[0] = (sp[0]*u + dp[0]*v)/al;
          dp[1] = (sp[1]*u + dp[1]*v)/al;
          dp[2] = (sp[2]*u + dp[2]*v)/al;
          dp[3] = al/255;
        }
        else
          memcpy(dp, sp, 4);
      }
    }  
  }
}

bool apng_decoder_decode_frame(apng_decoder d, opencv_mat mat) {
    auto cvMat = static_cast<cv::Mat*>(mat);

    uint8_t *dst = cvMat->data;

    uint32_t image_width = apng_decoder_get_width(d);
    uint32_t image_height = apng_decoder_get_height(d);

    uint8_t *frame = (uint8_t *) malloc(4 * d->frame_header.width * d->frame_header.height);
    if (!frame) return false;
    png_bytepp row_pointers = (png_bytepp) malloc(sizeof(png_bytep) * d->frame_header.height);
    if (!row_pointers) {
        free(frame);
        return false;
    }
    for(int i = 0; i < d->frame_header.height; i++) {
        row_pointers[i] = &frame[i * 4 * d->frame_header.width];
    }

    png_read_image(d->png_ptr, row_pointers);

    memcpy(dst, d->prev_frame, 4 * image_width * image_height);

    switch (d->frame_header.blend_op) {
        case PNG_BLEND_OP_SOURCE:
            for (int i = 0; i < d->frame_header.height; i++) {
                uint8_t row_offset = (d->frame_header.y_offset + i) * image_width;
                uint8_t *pos = dst + (row_offset + d->frame_header.x_offset) * 4;
                memcpy(pos, row_pointers[i], 4 * d->frame_header.width);
            }
            break;
        case PNG_BLEND_OP_OVER:
            BlendOver(dst, image_width, row_pointers,
                d->frame_header.x_offset, d->frame_header.y_offset,
                d->frame_header.width, d->frame_header.height);
            break;
    }

    switch (d->frame_header.dispose_op) {
        case PNG_DISPOSE_OP_NONE:
            memcpy(d->prev_frame, dst, 4 * image_width * image_height);
            break;
        case PNG_DISPOSE_OP_BACKGROUND:
            memset(d->prev_frame, 0, 4 * image_width * image_height);
            break;
        case PNG_DISPOSE_OP_PREVIOUS:
            // No-op - don't bother updating previous
            break;
    }

    free(row_pointers);
    free(frame);

    d->frame_num++;

    return true;
}

apng_decoder_frame_state apng_decoder_skip_frame(apng_decoder d) {
    if(setjmp(png_jmpbuf(d->png_ptr))) {
        return apng_decoder_error;
    }

    if (d->frame_num >= png_get_num_frames(d->png_ptr, d->info_ptr)) {
        return apng_decoder_eof;
    }

    png_read_frame_head(d->png_ptr, d->info_ptr);

    return apng_decoder_have_next_frame;
}

apng_encoder apng_encoder_create(void* buf, size_t buf_len) {
    apng_encoder e = new struct apng_encoder_struct();
    memset(e, 0, sizeof(struct apng_encoder_struct));

    e->buf = (uint8_t *) buf;
    e->buf_size = buf_len;

    e->png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!e->png_ptr)
        goto error;

    e->info_ptr = png_create_info_struct(e->png_ptr);
    if (!e->info_ptr)
        goto error;

    if (setjmp(png_jmpbuf(e->png_ptr))) {
        goto error;
    }

    png_set_write_fn(e->png_ptr, e, user_write_data, user_flush_data);

    return e;
error:
    png_destroy_write_struct(&e->png_ptr, &e->info_ptr);
    delete e;
    return NULL;
}

bool apng_encoder_init(apng_encoder e, int width, int height, int num_frames) {
    if (setjmp(png_jmpbuf(e->png_ptr))) {
        return false;
    }

    png_set_IHDR(e->png_ptr, e->info_ptr, width, height,
       8, PNG_COLOR_TYPE_RGB_ALPHA, PNG_INTERLACE_NONE,
       PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_set_acTL(e->png_ptr, e->info_ptr, num_frames, 0);

    png_write_info(e->png_ptr, e->info_ptr);
    png_set_bgr(e->png_ptr);

    e->prev_frame = new cv::Mat(height, width, CV_8UC4);

    return true;
}

void apng_find_diff_bounds(struct apng_frame_header *hdr, const cv::Mat* frame, const cv::Mat* prev_frame) {
    // count identical rows on top
    for (int i = 0; i < frame->rows; i++) {
        if (memcmp(frame->ptr(i, 0), prev_frame->ptr(i, 0), 4 * frame->cols) != 0) {
            hdr->y_offset = i;
            break;
        }
    }

    // count identical rows on bottom
    for (int i = frame->rows - 1; i >= 0; i--) {
        if (memcmp(frame->ptr(i, 0), prev_frame->ptr(i, 0), 4 * frame->cols) != 0) {
            hdr->height = i - hdr->y_offset + 1;
            break;
        }
    }

    // count identical cols on left
    for (int i = 0; i < frame->cols; i++) {
        bool differs = false;
        for (int y = hdr->y_offset; y < hdr->y_offset + hdr->height; y++) {
            if (memcmp(frame->ptr(y, i), prev_frame->ptr(y, i), 4) != 0) {
                differs = true;
                break;
            }
        }
        if (differs) {
            hdr->x_offset = i;
            break;
        }
    }

    // count identical cols on right
    for (int i = frame->cols - 1; i >= 0; i--) {
        bool differs = false;
        for (int y = hdr->y_offset; y < hdr->y_offset + hdr->height; y++) {
            if (memcmp(frame->ptr(y, i), prev_frame->ptr(y, i), 4) != 0) {
                differs = true;
                break;
            }
        }
        if (differs) {
            hdr->width = i - hdr->x_offset + 1;
            break;
        }
    }
}

void apng_diff_frame(uint8_t *out, const struct apng_frame_header *hdr, const cv::Mat* frame, const cv::Mat* prev_frame) {
    for (int y = hdr->y_offset; y < hdr->y_offset + hdr->height; y++) {
        for (int x = hdr->x_offset; x < hdr->x_offset + hdr->width; x++) {
            if (memcmp(frame->ptr(y, x), prev_frame->ptr(y, x), 4) == 0) {
                // Colors match, emit transparent pixel
                memset(out, 0, 4);
                out += 4;
            } else {
                memcpy(out, frame->ptr(y, x), 3);
                out += 3;
                // fully opaque pixel
                *out++ = 0xFF;
            }
        }
    }
}

bool apng_encoder_encode_frame(apng_encoder e, const opencv_mat frame, int ms) {
    auto mat = static_cast<const cv::Mat*>(frame);

    if (setjmp(png_jmpbuf(e->png_ptr))) {
        printf("jumpbuf happened\n");
        return false;
    }

    struct apng_frame_header hdr;

    // find the smallest rectangle of changed pixels
    apng_find_diff_bounds(&hdr, mat, e->prev_frame);

    // todo: handle case where new frame is partially transparent

    uint8_t *buf = (uint8_t*) calloc(4, hdr.width * hdr.height);
    // copy differing pixels into frame
    apng_diff_frame(buf, &hdr, mat, e->prev_frame);

    png_bytepp row_pointers = (png_bytepp) png_malloc(e->png_ptr, sizeof(png_bytep) * mat->rows);
    if (!row_pointers) return false;
    for(int i = 0; i < hdr.height; i++) {
        row_pointers[i] = buf + i * hdr.width * 4;
    }

    png_write_frame_head(e->png_ptr, e->info_ptr, row_pointers,
        hdr.width, hdr.height,
        hdr.x_offset, hdr.y_offset,
        ms, 1000, /* Delay */
        PNG_DISPOSE_OP_NONE,
        PNG_BLEND_OP_OVER);

    png_write_image(e->png_ptr, row_pointers);
    png_write_frame_tail(e->png_ptr, e->info_ptr);

    mat->copyTo(*e->prev_frame);

    free(row_pointers);

    return true;
}

bool apng_encoder_flush(apng_encoder e) {
    if (setjmp(png_jmpbuf(e->png_ptr))) {
        return false;
    }

    png_write_end(e->png_ptr, NULL);

    return true;
}

void apng_encoder_release(apng_encoder e) {
    png_destroy_write_struct(&e->png_ptr, &e->info_ptr);
    delete e->prev_frame;
    delete e;
}

int apng_encoder_get_output_length(apng_encoder e) {
    return e->write_pos;
}