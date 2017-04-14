/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                           License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000-2008, Intel Corporation, all rights reserved.
// Copyright (C) 2009, Willow Garage Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of the copyright holders may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/

#include "precomp.hpp"
#include "opencl_kernels_imgproc.hpp"
#include "opencv2/core/hal/intrin.hpp"

#include "opencv2/core/openvx/ovx_defs.hpp"

namespace cv
{

static void
thresh_8u( const Mat& _src, Mat& _dst, uchar thresh, uchar maxval, int type )
{
    Size roi = _src.size();
    roi.width *= _src.channels();
    size_t src_step = _src.step;
    size_t dst_step = _dst.step;

    if( _src.isContinuous() && _dst.isContinuous() )
    {
        roi.width *= roi.height;
        roi.height = 1;
        src_step = dst_step = roi.width;
    }

#ifdef HAVE_TEGRA_OPTIMIZATION
    if (tegra::useTegra() && tegra::thresh_8u(_src, _dst, roi.width, roi.height, thresh, maxval, type))
        return;
#endif

#if defined(HAVE_IPP)
    CV_IPP_CHECK()
    {
        IppiSize sz = { roi.width, roi.height };
        CV_SUPPRESS_DEPRECATED_START
        switch( type )
        {
        case THRESH_TRUNC:
            if (_src.data == _dst.data && CV_INSTRUMENT_FUN_IPP(ippiThreshold_GT_8u_C1IR, _dst.ptr(), (int)dst_step, sz, thresh) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            if (CV_INSTRUMENT_FUN_IPP(ippiThreshold_GT_8u_C1R, _src.ptr(), (int)src_step, _dst.ptr(), (int)dst_step, sz, thresh) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        case THRESH_TOZERO:
            if (_src.data == _dst.data && CV_INSTRUMENT_FUN_IPP(ippiThreshold_LTVal_8u_C1IR, _dst.ptr(), (int)dst_step, sz, thresh+1, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            if (CV_INSTRUMENT_FUN_IPP(ippiThreshold_LTVal_8u_C1R, _src.ptr(), (int)src_step, _dst.ptr(), (int)dst_step, sz, thresh + 1, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        case THRESH_TOZERO_INV:
            if (_src.data == _dst.data && CV_INSTRUMENT_FUN_IPP(ippiThreshold_GTVal_8u_C1IR, _dst.ptr(), (int)dst_step, sz, thresh, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            if (CV_INSTRUMENT_FUN_IPP(ippiThreshold_GTVal_8u_C1R, _src.ptr(), (int)src_step, _dst.ptr(), (int)dst_step, sz, thresh, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        }
        CV_SUPPRESS_DEPRECATED_END
    }
#endif

    int j = 0;
    const uchar* src = _src.ptr();
    uchar* dst = _dst.ptr();
#if CV_SIMD128
    bool useSIMD = checkHardwareSupport( CV_CPU_SSE2 ) || checkHardwareSupport( CV_CPU_NEON );
    if( useSIMD )
    {
        v_uint8x16 thresh_u = v_setall_u8( thresh );
        v_uint8x16 maxval16 = v_setall_u8( maxval );

        switch( type )
        {
        case THRESH_BINARY:
            for( int i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j <= roi.width - 16; j += 16 )
                {
                    v_uint8x16 v0;
                    v0 = v_load( src + j );
                    v0 = thresh_u < v0;
                    v0 = v0 & maxval16;
                    v_store( dst + j, v0 );
                }
            }
            break;

        case THRESH_BINARY_INV:
            for( int i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j <= roi.width - 16; j += 16 )
                {
                    v_uint8x16 v0;
                    v0 = v_load( src + j );
                    v0 = v0 <= thresh_u;
                    v0 = v0 & maxval16;
                    v_store( dst + j, v0 );
                }
            }
            break;

        case THRESH_TRUNC:
            for( int i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j <= roi.width - 16; j += 16 )
                {
                    v_uint8x16 v0;
                    v0 = v_load( src + j );
                    v0 = v0 - ( v0 - thresh_u );
                    v_store( dst + j, v0 );
                }
            }
            break;

        case THRESH_TOZERO:
            for( int i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j <= roi.width - 16; j += 16 )
                {
                    v_uint8x16 v0;
                    v0 = v_load( src + j );
                    v0 = ( thresh_u < v0 ) & v0;
                    v_store( dst + j, v0 );
                }
            }
            break;

        case THRESH_TOZERO_INV:
            for( int i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j <= roi.width - 16; j += 16 )
                {
                    v_uint8x16 v0;
                    v0 = v_load( src + j );
                    v0 = ( v0 <= thresh_u ) & v0;
                    v_store( dst + j, v0 );
                }
            }
            break;
        }
    }
#endif

    int j_scalar = j;
    if( j_scalar < roi.width )
    {
        const int thresh_pivot = thresh + 1;
        uchar tab[256];
        switch( type )
        {
        case THRESH_BINARY:
            memset(tab, 0, thresh_pivot);
            if (thresh_pivot < 256) {
                memset(tab + thresh_pivot, maxval, 256 - thresh_pivot);
            }
            break;
        case THRESH_BINARY_INV:
            memset(tab, maxval, thresh_pivot);
            if (thresh_pivot < 256) {
                memset(tab + thresh_pivot, 0, 256 - thresh_pivot);
            }
            break;
        case THRESH_TRUNC:
            for( int i = 0; i <= thresh; i++ )
                tab[i] = (uchar)i;
            if (thresh_pivot < 256) {
                memset(tab + thresh_pivot, thresh, 256 - thresh_pivot);
            }
            break;
        case THRESH_TOZERO:
            memset(tab, 0, thresh_pivot);
            for( int i = thresh_pivot; i < 256; i++ )
                tab[i] = (uchar)i;
            break;
        case THRESH_TOZERO_INV:
            for( int i = 0; i <= thresh; i++ )
                tab[i] = (uchar)i;
            if (thresh_pivot < 256) {
                memset(tab + thresh_pivot, 0, 256 - thresh_pivot);
            }
            break;
        }

        src = _src.ptr();
        dst = _dst.ptr();
        for( int i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
        {
            j = j_scalar;
#if CV_ENABLE_UNROLLED
            for( ; j <= roi.width - 4; j += 4 )
            {
                uchar t0 = tab[src[j]];
                uchar t1 = tab[src[j+1]];

                dst[j] = t0;
                dst[j+1] = t1;

                t0 = tab[src[j+2]];
                t1 = tab[src[j+3]];

                dst[j+2] = t0;
                dst[j+3] = t1;
            }
#endif
            for( ; j < roi.width; j++ )
                dst[j] = tab[src[j]];
        }
    }
}


static void
thresh_16s( const Mat& _src, Mat& _dst, short thresh, short maxval, int type )
{
    int i, j;
    Size roi = _src.size();
    roi.width *= _src.channels();
    const short* src = _src.ptr<short>();
    short* dst = _dst.ptr<short>();
    size_t src_step = _src.step/sizeof(src[0]);
    size_t dst_step = _dst.step/sizeof(dst[0]);

    if( _src.isContinuous() && _dst.isContinuous() )
    {
        roi.width *= roi.height;
        roi.height = 1;
        src_step = dst_step = roi.width;
    }

#ifdef HAVE_TEGRA_OPTIMIZATION
    if (tegra::useTegra() && tegra::thresh_16s(_src, _dst, roi.width, roi.height, thresh, maxval, type))
        return;
#endif

#if defined(HAVE_IPP)
    CV_IPP_CHECK()
    {
        IppiSize sz = { roi.width, roi.height };
        CV_SUPPRESS_DEPRECATED_START
        switch( type )
        {
        case THRESH_TRUNC:
            if (_src.data == _dst.data && CV_INSTRUMENT_FUN_IPP(ippiThreshold_GT_16s_C1IR, dst, (int)dst_step*sizeof(dst[0]), sz, thresh) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            if (CV_INSTRUMENT_FUN_IPP(ippiThreshold_GT_16s_C1R, src, (int)src_step*sizeof(src[0]), dst, (int)dst_step*sizeof(dst[0]), sz, thresh) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        case THRESH_TOZERO:
            if (_src.data == _dst.data && CV_INSTRUMENT_FUN_IPP(ippiThreshold_LTVal_16s_C1IR, dst, (int)dst_step*sizeof(dst[0]), sz, thresh + 1, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            if (CV_INSTRUMENT_FUN_IPP(ippiThreshold_LTVal_16s_C1R, src, (int)src_step*sizeof(src[0]), dst, (int)dst_step*sizeof(dst[0]), sz, thresh + 1, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        case THRESH_TOZERO_INV:
            if (_src.data == _dst.data && CV_INSTRUMENT_FUN_IPP(ippiThreshold_GTVal_16s_C1IR, dst, (int)dst_step*sizeof(dst[0]), sz, thresh, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            if (CV_INSTRUMENT_FUN_IPP(ippiThreshold_GTVal_16s_C1R, src, (int)src_step*sizeof(src[0]), dst, (int)dst_step*sizeof(dst[0]), sz, thresh, 0) >= 0)
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        }
        CV_SUPPRESS_DEPRECATED_END
    }
#endif

#if CV_SIMD128
    bool useSIMD = checkHardwareSupport( CV_CPU_SSE2 ) || checkHardwareSupport( CV_CPU_NEON );
    if( useSIMD )
    {
        v_int16x8 thresh8 = v_setall_s16( thresh );
        v_int16x8 maxval8 = v_setall_s16( maxval );

        switch( type )
        {
        case THRESH_BINARY:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 16; j += 16 )
                {
                    v_int16x8 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 8 );
                    v0 = thresh8 < v0;
                    v1 = thresh8 < v1;
                    v0 = v0 & maxval8;
                    v1 = v1 & maxval8;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 8, v1 );
                }

                for( ; j < roi.width; j++ )
                    dst[j] = src[j] > thresh ? maxval : 0;
            }
            break;

        case THRESH_BINARY_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 16; j += 16 )
                {
                    v_int16x8 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 8 );
                    v0 = v0 <= thresh8;
                    v1 = v1 <= thresh8;
                    v0 = v0 & maxval8;
                    v1 = v1 & maxval8;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 8, v1 );
                }

                for( ; j < roi.width; j++ )
                    dst[j] = src[j] <= thresh ? maxval : 0;
            }
            break;

        case THRESH_TRUNC:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 16; j += 16 )
                {
                    v_int16x8 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 8 );
                    v0 = v_min( v0, thresh8 );
                    v1 = v_min( v1, thresh8 );
                    v_store( dst + j, v0 );
                    v_store( dst + j + 8, v1 );
                }

                for( ; j < roi.width; j++ )
                    dst[j] = std::min( src[j], thresh );
            }
            break;

        case THRESH_TOZERO:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 16; j += 16 )
                {
                    v_int16x8 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 8 );
                    v0 = ( thresh8 < v0 ) & v0;
                    v1 = ( thresh8 < v1 ) & v1;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 8, v1 );
                }

                for( ; j < roi.width; j++ )
                {
                    short v = src[j];
                    dst[j] = v > thresh ? v : 0;
                }
            }
            break;

        case THRESH_TOZERO_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 16; j += 16 )
                {
                    v_int16x8 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 8 );
                    v0 = ( v0 <= thresh8 ) & v0;
                    v1 = ( v1 <= thresh8 ) & v1;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 8, v1 );
                }

                for( ; j < roi.width; j++ )
                {
                    short v = src[j];
                    dst[j] = v <= thresh ? v : 0;
                }
            }
            break;
        default:
            return CV_Error( CV_StsBadArg, "" );
        }
    }
    else
