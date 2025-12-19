#include "color_info.hpp"
#include <lcms2.h>

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
