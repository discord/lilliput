#pragma once

#include <opencv2/opencv.hpp>
#include <lcms2.h>
#include <cstdint>
#include <cstddef>

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