#endif
    {
        switch( type )
        {
        case THRESH_BINARY:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j < roi.width; j++ )
                    dst[j] = src[j] > thresh ? maxval : 0;
            }
            break;

        case THRESH_BINARY_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j < roi.width; j++ )
                    dst[j] = src[j] <= thresh ? maxval : 0;
            }
            break;

        case THRESH_TRUNC:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j < roi.width; j++ )
                    dst[j] = std::min( src[j], thresh );
            }
            break;

        case THRESH_TOZERO:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j < roi.width; j++ )
                {
                    short v = src[j];
                    dst[j] = v > thresh ? v : 0;
                }
            }
            break;

        case THRESH_TOZERO_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                for( j = 0; j < roi.width; j++ )
                {
                    short v = src[j];
                    dst[j] = v <= thresh ? v : 0;
                }
            }
            break;
        default:
            return CV_Error( CV_StsBadArg, "" );
        }
    }
}


static void
thresh_32f( const Mat& _src, Mat& _dst, float thresh, float maxval, int type )
{
    int i, j;
    Size roi = _src.size();
    roi.width *= _src.channels();
    const float* src = _src.ptr<float>();
    float* dst = _dst.ptr<float>();
    size_t src_step = _src.step/sizeof(src[0]);
    size_t dst_step = _dst.step/sizeof(dst[0]);

    if( _src.isContinuous() && _dst.isContinuous() )
    {
        roi.width *= roi.height;
        roi.height = 1;
    }

#ifdef HAVE_TEGRA_OPTIMIZATION
    if (tegra::useTegra() && tegra::thresh_32f(_src, _dst, roi.width, roi.height, thresh, maxval, type))
        return;
#endif

#if defined(HAVE_IPP)
    CV_IPP_CHECK()
    {
        IppiSize sz = { roi.width, roi.height };
        switch( type )
        {
        case THRESH_TRUNC:
            if (0 <= CV_INSTRUMENT_FUN_IPP(ippiThreshold_GT_32f_C1R, src, (int)src_step*sizeof(src[0]), dst, (int)dst_step*sizeof(dst[0]), sz, thresh))
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        case THRESH_TOZERO:
            if (0 <= CV_INSTRUMENT_FUN_IPP(ippiThreshold_LTVal_32f_C1R, src, (int)src_step*sizeof(src[0]), dst, (int)dst_step*sizeof(dst[0]), sz, thresh + FLT_EPSILON, 0))
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        case THRESH_TOZERO_INV:
            if (0 <= CV_INSTRUMENT_FUN_IPP(ippiThreshold_GTVal_32f_C1R, src, (int)src_step*sizeof(src[0]), dst, (int)dst_step*sizeof(dst[0]), sz, thresh, 0))
            {
                CV_IMPL_ADD(CV_IMPL_IPP);
                return;
            }
            setIppErrorStatus();
            break;
        }
    }
#endif

#if CV_SIMD128
    bool useSIMD = checkHardwareSupport( CV_CPU_SSE2 ) || checkHardwareSupport( CV_CPU_NEON );
    if( useSIMD )
    {
        v_float32x4 thresh4 = v_setall_f32( thresh );
        v_float32x4 maxval4 = v_setall_f32( maxval );

        switch( type )
        {
            case THRESH_BINARY:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    j = 0;
                    for( ; j <= roi.width - 8; j += 8 )
                    {
                        v_float32x4 v0, v1;
                        v0 = v_load( src + j );
                        v1 = v_load( src + j + 4 );
                        v0 = thresh4 < v0;
                        v1 = thresh4 < v1;
                        v0 = v0 & maxval4;
                        v1 = v1 & maxval4;
                        v_store( dst + j, v0 );
                        v_store( dst + j + 4, v1 );
                    }

                    for( ; j < roi.width; j++ )
                        dst[j] = src[j] > thresh ? maxval : 0;
                }
                break;

            case THRESH_BINARY_INV:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    j = 0;
                    for( ; j <= roi.width - 8; j += 8 )
                    {
                        v_float32x4 v0, v1;
                        v0 = v_load( src + j );
                        v1 = v_load( src + j + 4 );
                        v0 = v0 <= thresh4;
                        v1 = v1 <= thresh4;
                        v0 = v0 & maxval4;
                        v1 = v1 & maxval4;
                        v_store( dst + j, v0 );
                        v_store( dst + j + 4, v1 );
                    }

                    for( ; j < roi.width; j++ )
                        dst[j] = src[j] <= thresh ? maxval : 0;
                }
                break;

            case THRESH_TRUNC:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    j = 0;
                    for( ; j <= roi.width - 8; j += 8 )
                    {
                        v_float32x4 v0, v1;
                        v0 = v_load( src + j );
                        v1 = v_load( src + j + 4 );
                        v0 = v_min( v0, thresh4 );
                        v1 = v_min( v1, thresh4 );
                        v_store( dst + j, v0 );
                        v_store( dst + j + 4, v1 );
                    }

                    for( ; j < roi.width; j++ )
                        dst[j] = std::min( src[j], thresh );
                }
                break;

            case THRESH_TOZERO:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    j = 0;
                    for( ; j <= roi.width - 8; j += 8 )
                    {
                        v_float32x4 v0, v1;
                        v0 = v_load( src + j );
                        v1 = v_load( src + j + 4 );
                        v0 = ( thresh4 < v0 ) & v0;
                        v1 = ( thresh4 < v1 ) & v1;
                        v_store( dst + j, v0 );
                        v_store( dst + j + 4, v1 );
                    }

                    for( ; j < roi.width; j++ )
                    {
                        float v = src[j];
                        dst[j] = v > thresh ? v : 0;
                    }
                }
                break;

            case THRESH_TOZERO_INV:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    j = 0;
                    for( ; j <= roi.width - 8; j += 8 )
                    {
                        v_float32x4 v0, v1;
                        v0 = v_load( src + j );
                        v1 = v_load( src + j + 4 );
                        v0 = ( v0 <= thresh4 ) & v0;
                        v1 = ( v1 <= thresh4 ) & v1;
                        v_store( dst + j, v0 );
                        v_store( dst + j + 4, v1 );
                    }

                    for( ; j < roi.width; j++ )
                    {
                        float v = src[j];
                        dst[j] = v <= thresh ? v : 0;
                    }
                }
                break;
            default:
                return CV_Error( CV_StsBadArg, "" );
        }
    }
    else
