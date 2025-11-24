#pragma once

#include <stddef.h>
#include <stdint.h>

// FFI-safe type for opencv_mat
typedef void* opencv_mat;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * C wrapper for tone mapping - safe for FFI calls from Go/Rust.
 * Returns a new opencv_mat (caller must release with opencv_mat_release).
 * Returns NULL on error.
 */
opencv_mat apply_tone_mapping_ffi(
    const opencv_mat src,
    const uint8_t* icc_data,
    size_t icc_len
);

#ifdef __cplusplus
}
#endif

// C++ implementation details (not visible to C/FFI)
#ifdef __cplusplus
#include <opencv2/opencv.hpp>
#include <lcms2.h>

/**
 * Applies HDR to SDR tone mapping using Reinhard algorithm.
 *
 * This function detects PQ (Perceptual Quantizer) HDR profiles and applies
 * luminance-based tone mapping to reduce brightness while preserving color
 * relationships. Non-PQ images are returned unchanged (as a copy).
 *
 * @param src Source image (BGR or BGRA format, 8-bit)
 * @param icc_data ICC profile data from the source image
 * @param icc_len Length of ICC profile data in bytes
 * @return Tone-mapped image (caller owns the pointer), or nullptr on error
 */
cv::Mat* apply_hdr_to_sdr_tone_mapping(
    const cv::Mat* src,
    const uint8_t* icc_data,
    size_t icc_len
);
#endif
