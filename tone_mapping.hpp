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
opencv_mat apply_tone_mapping(
    const opencv_mat src,
    const uint8_t* icc_data,
    size_t icc_len
);

#ifdef __cplusplus
}
#endif
