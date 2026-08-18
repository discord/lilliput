#include "color_info.hpp"
#include <lcms2.h>
#include "icc_profiles/displayp3_profile.h"
#include "icc_profiles/rec2020_profile.h"
#include "icc_profiles/rec601_ntsc_profile.h"
#include "icc_profiles/rec601_pal_profile.h"
#include "icc_profiles/srgb_profile.h"
#include <opencv2/core.hpp>
#include <opencv2/photo.hpp>
#include <cmath>
#include <cstring>
#include <vector>

// Maximum ICC profile size we're willing to parse (1MB)
static const size_t MAX_ICC_PROFILE_SIZE = 1024 * 1024;

// Check if ICC profile indicates HDR (PQ or HLG transfer function)
bool is_hdr_transfer_function(const uint8_t* icc_data, size_t icc_len)
{
    if (!icc_data || icc_len == 0 || icc_len > MAX_ICC_PROFILE_SIZE) {
        return false;
    }

    cmsHPROFILE profile = cmsOpenProfileFromMem(icc_data, icc_len);
    if (!profile) {
        return false;
    }

    uint8_t transfer = CICP_TRANSFER_UNSPECIFIED;
    cmsVideoSignalType* cicp = (cmsVideoSignalType*)cmsReadTag(profile, cmsSigcicpTag);
    if (cicp && cicp->TransferCharacteristics != 0) {
        transfer = static_cast<uint8_t>(cicp->TransferCharacteristics);
    }

    cmsCloseProfile(profile);
    return (transfer == CICP_TRANSFER_PQ) || (transfer == CICP_TRANSFER_HLG);
}

bool cicp_is_hdr_transfer(uint8_t transfer)
{
    return (transfer == CICP_TRANSFER_PQ) || (transfer == CICP_TRANSFER_HLG);
}

const uint8_t* cicp_get_icc_profile(uint8_t primaries, size_t* profile_size)
{
    switch (primaries) {
    // SMPTE RP 431-2 (DCI-P3) and SMPTE EG 432-1 (Display-P3). Both share the
    // P3 primaries; the canned profile is D65-adapted, which matches
    // Display-P3 exactly and is the conventional substitute for DCI-P3
    // content in an RGB display pipeline.
    case CICP_PRIMARIES_SMPTE431:
    case CICP_PRIMARIES_SMPTE432:
        *profile_size = sizeof(displayp3_profile);
        return displayp3_profile;
    case CICP_PRIMARIES_BT2020:
        *profile_size = sizeof(rec2020_profile);
        return rec2020_profile;
    case 5: // BT.470BG / BT.601 PAL
        *profile_size = sizeof(rec601_pal_profile);
        return rec601_pal_profile;
    case CICP_PRIMARIES_BT601: // SMPTE 170M / BT.601 NTSC
        *profile_size = sizeof(rec601_ntsc_profile);
        return rec601_ntsc_profile;
    default:
        *profile_size = sizeof(srgb_profile);
        return srgb_profile;
    }
}

bool icc_header_is_sane(const uint8_t* icc, size_t icc_len)
{
    static const size_t ICC_HEADER_LEN = 128;
    if (!icc || icc_len < ICC_HEADER_LEN) {
        return false;
    }
    size_t declared = ((size_t)icc[0] << 24) | ((size_t)icc[1] << 16) | ((size_t)icc[2] << 8) |
                      (size_t)icc[3];
    return declared == icc_len;
}

// Convert PQ (SMPTE ST.2084) to linear
static float pq_to_linear(float x)
{
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;

    float xpow = std::pow(x, 1.0f / m2);
    float num = std::max(xpow - c1, 0.0f);
    float den = c2 - c3 * xpow;

    return std::pow(num / den, 1.0f / m1);
}

// Convert HLG to linear
static float hlg_to_linear(float x)
{
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;

    if (x <= 0.5f) {
        return x * x / 3.0f;
    }
    else {
        return (std::exp((x - c) / a) + b) / 12.0f;
    }
}

