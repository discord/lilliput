#pragma once

#include <stddef.h>
#include <stdint.h>

// CICP Transfer Characteristics (ITU-T H.273)
#define CICP_TRANSFER_UNSPECIFIED   0
#define CICP_TRANSFER_PQ            16  // SMPTE ST 2084 (HDR10)
#define CICP_TRANSFER_HLG           18  // ARIB STD-B67 (HLG)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Check if an ICC profile indicates HDR content (PQ or HLG transfer function).
 * Returns true if the profile's CICP tag indicates PQ or HLG transfer characteristics.
 */
bool is_hdr_transfer_function(
    const uint8_t* icc_data,
    size_t icc_len
);

#ifdef __cplusplus
}
#endif
