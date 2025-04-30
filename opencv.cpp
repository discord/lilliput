#include "opencv.hpp"

#include <stdbool.h>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <jpeglib.h>
#include <png.h>
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

/**
 * @brief Extract JPEG colorspace and color transform information from a JPEG file
 * 
 * @param src Pointer to the JPEG data buffer
 * @param src_len Length of the JPEG data buffer
 * @param colorspace Output parameter for colorspace value (1=grayscale, 2=YCbCr, etc.)
 * @param color_transform Output parameter for color transform value (0=unknown, 1=YCbCr, 2=YCCK)
 * @return true if extraction succeeded, false otherwise
 */
static bool opencv_get_jpeg_color_info(void* src, size_t src_len, int* colorspace, int* color_transform)
{
    struct jpeg_decompress_struct cinfo;
    struct opencv_jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = opencv_jpeg_error_exit;

    if (setjmp(jerr.setjmp_buffer)) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, static_cast<unsigned char*>(src), src_len);

    // Read JPEG header
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    fprintf(stderr, "DEBUG: JPEG color space from header: %d\n", cinfo.jpeg_color_space);
    
    *colorspace = cinfo.jpeg_color_space;
    
    // For JPEG with Adobe marker, color transform value can be extracted
    // 0 = Unknown, 1 = YCbCr, 2 = YCCK
    *color_transform = 0;
    
    // We need to set up marker handling before reading the header
    jpeg_destroy_decompress(&cinfo);
    jpeg_create_decompress(&cinfo);
    
    // Save markers for Adobe APP14 detection
    jpeg_save_markers(&cinfo, JPEG_APP0+14, 256);
    
    // Re-setup the source
    jpeg_mem_src(&cinfo, static_cast<unsigned char*>(src), src_len);
    
    // Read JPEG header again with marker processing
    jpeg_read_header(&cinfo, TRUE);
    
    // Check for Adobe marker which might contain color transform information
    for (jpeg_saved_marker_ptr marker = cinfo.marker_list; marker != NULL; marker = marker->next) {
        if (marker->marker == JPEG_APP0+14 && marker->data_length >= 12) {
            // Adobe APP14 marker
            if (marker->data[0] == 'A' && marker->data[1] == 'd' && 
                marker->data[2] == 'o' && marker->data[3] == 'b' && 
                marker->data[4] == 'e') {
                // Extract color transform value (usually at byte offset 11)
                *color_transform = marker->data[11] & 0x0F;
                fprintf(stderr, "DEBUG: Found Adobe marker with transform: %d\n", *color_transform);
                
                // Force YCbCr colorspace for RGB with ColorTransform=1
                if (*color_transform == 1 && *colorspace == 3) {
                    *colorspace = 2; // YCbCr
                    fprintf(stderr, "DEBUG: RGB with transform 1 - treating as YCbCr\n");
                }
                break;
            }
        }
    }
    
    // Default to YCbCr (2) for typical JPEG images when not explicitly specified
    if (*colorspace == 0 && cinfo.num_components == 3) {
        *colorspace = 2; // Assume YCbCr for typical color JPEGs
        fprintf(stderr, "DEBUG: Assuming YCbCr for 3-component JPEG\n");
    }
    
    // For YCbCr without explicit color transform info, assume transform 1
    if (*colorspace == 2 && *color_transform == 0) {
        *color_transform = 1;
        fprintf(stderr, "DEBUG: Setting default color transform 1 for YCbCr\n");
    }
    
    fprintf(stderr, "DEBUG: Final colorspace=%d, color_transform=%d\n", *colorspace, *color_transform);

    jpeg_destroy_decompress(&cinfo);
    return true;
}

/**
 * @brief Generate standardized XMP data containing color information
 *
 * @param colorspace Color space identifier (1=Gray, 2=YCbCr, 3=RGB, etc.)
 * @param color_transform Color transform value (0=None, 1=YCbCr, 2=YCCK)
 * @param dest Destination buffer for the generated XMP data
 * @param dest_len Size of the destination buffer
 * @return Length of the generated XMP data, 0 if generation failed
 */