void tonemap_rgb_to_sdr(const uint16_t* src,
                        uint8_t* dst,
                        int width,
                        int height,
                        int src_depth,
                        uint8_t transfer,
                        uint8_t primaries)
{
    float scale = 1.0f / ((1 << src_depth) - 1);

    cv::Mat hdrMat(height, width, CV_32FC3);
    cv::Mat sdrMat(height, width, CV_8UC3);

    // Convert to linear RGB
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            float r = src[idx] * scale;
            float g = src[idx + 1] * scale;
            float b = src[idx + 2] * scale;

            if (transfer == CICP_TRANSFER_PQ) {
                r = pq_to_linear(r);
                g = pq_to_linear(g);
                b = pq_to_linear(b);
            }
            else if (transfer == CICP_TRANSFER_HLG) {
                r = hlg_to_linear(r);
                g = hlg_to_linear(g);
                b = hlg_to_linear(b);
            }

            hdrMat.at<cv::Vec3f>(y, x) = cv::Vec3f(r, g, b);
        }
    }

    // Create a Reinhard tonemap with typical parameters for HDR content
    cv::Ptr<cv::TonemapReinhard> tonemap = cv::createTonemapReinhard(1.0f, 0.6f, 0.2f, 0.3f);
    cv::Mat tonemapped;
    tonemap->process(hdrMat, tonemapped);

    // Convert colorspace if needed
    cv::Mat converted;
    if (primaries == CICP_PRIMARIES_BT2020) {
        cv::Matx33f bt2020_to_bt709(
          1.6605f, -0.5876f, -0.0728f, -0.1246f, 1.1329f, -0.0083f, -0.0182f, -0.1006f, 1.1187f);
        cv::transform(tonemapped, converted, bt2020_to_bt709);
    }
    else if (primaries == CICP_PRIMARIES_SMPTE432 || primaries == CICP_PRIMARIES_SMPTE431) {
        cv::Matx33f p3_to_bt709(
          1.2249f, -0.2247f, -0.0002f, -0.0420f, 1.0419f, 0.0001f, -0.0197f, 0.0754f, 0.9443f);
        cv::transform(tonemapped, converted, p3_to_bt709);
    }
    else if (primaries == CICP_PRIMARIES_BT601) {
        cv::Matx33f bt601_to_bt709(
          1.0440f, -0.0440f, 0.0000f, -0.0000f, 1.0000f, 0.0000f, 0.0000f, 0.0000f, 1.0000f);
        cv::transform(tonemapped, converted, bt601_to_bt709);
    }
    else if (primaries == CICP_PRIMARIES_XYZ) {
        // CIE 1931 XYZ (SMPTE ST 428-1) to BT.709, D65 white point. The buffer is
        // channel-ordered B,G,R (Z,Y,X here), so rows and columns are reversed
        // relative to the textbook row-major XYZ->RGB matrix.
        cv::Matx33f xyz_to_bt709(1.0569715f,
                                 -0.2039770f,
                                 0.0556301f,
                                 0.0415551f,
                                 1.8759675f,
                                 -0.9692436f,
                                 -0.4986108f,
                                 -1.5373832f,
                                 3.2409699f);
        cv::transform(tonemapped, converted, xyz_to_bt709);
    }
    else {
        // For unknown colorspaces, default to assuming BT709
        converted = tonemapped;
    }

    // Convert to 8-bit with proper gamma correction
    cv::Mat gamma_corrected;
    if (transfer == CICP_TRANSFER_LINEAR) {
        cv::pow(converted, 1.0f / 2.2f, gamma_corrected);
    }
    else {
        // PQ and HLG already include transfer function
        gamma_corrected = converted;
    }
    gamma_corrected.convertTo(sdrMat, CV_8UC3, 255.0f);

    memcpy(dst, sdrMat.data, (size_t)width * height * 3);
}

void tonemap_rgb_8u_inplace(uint8_t* pixels,
                            int width,
                            int height,
                            int channels,
                            uint8_t transfer,
                            uint8_t primaries)
{
    if (!pixels || width <= 0 || height <= 0 || (channels != 3 && channels != 4)) {
        return;
    }

    const size_t count = (size_t)width * height;

    // Widen the colour channels to the 16-bit layout the shared tone-mapper
    // consumes. Samples are copied rather than rescaled: src_depth is 8, so
    // it normalizes against the same 0..255 range.
    std::vector<uint16_t> wide(count * 3);
    for (size_t i = 0; i < count; i++) {
        wide[i * 3 + 0] = pixels[i * channels + 0];
        wide[i * 3 + 1] = pixels[i * channels + 1];
        wide[i * 3 + 2] = pixels[i * channels + 2];
    }

    std::vector<uint8_t> sdr(count * 3);
    tonemap_rgb_to_sdr(wide.data(), sdr.data(), width, height, 8, transfer, primaries);

    for (size_t i = 0; i < count; i++) {
        pixels[i * channels + 0] = sdr[i * 3 + 0];
        pixels[i * channels + 1] = sdr[i * 3 + 1];
        pixels[i * channels + 2] = sdr[i * 3 + 2];
        // Alpha, when present, is untouched.
    }
}
