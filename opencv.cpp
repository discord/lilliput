#include "opencv.hpp"
#include <stdbool.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>

opencv_mat opencv_mat_create(int width, int height, int type)
{
    return new cv::Mat(height, width, type);
}

opencv_mat opencv_mat_create_from_data(int width, int height, int type, void* data, size_t data_len)
{
    size_t total_size = width * height * CV_ELEM_SIZE(type);
    if (total_size > data_len) {
        return NULL;
    }
    auto mat = new cv::Mat(height, width, type, data);
    mat->datalimit = (uint8_t*)data + data_len;
    return mat;
}

opencv_mat opencv_mat_create_empty_from_data(int length, void* data)
{
    // this is slightly sketchy - what we're going to do is build a 1x0 matrix
    // and then set its data* properties to reflect the capacity (given by length arg here)
    // this tells opencv internally that the Mat can store more but has nothing in it
    // this is directly analogous to Go's len and cap
    auto mat = new cv::Mat(0, 1, CV_8U, data);

    mat->datalimit = mat->data + length;

    return mat;
}

bool opencv_mat_set_row_stride(opencv_mat mat, size_t stride)
{
    auto m = static_cast<cv::Mat*>(mat);
    if (m->step == stride) {
        return true;
    }
    size_t width = m->cols;
    size_t height = m->rows;
    auto type = m->type();
    auto width_stride = width * CV_ELEM_SIZE(type);
    if (stride < width_stride) {
        return false;
    }
    if (m->step != width_stride) {
        // refuse to set the stride if it's already set
        // the math for that is confusing and probably unnecessary to figure out
        return false;
    }
    size_t total_size = stride * height;
    if ((m->datastart + total_size) > m->datalimit) {
        // don't exceed end of data array
        return false;
    }
    m->step = stride;
    return true;
}

void opencv_mat_release(opencv_mat mat)
{
    auto m = static_cast<cv::Mat*>(mat);
    delete m;
}

int opencv_type_depth(int type)
{
    return CV_ELEM_SIZE1(type) * 8;
}

int opencv_type_channels(int type)
{
    return CV_MAT_CN(type);
}

int opencv_type_convert_depth(int t, int depth)
{
    return CV_MAKETYPE(depth, CV_MAT_CN(t));
}

opencv_decoder opencv_decoder_create(const opencv_mat buf)
{
    auto mat = static_cast<const cv::Mat*>(buf);
    cv::ImageDecoder* d = new cv::ImageDecoder(*mat);
    if (d->empty()) {
        delete d;
        d = NULL;
    }
    return d;
}

const char* opencv_decoder_get_description(const opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    return d_ptr->getDescription().c_str();
}

void opencv_decoder_release(opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    delete d_ptr;
}

bool opencv_decoder_read_header(opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    return d_ptr->readHeader();
}

int opencv_decoder_get_width(const opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    return d_ptr->width();
}

int opencv_decoder_get_height(const opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    return d_ptr->height();
}

int opencv_decoder_get_pixel_type(const opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    return d_ptr->type();
}

int opencv_decoder_get_orientation(const opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    return d_ptr->orientation();
}

bool opencv_decoder_read_data(opencv_decoder d, opencv_mat dst)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    auto* mat = static_cast<cv::Mat*>(dst);
    return d_ptr->readData(*mat);
}

opencv_encoder opencv_encoder_create(const char* ext, opencv_mat dst)
{
    auto* mat = static_cast<cv::Mat*>(dst);
    return new cv::ImageEncoder(ext, *mat);
}

void opencv_encoder_release(opencv_encoder e)
{
    auto e_ptr = static_cast<cv::ImageEncoder*>(e);
    delete e_ptr;
}

bool opencv_encoder_write(opencv_encoder e, const opencv_mat src, const int* opt, size_t opt_len)
{
    auto e_ptr = static_cast<cv::ImageEncoder*>(e);
    auto mat = static_cast<const cv::Mat*>(src);
    std::vector<int> params;
    for (size_t i = 0; i < opt_len; i++) {
        params.push_back(opt[i]);
    }
    return e_ptr->write(*mat, params);
};