#endif
    {
        switch( type )
        {
            case THRESH_BINARY:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    for( j = 0; j < roi.width; j++ )
                        dst[j] = src[j] > thresh ? maxval : 0;
                }
                break;

            case THRESH_BINARY_INV:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    for( j = 0; j < roi.width; j++ )
                        dst[j] = src[j] <= thresh ? maxval : 0;
                }
                break;

            case THRESH_TRUNC:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    for( j = 0; j < roi.width; j++ )
                        dst[j] = std::min( src[j], thresh );
                }
                break;

            case THRESH_TOZERO:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    for( j = 0; j < roi.width; j++ )
                    {
                        float v = src[j];
                        dst[j] = v > thresh ? v : 0;
                    }
                }
                break;

            case THRESH_TOZERO_INV:
                for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
                {
                    for( j = 0; j < roi.width; j++ )
                    {
                        float v = src[j];
                        dst[j] = v <= thresh ? v : 0;
                    }
                }
                break;
            default:
                return CV_Error( CV_StsBadArg, "" );
        }
    }
}

static void
thresh_64f(const Mat& _src, Mat& _dst, double thresh, double maxval, int type)
{
    int i, j;
    Size roi = _src.size();
    roi.width *= _src.channels();
    const double* src = _src.ptr<double>();
    double* dst = _dst.ptr<double>();
    size_t src_step = _src.step / sizeof(src[0]);
    size_t dst_step = _dst.step / sizeof(dst[0]);

    if (_src.isContinuous() && _dst.isContinuous())
    {
        roi.width *= roi.height;
        roi.height = 1;
    }

#if CV_SIMD128_64F
    bool useSIMD = checkHardwareSupport( CV_CPU_SSE2 ) || checkHardwareSupport( CV_CPU_NEON );
    if( useSIMD )
    {
        v_float64x2 thresh2 = v_setall_f64( thresh );
        v_float64x2 maxval2 = v_setall_f64( maxval );

        switch( type )
        {
        case THRESH_BINARY:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 4; j += 4 )
                {
                    v_float64x2 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 2 );
                    v0 = thresh2 < v0;
                    v1 = thresh2 < v1;
                    v0 = v0 & maxval2;
                    v1 = v1 & maxval2;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 2, v1 );
                }

                for( ; j < roi.width; j++ )
                    dst[j] = src[j] > thresh ? maxval : 0;
            }
            break;

        case THRESH_BINARY_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 4; j += 4 )
                {
                    v_float64x2 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 2 );
                    v0 = v0 <= thresh2;
                    v1 = v1 <= thresh2;
                    v0 = v0 & maxval2;
                    v1 = v1 & maxval2;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 2, v1 );
                }

                for( ; j < roi.width; j++ )
                    dst[j] = src[j] <= thresh ? maxval : 0;
            }
            break;

        case THRESH_TRUNC:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 4; j += 4 )
                {
                    v_float64x2 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 2 );
                    v0 = v_min( v0, thresh2 );
                    v1 = v_min( v1, thresh2 );
                    v_store( dst + j, v0 );
                    v_store( dst + j + 2, v1 );
                }

                for( ; j < roi.width; j++ )
                    dst[j] = std::min( src[j], thresh );
            }
            break;

        case THRESH_TOZERO:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 4; j += 4 )
                {
                    v_float64x2 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 2 );
                    v0 = ( thresh2 < v0 ) & v0;
                    v1 = ( thresh2 < v1 ) & v1;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 2, v1 );
                }

                for( ; j < roi.width; j++ )
                {
                    double v = src[j];
                    dst[j] = v > thresh ? v : 0;
                }
            }
            break;

        case THRESH_TOZERO_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j <= roi.width - 4; j += 4 )
                {
                    v_float64x2 v0, v1;
                    v0 = v_load( src + j );
                    v1 = v_load( src + j + 2 );
                    v0 = ( v0 <= thresh2 ) & v0;
                    v1 = ( v1 <= thresh2 ) & v1;
                    v_store( dst + j, v0 );
                    v_store( dst + j + 2, v1 );
                }

                for( ; j < roi.width; j++ )
                {
                    double v = src[j];
                    dst[j] = v <= thresh ? v : 0;
                }
            }
            break;
        default:
            return CV_Error(CV_StsBadArg, "");
        }
    }
    else
