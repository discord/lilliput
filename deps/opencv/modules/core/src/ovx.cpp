// This file is part of OpenCV project.
// It is subject to the license terms in the LICENSE file found in the top-level directory
// of this distribution and at http://opencv.org/license.html.

// Copyright (C) 2016, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.

// OpenVX related functions

#include "precomp.hpp"
#include "opencv2/core/ovx.hpp"
#include "opencv2/core/openvx/ovx_defs.hpp"

namespace cv
{

bool haveOpenVX()
{
#ifdef HAVE_OPENVX
    static int g_haveOpenVX = -1;
    if(g_haveOpenVX < 0)
    {
        try
        {
        ivx::Context context = ivx::Context::create();
        vx_uint16 vComp = ivx::compiledWithVersion();
        vx_uint16 vCurr = context.version();
        g_haveOpenVX =
                VX_VERSION_MAJOR(vComp) == VX_VERSION_MAJOR(vCurr) &&
                VX_VERSION_MINOR(vComp) == VX_VERSION_MINOR(vCurr)
                ? 1 : 0;
        }
        catch(const ivx::WrapperError&)
        { g_haveOpenVX = 0; }
        catch(const ivx::RuntimeError&)
        { g_haveOpenVX = 0; }
    }
    return g_haveOpenVX == 1;
#else
    return false;
#endif
}

bool useOpenVX()
{
#ifdef HAVE_OPENVX
    CoreTLSData* data = getCoreTlsData().get();
    if( data->useOpenVX < 0 )
    {
        // enabled (if available) by default
        data->useOpenVX = haveOpenVX() ? 1 : 0;
    }
    return data->useOpenVX > 0;
#else
    return false;
#endif
}

void setUseOpenVX(bool flag)
{
#ifdef HAVE_OPENVX
    if( haveOpenVX() )
    {
        CoreTLSData* data = getCoreTlsData().get();
        data->useOpenVX = flag ? 1 : 0;
    }
#else
    CV_Assert(!flag && "OpenVX support isn't enabled at compile time");
#endif
}

} // namespace cv