int generate_color_xmp(int colorspace, int color_transform, void* dest, size_t dest_len)
{
    // If no meaningful color info was found, don't generate XMP
    if (colorspace == 0) {
        return 0;
    }
    
    // Generate human-readable colorspace name
    const char* colorspace_str = "Unknown";
    if (colorspace == 1) {
        colorspace_str = "Grayscale";
    } else if (colorspace == 2) {
        colorspace_str = "YCbCr";
    } else if (colorspace == 3) {
        colorspace_str = "RGB";
    } else if (colorspace == 4) {
        colorspace_str = "CMYK";
    } else if (colorspace == 5) {
        colorspace_str = "YCCK";
    }
    
    // For YCbCr colorspace with transform 1, add the specific ColorSpace that browsers need
    // We use xmp:ColorSpace specifically for YCbCr since photoshop:ColorMode doesn't support it
    const char* transform_str = "";
    if (colorspace == 2 && color_transform == 1) {
        // YCbCr with transform 1 needs specific XMP markers
        transform_str = "<xmp:ColorSpace>YCbCr</xmp:ColorSpace>";
    }
    
    // Get Adobe's standard photoshop:ColorMode value
    // According to Adobe's spec: https://developer.adobe.com/xmp/docs/XMPNamespaces/photoshop/
    // 0 = Bitmap, 1 = Gray scale, 2 = Indexed, 3 = RGB, 4 = CMYK, 7 = Multi-channel, 8 = Duotone, 9 = LAB
    int ps_colormode = 3; // Default to RGB (3)
    
    // If color_transform is already in the valid range for photoshop:ColorMode, use it directly
    if (color_transform >= 0 && color_transform <= 9 && 
        color_transform != 5 && color_transform != 6) { // 5,6 are not in Adobe's spec
        ps_colormode = color_transform;
    } else {
        // Otherwise, map from our internal colorspace values to Adobe's
        switch (colorspace) {
            case 1: ps_colormode = 1; break;  // Grayscale -> 1 (Grayscale)
            case 2: ps_colormode = 3; break;  // YCbCr -> 3 (RGB) - YCbCr is not in Adobe's spec
            case 3: ps_colormode = 3; break;  // RGB -> 3 (RGB)
            case 4: ps_colormode = 4; break;  // CMYK -> 4 (CMYK)
            case 5: ps_colormode = 4; break;  // YCCK -> 4 (CMYK) - closest equivalent
        }
    }
    
    // Generate XMP data with standard namespaces and color information
    // Use photoshop namespace for the color information
    int xmp_len = snprintf(static_cast<char*>(dest), dest_len, 
                         "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
                         "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                         "<rdf:Description rdf:about=\"\" "
                         "xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\" "
                         "xmlns:photoshop=\"http://ns.adobe.com/photoshop/1.0/\">"
                         "%s"
                         "<photoshop:ColorMode>%d</photoshop:ColorMode>"
                         "</rdf:Description>"
                         "</rdf:RDF>"
                         "</x:xmpmeta>",
                         transform_str, ps_colormode);
    
    // Ensure we didn't overflow the buffer
    if (xmp_len >= static_cast<int>(dest_len)) {
        return 0;
    }
    
    return xmp_len;
}

/**
 * @brief Check if the buffer starts with JPEG magic bytes
 * 
 * @param src Pointer to the data buffer
 * @param src_len Length of the data buffer
 * @return True if buffer appears to be a JPEG
 */
static bool is_jpeg(void* src, size_t src_len) {
    if (src_len < 2) {
        return false;
    }
    const uint8_t* data = static_cast<const uint8_t*>(src);
    return data[0] == 0xFF && data[1] == 0xD8; // JPEG SOI marker
}