#endif
    {
        switch( type )
        {
        case THRESH_BINARY:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j < roi.width; j++ )
                    dst[j] = src[j] > thresh ? maxval : 0;
            }
            break;

        case THRESH_BINARY_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j < roi.width; j++ )
                    dst[j] = src[j] <= thresh ? maxval : 0;
            }
            break;

        case THRESH_TRUNC:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j < roi.width; j++ )
                    dst[j] = std::min( src[j], thresh );
            }
            break;

        case THRESH_TOZERO:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j < roi.width; j++ )
                {
                    double v = src[j];
                    dst[j] = v > thresh ? v : 0;
                }
            }
            break;

        case THRESH_TOZERO_INV:
            for( i = 0; i < roi.height; i++, src += src_step, dst += dst_step )
            {
                j = 0;
                for( ; j < roi.width; j++ )
                {
                    double v = src[j];
                    dst[j] = v <= thresh ? v : 0;
                }
            }
            break;
        default:
            return CV_Error(CV_StsBadArg, "");
        }
    }
}

#ifdef HAVE_IPP
static bool ipp_getThreshVal_Otsu_8u( const unsigned char* _src, int step, Size size, unsigned char &thresh)
{
    CV_INSTRUMENT_REGION_IPP()

#if IPP_VERSION_X100 >= 810
    int ippStatus = -1;
    IppiSize srcSize = { size.width, size.height };
    CV_SUPPRESS_DEPRECATED_START
    ippStatus = CV_INSTRUMENT_FUN_IPP(ippiComputeThreshold_Otsu_8u_C1R, _src, step, srcSize, &thresh);
    CV_SUPPRESS_DEPRECATED_END

    if(ippStatus >= 0)
        return true;
#else
    CV_UNUSED(_src); CV_UNUSED(step); CV_UNUSED(size); CV_UNUSED(thresh);
#endif
    return false;
}
#endif

