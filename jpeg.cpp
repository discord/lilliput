#include "jpeg.hpp"

#include <cstdint>
#include <turbojpeg.h>
#include <cstdio>
#include <jpeglib.h>
#include <jerror.h>
#include <cstring>
#include <vector>
#include <span>

struct jpeg_encoder_struct {
    uint8_t* dst{};
    size_t dst_len{};
    std::span<const unsigned char> icc_profile{};
};

jpeg_encoder jpeg_encoder_create(void* dst,
                                 const size_t dst_len,
                                 const void* icc_data,
                                 const size_t icc_len)
{
    const auto impl = new jpeg_encoder_struct{
        static_cast<uint8_t*>(dst), dst_len
    };

    if (icc_data && icc_len > 0) {
        impl->icc_profile = {
            static_cast<const unsigned char*>(icc_data), icc_len
        };
    }

    return impl;
}

void jpeg_encoder_release(jpeg_encoder e)
{
    delete e;
}

// Helper function to get the canonical JPEG buffer error message from jerror.h
static const char* get_jpeg_buffer_error_message()
{
    jpeg_error_mgr jerr{};
    jpeg_std_error(&jerr);
    return jerr.jpeg_message_table[JERR_BUFFER_SIZE];
}

int jpeg_encoder_encode(jpeg_encoder e,
                        const void* src_data,
                        const int width,
                        const int height,
                        const int channels,
                        const size_t stride,
                        const int* opt,
                        const size_t opt_len,
                        size_t* out_size)
{
    // Validate input parameters
    // Check dimensions first for better error specificity
    if (width == 0 || height == 0 || stride == 0) {
        return L_JPEG_ERROR_INVALID_DIMENSIONS;
    }
    if (!e || !src_data || (!opt && opt_len > 0) || !out_size) {
        return L_JPEG_ERROR_INVALID_ARG;
    }
    if (!e->dst) {
        return L_JPEG_ERROR_NULL_MATRIX;
    }

    // Extract encoding parameters
    int quality = L_JPEG_DEFAULT_QUALITY;
    int progressive = L_JPEG_DEFAULT_PROGRESSIVE;
    if (opt) {
        for (size_t i = 0; i + 1 < opt_len; i += 2) {
            if (opt[i] == L_JPEG_QUALITY) {
                quality = opt[i + 1];
            } else if (opt[i] == L_JPEG_PROGRESSIVE) {
                progressive = opt[i + 1];
            }
        }
    }

    // Clamp quality to valid range [1, 100]
    if (quality <= 0) {
        quality = 1;
    } else if (quality > 100) {
        quality = 100;
    }

    // Determine pixel format based on channel count
    // TurboJPEG supports encoding from grayscale, BGR, and BGRA directly
    int pixelFormat;
    int subsampling;
    if (channels == 1) {
        pixelFormat = TJPF_GRAY;
        subsampling = TJSAMP_GRAY;
    } else if (channels == 3) {
        pixelFormat = TJPF_BGR;
        subsampling = TJSAMP_420;
    } else if (channels == 4) {
        pixelFormat = TJPF_BGRA;  // Alpha will be discarded
        subsampling = TJSAMP_420;
    } else {
        return L_JPEG_ERROR_INVALID_CHANNEL_COUNT;
    }

    // Set up TurboJPEG compressor
    tjhandle handle = tj3Init(TJINIT_COMPRESS);
    if (!handle) {
        return L_JPEG_ERROR_UNKNOWN;
    }

    // Configure compression parameters
    tj3Set(handle, TJPARAM_QUALITY, quality);
    tj3Set(handle, TJPARAM_OPTIMIZE, L_JPEG_DEFAULT_OPTIMIZE);
    tj3Set(handle, TJPARAM_SUBSAMP, subsampling);
    tj3Set(handle, TJPARAM_PROGRESSIVE, progressive);
    tj3Set(handle, TJPARAM_NOREALLOC, 1);  // Disable buffer reallocation

    // Set ICC profile if present
    if (!e->icc_profile.empty()) {
        tj3SetICCProfile(
            handle,
            const_cast<unsigned char*>(e->icc_profile.data()),
            e->icc_profile.size()
        );
    }

    // Compress image
    unsigned char* buffer = e->dst;
    size_t bufferSize = e->dst_len;
    const int result = tj3Compress8(
        handle,
        static_cast<const unsigned char*>(src_data),
        width,
        stride,
        height,
        pixelFormat,
        &buffer,
        &bufferSize
    );

    if (result == -1) {
        *out_size = 0;
        const char* errorStr = tj3GetErrorStr(handle);
        tj3Destroy(handle);

        if (errorStr) {
            // There doesn't seem to be a way to get the error code directly,
            // so check against jerror.h message corresponding to the buffer
            // being too small
            const char* bufferErrMsg = get_jpeg_buffer_error_message();
            if (bufferErrMsg && std::strstr(errorStr, bufferErrMsg)) {
                return L_JPEG_ERROR_BUFFER_TOO_SMALL;
            }
        }

        // The same strategy could be used to check for other specific
        // errors, but none of them seem too interesting at the moment
        return L_JPEG_ERROR_UNKNOWN;
    }

    // Success path
    tj3Destroy(handle);
    *out_size = bufferSize;
    return L_JPEG_SUCCESS;
}