void opencv_mat_resize(const opencv_mat src,
                       opencv_mat dst,
                       int width,
                       int height,
                       int interpolation)
{
    cv::resize(*static_cast<const cv::Mat*>(src),
               *static_cast<cv::Mat*>(dst),
               cv::Size(width, height),
               0,
               0,
               interpolation);
}

opencv_mat opencv_mat_crop(const opencv_mat src, int x, int y, int width, int height)
{
    auto ret = new cv::Mat;
    *ret = (*static_cast<const cv::Mat*>(src))(cv::Rect(x, y, width, height));
    return ret;
}

void opencv_mat_orientation_transform(CVImageOrientation orientation, opencv_mat mat)
{
    auto cvMat = static_cast<cv::Mat*>(mat);
    cv::OrientationTransform(int(orientation), *cvMat);
}

int opencv_mat_get_width(const opencv_mat mat)
{
    auto cvMat = static_cast<const cv::Mat*>(mat);
    return cvMat->cols;
}

int opencv_mat_get_height(const opencv_mat mat)
{
    auto cvMat = static_cast<const cv::Mat*>(mat);
    return cvMat->rows;
}

void* opencv_mat_get_data(const opencv_mat mat)
{
    auto cvMat = static_cast<const cv::Mat*>(mat);
    return cvMat->data;
}

struct opencv_jpeg_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

void opencv_jpeg_error_exit(j_common_ptr cinfo) {
    opencv_jpeg_error_mgr* myerr = (opencv_jpeg_error_mgr*) cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

int opencv_decoder_get_jpeg_icc(void* src, size_t src_len, void* dest, size_t dest_len) {
    struct jpeg_decompress_struct cinfo;
    struct opencv_jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = opencv_jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        // JPEG processing error
        jpeg_destroy_decompress(&cinfo);
        return 0;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, static_cast<unsigned char*>(src), src_len);

    // Ask libjpeg to save markers that might be ICC profiles
    jpeg_save_markers(&cinfo, JPEG_APP0 + 2, 0xFFFF);

    // Read JPEG header
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return 0;
    }

    // Check if ICC profile is available
    JOCTET *icc_profile = nullptr;
    unsigned int icc_length = 0;
    if (jpeg_read_icc_profile(&cinfo, &icc_profile, &icc_length)) {
        if (icc_length > 0 && icc_length <= dest_len) {
            memcpy(dest, icc_profile, icc_length);
            free(icc_profile);
            jpeg_destroy_decompress(&cinfo);
            return icc_length;
        }
    }

    if (icc_profile) {
        // Free the ICC profile if it was allocated but not copied
        free(icc_profile);
    }
    jpeg_destroy_decompress(&cinfo);
    return 0;
}

void opencv_decoder_png_read(png_structp png_ptr, png_bytep data, png_size_t length) {
    auto buffer_info = reinterpret_cast<std::pair<const char**, size_t*>*>(png_get_io_ptr(png_ptr));
    const char* &buffer = *buffer_info->first;
    size_t &buffer_size = *buffer_info->second;

    if (buffer_size < length) {
        png_error(png_ptr, "Read error: attempting to read beyond buffer size");
        return;
    }

    memcpy(data, buffer, length);
    buffer += length;
    buffer_size -= length;
}

int opencv_decoder_get_png_icc(void* src, size_t src_len, void* dest, size_t dest_len) {
    // Set up libpng to read from memory
    const char* buffer = reinterpret_cast<const char*>(src);
    size_t buffer_size = src_len;
    std::pair<const char**, size_t*> buffer_info(&buffer, &buffer_size);

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
        return 0;
    }
    png_set_read_fn(png_ptr, &buffer_info, opencv_decoder_png_read);
    png_read_info(png_ptr, info_ptr);

    // Check for ICC profile
    png_charp icc_name;
    int compression_type;
    png_bytep icc_profile;
    png_uint_32 icc_length;
    if (png_get_iCCP(png_ptr, info_ptr, &icc_name, &compression_type, &icc_profile, &icc_length)) {
        if (icc_length > 0 && icc_length <= dest_len) {
            memcpy(dest, icc_profile, icc_length);
            png_destroy_read_struct(&png_ptr, &info_ptr, nullptr); // handles freeing icc_profile
            return icc_length;
        }
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return 0;
}