static double
getThreshVal_Otsu_8u( const Mat& _src )
{
    Size size = _src.size();
    int step = (int) _src.step;
    if( _src.isContinuous() )
    {
        size.width *= size.height;
        size.height = 1;
        step = size.width;
    }

#ifdef HAVE_IPP
    unsigned char thresh;
    CV_IPP_RUN(IPP_VERSION_X100 >= 810, ipp_getThreshVal_Otsu_8u(_src.ptr(), step, size, thresh), thresh);
#endif

    const int N = 256;
    int i, j, h[N] = {0};
    for( i = 0; i < size.height; i++ )
    {
        const uchar* src = _src.ptr() + step*i;
        j = 0;
        #if CV_ENABLE_UNROLLED
        for( ; j <= size.width - 4; j += 4 )
        {
            int v0 = src[j], v1 = src[j+1];
            h[v0]++; h[v1]++;
            v0 = src[j+2]; v1 = src[j+3];
            h[v0]++; h[v1]++;
        }
        #endif
        for( ; j < size.width; j++ )
            h[src[j]]++;
    }

    double mu = 0, scale = 1./(size.width*size.height);
    for( i = 0; i < N; i++ )
        mu += i*(double)h[i];

    mu *= scale;
    double mu1 = 0, q1 = 0;
    double max_sigma = 0, max_val = 0;

    for( i = 0; i < N; i++ )
    {
        double p_i, q2, mu2, sigma;

        p_i = h[i]*scale;
        mu1 *= q1;
        q1 += p_i;
        q2 = 1. - q1;

        if( std::min(q1,q2) < FLT_EPSILON || std::max(q1,q2) > 1. - FLT_EPSILON )
            continue;

        mu1 = (mu1 + i*p_i)/q1;
        mu2 = (mu - q1*mu1)/q2;
        sigma = q1*q2*(mu1 - mu2)*(mu1 - mu2);
        if( sigma > max_sigma )
        {
            max_sigma = sigma;
            max_val = i;
        }
    }

    return max_val;
}

