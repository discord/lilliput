#include "png.hpp"

#include <cstdint>
#include <cstring>
#include <png.h>
#include <csetjmp>
#include <vector>
#include <span>

struct png_encoder_struct {
    uint8_t* dst{};
    size_t dst_len{};
    std::span<const unsigned char> icc_profile{};
};

// Memory write context for libpng
struct png_mem_encode {
    uint8_t* buffer;
    size_t buffer_size;
    size_t current_pos;
    bool buffer_overflow;
};

png_encoder png_encoder_create(void* dst,
                               const size_t dst_len,
                               const void* icc_data,
                               size_t icc_len)
{
    const auto e = new png_encoder_struct{static_cast<uint8_t*>(dst), dst_len};

    if (icc_data && icc_len > 0) {
        e->icc_profile = {static_cast<const unsigned char*>(icc_data), icc_len};
    }

    return e;
}

// Custom write function for libpng to write to memory buffer
static void png_write_to_memory(png_structp png_ptr, png_bytep data, const png_size_t length)
{
    const auto mem = static_cast<png_mem_encode*>(png_get_io_ptr(png_ptr));
    if (!mem) {
        png_error(png_ptr, "Invalid write context");
    }

    if (mem->current_pos + length > mem->buffer_size) {
        mem->buffer_overflow = true;
        // Returns to the error handler without printing an error;
        // this is bubbled up as a distinct error return value later,
        // so printing it out as well at this level isn't really necessary
        png_longjmp(png_ptr, 1);
    }

    std::memcpy(mem->buffer + mem->current_pos, data, length);
    mem->current_pos += length;
}

// Custom flush function for libpng (no-op for writing to memory)
static void png_flush_memory(png_structp) {}


void png_encoder_release(png_encoder e)
{
    delete e;
}

int png_encoder_encode(png_encoder e,
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
        return L_PNG_ERROR_INVALID_DIMENSIONS;
    }
    if (!e || !src_data || (!opt && opt_len > 0) || !out_size) {
        return L_PNG_ERROR_INVALID_ARG;
    }
    if (!e->dst) {
        return L_PNG_ERROR_NULL_MATRIX;
    }

    // Extract compression parameter
    int compression = L_PNG_DEFAULT_COMPRESSION;
    if (opt) {
        for (size_t i = 0; i + 1 < opt_len; i += 2) {
            if (opt[i] == L_PNG_COMPRESSION) {
                compression = opt[i + 1];
            }
        }
    }

    // Set up libpng
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png_ptr) {
        return L_PNG_ERROR_UNKNOWN;
    }

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_write_struct(&png_ptr, nullptr);
        return L_PNG_ERROR_UNKNOWN;
    }

    // Set up memory write handler
    png_mem_encode mem{e->dst, e->dst_len, 0, false};

    // Error handling: if any libpng function fails, jump here
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        if (mem.buffer_overflow) {
            return L_PNG_ERROR_BUFFER_TOO_SMALL;
        }
        return L_PNG_ERROR_UNKNOWN;
    }

    png_set_write_fn(png_ptr, &mem, png_write_to_memory, png_flush_memory);

    // Determine color type
    int color_type;
    if (channels == 3) {
        color_type = PNG_COLOR_TYPE_RGB;
    } else if (channels == 4) {
        color_type = PNG_COLOR_TYPE_RGBA;
    } else {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return L_PNG_ERROR_INVALID_CHANNEL_COUNT;
    }

    // Signal that the source data is in BGR format
    png_set_bgr(png_ptr);

    png_set_IHDR(
        png_ptr,
        info_ptr,
        width,
        height,
        8,
        color_type,
        PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT,
        PNG_FILTER_TYPE_DEFAULT
    );

    png_set_compression_level(png_ptr, compression);

    // Write ICC profile if present
    if (!e->icc_profile.empty()) {
        png_set_iCCP(
            png_ptr,
            info_ptr,
            "ICC Profile",
            PNG_COMPRESSION_TYPE_BASE,
            e->icc_profile.data(),
            e->icc_profile.size()
        );
    }

    png_write_info(png_ptr, info_ptr);

    // Write rows
    for (int y = 0; y < height; y++) {
        png_write_row(png_ptr, static_cast<png_const_bytep>(src_data) + y * stride);
    }

    png_write_end(png_ptr, nullptr);
    *out_size = mem.current_pos;
    png_destroy_write_struct(&png_ptr, &info_ptr);

    return L_PNG_SUCCESS;
}
