#pragma once

#include <stddef.h>
#include <stdint.h>

// CICP Transfer Characteristics (ITU-T H.273)
#define CICP_TRANSFER_UNSPECIFIED   0
#define CICP_TRANSFER_LINEAR        8
#define CICP_TRANSFER_PQ            16  // SMPTE ST 2084 (HDR10)
#define CICP_TRANSFER_HLG           18  // ARIB STD-B67 (HLG)

// CICP Colour Primaries (ITU-T H.273)
#define CICP_PRIMARIES_UNSPECIFIED  2
#define CICP_PRIMARIES_BT601        6
#define CICP_PRIMARIES_BT2020       9
#define CICP_PRIMARIES_XYZ          10  // CIE 1931 XYZ (SMPTE ST 428-1)
#define CICP_PRIMARIES_SMPTE431     11  // DCI-P3
#define CICP_PRIMARIES_SMPTE432     12  // Display-P3

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

/**
 * Check whether a CICP transfer characteristic denotes HDR content that must
 * be tone-mapped before display on an SDR surface.
 *
 * Unlike the AVIF decoder's own HDR predicate this does not consider bit
 * depth: an 8-bit image carrying a PQ transfer is legal (PNG 3rd edition
 * permits it and HDR screenshot pipelines emit it), so depth cannot be part
 * of the test for container-signalled colour.
 */
bool cicp_is_hdr_transfer(uint8_t transfer);

/**
 * Map a CICP `colour_primaries` value to a canned ICC profile, for a
 * still-image source that signalled colour out-of-band (a PNG cICP chunk) but
 * is being re-encoded into a format with no cICP channel of its own, so the
 * signalling has to be carried as a real profile instead.
 *
 * This is `avcodec_get_icc_profile` plus Display-P3. The split is deliberate:
 * `avcodec_get_icc_profile` is the video path's mapping and has no P3 entry,
 * so adding one there would silently change tagged-video output. Still images
 * have no such constraint, and a P3 cICP chunk resolving to sRGB would discard
 * the wide gamut the chunk exists to declare.
 */
const uint8_t* cicp_get_icc_profile(uint8_t primaries, size_t* profile_size);

/**
 * Basic ICC header sanity check: every ICC profile begins with a 4-byte
 * big-endian size field that must equal the profile's own length, and the
 * header itself is 128 bytes. A blob failing this is structurally unusable,
 * and muxing it produces output that decoders either reject or silently
 * render with the wrong colour.
 *
 * Deliberately shallow: this rejects truncated/garbage blobs, not semantically
 * odd but well-formed profiles, which we have no business second-guessing.
 */
bool icc_header_is_sane(const uint8_t* icc, size_t icc_len);

/**
 * Tone-map a high-bit-depth RGB buffer down to 8-bit SDR, converting to
 * BT.709 primaries along the way.
 *
 * `src` is interleaved 3-channel with width*height*3 samples in
 * [0, (1<<src_depth)-1]; `dst` receives interleaved 8-bit 3-channel data.
 * Channel order is preserved positionally, so a BGR source yields BGR output.
 *
 * Shared by the AVIF decoder and the still-image (OpenCV) decoder so both
 * apply an identical transform; see also `tonemap_rgb_8u_inplace` for callers
 * that already hold an 8-bit buffer.
 */
void tonemap_rgb_to_sdr(
    const uint16_t* src,
    uint8_t* dst,
    int width,
    int height,
    int src_depth,
    uint8_t transfer,
    uint8_t primaries
);

/**
 * In-place tone-map of an interleaved 8-bit buffer with `channels` channels
 * (3 = BGR, 4 = BGRA). Alpha, when present, is passed through untouched.
 *
 * Convenience wrapper over `tonemap_rgb_to_sdr` for the still-image path,
 * whose decoded frames are already 8-bit.
 */
void tonemap_rgb_8u_inplace(
    uint8_t* pixels,
    int width,
    int height,
    int channels,
    uint8_t transfer,
    uint8_t primaries
);

#ifdef __cplusplus
}
#endif