static double
getThreshVal_Triangle_8u( const Mat& _src )
{
    Size size = _src.size();
    int step = (int) _src.step;
    if( _src.isContinuous() )
    {
        size.width *= size.height;
        size.height = 1;
        step = size.width;
    }

    const int N = 256;
    int i, j, h[N] = {0};
    for( i = 0; i < size.height; i++ )
    {
        const uchar* src = _src.ptr() + step*i;
        j = 0;
        #if CV_ENABLE_UNROLLED
        for( ; j <= size.width - 4; j += 4 )
        {
            int v0 = src[j], v1 = src[j+1];
            h[v0]++; h[v1]++;
            v0 = src[j+2]; v1 = src[j+3];
            h[v0]++; h[v1]++;
        }
        #endif
        for( ; j < size.width; j++ )
            h[src[j]]++;
    }

    int left_bound = 0, right_bound = 0, max_ind = 0, max = 0;
    int temp;
    bool isflipped = false;

    for( i = 0; i < N; i++ )
    {
        if( h[i] > 0 )
        {
            left_bound = i;
            break;
        }
    }
    if( left_bound > 0 )
        left_bound--;

    for( i = N-1; i > 0; i-- )
    {
        if( h[i] > 0 )
        {
            right_bound = i;
            break;
        }
    }
    if( right_bound < N-1 )
        right_bound++;

    for( i = 0; i < N; i++ )
    {
        if( h[i] > max)
        {
            max = h[i];
            max_ind = i;
        }
    }

    if( max_ind-left_bound < right_bound-max_ind)
    {
        isflipped = true;
        i = 0, j = N-1;
        while( i < j )
        {
            temp = h[i]; h[i] = h[j]; h[j] = temp;
            i++; j--;
        }
        left_bound = N-1-right_bound;
        max_ind = N-1-max_ind;
    }

    double thresh = left_bound;
    double a, b, dist = 0, tempdist;

    /*
     * We do not need to compute precise distance here. Distance is maximized, so some constants can
     * be omitted. This speeds up a computation a bit.
     */
    a = max; b = left_bound-max_ind;
    for( i = left_bound+1; i <= max_ind; i++ )
    {
        tempdist = a*i + b*h[i];
        if( tempdist > dist)
        {
            dist = tempdist;
            thresh = i;
        }
    }
    thresh--;

    if( isflipped )
        thresh = N-1-thresh;

    return thresh;
}

class ThresholdRunner : public ParallelLoopBody
{
public:
    ThresholdRunner(Mat _src, Mat _dst, double _thresh, double _maxval, int _thresholdType)
    {
        src = _src;
        dst = _dst;

        thresh = _thresh;
        maxval = _maxval;
        thresholdType = _thresholdType;
    }

    void operator () ( const Range& range ) const
    {
        int row0 = range.start;
        int row1 = range.end;

        Mat srcStripe = src.rowRange(row0, row1);
        Mat dstStripe = dst.rowRange(row0, row1);

        if (srcStripe.depth() == CV_8U)
        {
            thresh_8u( srcStripe, dstStripe, (uchar)thresh, (uchar)maxval, thresholdType );
        }
        else if( srcStripe.depth() == CV_16S )
        {
            thresh_16s( srcStripe, dstStripe, (short)thresh, (short)maxval, thresholdType );
        }
        else if( srcStripe.depth() == CV_32F )
        {
            thresh_32f( srcStripe, dstStripe, (float)thresh, (float)maxval, thresholdType );
        }
        else if( srcStripe.depth() == CV_64F )
        {
            thresh_64f(srcStripe, dstStripe, thresh, maxval, thresholdType);
        }
    }

private:
    Mat src;
    Mat dst;

    double thresh;
    double maxval;
    int thresholdType;
};

#ifdef HAVE_OPENCL

static bool ocl_threshold( InputArray _src, OutputArray _dst, double & thresh, double maxval, int thresh_type )
{
    int type = _src.type(), depth = CV_MAT_DEPTH(type), cn = CV_MAT_CN(type),
        kercn = ocl::predictOptimalVectorWidth(_src, _dst), ktype = CV_MAKE_TYPE(depth, kercn);
    bool doubleSupport = ocl::Device::getDefault().doubleFPConfig() > 0;

    if ( !(thresh_type == THRESH_BINARY || thresh_type == THRESH_BINARY_INV || thresh_type == THRESH_TRUNC ||
           thresh_type == THRESH_TOZERO || thresh_type == THRESH_TOZERO_INV) ||
         (!doubleSupport && depth == CV_64F))
        return false;

    const char * const thresholdMap[] = { "THRESH_BINARY", "THRESH_BINARY_INV", "THRESH_TRUNC",
                                          "THRESH_TOZERO", "THRESH_TOZERO_INV" };
    ocl::Device dev = ocl::Device::getDefault();
    int stride_size = dev.isIntel() && (dev.type() & ocl::Device::TYPE_GPU) ? 4 : 1;

    ocl::Kernel k("threshold", ocl::imgproc::threshold_oclsrc,
                  format("-D %s -D T=%s -D T1=%s -D STRIDE_SIZE=%d%s", thresholdMap[thresh_type],
                         ocl::typeToStr(ktype), ocl::typeToStr(depth), stride_size,
                         doubleSupport ? " -D DOUBLE_SUPPORT" : ""));
    if (k.empty())
        return false;

    UMat src = _src.getUMat();
    _dst.create(src.size(), type);
    UMat dst = _dst.getUMat();

    if (depth <= CV_32S)
        thresh = cvFloor(thresh);

    const double min_vals[] = { 0, CHAR_MIN, 0, SHRT_MIN, INT_MIN, -FLT_MAX, -DBL_MAX, 0 };
    double min_val = min_vals[depth];

    k.args(ocl::KernelArg::ReadOnlyNoSize(src), ocl::KernelArg::WriteOnly(dst, cn, kercn),
           ocl::KernelArg::Constant(Mat(1, 1, depth, Scalar::all(thresh))),
           ocl::KernelArg::Constant(Mat(1, 1, depth, Scalar::all(maxval))),
           ocl::KernelArg::Constant(Mat(1, 1, depth, Scalar::all(min_val))));

    size_t globalsize[2] = { (size_t)dst.cols * cn / kercn, (size_t)dst.rows };
    globalsize[1] = (globalsize[1] + stride_size - 1) / stride_size;
    return k.run(2, globalsize, NULL, false);
}