/**
 * @brief Extract any existing XMP data from JPEG file
 * 
 * @param src Pointer to the JPEG data buffer
 * @param src_len Length of the JPEG data buffer
 * @param dest Destination buffer for the extracted XMP data
 * @param dest_len Size of the destination buffer
 * @return Length of the XMP data copied to destination buffer, 0 if none found
 */
static int jpeg_extract_xmp(void* src, size_t src_len, void* dest, size_t dest_len) {
    if (!src || src_len < 4 || !dest || dest_len == 0) {
        return 0;
    }
    
    const uint8_t* data = static_cast<const uint8_t*>(src);
    size_t offset = 2; // Skip SOI marker
    
    // Standard APP1 XMP marker identifier
    static const uint8_t XMP_IDENTIFIER[] = {
        'h', 't', 't', 'p', ':', '/', '/', 'n', 's', '.', 'a', 'd', 'o', 'b', 'e', '.',
        'c', 'o', 'm', '/', 'x', 'a', 'p', '/', '1', '.', '0', '/', '\0'
    };
    const size_t XMP_IDENTIFIER_LEN = sizeof(XMP_IDENTIFIER);
    
    // Search through the JPEG segments
    while (offset + 4 <= src_len) {
        if (data[offset] != 0xFF) {
            // Not a valid JPEG marker, advance to next byte
            offset++;
            continue;
        }
        
        uint8_t marker = data[offset + 1];
        if (marker == 0xDA) {
            // SOS marker, no more header segments
            break;
        }
        
        if (marker == 0xD9) {
            // EOI marker, end of file
            break;
        }
        
        if (marker < 0xD0 || marker > 0xD7) {
            // Regular segment with length
            uint16_t segment_len = (data[offset + 2] << 8) | data[offset + 3];
            
            // Check if it's an APP1 marker (0xE1) with XMP data
            if (marker == 0xE1 && 
                offset + 4 + XMP_IDENTIFIER_LEN < src_len &&
                segment_len > XMP_IDENTIFIER_LEN + 2) {
                
                // Check for XMP identifier
                if (memcmp(data + offset + 4, XMP_IDENTIFIER, XMP_IDENTIFIER_LEN) == 0) {
                    // Found XMP data
                    size_t xmp_len = segment_len - 2 - XMP_IDENTIFIER_LEN;
                    if (xmp_len <= dest_len) {
                        memcpy(dest, data + offset + 4 + XMP_IDENTIFIER_LEN, xmp_len);
                        return xmp_len;
                    }
                }
            }
            
            // Move to next segment
            offset += 2 + segment_len;
        } else {
            // Marker without length, move to next marker
            offset += 2;
        }
    }
    
    // No XMP found
    return 0;
}

/**
 * @brief Extract XMP data from images, only generating it for critical YCbCr cases
 *
 * @param src Pointer to the image data buffer
 * @param src_len Length of the image data buffer
 * @param dest Destination buffer for the XMP data
 * @param dest_len Size of the destination buffer
 * @return Length of the XMP data copied to destination buffer, 0 if none found or generation failed
 */
int opencv_decoder_get_color_xmp(void* src, size_t src_len, void* dest, size_t dest_len)
{
    if (!src || src_len == 0 || !dest || dest_len == 0) {
        return 0;
    }
    
    // Only handle JPEG files for now
    if (!is_jpeg(src, src_len)) {
        return 0;
    }
    
    // First check for existing XMP metadata in the JPEG file
    int xmp_len = jpeg_extract_xmp(src, src_len, dest, dest_len);
    if (xmp_len > 0) {
        return xmp_len;
    }
    
    // Generate XMP ONLY for YCbCr with transform 1 (Adobe marker present)
    // as this is the critical case that browsers need for proper display
    int colorspace = 0;
    int color_transform = 0;
    
    if (opencv_get_jpeg_color_info(src, src_len, &colorspace, &color_transform) &&
        colorspace == 2 && color_transform == 1) {
        return generate_color_xmp(colorspace, color_transform, dest, dest_len);
    }
    
    return 0; // No existing XMP and not a critical case
}