void opencv_mat_reset(opencv_mat mat) {
    if (mat) {
        cv::Mat* m = static_cast<cv::Mat*>(mat);
        m->setTo(cv::Scalar(0));
    }
}

void opencv_mat_set_color(opencv_mat mat, int red, int green, int blue, int alpha) {
    auto cvMat = static_cast<cv::Mat*>(mat);
    if (cvMat) {
        if (alpha >= 0) {
            // For 4-channel (RGBA)
            cvMat->setTo(cv::Scalar(blue, green, red, alpha));
        } else {
            // For 3-channel (RGB)
            cvMat->setTo(cv::Scalar(blue, green, red));
        }
    }
}

void opencv_copy_with_alpha_blending(opencv_mat src, opencv_mat dst, int xOffset, int yOffset, int width, int height) {
    auto srcMat = static_cast<cv::Mat*>(src);
    auto dstMat = static_cast<cv::Mat*>(dst);

    if (srcMat->channels() != 3 && srcMat->channels() != 4) {
        throw std::invalid_argument("Source image must have 3 or 4 channels (RGB or RGBA).");
    }

    if (dstMat->channels() != 3) {
        throw std::invalid_argument("Destination image must have 3 channels (RGB).");
    }

    if (xOffset < 0 || yOffset < 0 || xOffset + srcMat->cols > dstMat->cols || yOffset + srcMat->rows > dstMat->rows) {
        throw std::invalid_argument("Source image with offsets exceeds the bounds of the destination framebuffer");
    }

    // Create an ROI on the destination where the source image will be placed
    cv::Rect roi(xOffset, yOffset, srcMat->cols, srcMat->rows);
    cv::Mat dstROI = (*dstMat)(roi);

    // Handle 4-channel (RGBA) source image
    if (srcMat->channels() == 4) {
        // Split the source image into RGB and alpha channels
        std::vector<cv::Mat> srcChannels(4);
        cv::split(*srcMat, srcChannels);
        cv::Mat srcRGB;
        cv::merge(srcChannels.data(), 3, srcRGB);
        cv::Mat srcAlpha = srcChannels[3];

        // Resize srcRGB and srcAlpha to match the destination ROI if necessary
        if (srcRGB.size() != dstROI.size()) {
            cv::resize(srcRGB, srcRGB, dstROI.size());
            cv::resize(srcAlpha, srcAlpha, dstROI.size());
        }

        // Convert the alpha mask to a 3-channel image by repeating it across the RGB channels
        cv::Mat alphaMask;
        cv::cvtColor(srcAlpha, alphaMask, cv::COLOR_GRAY2BGR);
        alphaMask.convertTo(alphaMask, CV_32FC3, 1.0 / 255.0); // Normalize to [0, 1] range

        // Convert srcRGB and dstROI to float for blending
        cv::Mat srcRGBFloat, dstROIFloat;
        srcRGB.convertTo(srcRGBFloat, CV_32FC3);
        dstROI.convertTo(dstROIFloat, CV_32FC3);

        // Perform alpha blending: dst = src * alpha + dst * (1 - alpha)
        cv::Mat blendedRoi = srcRGBFloat.mul(alphaMask) + dstROIFloat.mul(cv::Scalar::all(1.0) - alphaMask);

        // Convert the blended result back to the original format
        blendedRoi.convertTo(dstROI, dstMat->type());
    } 
    // Handle 3-channel (RGB) source image
    else if (srcMat->channels() == 3) {
        cv::Mat srcROI = (*srcMat)(cv::Rect(xOffset, yOffset, srcMat->cols, srcMat->rows));
        srcROI.copyTo(dstROI);
    }
}