#endif


#ifdef HAVE_OPENVX
#define IMPL_OPENVX_TOZERO 1
static bool openvx_threshold(Mat src, Mat dst, int thresh, int maxval, int type)
{
    Mat a = src;

    int trueVal, falseVal;
    switch (type)
    {
    case THRESH_BINARY:
#ifndef VX_VERSION_1_1
        if (maxval != 255)
            return false;
#endif
        trueVal = maxval;
        falseVal = 0;
        break;
    case THRESH_TOZERO:
#if IMPL_OPENVX_TOZERO
        trueVal = 255;
        falseVal = 0;
        if (dst.data == src.data)
        {
            a = Mat(src.size(), src.type());
            src.copyTo(a);
        }
        break;
#endif
    case THRESH_BINARY_INV:
#ifdef VX_VERSION_1_1
        trueVal = 0;
        falseVal = maxval;
        break;
#endif
    case THRESH_TOZERO_INV:
#ifdef VX_VERSION_1_1
#if IMPL_OPENVX_TOZERO
        trueVal = 0;
        falseVal = 255;
        if (dst.data == src.data)
        {
            a = Mat(src.size(), src.type());
            src.copyTo(a);
        }
        break;
#endif
#endif
    case THRESH_TRUNC:
    default:
        return false;
    }

    try
    {
        ivx::Context ctx = ivx::Context::create();

        ivx::Threshold thh = ivx::Threshold::createBinary(ctx, VX_TYPE_UINT8, thresh);
        thh.setValueTrue(trueVal);
        thh.setValueFalse(falseVal);

        ivx::Image
            ia = ivx::Image::createFromHandle(ctx, VX_DF_IMAGE_U8,
                ivx::Image::createAddressing(a.cols*a.channels(), a.rows, 1, (vx_int32)(a.step)), src.data),
            ib = ivx::Image::createFromHandle(ctx, VX_DF_IMAGE_U8,
                ivx::Image::createAddressing(dst.cols*dst.channels(), dst.rows, 1, (vx_int32)(dst.step)), dst.data);

        ivx::IVX_CHECK_STATUS(vxuThreshold(ctx, ia, thh, ib));
#if IMPL_OPENVX_TOZERO
        if (type == THRESH_TOZERO || type == THRESH_TOZERO_INV)
        {
            ivx::Image
                ic = ivx::Image::createFromHandle(ctx, VX_DF_IMAGE_U8,
                    ivx::Image::createAddressing(dst.cols*dst.channels(), dst.rows, 1, (vx_int32)(dst.step)), dst.data);
            ivx::IVX_CHECK_STATUS(vxuAnd(ctx, ib, ia, ic));
        }
#endif
    }
    catch (ivx::RuntimeError & e)
    {
        VX_DbgThrow(e.what());
    }
    catch (ivx::WrapperError & e)
    {
        VX_DbgThrow(e.what());
    }

    return true;
}
#endif

}

double cv::threshold( InputArray _src, OutputArray _dst, double thresh, double maxval, int type )
{
    CV_INSTRUMENT_REGION()

    CV_OCL_RUN_(_src.dims() <= 2 && _dst.isUMat(),
                ocl_threshold(_src, _dst, thresh, maxval, type), thresh)

    Mat src = _src.getMat();
    int automatic_thresh = (type & ~CV_THRESH_MASK);
    type &= THRESH_MASK;

    CV_Assert( automatic_thresh != (CV_THRESH_OTSU | CV_THRESH_TRIANGLE) );
    if( automatic_thresh == CV_THRESH_OTSU )
    {
        CV_Assert( src.type() == CV_8UC1 );
        thresh = getThreshVal_Otsu_8u( src );
    }
    else if( automatic_thresh == CV_THRESH_TRIANGLE )
    {
        CV_Assert( src.type() == CV_8UC1 );
        thresh = getThreshVal_Triangle_8u( src );
    }

    _dst.create( src.size(), src.type() );
    Mat dst = _dst.getMat();

    if( src.depth() == CV_8U )
    {
        int ithresh = cvFloor(thresh);
        thresh = ithresh;
        int imaxval = cvRound(maxval);
        if( type == THRESH_TRUNC )
            imaxval = ithresh;
        imaxval = saturate_cast<uchar>(imaxval);

        if( ithresh < 0 || ithresh >= 255 )
        {
            if( type == THRESH_BINARY || type == THRESH_BINARY_INV ||
                ((type == THRESH_TRUNC || type == THRESH_TOZERO_INV) && ithresh < 0) ||
                (type == THRESH_TOZERO && ithresh >= 255) )
            {
                int v = type == THRESH_BINARY ? (ithresh >= 255 ? 0 : imaxval) :
                        type == THRESH_BINARY_INV ? (ithresh >= 255 ? imaxval : 0) :
                        /*type == THRESH_TRUNC ? imaxval :*/ 0;
                dst.setTo(v);
            }
            else
                src.copyTo(dst);
            return thresh;
        }

       CV_OVX_RUN(true,
                  openvx_threshold(src, dst, ithresh, imaxval, type), (double)ithresh)

        thresh = ithresh;
        maxval = imaxval;
    }
    else if( src.depth() == CV_16S )
    {
        int ithresh = cvFloor(thresh);
        thresh = ithresh;
        int imaxval = cvRound(maxval);
        if( type == THRESH_TRUNC )
            imaxval = ithresh;
        imaxval = saturate_cast<short>(imaxval);

        if( ithresh < SHRT_MIN || ithresh >= SHRT_MAX )
        {
            if( type == THRESH_BINARY || type == THRESH_BINARY_INV ||
               ((type == THRESH_TRUNC || type == THRESH_TOZERO_INV) && ithresh < SHRT_MIN) ||
               (type == THRESH_TOZERO && ithresh >= SHRT_MAX) )
            {
                int v = type == THRESH_BINARY ? (ithresh >= SHRT_MAX ? 0 : imaxval) :
                type == THRESH_BINARY_INV ? (ithresh >= SHRT_MAX ? imaxval : 0) :
                /*type == THRESH_TRUNC ? imaxval :*/ 0;
                dst.setTo(v);
            }
            else
                src.copyTo(dst);
            return thresh;
        }
        thresh = ithresh;
        maxval = imaxval;
    }
    else if( src.depth() == CV_32F )
        ;
    else if( src.depth() == CV_64F )
        ;
    else
        CV_Error( CV_StsUnsupportedFormat, "" );

    parallel_for_(Range(0, dst.rows),
                  ThresholdRunner(src, dst, thresh, maxval, type),
                  dst.total()/(double)(1<<16));
    return thresh;
}


