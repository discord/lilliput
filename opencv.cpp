#include "opencv.hpp"

#include <stdbool.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <jpeglib.h>
// Include the vendored libpng explicitly by its versioned path. A bare
// <png.h> resolves to the system libpng, which on many build hosts predates
// PNG 3rd edition and so lacks png_get_cICP, while the library we actually
// link is the vendored 1.6.47. Pinning the include keeps the header and the
// linked implementation on the same version.
#include <libpng16/png.h>
#include <zlib.h>
#include <setjmp.h>
#include <iostream>

// Interpolation constants
const int CV_INTER_AREA = cv::INTER_AREA;
const int CV_INTER_LINEAR = cv::INTER_LINEAR;
const int CV_INTER_CUBIC = cv::INTER_CUBIC;

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
    if (!d)
        return nullptr;
    static thread_local std::string desc; // Keep string alive
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    desc = d_ptr->getDescription();
    return desc.c_str();
}

void opencv_decoder_release(opencv_decoder d)
{
    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    delete d_ptr;
}

bool opencv_decoder_read_header(opencv_decoder d)
{
    if (!d) {
        return false;
    }

    auto d_ptr = static_cast<cv::ImageDecoder*>(d);
    try {
        return d_ptr->readHeader();
    }
    catch (const cv::Exception& e) {
        std::cerr << "OpenCV exception in opencv_decoder_read_header: " << e.what() << std::endl;
        return false;
    }
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

void opencv_jpeg_error_exit(j_common_ptr cinfo)
{
    opencv_jpeg_error_mgr* myerr = (opencv_jpeg_error_mgr*)cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

int opencv_decoder_get_jpeg_icc(void* src, size_t src_len, void* dest, size_t dest_len)
{
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
    JOCTET* icc_profile = nullptr;
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

void opencv_decoder_png_read(png_structp png_ptr, png_bytep data, png_size_t length)
{
    auto buffer_info = reinterpret_cast<std::pair<const char**, size_t*>*>(png_get_io_ptr(png_ptr));
    const char*& buffer = *buffer_info->first;
    size_t& buffer_size = *buffer_info->second;

    if (buffer_size < length) {
        png_error(png_ptr, "Read error: attempting to read beyond buffer size");
        return;
    }

    memcpy(data, buffer, length);
    buffer += length;
    buffer_size -= length;
}

int opencv_decoder_get_png_icc(void* src, size_t src_len, void* dest, size_t dest_len)
{
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

/**
 * Read the top-level cICP chunk (PNG 3rd edition, W3C CR-png-3-20250313) from a
 * PNG buffer.
 *
 * cICP is the highest-priority colour descriptor in PNG: when present it takes
 * precedence over iCCP, sRGB and cHRM+gAMA. Unlike an ICC profile it is not a
 * blob to pass through but four H.273 code points, so it is reported via
 * out-params rather than copied into a buffer.
 *
 * Returns 1 when a cICP chunk was found, 0 otherwise.
 */
int opencv_decoder_get_png_cicp(void* src,
                                size_t src_len,
                                uint8_t* primaries,
                                uint8_t* transfer,
                                uint8_t* matrix,
                                uint8_t* full_range)
{
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

    png_byte cicp_primaries = 0;
    png_byte cicp_transfer = 0;
    png_byte cicp_matrix = 0;
    png_byte cicp_range = 0;
    int found = 0;
    if (png_get_cICP(
          png_ptr, info_ptr, &cicp_primaries, &cicp_transfer, &cicp_matrix, &cicp_range)) {
        *primaries = static_cast<uint8_t>(cicp_primaries);
        *transfer = static_cast<uint8_t>(cicp_transfer);
        *matrix = static_cast<uint8_t>(cicp_matrix);
        *full_range = static_cast<uint8_t>(cicp_range);
        found = 1;
    }

    png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
    return found;
}

/**
 * Insert a cICP chunk into an already-encoded PNG buffer, in place.
 *
 * cv::imencode cannot emit ancillary chunks, and swapping the PNG encode for a
 * hand-rolled libpng write path would mean re-implementing (and diverging from)
 * OpenCV's encoder for the sake of four bytes of metadata. Injecting the chunk
 * into the finished stream keeps the pixel encoder exactly as it is today; the
 * tradeoff is that this function has to know PNG's container framing, which is
 * a fixed 8-byte signature followed by length/type/data/CRC records.
 *
 * The chunk is placed immediately after IHDR, as the spec requires for colour
 * chunks. `png_cap` is the capacity of the caller's buffer; the insert is
 * skipped (returning the original length) if the 16 extra bytes would not fit.
 *
 * Returns the new buffer length, or the original length if nothing was written.
 */
size_t opencv_png_insert_cicp(void* png,
                              size_t png_len,
                              size_t png_cap,
                              uint8_t primaries,
                              uint8_t transfer,
                              uint8_t matrix,
                              uint8_t full_range)
{
    static const uint8_t kSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    const size_t kChunkLen = 12 + 4; // length + type + 4-byte body + CRC

    uint8_t* buf = reinterpret_cast<uint8_t*>(png);
    if (!buf || png_len < sizeof(kSignature) + 12 || png_len + kChunkLen > png_cap) {
        return png_len;
    }
    if (memcmp(buf, kSignature, sizeof(kSignature)) != 0) {
        return png_len;
    }

    // IHDR is required to be the first chunk; bail rather than guess if it isn't.
    size_t ihdr_off = sizeof(kSignature);
    uint32_t ihdr_data_len = ((uint32_t)buf[ihdr_off] << 24) | ((uint32_t)buf[ihdr_off + 1] << 16) |
                             ((uint32_t)buf[ihdr_off + 2] << 8) | (uint32_t)buf[ihdr_off + 3];
    if (memcmp(buf + ihdr_off + 4, "IHDR", 4) != 0) {
        return png_len;
    }
    size_t insert_at = ihdr_off + 12 + ihdr_data_len;
    if (insert_at > png_len) {
        return png_len;
    }

    uint8_t chunk[16];
    chunk[0] = 0;
    chunk[1] = 0;
    chunk[2] = 0;
    chunk[3] = 4; // data length
    memcpy(chunk + 4, "cICP", 4);
    chunk[8] = primaries;
    chunk[9] = transfer;
    chunk[10] = matrix;
    chunk[11] = full_range;
    // CRC covers the type and data, not the length field.
    uint32_t crc = (uint32_t)crc32(0, chunk + 4, 8);
    chunk[12] = (uint8_t)((crc >> 24) & 0xFF);
    chunk[13] = (uint8_t)((crc >> 16) & 0xFF);
    chunk[14] = (uint8_t)((crc >> 8) & 0xFF);
    chunk[15] = (uint8_t)(crc & 0xFF);

    memmove(buf + insert_at + kChunkLen, buf + insert_at, png_len - insert_at);
    memcpy(buf + insert_at, chunk, kChunkLen);
    return png_len + kChunkLen;
}

/**
 * @brief Reset all pixels in the matrix to zero.
 *
 * @param mat Pointer to the OpenCV matrix to be reset.
 */
void opencv_mat_reset(opencv_mat mat)
{
    if (mat) {
        cv::Mat* m = static_cast<cv::Mat*>(mat);
        m->setTo(cv::Scalar(0));
    }
}

/**
 * @brief Set the entire matrix to a specific color.
 *
 * @param mat Pointer to the OpenCV matrix to be colored.
 * @param red Red component of the color (0-255).
 * @param green Green component of the color (0-255).
 * @param blue Blue component of the color (0-255).
 * @param alpha Alpha component of the color (0-255). If negative, treated as a 3-channel image.
 */
void opencv_mat_set_color(opencv_mat mat, int red, int green, int blue, int alpha)
{
    auto cvMat = static_cast<cv::Mat*>(mat);
    if (cvMat) {
        cv::Scalar color =
          (alpha >= 0) ? cv::Scalar(blue, green, red, alpha) : cv::Scalar(blue, green, red);
        cvMat->setTo(color);
    }
}

/**
 * @brief Clear a rectangular region of the matrix to transparent.
 *
 * @param mat Pointer to the OpenCV matrix to be modified.
 * @param xOffset X-coordinate of the top-left corner of the rectangle.
 * @param yOffset Y-coordinate of the top-left corner of the rectangle.
 * @param width Width of the rectangle.
 * @param height Height of the rectangle.
 * @return int Error code.
 */
int opencv_mat_clear_to_transparent(opencv_mat mat, int xOffset, int yOffset, int width, int height)
{
    auto cvMat = static_cast<cv::Mat*>(mat);
    if (!cvMat) {
        return OPENCV_ERROR_NULL_MATRIX;
    }

    if (xOffset < 0 || yOffset < 0 || xOffset + width > cvMat->cols ||
        yOffset + height > cvMat->rows) {
        return OPENCV_ERROR_OUT_OF_BOUNDS;
    }

    if (width <= 0 || height <= 0) {
        return OPENCV_ERROR_INVALID_DIMENSIONS;
    }

    try {
        cv::Rect roi(xOffset, yOffset, width, height);
        if (cvMat->channels() == 4) {
            cvMat->operator()(roi).setTo(cv::Scalar(0, 0, 0, 0));
        }
        else if (cvMat->channels() == 3) {
            // For 3-channel images, we'll use black as "transparent"
            cvMat->operator()(roi).setTo(cv::Scalar(0, 0, 0));
        }
        else {
            return OPENCV_ERROR_INVALID_CHANNEL_COUNT;
        }
        return OPENCV_SUCCESS;
    }
    catch (const cv::Exception& e) {
        std::cerr << "OpenCV exception in opencv_mat_clear_to_transparent: " << e.what()
                  << std::endl;
        return OPENCV_ERROR_UNKNOWN;
    }
}

/**
 * @brief Blend source image with destination image using alpha blending.
 *
 * @param src Pointer to the source OpenCV matrix.
 * @param dst Pointer to the destination OpenCV matrix.
 * @param xOffset X-coordinate offset in the destination image.
 * @param yOffset Y-coordinate offset in the destination image.
 * @param width Width of the region to copy.
 * @param height Height of the region to copy.
 * @return int Error code.
 */
int opencv_copy_to_region_with_alpha(opencv_mat src,
                                     opencv_mat dst,
                                     int xOffset,
                                     int yOffset,
                                     int width,
                                     int height)
{
    try {
        auto srcMat = static_cast<cv::Mat*>(src);
        auto dstMat = static_cast<cv::Mat*>(dst);

        if (!srcMat || !dstMat || srcMat->empty() || dstMat->empty()) {
            return OPENCV_ERROR_NULL_MATRIX;
        }

        if (xOffset < 0 || yOffset < 0 || xOffset + width > dstMat->cols ||
            yOffset + height > dstMat->rows) {
            return OPENCV_ERROR_OUT_OF_BOUNDS;
        }

        if (width <= 0 || height <= 0) {
            return OPENCV_ERROR_INVALID_DIMENSIONS;
        }

        cv::Rect roi(xOffset, yOffset, width, height);
        cv::Mat dstROI = dstMat->operator()(roi);

        cv::Mat srcResized;
        if (srcMat->size() != dstROI.size()) {
            cv::resize(*srcMat, srcResized, dstROI.size(), 0, 0, cv::INTER_LINEAR);
        }
        else {
            srcResized = *srcMat;
        }

        // Handle grayscale source
        if (srcResized.channels() == 1) {
            cv::cvtColor(srcResized, srcResized, cv::COLOR_GRAY2BGR);
        }

        // Ensure both matrices are 4-channel
        cv::Mat src4, dst4;
        if (srcResized.channels() == 3) {
            cv::cvtColor(srcResized, src4, cv::COLOR_BGR2BGRA);
        }
        else if (srcResized.channels() == 4) {
            src4 = srcResized;
        }
        else {
            return OPENCV_ERROR_INVALID_CHANNEL_COUNT;
        }

        if (dstROI.channels() == 3) {
            cv::cvtColor(dstROI, dst4, cv::COLOR_BGR2BGRA);
        }
        else if (dstROI.channels() == 4) {
            dst4 = dstROI;
        }
        else {
            return OPENCV_ERROR_INVALID_CHANNEL_COUNT;
        }

        // Perform alpha blending
        std::vector<cv::Mat> srcChannels, dstChannels;
        cv::split(src4, srcChannels);
        cv::split(dst4, dstChannels);

        cv::Mat srcAlpha = srcChannels[3];
        cv::Mat dstAlpha = dstChannels[3];
        cv::Mat srcAlphaF, dstAlphaF, outAlphaF;
        srcAlpha.convertTo(srcAlphaF, CV_32F, 1.0 / 255.0);
        dstAlpha.convertTo(dstAlphaF, CV_32F, 1.0 / 255.0);
        outAlphaF = srcAlphaF + dstAlphaF.mul(1.0f - srcAlphaF);

        for (int i = 0; i < 3; ++i) {
            cv::Mat srcChannelF, dstChannelF;
            srcChannels[i].convertTo(srcChannelF, CV_32F, 1.0 / 255.0);
            dstChannels[i].convertTo(dstChannelF, CV_32F, 1.0 / 255.0);
            cv::Mat blended =
              (srcChannelF.mul(srcAlphaF) + dstChannelF.mul(dstAlphaF).mul(1.0f - srcAlphaF)) /
              outAlphaF;
            blended.convertTo(dstChannels[i], CV_8U, 255.0);
        }
        outAlphaF.convertTo(dstChannels[3], CV_8U, 255.0);

        cv::merge(dstChannels, dst4);

        // Convert back to original channel count if necessary
        if (dstROI.channels() == 3) {
            cv::cvtColor(dst4, dstROI, cv::COLOR_BGRA2BGR);
        }
        else {
            dst4.copyTo(dstROI);
        }

        return OPENCV_SUCCESS;
    }
    catch (const cv::Exception& e) {
        std::cerr << "OpenCV exception in opencv_copy_to_region_with_alpha: " << e.what()
                  << std::endl;
        return OPENCV_ERROR_ALPHA_BLENDING_FAILED;
    }
    catch (const std::exception& e) {
        std::cerr << "Standard exception in opencv_copy_to_region_with_alpha: " << e.what()
                  << std::endl;
        return OPENCV_ERROR_UNKNOWN;
    }
    catch (...) {
        std::cerr << "Unknown exception in opencv_copy_to_region_with_alpha" << std::endl;
        return OPENCV_ERROR_UNKNOWN;
    }
}

/**
 * @brief Copy source image to a rectangular region of the destination image.
 *
 * @param src Pointer to the source OpenCV matrix.
 * @param dst Pointer to the destination OpenCV matrix.
 * @param xOffset X-coordinate of the top-left corner in the destination image.
 * @param yOffset Y-coordinate of the top-left corner in the destination image.
 * @param width Width of the region to copy.
 * @param height Height of the region to copy.
 * @return int Error code.
 */
int opencv_copy_to_region(opencv_mat src,
                          opencv_mat dst,
                          int xOffset,
                          int yOffset,
                          int width,
                          int height)
{
    try {
        auto srcMat = static_cast<cv::Mat*>(src);
        auto dstMat = static_cast<cv::Mat*>(dst);

        if (!srcMat || !dstMat || srcMat->empty() || dstMat->empty()) {
            return OPENCV_ERROR_NULL_MATRIX;
        }

        if (xOffset < 0 || yOffset < 0 || xOffset + width > dstMat->cols ||
            yOffset + height > dstMat->rows) {
            return OPENCV_ERROR_OUT_OF_BOUNDS;
        }

        if (width <= 0 || height <= 0) {
            return OPENCV_ERROR_INVALID_DIMENSIONS;
        }

        cv::Rect roi(xOffset, yOffset, width, height);
        cv::Mat dstROI = dstMat->operator()(roi);

        // Resize source if necessary
        cv::Mat srcResized;
        if (srcMat->size() != dstROI.size()) {
            cv::resize(*srcMat, srcResized, dstROI.size(), 0, 0, cv::INTER_LINEAR);
        }
        else {
            srcResized = *srcMat;
        }

        // Handle channel count mismatch
        if (srcResized.channels() != dstROI.channels()) {
            if (srcResized.channels() == 3 && dstROI.channels() == 4) {
                cv::cvtColor(srcResized, srcResized, cv::COLOR_BGR2BGRA);
            }
            else if (srcResized.channels() == 4 && dstROI.channels() == 3) {
                cv::cvtColor(srcResized, srcResized, cv::COLOR_BGRA2BGR);
            }
            else if (srcResized.channels() == 1 && dstROI.channels() == 3) {
                cv::cvtColor(srcResized, srcResized, cv::COLOR_GRAY2BGR);
            }
            else if (srcResized.channels() == 1 && dstROI.channels() == 4) {
                cv::cvtColor(srcResized, srcResized, cv::COLOR_GRAY2BGRA);
            }
            else {
                return OPENCV_ERROR_INVALID_CHANNEL_COUNT;
            }
        }

        // Perform the copy
        srcResized.copyTo(dstROI);

        return OPENCV_SUCCESS;
    }
    catch (const cv::Exception& e) {
        std::cerr << "OpenCV exception in opencv_copy_to_region: " << e.what() << std::endl;
        return OPENCV_ERROR_COPY_FAILED;
    }
    catch (const std::exception& e) {
        std::cerr << "Standard exception in opencv_copy_to_region: " << e.what() << std::endl;
        return OPENCV_ERROR_UNKNOWN;
    }
    catch (...) {
        std::cerr << "Unknown exception in opencv_copy_to_region" << std::endl;
        return OPENCV_ERROR_UNKNOWN;
    }
}