void cv::adaptiveThreshold( InputArray _src, OutputArray _dst, double maxValue,
                            int method, int type, int blockSize, double delta )
{
    CV_INSTRUMENT_REGION()

    Mat src = _src.getMat();
    CV_Assert( src.type() == CV_8UC1 );
    CV_Assert( blockSize % 2 == 1 && blockSize > 1 );
    Size size = src.size();

    _dst.create( size, src.type() );
    Mat dst = _dst.getMat();

    if( maxValue < 0 )
    {
        dst = Scalar(0);
        return;
    }

    Mat mean;

    if( src.data != dst.data )
        mean = dst;

    if (method == ADAPTIVE_THRESH_MEAN_C)
        boxFilter( src, mean, src.type(), Size(blockSize, blockSize),
                   Point(-1,-1), true, BORDER_REPLICATE );
    else if (method == ADAPTIVE_THRESH_GAUSSIAN_C)
    {
        Mat srcfloat,meanfloat;
        src.convertTo(srcfloat,CV_32F);
        meanfloat=srcfloat;
        GaussianBlur(srcfloat, meanfloat, Size(blockSize, blockSize), 0, 0, BORDER_REPLICATE);
        meanfloat.convertTo(mean, src.type());
    }
    else
        CV_Error( CV_StsBadFlag, "Unknown/unsupported adaptive threshold method" );

    int i, j;
    uchar imaxval = saturate_cast<uchar>(maxValue);
    int idelta = type == THRESH_BINARY ? cvCeil(delta) : cvFloor(delta);
    uchar tab[768];

    if( type == CV_THRESH_BINARY )
        for( i = 0; i < 768; i++ )
            tab[i] = (uchar)(i - 255 > -idelta ? imaxval : 0);
    else if( type == CV_THRESH_BINARY_INV )
        for( i = 0; i < 768; i++ )
            tab[i] = (uchar)(i - 255 <= -idelta ? imaxval : 0);
    else
        CV_Error( CV_StsBadFlag, "Unknown/unsupported threshold type" );

    if( src.isContinuous() && mean.isContinuous() && dst.isContinuous() )
    {
        size.width *= size.height;
        size.height = 1;
    }

    for( i = 0; i < size.height; i++ )
    {
        const uchar* sdata = src.ptr(i);
        const uchar* mdata = mean.ptr(i);
        uchar* ddata = dst.ptr(i);

        for( j = 0; j < size.width; j++ )
            ddata[j] = tab[sdata[j] - mdata[j] + 255];
    }
}

CV_IMPL double
cvThreshold( const void* srcarr, void* dstarr, double thresh, double maxval, int type )
{
    cv::Mat src = cv::cvarrToMat(srcarr), dst = cv::cvarrToMat(dstarr), dst0 = dst;

    CV_Assert( src.size == dst.size && src.channels() == dst.channels() &&
        (src.depth() == dst.depth() || dst.depth() == CV_8U));

    thresh = cv::threshold( src, dst, thresh, maxval, type );
    if( dst0.data != dst.data )
        dst.convertTo( dst0, dst0.depth() );
    return thresh;
}


CV_IMPL void
cvAdaptiveThreshold( const void *srcIm, void *dstIm, double maxValue,
                     int method, int type, int blockSize, double delta )
{
    cv::Mat src = cv::cvarrToMat(srcIm), dst = cv::cvarrToMat(dstIm);
    CV_Assert( src.size == dst.size && src.type() == dst.type() );
    cv::adaptiveThreshold( src, dst, maxValue, method, type, blockSize, delta );
}

/* End of file. */
