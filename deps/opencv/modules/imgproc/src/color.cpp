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
// Copyright (C) 2009-2010, Willow Garage Inc., all rights reserved.
// Copyright (C) 2014-2015, Itseez Inc., all rights reserved.
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

/********************************* COPYRIGHT NOTICE *******************************\
  The function for RGB to Lab conversion is based on the MATLAB script
  RGB2Lab.m translated by Mark Ruzon from C code by Yossi Rubner, 23 September 1997.
  See the page [http://vision.stanford.edu/~ruzon/software/rgblab.html]
\**********************************************************************************/

/********************************* COPYRIGHT NOTICE *******************************\
  Original code for Bayer->BGR/RGB conversion is provided by Dirk Schaefer
  from MD-Mathematische Dienste GmbH. Below is the copyright notice:

    IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
    By downloading, copying, installing or using the software you agree
    to this license. If you do not agree to this license, do not download,
    install, copy or use the software.

    Contributors License Agreement:

      Copyright (c) 2002,
      MD-Mathematische Dienste GmbH
      Im Defdahl 5-10
      44141 Dortmund
      Germany
      www.md-it.de

    Redistribution and use in source and binary forms,
    with or without modification, are permitted provided
    that the following conditions are met:

    Redistributions of source code must retain
    the above copyright notice, this list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
    The name of Contributor may not be used to endorse or promote products
    derived from this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
    THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
    PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
    OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
    THE POSSIBILITY OF SUCH DAMAGE.
\**********************************************************************************/

#include "precomp.hpp"
#include "opencl_kernels_imgproc.hpp"
#include <limits>
#include "hal_replacement.hpp"

#define  CV_DESCALE(x,n)     (((x) + (1 << ((n)-1))) >> (n))

#if defined (HAVE_IPP) && (IPP_VERSION_X100 >= 700)
#define MAX_IPP8u   255
#define MAX_IPP16u  65535
#define MAX_IPP32f  1.0
#endif

namespace cv
{
//constants for conversion from/to RGB and Gray, YUV, YCrCb according to BT.601
const float B2YF = 0.114f;
const float G2YF = 0.587f;
const float R2YF = 0.299f;
//to YCbCr
const float YCBF = 0.564f; // == 1/2/(1-B2YF)
const float YCRF = 0.713f; // == 1/2/(1-R2YF)
const int YCBI = 9241;  // == YCBF*16384
const int YCRI = 11682; // == YCRF*16384
//to YUV
const float B2UF = 0.492f;
const float R2VF = 0.877f;
const int B2UI = 8061;  // == B2UF*16384
const int R2VI = 14369; // == R2VF*16384
//from YUV
const float U2BF = 2.032f;
const float U2GF = -0.395f;
const float V2GF = -0.581f;
const float V2RF = 1.140f;
const int U2BI = 33292;
const int U2GI = -6472;
const int V2GI = -9519;
const int V2RI = 18678;
//from YCrCb
const float CB2BF = 1.773f;
const float CB2GF = -0.344f;
const float CR2GF = -0.714f;
const float CR2RF = 1.403f;
const int CB2BI = 29049;
const int CB2GI = -5636;
const int CR2GI = -11698;
const int CR2RI = 22987;


// computes cubic spline coefficients for a function: (xi=i, yi=f[i]), i=0..n
template<typename _Tp> static void splineBuild(const _Tp* f, int n, _Tp* tab)
{
    _Tp cn = 0;
    int i;
    tab[0] = tab[1] = (_Tp)0;

    for(i = 1; i < n-1; i++)
    {
        _Tp t = 3*(f[i+1] - 2*f[i] + f[i-1]);
        _Tp l = 1/(4 - tab[(i-1)*4]);
        tab[i*4] = l; tab[i*4+1] = (t - tab[(i-1)*4+1])*l;
    }

    for(i = n-1; i >= 0; i--)
    {
        _Tp c = tab[i*4+1] - tab[i*4]*cn;
        _Tp b = f[i+1] - f[i] - (cn + c*2)*(_Tp)0.3333333333333333;
        _Tp d = (cn - c)*(_Tp)0.3333333333333333;
        tab[i*4] = f[i]; tab[i*4+1] = b;
        tab[i*4+2] = c; tab[i*4+3] = d;
        cn = c;
    }
}

// interpolates value of a function at x, 0 <= x <= n using a cubic spline.
template<typename _Tp> static inline _Tp splineInterpolate(_Tp x, const _Tp* tab, int n)
{
    // don't touch this function without urgent need - some versions of gcc fail to inline it correctly
    int ix = std::min(std::max(int(x), 0), n-1);
    x -= ix;
    tab += ix*4;
    return ((tab[3]*x + tab[2])*x + tab[1])*x + tab[0];
}

#if CV_NEON
template<typename _Tp> static inline void splineInterpolate(float32x4_t& v_x, const _Tp* tab, int n)
{
    int32x4_t v_ix = vcvtq_s32_f32(vminq_f32(vmaxq_f32(v_x, vdupq_n_f32(0)), vdupq_n_f32(n - 1)));
    v_x = vsubq_f32(v_x, vcvtq_f32_s32(v_ix));
    v_ix = vshlq_n_s32(v_ix, 2);

    int CV_DECL_ALIGNED(16) ix[4];
    vst1q_s32(ix, v_ix);

    float32x4_t v_tab0 = vld1q_f32(tab + ix[0]);
    float32x4_t v_tab1 = vld1q_f32(tab + ix[1]);
    float32x4_t v_tab2 = vld1q_f32(tab + ix[2]);
    float32x4_t v_tab3 = vld1q_f32(tab + ix[3]);

    float32x4x2_t v01 = vtrnq_f32(v_tab0, v_tab1);
    float32x4x2_t v23 = vtrnq_f32(v_tab2, v_tab3);

    v_tab0 = vcombine_f32(vget_low_f32(v01.val[0]), vget_low_f32(v23.val[0]));
    v_tab1 = vcombine_f32(vget_low_f32(v01.val[1]), vget_low_f32(v23.val[1]));
    v_tab2 = vcombine_f32(vget_high_f32(v01.val[0]), vget_high_f32(v23.val[0]));
    v_tab3 = vcombine_f32(vget_high_f32(v01.val[1]), vget_high_f32(v23.val[1]));

    v_x = vmlaq_f32(v_tab0, vmlaq_f32(v_tab1, vmlaq_f32(v_tab2, v_tab3, v_x), v_x), v_x);
}
#elif CV_SSE2
template<typename _Tp> static inline void splineInterpolate(__m128& v_x, const _Tp* tab, int n)
{
    __m128i v_ix = _mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(v_x, _mm_setzero_ps()), _mm_set1_ps(float(n - 1))));
    v_x = _mm_sub_ps(v_x, _mm_cvtepi32_ps(v_ix));
    v_ix = _mm_slli_epi32(v_ix, 2);

    int CV_DECL_ALIGNED(16) ix[4];
    _mm_store_si128((__m128i *)ix, v_ix);

    __m128 v_tab0 = _mm_loadu_ps(tab + ix[0]);
    __m128 v_tab1 = _mm_loadu_ps(tab + ix[1]);
    __m128 v_tab2 = _mm_loadu_ps(tab + ix[2]);
    __m128 v_tab3 = _mm_loadu_ps(tab + ix[3]);

    __m128 v_tmp0 = _mm_unpacklo_ps(v_tab0, v_tab1);
    __m128 v_tmp1 = _mm_unpacklo_ps(v_tab2, v_tab3);
    __m128 v_tmp2 = _mm_unpackhi_ps(v_tab0, v_tab1);
    __m128 v_tmp3 = _mm_unpackhi_ps(v_tab2, v_tab3);

    v_tab0 = _mm_shuffle_ps(v_tmp0, v_tmp1, 0x44);
    v_tab2 = _mm_shuffle_ps(v_tmp2, v_tmp3, 0x44);
    v_tab1 = _mm_shuffle_ps(v_tmp0, v_tmp1, 0xee);
    v_tab3 = _mm_shuffle_ps(v_tmp2, v_tmp3, 0xee);

    __m128 v_l = _mm_mul_ps(v_x, v_tab3);
    v_l = _mm_add_ps(v_l, v_tab2);
    v_l = _mm_mul_ps(v_l, v_x);
    v_l = _mm_add_ps(v_l, v_tab1);
    v_l = _mm_mul_ps(v_l, v_x);
    v_x = _mm_add_ps(v_l, v_tab0);
}
#endif

template<typename _Tp> struct ColorChannel
{
    typedef float worktype_f;
    static _Tp max() { return std::numeric_limits<_Tp>::max(); }
    static _Tp half() { return (_Tp)(max()/2 + 1); }
};

template<> struct ColorChannel<float>
{
    typedef float worktype_f;
    static float max() { return 1.f; }
    static float half() { return 0.5f; }
};

/*template<> struct ColorChannel<double>
{
    typedef double worktype_f;
    static double max() { return 1.; }
    static double half() { return 0.5; }
};*/


///////////////////////////// Top-level template function ////////////////////////////////

template <typename Cvt>
class CvtColorLoop_Invoker : public ParallelLoopBody
{
    typedef typename Cvt::channel_type _Tp;
public:

    CvtColorLoop_Invoker(const uchar * src_data_, size_t src_step_, uchar * dst_data_, size_t dst_step_, int width_, const Cvt& _cvt) :
        ParallelLoopBody(), src_data(src_data_), src_step(src_step_), dst_data(dst_data_), dst_step(dst_step_),
        width(width_), cvt(_cvt)
    {
    }

    virtual void operator()(const Range& range) const
    {
        const uchar* yS = src_data + static_cast<size_t>(range.start) * src_step;
        uchar* yD = dst_data + static_cast<size_t>(range.start) * dst_step;

        for( int i = range.start; i < range.end; ++i, yS += src_step, yD += dst_step )
            cvt(reinterpret_cast<const _Tp*>(yS), reinterpret_cast<_Tp*>(yD), width);
    }

private:
    const uchar * src_data;
    size_t src_step;
    uchar * dst_data;
    size_t dst_step;
    int width;
    const Cvt& cvt;

    const CvtColorLoop_Invoker& operator= (const CvtColorLoop_Invoker&);
};

template <typename Cvt>
void CvtColorLoop(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, const Cvt& cvt)
{
    parallel_for_(Range(0, height),
                  CvtColorLoop_Invoker<Cvt>(src_data, src_step, dst_data, dst_step, width, cvt),
                  (width * height) / static_cast<double>(1<<16));
}

#if defined (HAVE_IPP) && (IPP_VERSION_X100 >= 700)

typedef IppStatus (CV_STDCALL* ippiReorderFunc)(const void *, int, void *, int, IppiSize, const int *);
typedef IppStatus (CV_STDCALL* ippiGeneralFunc)(const void *, int, void *, int, IppiSize);
typedef IppStatus (CV_STDCALL* ippiColor2GrayFunc)(const void *, int, void *, int, IppiSize, const Ipp32f *);

template <typename Cvt>
class CvtColorIPPLoop_Invoker :
        public ParallelLoopBody
{
public:

    CvtColorIPPLoop_Invoker(const uchar * src_data_, size_t src_step_, uchar * dst_data_, size_t dst_step_, int width_, const Cvt& _cvt, bool *_ok) :
        ParallelLoopBody(), src_data(src_data_), src_step(src_step_), dst_data(dst_data_), dst_step(dst_step_), width(width_), cvt(_cvt), ok(_ok)
    {
        *ok = true;
    }

    virtual void operator()(const Range& range) const
    {
        const void *yS = src_data + src_step * range.start;
        void *yD = dst_data + dst_step * range.start;
        if( !cvt(yS, static_cast<int>(src_step), yD, static_cast<int>(dst_step), width, range.end - range.start) )
            *ok = false;
        else
        {
            CV_IMPL_ADD(CV_IMPL_IPP|CV_IMPL_MT);
        }
    }

private:
    const uchar * src_data;
    size_t src_step;
    uchar * dst_data;
    size_t dst_step;
    int width;
    const Cvt& cvt;
    bool *ok;

    const CvtColorIPPLoop_Invoker& operator= (const CvtColorIPPLoop_Invoker&);
};

template <typename Cvt>
bool CvtColorIPPLoop(const uchar * src_data, size_t src_step, uchar * dst_data, size_t dst_step, int width, int height, const Cvt& cvt)
{
    bool ok;
    parallel_for_(Range(0, height), CvtColorIPPLoop_Invoker<Cvt>(src_data, src_step, dst_data, dst_step, width, cvt, &ok), (width * height)/(double)(1<<16) );
    return ok;
}

template <typename Cvt>
bool CvtColorIPPLoopCopy(const uchar * src_data, size_t src_step, int src_type, uchar * dst_data, size_t dst_step, int width, int height, const Cvt& cvt)
{
    Mat temp;
    Mat src(Size(width, height), src_type, const_cast<uchar*>(src_data), src_step);
    Mat source = src;
    if( src_data == dst_data )
    {
        src.copyTo(temp);
        source = temp;
    }
    bool ok;
    parallel_for_(Range(0, source.rows),
                  CvtColorIPPLoop_Invoker<Cvt>(source.data, source.step, dst_data, dst_step,
                                               source.cols, cvt, &ok),
                  source.total()/(double)(1<<16) );
    return ok;
}

static IppStatus CV_STDCALL ippiSwapChannels_8u_C3C4Rf(const Ipp8u* pSrc, int srcStep, Ipp8u* pDst, int dstStep,
         IppiSize roiSize, const int *dstOrder)
{
    return CV_INSTRUMENT_FUN_IPP(ippiSwapChannels_8u_C3C4R, pSrc, srcStep, pDst, dstStep, roiSize, dstOrder, MAX_IPP8u);
}

static IppStatus CV_STDCALL ippiSwapChannels_16u_C3C4Rf(const Ipp16u* pSrc, int srcStep, Ipp16u* pDst, int dstStep,
         IppiSize roiSize, const int *dstOrder)
{
    return CV_INSTRUMENT_FUN_IPP(ippiSwapChannels_16u_C3C4R, pSrc, srcStep, pDst, dstStep, roiSize, dstOrder, MAX_IPP16u);
}

static IppStatus CV_STDCALL ippiSwapChannels_32f_C3C4Rf(const Ipp32f* pSrc, int srcStep, Ipp32f* pDst, int dstStep,
         IppiSize roiSize, const int *dstOrder)
{
    return CV_INSTRUMENT_FUN_IPP(ippiSwapChannels_32f_C3C4R, pSrc, srcStep, pDst, dstStep, roiSize, dstOrder, MAX_IPP32f);
}

static ippiReorderFunc ippiSwapChannelsC3C4RTab[] =
{
    (ippiReorderFunc)ippiSwapChannels_8u_C3C4Rf, 0, (ippiReorderFunc)ippiSwapChannels_16u_C3C4Rf, 0,
    0, (ippiReorderFunc)ippiSwapChannels_32f_C3C4Rf, 0, 0
};

static ippiGeneralFunc ippiCopyAC4C3RTab[] =
{
    (ippiGeneralFunc)ippiCopy_8u_AC4C3R, 0, (ippiGeneralFunc)ippiCopy_16u_AC4C3R, 0,
    0, (ippiGeneralFunc)ippiCopy_32f_AC4C3R, 0, 0
};

static ippiReorderFunc ippiSwapChannelsC4C3RTab[] =
{
    (ippiReorderFunc)ippiSwapChannels_8u_C4C3R, 0, (ippiReorderFunc)ippiSwapChannels_16u_C4C3R, 0,
    0, (ippiReorderFunc)ippiSwapChannels_32f_C4C3R, 0, 0
};

static ippiReorderFunc ippiSwapChannelsC3RTab[] =
{
    (ippiReorderFunc)ippiSwapChannels_8u_C3R, 0, (ippiReorderFunc)ippiSwapChannels_16u_C3R, 0,
    0, (ippiReorderFunc)ippiSwapChannels_32f_C3R, 0, 0
};

#if IPP_VERSION_X100 >= 810
static ippiReorderFunc ippiSwapChannelsC4RTab[] =
{
    (ippiReorderFunc)ippiSwapChannels_8u_C4R, 0, (ippiReorderFunc)ippiSwapChannels_16u_C4R, 0,
    0, (ippiReorderFunc)ippiSwapChannels_32f_C4R, 0, 0
};
#endif

static ippiColor2GrayFunc ippiColor2GrayC3Tab[] =
{
    (ippiColor2GrayFunc)ippiColorToGray_8u_C3C1R, 0, (ippiColor2GrayFunc)ippiColorToGray_16u_C3C1R, 0,
    0, (ippiColor2GrayFunc)ippiColorToGray_32f_C3C1R, 0, 0
};

static ippiColor2GrayFunc ippiColor2GrayC4Tab[] =
{
    (ippiColor2GrayFunc)ippiColorToGray_8u_AC4C1R, 0, (ippiColor2GrayFunc)ippiColorToGray_16u_AC4C1R, 0,
    0, (ippiColor2GrayFunc)ippiColorToGray_32f_AC4C1R, 0, 0
};

static ippiGeneralFunc ippiRGB2GrayC3Tab[] =
{
    (ippiGeneralFunc)ippiRGBToGray_8u_C3C1R, 0, (ippiGeneralFunc)ippiRGBToGray_16u_C3C1R, 0,
    0, (ippiGeneralFunc)ippiRGBToGray_32f_C3C1R, 0, 0
};

static ippiGeneralFunc ippiRGB2GrayC4Tab[] =
{
    (ippiGeneralFunc)ippiRGBToGray_8u_AC4C1R, 0, (ippiGeneralFunc)ippiRGBToGray_16u_AC4C1R, 0,
    0, (ippiGeneralFunc)ippiRGBToGray_32f_AC4C1R, 0, 0
};

static ippiGeneralFunc ippiCopyP3C3RTab[] =
{
    (ippiGeneralFunc)ippiCopy_8u_P3C3R, 0, (ippiGeneralFunc)ippiCopy_16u_P3C3R, 0,
    0, (ippiGeneralFunc)ippiCopy_32f_P3C3R, 0, 0
};

static ippiGeneralFunc ippiRGB2XYZTab[] =
{
    (ippiGeneralFunc)ippiRGBToXYZ_8u_C3R, 0, (ippiGeneralFunc)ippiRGBToXYZ_16u_C3R, 0,
    0, (ippiGeneralFunc)ippiRGBToXYZ_32f_C3R, 0, 0
};

static ippiGeneralFunc ippiXYZ2RGBTab[] =
{
    (ippiGeneralFunc)ippiXYZToRGB_8u_C3R, 0, (ippiGeneralFunc)ippiXYZToRGB_16u_C3R, 0,
    0, (ippiGeneralFunc)ippiXYZToRGB_32f_C3R, 0, 0
};

#if IPP_DISABLE_BLOCK
static ippiGeneralFunc ippiRGB2HSVTab[] =
{
    (ippiGeneralFunc)ippiRGBToHSV_8u_C3R, 0, (ippiGeneralFunc)ippiRGBToHSV_16u_C3R, 0,
    0, 0, 0, 0
};
#endif

static ippiGeneralFunc ippiHSV2RGBTab[] =
{
    (ippiGeneralFunc)ippiHSVToRGB_8u_C3R, 0, (ippiGeneralFunc)ippiHSVToRGB_16u_C3R, 0,
    0, 0, 0, 0
};

static ippiGeneralFunc ippiRGB2HLSTab[] =
{
    (ippiGeneralFunc)ippiRGBToHLS_8u_C3R, 0, (ippiGeneralFunc)ippiRGBToHLS_16u_C3R, 0,
    0, (ippiGeneralFunc)ippiRGBToHLS_32f_C3R, 0, 0
};

static ippiGeneralFunc ippiHLS2RGBTab[] =
{
    (ippiGeneralFunc)ippiHLSToRGB_8u_C3R, 0, (ippiGeneralFunc)ippiHLSToRGB_16u_C3R, 0,
    0, (ippiGeneralFunc)ippiHLSToRGB_32f_C3R, 0, 0
};

#if IPP_DISABLE_BLOCK
static ippiGeneralFunc ippiRGBToLUVTab[] =
{
    (ippiGeneralFunc)ippiRGBToLUV_8u_C3R, 0, (ippiGeneralFunc)ippiRGBToLUV_16u_C3R, 0,
    0, (ippiGeneralFunc)ippiRGBToLUV_32f_C3R, 0, 0
};

static ippiGeneralFunc ippiLUVToRGBTab[] =
{
    (ippiGeneralFunc)ippiLUVToRGB_8u_C3R, 0, (ippiGeneralFunc)ippiLUVToRGB_16u_C3R, 0,
    0, (ippiGeneralFunc)ippiLUVToRGB_32f_C3R, 0, 0
};
#endif

struct IPPGeneralFunctor
{
    IPPGeneralFunctor(ippiGeneralFunc _func) : ippiColorConvertGeneral(_func){}
    bool operator()(const void *src, int srcStep, void *dst, int dstStep, int cols, int rows) const
    {
        return ippiColorConvertGeneral ? CV_INSTRUMENT_FUN_IPP(ippiColorConvertGeneral, src, srcStep, dst, dstStep, ippiSize(cols, rows)) >= 0 : false;
    }
private:
    ippiGeneralFunc ippiColorConvertGeneral;
};

struct IPPReorderFunctor
{
    IPPReorderFunctor(ippiReorderFunc _func, int _order0, int _order1, int _order2) : ippiColorConvertReorder(_func)
    {
        order[0] = _order0;
        order[1] = _order1;
        order[2] = _order2;
        order[3] = 3;
    }
    bool operator()(const void *src, int srcStep, void *dst, int dstStep, int cols, int rows) const
    {
        return ippiColorConvertReorder ? CV_INSTRUMENT_FUN_IPP(ippiColorConvertReorder, src, srcStep, dst, dstStep, ippiSize(cols, rows), order) >= 0 : false;
    }
private:
    ippiReorderFunc ippiColorConvertReorder;
    int order[4];
};

struct IPPColor2GrayFunctor
{
    IPPColor2GrayFunctor(ippiColor2GrayFunc _func) :
        ippiColorToGray(_func)
    {
        coeffs[0] = B2YF;
        coeffs[1] = G2YF;
        coeffs[2] = R2YF;
    }
    bool operator()(const void *src, int srcStep, void *dst, int dstStep, int cols, int rows) const
    {
        return ippiColorToGray ? CV_INSTRUMENT_FUN_IPP(ippiColorToGray, src, srcStep, dst, dstStep, ippiSize(cols, rows), coeffs) >= 0 : false;
    }
private:
    ippiColor2GrayFunc ippiColorToGray;
    Ipp32f coeffs[3];
};

struct IPPGray2BGRFunctor
{
    IPPGray2BGRFunctor(ippiGeneralFunc _func) :
        ippiGrayToBGR(_func)
    {
    }

    bool operator()(const void *src, int srcStep, void *dst, int dstStep, int cols, int rows) const
    {
        if (ippiGrayToBGR == 0)
            return false;

        const void* srcarray[3] = { src, src, src };
        return CV_INSTRUMENT_FUN_IPP(ippiGrayToBGR, srcarray, srcStep, dst, dstStep, ippiSize(cols, rows)) >= 0;
    }
private:
    ippiGeneralFunc ippiGrayToBGR;
};

struct IPPGray2BGRAFunctor
{
    IPPGray2BGRAFunctor(ippiGeneralFunc _func1, ippiReorderFunc _func2, int _depth) :
        ippiColorConvertGeneral(_func1), ippiColorConvertReorder(_func2), depth(_depth)
    {
    }

    bool operator()(const void *src, int srcStep, void *dst, int dstStep, int cols, int rows) const
    {
        if (ippiColorConvertGeneral == 0 || ippiColorConvertReorder == 0)
            return false;

        const void* srcarray[3] = { src, src, src };
        Mat temp(rows, cols, CV_MAKETYPE(depth, 3));
        if(CV_INSTRUMENT_FUN_IPP(ippiColorConvertGeneral, srcarray, srcStep, temp.ptr(), (int)temp.step[0], ippiSize(cols, rows)) < 0)
            return false;
        int order[4] = {0, 1, 2, 3};
        return CV_INSTRUMENT_FUN_IPP(ippiColorConvertReorder, temp.ptr(), (int)temp.step[0], dst, dstStep, ippiSize(cols, rows), order) >= 0;
    }
private:
    ippiGeneralFunc ippiColorConvertGeneral;
    ippiReorderFunc ippiColorConvertReorder;
    int depth;
};

struct IPPReorderGeneralFunctor
{
    IPPReorderGeneralFunctor(ippiReorderFunc _func1, ippiGeneralFunc _func2, int _order0, int _order1, int _order2, int _depth) :
        ippiColorConvertReorder(_func1), ippiColorConvertGeneral(_func2), depth(_depth)
    {
        order[0] = _order0;
        order[1] = _order1;
        order[2] = _order2;
        order[3] = 3;
    }
    bool operator()(const void *src, int srcStep, void *dst, int dstStep, int cols, int rows) const
    {
        if (ippiColorConvertReorder == 0 || ippiColorConvertGeneral == 0)
            return false;

        Mat temp;
        temp.create(rows, cols, CV_MAKETYPE(depth, 3));
        if(CV_INSTRUMENT_FUN_IPP(ippiColorConvertReorder, src, srcStep, temp.ptr(), (int)temp.step[0], ippiSize(cols, rows), order) < 0)
            return false;
        return CV_INSTRUMENT_FUN_IPP(ippiColorConvertGeneral, temp.ptr(), (int)temp.step[0], dst, dstStep, ippiSize(cols, rows)) >= 0;
    }
private:
    ippiReorderFunc ippiColorConvertReorder;
    ippiGeneralFunc ippiColorConvertGeneral;
    int order[4];
    int depth;
};

struct IPPGeneralReorderFunctor
{
    IPPGeneralReorderFunctor(ippiGeneralFunc _func1, ippiReorderFunc _func2, int _order0, int _order1, int _order2, int _depth) :
        ippiColorConvertGeneral(_func1), ippiColorConvertReorder(_func2), depth(_depth)
    {
        order[0] = _order0;
        order[1] = _order1;
        order[2] = _order2;
        order[3] = 3;
    }
    bool operator()(const void *src, int srcStep, void *dst, int dstStep, int cols, int rows) const
    {
        if (ippiColorConvertGeneral == 0 || ippiColorConvertReorder == 0)
            return false;

        Mat temp;
        temp.create(rows, cols, CV_MAKETYPE(depth, 3));
        if(CV_INSTRUMENT_FUN_IPP(ippiColorConvertGeneral, src, srcStep, temp.ptr(), (int)temp.step[0], ippiSize(cols, rows)) < 0)
            return false;
        return CV_INSTRUMENT_FUN_IPP(ippiColorConvertReorder, temp.ptr(), (int)temp.step[0], dst, dstStep, ippiSize(cols, rows), order) >= 0;
    }
private:
    ippiGeneralFunc ippiColorConvertGeneral;
    ippiReorderFunc ippiColorConvertReorder;
    int order[4];
    int depth;
};

#endif

////////////////// Various 3/4-channel to 3/4-channel RGB transformations /////////////////

template<typename _Tp> struct RGB2RGB
{
    typedef _Tp channel_type;

    RGB2RGB(int _srccn, int _dstcn, int _blueIdx) : srccn(_srccn), dstcn(_dstcn), blueIdx(_blueIdx) {}
    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int scn = srccn, dcn = dstcn, bidx = blueIdx;
        if( dcn == 3 )
        {
            n *= 3;
            for( int i = 0; i < n; i += 3, src += scn )
            {
                _Tp t0 = src[bidx], t1 = src[1], t2 = src[bidx ^ 2];
                dst[i] = t0; dst[i+1] = t1; dst[i+2] = t2;
            }
        }
        else if( scn == 3 )
        {
            n *= 3;
            _Tp alpha = ColorChannel<_Tp>::max();
            for( int i = 0; i < n; i += 3, dst += 4 )
            {
                _Tp t0 = src[i], t1 = src[i+1], t2 = src[i+2];
                dst[bidx] = t0; dst[1] = t1; dst[bidx^2] = t2; dst[3] = alpha;
            }
        }
        else
        {
            n *= 4;
            for( int i = 0; i < n; i += 4 )
            {
                _Tp t0 = src[i], t1 = src[i+1], t2 = src[i+2], t3 = src[i+3];
                dst[i] = t2; dst[i+1] = t1; dst[i+2] = t0; dst[i+3] = t3;
            }
        }
    }

    int srccn, dstcn, blueIdx;
};

#if CV_NEON

template<> struct RGB2RGB<uchar>
{
    typedef uchar channel_type;

    RGB2RGB(int _srccn, int _dstcn, int _blueIdx) :
        srccn(_srccn), dstcn(_dstcn), blueIdx(_blueIdx)
    {
        v_alpha = vdupq_n_u8(ColorChannel<uchar>::max());
        v_alpha2 = vget_low_u8(v_alpha);
    }

    void operator()(const uchar * src, uchar * dst, int n) const
    {
        int scn = srccn, dcn = dstcn, bidx = blueIdx, i = 0;
        if (dcn == 3)
        {
            n *= 3;
            if (scn == 3)
            {
                for ( ; i <= n - 48; i += 48, src += 48 )
                {
                    uint8x16x3_t v_src = vld3q_u8(src), v_dst;
                    v_dst.val[0] = v_src.val[bidx];
                    v_dst.val[1] = v_src.val[1];
                    v_dst.val[2] = v_src.val[bidx ^ 2];
                    vst3q_u8(dst + i, v_dst);
                }
                for ( ; i <= n - 24; i += 24, src += 24 )
                {
                    uint8x8x3_t v_src = vld3_u8(src), v_dst;
                    v_dst.val[0] = v_src.val[bidx];
                    v_dst.val[1] = v_src.val[1];
                    v_dst.val[2] = v_src.val[bidx ^ 2];
                    vst3_u8(dst + i, v_dst);
                }
                for ( ; i < n; i += 3, src += 3 )
                {
                    uchar t0 = src[bidx], t1 = src[1], t2 = src[bidx ^ 2];
                    dst[i] = t0; dst[i+1] = t1; dst[i+2] = t2;
                }
            }
            else
            {
                for ( ; i <= n - 48; i += 48, src += 64 )
                {
                    uint8x16x4_t v_src = vld4q_u8(src);
                    uint8x16x3_t v_dst;
                    v_dst.val[0] = v_src.val[bidx];
                    v_dst.val[1] = v_src.val[1];
                    v_dst.val[2] = v_src.val[bidx ^ 2];
                    vst3q_u8(dst + i, v_dst);
                }
                for ( ; i <= n - 24; i += 24, src += 32 )
                {
                    uint8x8x4_t v_src = vld4_u8(src);
                    uint8x8x3_t v_dst;
                    v_dst.val[0] = v_src.val[bidx];
                    v_dst.val[1] = v_src.val[1];
                    v_dst.val[2] = v_src.val[bidx ^ 2];
                    vst3_u8(dst + i, v_dst);
                }
                for ( ; i < n; i += 3, src += 4 )
                {
                    uchar t0 = src[bidx], t1 = src[1], t2 = src[bidx ^ 2];
                    dst[i] = t0; dst[i+1] = t1; dst[i+2] = t2;
                }
            }
        }
        else if (scn == 3)
        {
            n *= 3;
            for ( ; i <= n - 48; i += 48, dst += 64 )
            {
                uint8x16x3_t v_src = vld3q_u8(src + i);
                uint8x16x4_t v_dst;
                v_dst.val[bidx] = v_src.val[0];
                v_dst.val[1] = v_src.val[1];
                v_dst.val[bidx ^ 2] = v_src.val[2];
                v_dst.val[3] = v_alpha;
                vst4q_u8(dst, v_dst);
            }
            for ( ; i <= n - 24; i += 24, dst += 32 )
            {
                uint8x8x3_t v_src = vld3_u8(src + i);
                uint8x8x4_t v_dst;
                v_dst.val[bidx] = v_src.val[0];
                v_dst.val[1] = v_src.val[1];
                v_dst.val[bidx ^ 2] = v_src.val[2];
                v_dst.val[3] = v_alpha2;
                vst4_u8(dst, v_dst);
            }
            uchar alpha = ColorChannel<uchar>::max();
            for (; i < n; i += 3, dst += 4 )
            {
                uchar t0 = src[i], t1 = src[i+1], t2 = src[i+2];
                dst[bidx] = t0; dst[1] = t1; dst[bidx^2] = t2; dst[3] = alpha;
            }
        }
        else
        {
            n *= 4;
            for ( ; i <= n - 64; i += 64 )
            {
                uint8x16x4_t v_src = vld4q_u8(src + i), v_dst;
                v_dst.val[0] = v_src.val[2];
                v_dst.val[1] = v_src.val[1];
                v_dst.val[2] = v_src.val[0];
                v_dst.val[3] = v_src.val[3];
                vst4q_u8(dst + i, v_dst);
            }
            for ( ; i <= n - 32; i += 32 )
            {
                uint8x8x4_t v_src = vld4_u8(src + i), v_dst;
                v_dst.val[0] = v_src.val[2];
                v_dst.val[1] = v_src.val[1];
                v_dst.val[2] = v_src.val[0];
                v_dst.val[3] = v_src.val[3];
                vst4_u8(dst + i, v_dst);
            }
            for ( ; i < n; i += 4)
            {
                uchar t0 = src[i], t1 = src[i+1], t2 = src[i+2], t3 = src[i+3];
                dst[i] = t2; dst[i+1] = t1; dst[i+2] = t0; dst[i+3] = t3;
            }
        }
    }

    int srccn, dstcn, blueIdx;

    uint8x16_t v_alpha;
    uint8x8_t v_alpha2;
};

#endif

/////////// Transforming 16-bit (565 or 555) RGB to/from 24/32-bit (888[8]) RGB //////////

struct RGB5x52RGB
{
    typedef uchar channel_type;

    RGB5x52RGB(int _dstcn, int _blueIdx, int _greenBits)
        : dstcn(_dstcn), blueIdx(_blueIdx), greenBits(_greenBits)
    {
        #if CV_NEON
        v_n3 = vdupq_n_u16(~3);
        v_n7 = vdupq_n_u16(~7);
        v_255 = vdupq_n_u8(255);
        v_0 = vdupq_n_u8(0);
        v_mask = vdupq_n_u16(0x8000);
        #endif
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx, i = 0;
        if( greenBits == 6 )
        {
            #if CV_NEON
            for ( ; i <= n - 16; i += 16, dst += dcn * 16)
            {
                uint16x8_t v_src0 = vld1q_u16((const ushort *)src + i), v_src1 = vld1q_u16((const ushort *)src + i + 8);
                uint8x16_t v_b = vcombine_u8(vmovn_u16(vshlq_n_u16(v_src0, 3)), vmovn_u16(vshlq_n_u16(v_src1, 3)));
                uint8x16_t v_g = vcombine_u8(vmovn_u16(vandq_u16(vshrq_n_u16(v_src0, 3), v_n3)),
                                             vmovn_u16(vandq_u16(vshrq_n_u16(v_src1, 3), v_n3)));
                uint8x16_t v_r = vcombine_u8(vmovn_u16(vandq_u16(vshrq_n_u16(v_src0, 8), v_n7)),
                                             vmovn_u16(vandq_u16(vshrq_n_u16(v_src1, 8), v_n7)));
                if (dcn == 3)
                {
                    uint8x16x3_t v_dst;
                    v_dst.val[bidx] = v_b;
                    v_dst.val[1] = v_g;
                    v_dst.val[bidx^2] = v_r;
                    vst3q_u8(dst, v_dst);
                }
                else
                {
                    uint8x16x4_t v_dst;
                    v_dst.val[bidx] = v_b;
                    v_dst.val[1] = v_g;
                    v_dst.val[bidx^2] = v_r;
                    v_dst.val[3] = v_255;
                    vst4q_u8(dst, v_dst);
                }
            }
            #endif
            for( ; i < n; i++, dst += dcn )
            {
                unsigned t = ((const ushort*)src)[i];
                dst[bidx] = (uchar)(t << 3);
                dst[1] = (uchar)((t >> 3) & ~3);
                dst[bidx ^ 2] = (uchar)((t >> 8) & ~7);
                if( dcn == 4 )
                    dst[3] = 255;
            }
        }
        else
        {
            #if CV_NEON
            for ( ; i <= n - 16; i += 16, dst += dcn * 16)
            {
                uint16x8_t v_src0 = vld1q_u16((const ushort *)src + i), v_src1 = vld1q_u16((const ushort *)src + i + 8);
                uint8x16_t v_b = vcombine_u8(vmovn_u16(vshlq_n_u16(v_src0, 3)), vmovn_u16(vshlq_n_u16(v_src1, 3)));
                uint8x16_t v_g = vcombine_u8(vmovn_u16(vandq_u16(vshrq_n_u16(v_src0, 2), v_n7)),
                                             vmovn_u16(vandq_u16(vshrq_n_u16(v_src1, 2), v_n7)));
                uint8x16_t v_r = vcombine_u8(vmovn_u16(vandq_u16(vshrq_n_u16(v_src0, 7), v_n7)),
                                             vmovn_u16(vandq_u16(vshrq_n_u16(v_src1, 7), v_n7)));
                if (dcn == 3)
                {
                    uint8x16x3_t v_dst;
                    v_dst.val[bidx] = v_b;
                    v_dst.val[1] = v_g;
                    v_dst.val[bidx^2] = v_r;
                    vst3q_u8(dst, v_dst);
                }
                else
                {
                    uint8x16x4_t v_dst;
                    v_dst.val[bidx] = v_b;
                    v_dst.val[1] = v_g;
                    v_dst.val[bidx^2] = v_r;
                    v_dst.val[3] = vbslq_u8(vcombine_u8(vqmovn_u16(vandq_u16(v_src0, v_mask)),
                                                        vqmovn_u16(vandq_u16(v_src1, v_mask))), v_255, v_0);
                    vst4q_u8(dst, v_dst);
                }
            }
            #endif
            for( ; i < n; i++, dst += dcn )
            {
                unsigned t = ((const ushort*)src)[i];
                dst[bidx] = (uchar)(t << 3);
                dst[1] = (uchar)((t >> 2) & ~7);
                dst[bidx ^ 2] = (uchar)((t >> 7) & ~7);
                if( dcn == 4 )
                    dst[3] = t & 0x8000 ? 255 : 0;
            }
        }
    }

    int dstcn, blueIdx, greenBits;
    #if CV_NEON
    uint16x8_t v_n3, v_n7, v_mask;
    uint8x16_t v_255, v_0;
    #endif
};


struct RGB2RGB5x5
{
    typedef uchar channel_type;

    RGB2RGB5x5(int _srccn, int _blueIdx, int _greenBits)
        : srccn(_srccn), blueIdx(_blueIdx), greenBits(_greenBits)
    {
        #if CV_NEON
        v_n3 = vdup_n_u8(~3);
        v_n7 = vdup_n_u8(~7);
        v_mask = vdupq_n_u16(0x8000);
        v_0 = vdupq_n_u16(0);
        v_full = vdupq_n_u16(0xffff);
        #endif
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int scn = srccn, bidx = blueIdx, i = 0;
        if (greenBits == 6)
        {
            if (scn == 3)
            {
                #if CV_NEON
                for ( ; i <= n - 8; i += 8, src += 24 )
                {
                    uint8x8x3_t v_src = vld3_u8(src);
                    uint16x8_t v_dst = vmovl_u8(vshr_n_u8(v_src.val[bidx], 3));
                    v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[1], v_n3)), 3));
                    v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[bidx^2], v_n7)), 8));
                    vst1q_u16((ushort *)dst + i, v_dst);
                }
                #endif
                for ( ; i < n; i++, src += 3 )
                    ((ushort*)dst)[i] = (ushort)((src[bidx] >> 3)|((src[1]&~3) << 3)|((src[bidx^2]&~7) << 8));
            }
            else
            {
                #if CV_NEON
                for ( ; i <= n - 8; i += 8, src += 32 )
                {
                    uint8x8x4_t v_src = vld4_u8(src);
                    uint16x8_t v_dst = vmovl_u8(vshr_n_u8(v_src.val[bidx], 3));
                    v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[1], v_n3)), 3));
                    v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[bidx^2], v_n7)), 8));
                    vst1q_u16((ushort *)dst + i, v_dst);
                }
                #endif
                for ( ; i < n; i++, src += 4 )
                    ((ushort*)dst)[i] = (ushort)((src[bidx] >> 3)|((src[1]&~3) << 3)|((src[bidx^2]&~7) << 8));
            }
        }
        else if (scn == 3)
        {
            #if CV_NEON
            for ( ; i <= n - 8; i += 8, src += 24 )
            {
                uint8x8x3_t v_src = vld3_u8(src);
                uint16x8_t v_dst = vmovl_u8(vshr_n_u8(v_src.val[bidx], 3));
                v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[1], v_n7)), 2));
                v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[bidx^2], v_n7)), 7));
                vst1q_u16((ushort *)dst + i, v_dst);
            }
            #endif
            for ( ; i < n; i++, src += 3 )
                ((ushort*)dst)[i] = (ushort)((src[bidx] >> 3)|((src[1]&~7) << 2)|((src[bidx^2]&~7) << 7));
        }
        else
        {
            #if CV_NEON
            for ( ; i <= n - 8; i += 8, src += 32 )
            {
                uint8x8x4_t v_src = vld4_u8(src);
                uint16x8_t v_dst = vmovl_u8(vshr_n_u8(v_src.val[bidx], 3));
                v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[1], v_n7)), 2));
                v_dst = vorrq_u16(v_dst, vorrq_u16(vshlq_n_u16(vmovl_u8(vand_u8(v_src.val[bidx^2], v_n7)), 7),
                                                   vbslq_u16(veorq_u16(vceqq_u16(vmovl_u8(v_src.val[3]), v_0), v_full), v_mask, v_0)));
                vst1q_u16((ushort *)dst + i, v_dst);
            }
            #endif
            for ( ; i < n; i++, src += 4 )
                ((ushort*)dst)[i] = (ushort)((src[bidx] >> 3)|((src[1]&~7) << 2)|
                    ((src[bidx^2]&~7) << 7)|(src[3] ? 0x8000 : 0));
        }
    }

    int srccn, blueIdx, greenBits;
    #if CV_NEON
    uint8x8_t v_n3, v_n7;
    uint16x8_t v_mask, v_0, v_full;
    #endif
};

///////////////////////////////// Color to/from Grayscale ////////////////////////////////

template<typename _Tp>
struct Gray2RGB
{
    typedef _Tp channel_type;

    Gray2RGB(int _dstcn) : dstcn(_dstcn) {}
    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        if( dstcn == 3 )
            for( int i = 0; i < n; i++, dst += 3 )
            {
                dst[0] = dst[1] = dst[2] = src[i];
            }
        else
        {
            _Tp alpha = ColorChannel<_Tp>::max();
            for( int i = 0; i < n; i++, dst += 4 )
            {
                dst[0] = dst[1] = dst[2] = src[i];
                dst[3] = alpha;
            }
        }
    }

    int dstcn;
};


struct Gray2RGB5x5
{
    typedef uchar channel_type;

    Gray2RGB5x5(int _greenBits) : greenBits(_greenBits)
    {
        #if CV_NEON
        v_n7 = vdup_n_u8(~7);
        v_n3 = vdup_n_u8(~3);
        #elif CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        v_n7 = _mm_set1_epi16(~7);
        v_n3 = _mm_set1_epi16(~3);
        v_zero = _mm_setzero_si128();
        #endif
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i = 0;
        if( greenBits == 6 )
        {
            #if CV_NEON
            for ( ; i <= n - 8; i += 8 )
            {
                uint8x8_t v_src = vld1_u8(src + i);
                uint16x8_t v_dst = vmovl_u8(vshr_n_u8(v_src, 3));
                v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src, v_n3)), 3));
                v_dst = vorrq_u16(v_dst, vshlq_n_u16(vmovl_u8(vand_u8(v_src, v_n7)), 8));
                vst1q_u16((ushort *)dst + i, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; i <= n - 16; i += 16 )
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)(src + i));

                    __m128i v_src_p = _mm_unpacklo_epi8(v_src, v_zero);
                    __m128i v_dst = _mm_or_si128(_mm_srli_epi16(v_src_p, 3),
                                    _mm_or_si128(_mm_slli_epi16(_mm_and_si128(v_src_p, v_n3), 3),
                                                 _mm_slli_epi16(_mm_and_si128(v_src_p, v_n7), 8)));
                    _mm_storeu_si128((__m128i *)((ushort *)dst + i), v_dst);

                    v_src_p = _mm_unpackhi_epi8(v_src, v_zero);
                    v_dst = _mm_or_si128(_mm_srli_epi16(v_src_p, 3),
                            _mm_or_si128(_mm_slli_epi16(_mm_and_si128(v_src_p, v_n3), 3),
                                         _mm_slli_epi16(_mm_and_si128(v_src_p, v_n7), 8)));
                    _mm_storeu_si128((__m128i *)((ushort *)dst + i + 8), v_dst);
                }
            }
            #endif
            for ( ; i < n; i++ )
            {
                int t = src[i];
                ((ushort*)dst)[i] = (ushort)((t >> 3)|((t & ~3) << 3)|((t & ~7) << 8));
            }
        }
        else
        {
            #if CV_NEON
            for ( ; i <= n - 8; i += 8 )
            {
                uint16x8_t v_src = vmovl_u8(vshr_n_u8(vld1_u8(src + i), 3));
                uint16x8_t v_dst = vorrq_u16(vorrq_u16(v_src, vshlq_n_u16(v_src, 5)), vshlq_n_u16(v_src, 10));
                vst1q_u16((ushort *)dst + i, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; i <= n - 16; i += 8 )
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)(src + i));

                    __m128i v_src_p = _mm_srli_epi16(_mm_unpacklo_epi8(v_src, v_zero), 3);
                    __m128i v_dst = _mm_or_si128(v_src_p,
                                    _mm_or_si128(_mm_slli_epi32(v_src_p, 5),
                                                 _mm_slli_epi16(v_src_p, 10)));
                    _mm_storeu_si128((__m128i *)((ushort *)dst + i), v_dst);

                    v_src_p = _mm_srli_epi16(_mm_unpackhi_epi8(v_src, v_zero), 3);
                    v_dst = _mm_or_si128(v_src_p,
                            _mm_or_si128(_mm_slli_epi16(v_src_p, 5),
                                         _mm_slli_epi16(v_src_p, 10)));
                    _mm_storeu_si128((__m128i *)((ushort *)dst + i + 8), v_dst);
                }
            }
            #endif
            for( ; i < n; i++ )
            {
                int t = src[i] >> 3;
                ((ushort*)dst)[i] = (ushort)(t|(t << 5)|(t << 10));
            }
        }
    }
    int greenBits;

    #if CV_NEON
    uint8x8_t v_n7, v_n3;
    #elif CV_SSE2
    __m128i v_n7, v_n3, v_zero;
    bool haveSIMD;
    #endif
};


#undef R2Y
#undef G2Y
#undef B2Y

enum
{
    yuv_shift = 14,
    xyz_shift = 12,
    R2Y = 4899, // == R2YF*16384
    G2Y = 9617, // == G2YF*16384
    B2Y = 1868, // == B2YF*16384
    BLOCK_SIZE = 256
};


struct RGB5x52Gray
{
    typedef uchar channel_type;

    RGB5x52Gray(int _greenBits) : greenBits(_greenBits)
    {
        #if CV_NEON
        v_b2y = vdup_n_u16(B2Y);
        v_g2y = vdup_n_u16(G2Y);
        v_r2y = vdup_n_u16(R2Y);
        v_delta = vdupq_n_u32(1 << (yuv_shift - 1));
        v_f8 = vdupq_n_u16(0xf8);
        v_fc = vdupq_n_u16(0xfc);
        #elif CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        const __m128i v_b2y = _mm_set1_epi16(B2Y);
        const __m128i v_g2y = _mm_set1_epi16(G2Y);
        v_bg2y = _mm_unpacklo_epi16(v_b2y, v_g2y);
        const __m128i v_r2y = _mm_set1_epi16(R2Y);
        const __m128i v_one = _mm_set1_epi16(1);
        v_rd2y = _mm_unpacklo_epi16(v_r2y, v_one);
        v_delta = _mm_slli_epi16(v_one, yuv_shift - 1);
        #endif
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i = 0;
        if( greenBits == 6 )
        {
            #if CV_NEON
            for ( ; i <= n - 8; i += 8)
            {
                uint16x8_t v_src = vld1q_u16((ushort *)src + i);
                uint16x8_t v_t0 = vandq_u16(vshlq_n_u16(v_src, 3), v_f8),
                           v_t1 = vandq_u16(vshrq_n_u16(v_src, 3), v_fc),
                           v_t2 = vandq_u16(vshrq_n_u16(v_src, 8), v_f8);

                uint32x4_t v_dst0 = vmlal_u16(vmlal_u16(vmull_u16(vget_low_u16(v_t0), v_b2y),
                                              vget_low_u16(v_t1), v_g2y), vget_low_u16(v_t2), v_r2y);
                uint32x4_t v_dst1 = vmlal_u16(vmlal_u16(vmull_u16(vget_high_u16(v_t0), v_b2y),
                                              vget_high_u16(v_t1), v_g2y), vget_high_u16(v_t2), v_r2y);
                v_dst0 = vshrq_n_u32(vaddq_u32(v_dst0, v_delta), yuv_shift);
                v_dst1 = vshrq_n_u32(vaddq_u32(v_dst1, v_delta), yuv_shift);

                vst1_u8(dst + i, vmovn_u16(vcombine_u16(vmovn_u32(v_dst0), vmovn_u32(v_dst1))));
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; i <= n - 8; i += 8)
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)((ushort *)src + i));
                    __m128i v_b = _mm_srli_epi16(_mm_slli_epi16(v_src, 11), 8),
                            v_g = _mm_srli_epi16(_mm_slli_epi16(_mm_srli_epi16(v_src, 5), 10),8),
                            v_r = _mm_slli_epi16(_mm_srli_epi16(v_src, 11), 3);

                    __m128i v_bg_lo = _mm_unpacklo_epi16(v_b, v_g);
                    __m128i v_rd_lo = _mm_unpacklo_epi16(v_r, v_delta);
                    __m128i v_bg_hi = _mm_unpackhi_epi16(v_b, v_g);
                    __m128i v_rd_hi = _mm_unpackhi_epi16(v_r, v_delta);
                    v_bg_lo = _mm_madd_epi16(v_bg_lo, v_bg2y);
                    v_rd_lo = _mm_madd_epi16(v_rd_lo, v_rd2y);
                    v_bg_hi = _mm_madd_epi16(v_bg_hi, v_bg2y);
                    v_rd_hi = _mm_madd_epi16(v_rd_hi, v_rd2y);

                    __m128i v_bgr_lo = _mm_add_epi32(v_bg_lo, v_rd_lo);
                    __m128i v_bgr_hi = _mm_add_epi32(v_bg_hi, v_rd_hi);
                    v_bgr_lo = _mm_srli_epi32(v_bgr_lo, yuv_shift);
                    v_bgr_hi = _mm_srli_epi32(v_bgr_hi, yuv_shift);

                    __m128i v_dst = _mm_packs_epi32(v_bgr_lo, v_bgr_hi);
                    v_dst = _mm_packus_epi16(v_dst, v_dst);
                    _mm_storel_epi64((__m128i *)(dst + i), v_dst);
                }
            }
            #endif
            for ( ; i < n; i++)
            {
                int t = ((ushort*)src)[i];
                dst[i] = (uchar)CV_DESCALE(((t << 3) & 0xf8)*B2Y +
                                           ((t >> 3) & 0xfc)*G2Y +
                                           ((t >> 8) & 0xf8)*R2Y, yuv_shift);
            }
        }
        else
        {
            #if CV_NEON
            for ( ; i <= n - 8; i += 8)
            {
                uint16x8_t v_src = vld1q_u16((ushort *)src + i);
                uint16x8_t v_t0 = vandq_u16(vshlq_n_u16(v_src, 3), v_f8),
                           v_t1 = vandq_u16(vshrq_n_u16(v_src, 2), v_f8),
                           v_t2 = vandq_u16(vshrq_n_u16(v_src, 7), v_f8);

                uint32x4_t v_dst0 = vmlal_u16(vmlal_u16(vmull_u16(vget_low_u16(v_t0), v_b2y),
                                              vget_low_u16(v_t1), v_g2y), vget_low_u16(v_t2), v_r2y);
                uint32x4_t v_dst1 = vmlal_u16(vmlal_u16(vmull_u16(vget_high_u16(v_t0), v_b2y),
                                              vget_high_u16(v_t1), v_g2y), vget_high_u16(v_t2), v_r2y);
                v_dst0 = vshrq_n_u32(vaddq_u32(v_dst0, v_delta), yuv_shift);
                v_dst1 = vshrq_n_u32(vaddq_u32(v_dst1, v_delta), yuv_shift);

                vst1_u8(dst + i, vmovn_u16(vcombine_u16(vmovn_u32(v_dst0), vmovn_u32(v_dst1))));
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; i <= n - 8; i += 8)
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)((ushort *)src + i));
                    __m128i v_b = _mm_srli_epi16(_mm_slli_epi16(v_src, 11), 8),
                            v_g = _mm_srli_epi16(_mm_slli_epi16(_mm_srli_epi16(v_src, 5), 11),8),
                            v_r = _mm_srli_epi16(_mm_slli_epi16(_mm_srli_epi16(v_src, 10), 11),8);

                    __m128i v_bg_lo = _mm_unpacklo_epi16(v_b, v_g);
                    __m128i v_rd_lo = _mm_unpacklo_epi16(v_r, v_delta);
                    __m128i v_bg_hi = _mm_unpackhi_epi16(v_b, v_g);
                    __m128i v_rd_hi = _mm_unpackhi_epi16(v_r, v_delta);
                    v_bg_lo = _mm_madd_epi16(v_bg_lo, v_bg2y);
                    v_rd_lo = _mm_madd_epi16(v_rd_lo, v_rd2y);
                    v_bg_hi = _mm_madd_epi16(v_bg_hi, v_bg2y);
                    v_rd_hi = _mm_madd_epi16(v_rd_hi, v_rd2y);

                    __m128i v_bgr_lo = _mm_add_epi32(v_bg_lo, v_rd_lo);
                    __m128i v_bgr_hi = _mm_add_epi32(v_bg_hi, v_rd_hi);
                    v_bgr_lo = _mm_srli_epi32(v_bgr_lo, yuv_shift);
                    v_bgr_hi = _mm_srli_epi32(v_bgr_hi, yuv_shift);

                    __m128i v_dst = _mm_packs_epi32(v_bgr_lo, v_bgr_hi);
                    v_dst = _mm_packus_epi16(v_dst, v_dst);
                    _mm_storel_epi64((__m128i *)(dst + i), v_dst);
                }
            }
            #endif
            for ( ; i < n; i++)
            {
                int t = ((ushort*)src)[i];
                dst[i] = (uchar)CV_DESCALE(((t << 3) & 0xf8)*B2Y +
                                           ((t >> 2) & 0xf8)*G2Y +
                                           ((t >> 7) & 0xf8)*R2Y, yuv_shift);
            }
        }
    }
    int greenBits;

    #if CV_NEON
    uint16x4_t v_b2y, v_g2y, v_r2y;
    uint32x4_t v_delta;
    uint16x8_t v_f8, v_fc;
    #elif CV_SSE2
    bool haveSIMD;
    __m128i v_bg2y, v_rd2y;
    __m128i v_delta;
    #endif
};


template<typename _Tp> struct RGB2Gray
{
    typedef _Tp channel_type;

    RGB2Gray(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        static const float coeffs0[] = { R2YF, G2YF, B2YF };
        memcpy( coeffs, _coeffs ? _coeffs : coeffs0, 3*sizeof(coeffs[0]) );
        if(blueIdx == 0)
            std::swap(coeffs[0], coeffs[2]);
    }

    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int scn = srccn;
        float cb = coeffs[0], cg = coeffs[1], cr = coeffs[2];
        for(int i = 0; i < n; i++, src += scn)
            dst[i] = saturate_cast<_Tp>(src[0]*cb + src[1]*cg + src[2]*cr);
    }
    int srccn;
    float coeffs[3];
};

template<> struct RGB2Gray<uchar>
{
    typedef uchar channel_type;

    RGB2Gray(int _srccn, int blueIdx, const int* coeffs) : srccn(_srccn)
    {
        const int coeffs0[] = { R2Y, G2Y, B2Y };
        if(!coeffs) coeffs = coeffs0;

        int b = 0, g = 0, r = (1 << (yuv_shift-1));
        int db = coeffs[blueIdx^2], dg = coeffs[1], dr = coeffs[blueIdx];

        for( int i = 0; i < 256; i++, b += db, g += dg, r += dr )
        {
            tab[i] = b;
            tab[i+256] = g;
            tab[i+512] = r;
        }
    }
    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int scn = srccn;
        const int* _tab = tab;
        for(int i = 0; i < n; i++, src += scn)
            dst[i] = (uchar)((_tab[src[0]] + _tab[src[1]+256] + _tab[src[2]+512]) >> yuv_shift);
    }
    int srccn;
    int tab[256*3];
};

#if CV_NEON

template <>
struct RGB2Gray<ushort>
{
    typedef ushort channel_type;

    RGB2Gray(int _srccn, int blueIdx, const int* _coeffs) :
        srccn(_srccn)
    {
        static const int coeffs0[] = { R2Y, G2Y, B2Y };
        memcpy(coeffs, _coeffs ? _coeffs : coeffs0, 3*sizeof(coeffs[0]));
        if( blueIdx == 0 )
            std::swap(coeffs[0], coeffs[2]);

        v_cb = vdup_n_u16(coeffs[0]);
        v_cg = vdup_n_u16(coeffs[1]);
        v_cr = vdup_n_u16(coeffs[2]);
        v_delta = vdupq_n_u32(1 << (yuv_shift - 1));
    }

    void operator()(const ushort* src, ushort* dst, int n) const
    {
        int scn = srccn, cb = coeffs[0], cg = coeffs[1], cr = coeffs[2], i = 0;

        for ( ; i <= n - 8; i += 8, src += scn * 8)
        {
            uint16x8_t v_b, v_r, v_g;
            if (scn == 3)
            {
                uint16x8x3_t v_src = vld3q_u16(src);
                v_b = v_src.val[0];
                v_g = v_src.val[1];
                v_r = v_src.val[2];
            }
            else
            {
                uint16x8x4_t v_src = vld4q_u16(src);
                v_b = v_src.val[0];
                v_g = v_src.val[1];
                v_r = v_src.val[2];
            }

            uint32x4_t v_dst0_ = vmlal_u16(vmlal_u16(
                                           vmull_u16(vget_low_u16(v_b), v_cb),
                                                     vget_low_u16(v_g), v_cg),
                                                     vget_low_u16(v_r), v_cr);
            uint32x4_t v_dst1_ = vmlal_u16(vmlal_u16(
                                           vmull_u16(vget_high_u16(v_b), v_cb),
                                                     vget_high_u16(v_g), v_cg),
                                                     vget_high_u16(v_r), v_cr);

            uint16x4_t v_dst0 = vmovn_u32(vshrq_n_u32(vaddq_u32(v_dst0_, v_delta), yuv_shift));
            uint16x4_t v_dst1 = vmovn_u32(vshrq_n_u32(vaddq_u32(v_dst1_, v_delta), yuv_shift));

            vst1q_u16(dst + i, vcombine_u16(v_dst0, v_dst1));
        }

        for ( ; i <= n - 4; i += 4, src += scn * 4)
        {
            uint16x4_t v_b, v_r, v_g;
            if (scn == 3)
            {
                uint16x4x3_t v_src = vld3_u16(src);
                v_b = v_src.val[0];
                v_g = v_src.val[1];
                v_r = v_src.val[2];
            }
            else
            {
                uint16x4x4_t v_src = vld4_u16(src);
                v_b = v_src.val[0];
                v_g = v_src.val[1];
                v_r = v_src.val[2];
            }

            uint32x4_t v_dst = vmlal_u16(vmlal_u16(
                                         vmull_u16(v_b, v_cb),
                                                   v_g, v_cg),
                                                   v_r, v_cr);

            vst1_u16(dst + i, vmovn_u32(vshrq_n_u32(vaddq_u32(v_dst, v_delta), yuv_shift)));
        }

        for( ; i < n; i++, src += scn)
            dst[i] = (ushort)CV_DESCALE((unsigned)(src[0]*cb + src[1]*cg + src[2]*cr), yuv_shift);
    }

    int srccn, coeffs[3];
    uint16x4_t v_cb, v_cg, v_cr;
    uint32x4_t v_delta;
};

template <>
struct RGB2Gray<float>
{
    typedef float channel_type;

    RGB2Gray(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        static const float coeffs0[] = { R2YF, G2YF, B2YF };
        memcpy( coeffs, _coeffs ? _coeffs : coeffs0, 3*sizeof(coeffs[0]) );
        if(blueIdx == 0)
            std::swap(coeffs[0], coeffs[2]);

        v_cb = vdupq_n_f32(coeffs[0]);
        v_cg = vdupq_n_f32(coeffs[1]);
        v_cr = vdupq_n_f32(coeffs[2]);
    }

    void operator()(const float * src, float * dst, int n) const
    {
        int scn = srccn, i = 0;
        float cb = coeffs[0], cg = coeffs[1], cr = coeffs[2];

        if (scn == 3)
        {
            for ( ; i <= n - 8; i += 8, src += scn * 8)
            {
                float32x4x3_t v_src = vld3q_f32(src);
                vst1q_f32(dst + i, vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_cb), v_src.val[1], v_cg), v_src.val[2], v_cr));

                v_src = vld3q_f32(src + scn * 4);
                vst1q_f32(dst + i + 4, vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_cb), v_src.val[1], v_cg), v_src.val[2], v_cr));
            }

            for ( ; i <= n - 4; i += 4, src += scn * 4)
            {
                float32x4x3_t v_src = vld3q_f32(src);
                vst1q_f32(dst + i, vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_cb), v_src.val[1], v_cg), v_src.val[2], v_cr));
            }
        }
        else
        {
            for ( ; i <= n - 8; i += 8, src += scn * 8)
            {
                float32x4x4_t v_src = vld4q_f32(src);
                vst1q_f32(dst + i, vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_cb), v_src.val[1], v_cg), v_src.val[2], v_cr));

                v_src = vld4q_f32(src + scn * 4);
                vst1q_f32(dst + i + 4, vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_cb), v_src.val[1], v_cg), v_src.val[2], v_cr));
            }

            for ( ; i <= n - 4; i += 4, src += scn * 4)
            {
                float32x4x4_t v_src = vld4q_f32(src);
                vst1q_f32(dst + i, vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_cb), v_src.val[1], v_cg), v_src.val[2], v_cr));
            }
        }

        for ( ; i < n; i++, src += scn)
            dst[i] = src[0]*cb + src[1]*cg + src[2]*cr;
    }

    int srccn;
    float coeffs[3];
    float32x4_t v_cb, v_cg, v_cr;
};

#elif CV_SSE2

#if CV_SSE4_1

template <>
struct RGB2Gray<ushort>
{
    typedef ushort channel_type;

    RGB2Gray(int _srccn, int blueIdx, const int* _coeffs) :
        srccn(_srccn)
    {
        static const int coeffs0[] = { R2Y, G2Y, B2Y };
        memcpy(coeffs, _coeffs ? _coeffs : coeffs0, 3*sizeof(coeffs[0]));
        if( blueIdx == 0 )
            std::swap(coeffs[0], coeffs[2]);

        v_delta = _mm_set1_epi32(1 << (yuv_shift - 1));
        v_zero = _mm_setzero_si128();

        haveSIMD = checkHardwareSupport(CV_CPU_SSE4_1);
    }

    // 16s x 8
    void process(__m128i* v_rgb, __m128i* v_coeffs,
                 __m128i & v_gray) const
    {
        __m128i v_rgb_hi[4];
        v_rgb_hi[0] = _mm_cmplt_epi16(v_rgb[0], v_zero);
        v_rgb_hi[1] = _mm_cmplt_epi16(v_rgb[1], v_zero);
        v_rgb_hi[2] = _mm_cmplt_epi16(v_rgb[2], v_zero);
        v_rgb_hi[3] = _mm_cmplt_epi16(v_rgb[3], v_zero);

        v_rgb_hi[0] = _mm_and_si128(v_rgb_hi[0], v_coeffs[1]);
        v_rgb_hi[1] = _mm_and_si128(v_rgb_hi[1], v_coeffs[1]);
        v_rgb_hi[2] = _mm_and_si128(v_rgb_hi[2], v_coeffs[1]);
        v_rgb_hi[3] = _mm_and_si128(v_rgb_hi[3], v_coeffs[1]);

        v_rgb_hi[0] = _mm_hadd_epi16(v_rgb_hi[0], v_rgb_hi[1]);
        v_rgb_hi[2] = _mm_hadd_epi16(v_rgb_hi[2], v_rgb_hi[3]);
        v_rgb_hi[0] = _mm_hadd_epi16(v_rgb_hi[0], v_rgb_hi[2]);

        v_rgb[0] = _mm_madd_epi16(v_rgb[0], v_coeffs[0]);
        v_rgb[1] = _mm_madd_epi16(v_rgb[1], v_coeffs[0]);
        v_rgb[2] = _mm_madd_epi16(v_rgb[2], v_coeffs[0]);
        v_rgb[3] = _mm_madd_epi16(v_rgb[3], v_coeffs[0]);

        v_rgb[0] = _mm_hadd_epi32(v_rgb[0], v_rgb[1]);
        v_rgb[2] = _mm_hadd_epi32(v_rgb[2], v_rgb[3]);

        v_rgb[0] = _mm_add_epi32(v_rgb[0], v_delta);
        v_rgb[2] = _mm_add_epi32(v_rgb[2], v_delta);

        v_rgb[0] = _mm_srai_epi32(v_rgb[0], yuv_shift);
        v_rgb[2] = _mm_srai_epi32(v_rgb[2], yuv_shift);

        v_gray = _mm_packs_epi32(v_rgb[0], v_rgb[2]);
        v_gray = _mm_add_epi16(v_gray, v_rgb_hi[0]);
    }

    void operator()(const ushort* src, ushort* dst, int n) const
    {
        int scn = srccn, cb = coeffs[0], cg = coeffs[1], cr = coeffs[2], i = 0;

        if (scn == 3 && haveSIMD)
        {
            __m128i v_coeffs[2];
            v_coeffs[0] = _mm_set_epi16(0, (short)coeffs[2], (short)coeffs[1], (short)coeffs[0], (short)coeffs[2], (short)coeffs[1], (short)coeffs[0], 0);
            v_coeffs[1] = _mm_slli_epi16(v_coeffs[0], 2);

            for ( ; i <= n - 8; i += 8, src += scn * 8)
            {
                __m128i v_src[3];
                v_src[0] = _mm_loadu_si128((__m128i const *)(src));
                v_src[1] = _mm_loadu_si128((__m128i const *)(src + 8));
                v_src[2] = _mm_loadu_si128((__m128i const *)(src + 16));

                __m128i v_rgb[4];
                v_rgb[0] = _mm_slli_si128(v_src[0], 2);
                v_rgb[1] = _mm_alignr_epi8(v_src[1], v_src[0], 10);
                v_rgb[2] = _mm_alignr_epi8(v_src[2], v_src[1], 6);
                v_rgb[3] = _mm_srli_si128(v_src[2], 2);

                __m128i v_gray;
                process(v_rgb, v_coeffs,
                        v_gray);

                _mm_storeu_si128((__m128i *)(dst + i), v_gray);
            }
        }
        else if (scn == 4 && haveSIMD)
        {
            __m128i v_coeffs[2];
            v_coeffs[0] = _mm_set_epi16(0, (short)coeffs[2], (short)coeffs[1], (short)coeffs[0], 0, (short)coeffs[2], (short)coeffs[1], (short)coeffs[0]);
            v_coeffs[1] = _mm_slli_epi16(v_coeffs[0], 2);

            for ( ; i <= n - 8; i += 8, src += scn * 8)
            {
                __m128i v_rgb[4];
                v_rgb[0] = _mm_loadu_si128((__m128i const *)(src));
                v_rgb[1] = _mm_loadu_si128((__m128i const *)(src + 8));
                v_rgb[2] = _mm_loadu_si128((__m128i const *)(src + 16));
                v_rgb[3] = _mm_loadu_si128((__m128i const *)(src + 24));

                __m128i v_gray;
                process(v_rgb, v_coeffs,
                        v_gray);

                _mm_storeu_si128((__m128i *)(dst + i), v_gray);
            }
        }

        for( ; i < n; i++, src += scn)
            dst[i] = (ushort)CV_DESCALE((unsigned)(src[0]*cb + src[1]*cg + src[2]*cr), yuv_shift);
    }

    int srccn, coeffs[3];
    __m128i v_delta;
    __m128i v_zero;
    bool haveSIMD;
};

#endif // CV_SSE4_1

template <>
struct RGB2Gray<float>
{
    typedef float channel_type;

    RGB2Gray(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        static const float coeffs0[] = { R2YF, G2YF, B2YF };
        memcpy( coeffs, _coeffs ? _coeffs : coeffs0, 3*sizeof(coeffs[0]) );
        if(blueIdx == 0)
            std::swap(coeffs[0], coeffs[2]);

        v_cb = _mm_set1_ps(coeffs[0]);
        v_cg = _mm_set1_ps(coeffs[1]);
        v_cr = _mm_set1_ps(coeffs[2]);

        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
    }

    void process(__m128 v_b, __m128 v_g, __m128 v_r,
                 __m128 & v_gray) const
    {
        v_gray = _mm_mul_ps(v_r, v_cr);
        v_gray = _mm_add_ps(v_gray, _mm_mul_ps(v_g, v_cg));
        v_gray = _mm_add_ps(v_gray, _mm_mul_ps(v_b, v_cb));
    }

    void operator()(const float * src, float * dst, int n) const
    {
        int scn = srccn, i = 0;
        float cb = coeffs[0], cg = coeffs[1], cr = coeffs[2];

        if (scn == 3 && haveSIMD)
        {
            for ( ; i <= n - 8; i += 8, src += scn * 8)
            {
                __m128 v_r0 = _mm_loadu_ps(src);
                __m128 v_r1 = _mm_loadu_ps(src + 4);
                __m128 v_g0 = _mm_loadu_ps(src + 8);
                __m128 v_g1 = _mm_loadu_ps(src + 12);
                __m128 v_b0 = _mm_loadu_ps(src + 16);
                __m128 v_b1 = _mm_loadu_ps(src + 20);

                _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);

                __m128 v_gray0;
                process(v_r0, v_g0, v_b0,
                        v_gray0);

                __m128 v_gray1;
                process(v_r1, v_g1, v_b1,
                        v_gray1);

                _mm_storeu_ps(dst + i, v_gray0);
                _mm_storeu_ps(dst + i + 4, v_gray1);
            }
        }
        else if (scn == 4 && haveSIMD)
        {
            for ( ; i <= n - 8; i += 8, src += scn * 8)
            {
                __m128 v_r0 = _mm_loadu_ps(src);
                __m128 v_r1 = _mm_loadu_ps(src + 4);
                __m128 v_g0 = _mm_loadu_ps(src + 8);
                __m128 v_g1 = _mm_loadu_ps(src + 12);
                __m128 v_b0 = _mm_loadu_ps(src + 16);
                __m128 v_b1 = _mm_loadu_ps(src + 20);
                __m128 v_a0 = _mm_loadu_ps(src + 24);
                __m128 v_a1 = _mm_loadu_ps(src + 28);

                _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1, v_a0, v_a1);

                __m128 v_gray0;
                process(v_r0, v_g0, v_b0,
                        v_gray0);

                __m128 v_gray1;
                process(v_r1, v_g1, v_b1,
                        v_gray1);

                _mm_storeu_ps(dst + i, v_gray0);
                _mm_storeu_ps(dst + i + 4, v_gray1);
            }
        }

        for ( ; i < n; i++, src += scn)
            dst[i] = src[0]*cb + src[1]*cg + src[2]*cr;
    }

    int srccn;
    float coeffs[3];
    __m128 v_cb, v_cg, v_cr;
    bool haveSIMD;
};

#endif // CV_SSE2

#if !CV_NEON && !CV_SSE4_1

template<> struct RGB2Gray<ushort>
{
    typedef ushort channel_type;

    RGB2Gray(int _srccn, int blueIdx, const int* _coeffs) : srccn(_srccn)
    {
        static const int coeffs0[] = { R2Y, G2Y, B2Y };
        memcpy(coeffs, _coeffs ? _coeffs : coeffs0, 3*sizeof(coeffs[0]));
        if( blueIdx == 0 )
            std::swap(coeffs[0], coeffs[2]);
    }

    void operator()(const ushort* src, ushort* dst, int n) const
    {
        int scn = srccn, cb = coeffs[0], cg = coeffs[1], cr = coeffs[2];
        for(int i = 0; i < n; i++, src += scn)
            dst[i] = (ushort)CV_DESCALE((unsigned)(src[0]*cb + src[1]*cg + src[2]*cr), yuv_shift);
    }
    int srccn;
    int coeffs[3];
};

#endif // !CV_NEON && !CV_SSE4_1

///////////////////////////////////// RGB <-> YCrCb //////////////////////////////////////

template<typename _Tp> struct RGB2YCrCb_f
{
    typedef _Tp channel_type;

    RGB2YCrCb_f(int _srccn, int _blueIdx, bool _isCrCb) : srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const float coeffs_crb[] = { R2YF, G2YF, B2YF, YCRF, YCBF };
        static const float coeffs_yuv[] = { R2YF, G2YF, B2YF, R2VF, B2UF };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if(blueIdx==0) std::swap(coeffs[0], coeffs[2]);
    }

    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int scn = srccn, bidx = blueIdx;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const _Tp delta = ColorChannel<_Tp>::half();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        n *= 3;
        for(int i = 0; i < n; i += 3, src += scn)
        {
            _Tp Y = saturate_cast<_Tp>(src[0]*C0 + src[1]*C1 + src[2]*C2);
            _Tp Cr = saturate_cast<_Tp>((src[bidx^2] - Y)*C3 + delta);
            _Tp Cb = saturate_cast<_Tp>((src[bidx] - Y)*C4 + delta);
            dst[i] = Y; dst[i+1+yuvOrder] = Cr; dst[i+2-yuvOrder] = Cb;
        }
    }
    int srccn, blueIdx;
    bool isCrCb;
    float coeffs[5];
};

#if CV_NEON

template <>
struct RGB2YCrCb_f<float>
{
    typedef float channel_type;

    RGB2YCrCb_f(int _srccn, int _blueIdx, bool _isCrCb) :
        srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const float coeffs_crb[] = { R2YF, G2YF, B2YF, YCRF, YCBF };
        static const float coeffs_yuv[] = { R2YF, G2YF, B2YF, R2VF, B2UF };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if(blueIdx==0)
            std::swap(coeffs[0], coeffs[2]);

        v_c0 = vdupq_n_f32(coeffs[0]);
        v_c1 = vdupq_n_f32(coeffs[1]);
        v_c2 = vdupq_n_f32(coeffs[2]);
        v_c3 = vdupq_n_f32(coeffs[3]);
        v_c4 = vdupq_n_f32(coeffs[4]);
        v_delta = vdupq_n_f32(ColorChannel<float>::half());
    }

    void operator()(const float * src, float * dst, int n) const
    {
        int scn = srccn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const float delta = ColorChannel<float>::half();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        n *= 3;

        if (scn == 3)
            for ( ; i <= n - 12; i += 12, src += 12)
            {
                float32x4x3_t v_src = vld3q_f32(src), v_dst;
                v_dst.val[0] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c0), v_src.val[1], v_c1), v_src.val[2], v_c2);
                v_dst.val[1+yuvOrder] = vmlaq_f32(v_delta, vsubq_f32(v_src.val[bidx^2], v_dst.val[0]), v_c3);
                v_dst.val[2-yuvOrder] = vmlaq_f32(v_delta, vsubq_f32(v_src.val[bidx], v_dst.val[0]), v_c4);

                vst3q_f32(dst + i, v_dst);
            }
        else
            for ( ; i <= n - 12; i += 12, src += 16)
            {
                float32x4x4_t v_src = vld4q_f32(src);
                float32x4x3_t v_dst;
                v_dst.val[0] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c0), v_src.val[1], v_c1), v_src.val[2], v_c2);
                v_dst.val[1+yuvOrder] = vmlaq_f32(v_delta, vsubq_f32(v_src.val[bidx^2], v_dst.val[0]), v_c3);
                v_dst.val[2-yuvOrder] = vmlaq_f32(v_delta, vsubq_f32(v_src.val[bidx], v_dst.val[0]), v_c4);

                vst3q_f32(dst + i, v_dst);
            }

        for ( ; i < n; i += 3, src += scn)
        {
            float Y = src[0]*C0 + src[1]*C1 + src[2]*C2;
            float Cr = (src[bidx^2] - Y)*C3 + delta;
            float Cb = (src[bidx] - Y)*C4 + delta;
            dst[i] = Y; dst[i+1+yuvOrder] = Cr; dst[i+2-yuvOrder] = Cb;
        }
    }
    int srccn, blueIdx;
    bool isCrCb;
    float coeffs[5];
    float32x4_t v_c0, v_c1, v_c2, v_c3, v_c4, v_delta;
};

#elif CV_SSE2

template <>
struct RGB2YCrCb_f<float>
{
    typedef float channel_type;

    RGB2YCrCb_f(int _srccn, int _blueIdx, bool _isCrCb) :
        srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const float coeffs_crb[] = { R2YF, G2YF, B2YF, YCRF, YCBF };
        static const float coeffs_yuv[] = { R2YF, G2YF, B2YF, R2VF, B2UF };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if (blueIdx==0)
            std::swap(coeffs[0], coeffs[2]);

        v_c0 = _mm_set1_ps(coeffs[0]);
        v_c1 = _mm_set1_ps(coeffs[1]);
        v_c2 = _mm_set1_ps(coeffs[2]);
        v_c3 = _mm_set1_ps(coeffs[3]);
        v_c4 = _mm_set1_ps(coeffs[4]);
        v_delta = _mm_set1_ps(ColorChannel<float>::half());

        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
    }

    void process(__m128 v_r, __m128 v_g, __m128 v_b,
                 __m128 & v_y, __m128 & v_cr, __m128 & v_cb) const
    {
        v_y = _mm_mul_ps(v_r, v_c0);
        v_y = _mm_add_ps(v_y, _mm_mul_ps(v_g, v_c1));
        v_y = _mm_add_ps(v_y, _mm_mul_ps(v_b, v_c2));

        v_cr = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(blueIdx == 0 ? v_b : v_r, v_y), v_c3), v_delta);
        v_cb = _mm_add_ps(_mm_mul_ps(_mm_sub_ps(blueIdx == 2 ? v_b : v_r, v_y), v_c4), v_delta);
    }

    void operator()(const float * src, float * dst, int n) const
    {
        int scn = srccn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const float delta = ColorChannel<float>::half();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        n *= 3;

        if (haveSIMD)
        {
            for ( ; i <= n - 24; i += 24, src += 8 * scn)
            {
                __m128 v_r0 = _mm_loadu_ps(src);
                __m128 v_r1 = _mm_loadu_ps(src + 4);
                __m128 v_g0 = _mm_loadu_ps(src + 8);
                __m128 v_g1 = _mm_loadu_ps(src + 12);
                __m128 v_b0 = _mm_loadu_ps(src + 16);
                __m128 v_b1 = _mm_loadu_ps(src + 20);

                if (scn == 4)
                {
                    __m128 v_a0 = _mm_loadu_ps(src + 24);
                    __m128 v_a1 = _mm_loadu_ps(src + 28);
                    _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1,
                                        v_b0, v_b1, v_a0, v_a1);
                }
                else
                    _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);

                __m128 v_y0, v_cr0, v_cb0;
                process(v_r0, v_g0, v_b0,
                        v_y0, v_cr0, v_cb0);

                __m128 v_y1, v_cr1, v_cb1;
                process(v_r1, v_g1, v_b1,
                        v_y1, v_cr1, v_cb1);

                if(isCrCb)
                    _mm_interleave_ps(v_y0, v_y1, v_cr0, v_cr1, v_cb0, v_cb1);
                else //YUV
                {
                    _mm_interleave_ps(v_y0, v_y1, v_cb0, v_cb1, v_cr0, v_cr1);
                }

                _mm_storeu_ps(dst + i, v_y0);
                _mm_storeu_ps(dst + i + 4, v_y1);
                _mm_storeu_ps(dst + i + 8  + yuvOrder*8, v_cr0);
                _mm_storeu_ps(dst + i + 12 + yuvOrder*8, v_cr1);
                _mm_storeu_ps(dst + i + 16 - yuvOrder*8, v_cb0);
                _mm_storeu_ps(dst + i + 20 - yuvOrder*8, v_cb1);
            }
        }

        for ( ; i < n; i += 3, src += scn)
        {
            float Y = src[0]*C0 + src[1]*C1 + src[2]*C2;
            float Cr = (src[bidx^2] - Y)*C3 + delta;
            float Cb = (src[bidx] - Y)*C4 + delta;
            dst[i] = Y; dst[i+1+yuvOrder] = Cr; dst[i+2-yuvOrder] = Cb;
        }
    }
    int srccn, blueIdx;
    bool isCrCb;
    float coeffs[5];
    __m128 v_c0, v_c1, v_c2, v_c3, v_c4, v_delta;
    bool haveSIMD;
};

#endif

template<typename _Tp> struct RGB2YCrCb_i
{
    typedef _Tp channel_type;

    RGB2YCrCb_i(int _srccn, int _blueIdx, bool _isCrCb)
        : srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { R2Y, G2Y, B2Y, YCRI, YCBI };
        static const int coeffs_yuv[] = { R2Y, G2Y, B2Y, R2VI, B2UI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if(blueIdx==0) std::swap(coeffs[0], coeffs[2]);
    }
    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int scn = srccn, bidx = blueIdx;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        int delta = ColorChannel<_Tp>::half()*(1 << yuv_shift);
        n *= 3;
        for(int i = 0; i < n; i += 3, src += scn)
        {
            int Y = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, yuv_shift);
            int Cr = CV_DESCALE((src[bidx^2] - Y)*C3 + delta, yuv_shift);
            int Cb = CV_DESCALE((src[bidx] - Y)*C4 + delta, yuv_shift);
            dst[i] = saturate_cast<_Tp>(Y);
            dst[i+1+yuvOrder] = saturate_cast<_Tp>(Cr);
            dst[i+2-yuvOrder] = saturate_cast<_Tp>(Cb);
        }
    }
    int srccn, blueIdx;
    bool isCrCb;
    int coeffs[5];
};

#if CV_NEON

template <>
struct RGB2YCrCb_i<uchar>
{
    typedef uchar channel_type;

    RGB2YCrCb_i(int _srccn, int _blueIdx, bool _isCrCb)
        : srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { R2Y, G2Y, B2Y, YCRI, YCBI };
        static const int coeffs_yuv[] = { R2Y, G2Y, B2Y, R2VI, B2UI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if (blueIdx==0)
            std::swap(coeffs[0], coeffs[2]);

        v_c0 = vdup_n_s16(coeffs[0]);
        v_c1 = vdup_n_s16(coeffs[1]);
        v_c2 = vdup_n_s16(coeffs[2]);
        v_c3 = vdupq_n_s32(coeffs[3]);
        v_c4 = vdupq_n_s32(coeffs[4]);
        v_delta = vdupq_n_s32(ColorChannel<uchar>::half()*(1 << yuv_shift));
        v_delta2 = vdupq_n_s32(1 << (yuv_shift - 1));
    }

    void operator()(const uchar * src, uchar * dst, int n) const
    {
        int scn = srccn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        int delta = ColorChannel<uchar>::half()*(1 << yuv_shift);
        n *= 3;

        for ( ; i <= n - 24; i += 24, src += scn * 8)
        {
            uint8x8x3_t v_dst;
            int16x8x3_t v_src16;

            if (scn == 3)
            {
                uint8x8x3_t v_src = vld3_u8(src);
                v_src16.val[0] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[0]));
                v_src16.val[1] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[1]));
                v_src16.val[2] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[2]));
            }
            else
            {
                uint8x8x4_t v_src = vld4_u8(src);
                v_src16.val[0] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[0]));
                v_src16.val[1] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[1]));
                v_src16.val[2] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[2]));
            }

            int16x4x3_t v_src0;
            v_src0.val[0] = vget_low_s16(v_src16.val[0]);
            v_src0.val[1] = vget_low_s16(v_src16.val[1]);
            v_src0.val[2] = vget_low_s16(v_src16.val[2]);

            int32x4_t v_Y0 = vmlal_s16(vmlal_s16(vmull_s16(v_src0.val[0], v_c0), v_src0.val[1], v_c1), v_src0.val[2], v_c2);
            v_Y0 = vshrq_n_s32(vaddq_s32(v_Y0, v_delta2), yuv_shift);
            int32x4_t v_Cr0 = vmlaq_s32(v_delta, vsubq_s32(vmovl_s16(v_src0.val[bidx^2]), v_Y0), v_c3);
            v_Cr0 = vshrq_n_s32(vaddq_s32(v_Cr0, v_delta2), yuv_shift);
            int32x4_t v_Cb0 = vmlaq_s32(v_delta, vsubq_s32(vmovl_s16(v_src0.val[bidx]), v_Y0), v_c4);
            v_Cb0 = vshrq_n_s32(vaddq_s32(v_Cb0, v_delta2), yuv_shift);

            v_src0.val[0] = vget_high_s16(v_src16.val[0]);
            v_src0.val[1] = vget_high_s16(v_src16.val[1]);
            v_src0.val[2] = vget_high_s16(v_src16.val[2]);

            int32x4_t v_Y1 = vmlal_s16(vmlal_s16(vmull_s16(v_src0.val[0], v_c0), v_src0.val[1], v_c1), v_src0.val[2], v_c2);
            v_Y1 = vshrq_n_s32(vaddq_s32(v_Y1, v_delta2), yuv_shift);
            int32x4_t v_Cr1 = vmlaq_s32(v_delta, vsubq_s32(vmovl_s16(v_src0.val[bidx^2]), v_Y1), v_c3);
            v_Cr1 = vshrq_n_s32(vaddq_s32(v_Cr1, v_delta2), yuv_shift);
            int32x4_t v_Cb1 = vmlaq_s32(v_delta, vsubq_s32(vmovl_s16(v_src0.val[bidx]), v_Y1), v_c4);
            v_Cb1 = vshrq_n_s32(vaddq_s32(v_Cb1, v_delta2), yuv_shift);

            v_dst.val[0] = vqmovun_s16(vcombine_s16(vqmovn_s32(v_Y0), vqmovn_s32(v_Y1)));
            v_dst.val[1+yuvOrder] = vqmovun_s16(vcombine_s16(vqmovn_s32(v_Cr0), vqmovn_s32(v_Cr1)));
            v_dst.val[2-yuvOrder] = vqmovun_s16(vcombine_s16(vqmovn_s32(v_Cb0), vqmovn_s32(v_Cb1)));

            vst3_u8(dst + i, v_dst);
        }

        for ( ; i < n; i += 3, src += scn)
        {
            int Y = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, yuv_shift);
            int Cr = CV_DESCALE((src[bidx^2] - Y)*C3 + delta, yuv_shift);
            int Cb = CV_DESCALE((src[bidx] - Y)*C4 + delta, yuv_shift);
            dst[i] = saturate_cast<uchar>(Y);
            dst[i+1+yuvOrder] = saturate_cast<uchar>(Cr);
            dst[i+2-yuvOrder] = saturate_cast<uchar>(Cb);
        }
    }
    int srccn, blueIdx, coeffs[5];
    bool isCrCb;
    int16x4_t v_c0, v_c1, v_c2;
    int32x4_t v_c3, v_c4, v_delta, v_delta2;
};

template <>
struct RGB2YCrCb_i<ushort>
{
    typedef ushort channel_type;

    RGB2YCrCb_i(int _srccn, int _blueIdx, bool _isCrCb)
        : srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { R2Y, G2Y, B2Y, YCRI, YCBI };
        static const int coeffs_yuv[] = { R2Y, G2Y, B2Y, R2VI, B2UI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if (blueIdx==0)
            std::swap(coeffs[0], coeffs[2]);

        v_c0 = vdupq_n_s32(coeffs[0]);
        v_c1 = vdupq_n_s32(coeffs[1]);
        v_c2 = vdupq_n_s32(coeffs[2]);
        v_c3 = vdupq_n_s32(coeffs[3]);
        v_c4 = vdupq_n_s32(coeffs[4]);
        v_delta = vdupq_n_s32(ColorChannel<ushort>::half()*(1 << yuv_shift));
        v_delta2 = vdupq_n_s32(1 << (yuv_shift - 1));
    }

    void operator()(const ushort * src, ushort * dst, int n) const
    {
        int scn = srccn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        int delta = ColorChannel<ushort>::half()*(1 << yuv_shift);
        n *= 3;

        for ( ; i <= n - 24; i += 24, src += scn * 8)
        {
            uint16x8x3_t v_src, v_dst;
            int32x4x3_t v_src0;

            if (scn == 3)
                v_src = vld3q_u16(src);
            else
            {
                uint16x8x4_t v_src_ = vld4q_u16(src);
                v_src.val[0] = v_src_.val[0];
                v_src.val[1] = v_src_.val[1];
                v_src.val[2] = v_src_.val[2];
            }

            v_src0.val[0] = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[0])));
            v_src0.val[1] = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[1])));
            v_src0.val[2] = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[2])));

            int32x4_t v_Y0 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_src0.val[0], v_c0), v_src0.val[1], v_c1), v_src0.val[2], v_c2);
            v_Y0 = vshrq_n_s32(vaddq_s32(v_Y0, v_delta2), yuv_shift);
            int32x4_t v_Cr0 = vmlaq_s32(v_delta, vsubq_s32(v_src0.val[bidx^2], v_Y0), v_c3);
            v_Cr0 = vshrq_n_s32(vaddq_s32(v_Cr0, v_delta2), yuv_shift);
            int32x4_t v_Cb0 = vmlaq_s32(v_delta, vsubq_s32(v_src0.val[bidx], v_Y0), v_c4);
            v_Cb0 = vshrq_n_s32(vaddq_s32(v_Cb0, v_delta2), yuv_shift);

            v_src0.val[0] = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[0])));
            v_src0.val[1] = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[1])));
            v_src0.val[2] = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[2])));

            int32x4_t v_Y1 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_src0.val[0], v_c0), v_src0.val[1], v_c1), v_src0.val[2], v_c2);
            v_Y1 = vshrq_n_s32(vaddq_s32(v_Y1, v_delta2), yuv_shift);
            int32x4_t v_Cr1 = vmlaq_s32(v_delta, vsubq_s32(v_src0.val[bidx^2], v_Y1), v_c3);
            v_Cr1 = vshrq_n_s32(vaddq_s32(v_Cr1, v_delta2), yuv_shift);
            int32x4_t v_Cb1 = vmlaq_s32(v_delta, vsubq_s32(v_src0.val[bidx], v_Y1), v_c4);
            v_Cb1 = vshrq_n_s32(vaddq_s32(v_Cb1, v_delta2), yuv_shift);

            v_dst.val[0] = vcombine_u16(vqmovun_s32(v_Y0), vqmovun_s32(v_Y1));
            v_dst.val[1+yuvOrder] = vcombine_u16(vqmovun_s32(v_Cr0), vqmovun_s32(v_Cr1));
            v_dst.val[2-yuvOrder] = vcombine_u16(vqmovun_s32(v_Cb0), vqmovun_s32(v_Cb1));

            vst3q_u16(dst + i, v_dst);
        }

        for ( ; i <= n - 12; i += 12, src += scn * 4)
        {
            uint16x4x3_t v_dst;
            int32x4x3_t v_src0;

            if (scn == 3)
            {
                uint16x4x3_t v_src = vld3_u16(src);
                v_src0.val[0] = vreinterpretq_s32_u32(vmovl_u16(v_src.val[0]));
                v_src0.val[1] = vreinterpretq_s32_u32(vmovl_u16(v_src.val[1]));
                v_src0.val[2] = vreinterpretq_s32_u32(vmovl_u16(v_src.val[2]));
            }
            else
            {
                uint16x4x4_t v_src = vld4_u16(src);
                v_src0.val[0] = vreinterpretq_s32_u32(vmovl_u16(v_src.val[0]));
                v_src0.val[1] = vreinterpretq_s32_u32(vmovl_u16(v_src.val[1]));
                v_src0.val[2] = vreinterpretq_s32_u32(vmovl_u16(v_src.val[2]));
            }

            int32x4_t v_Y = vmlaq_s32(vmlaq_s32(vmulq_s32(v_src0.val[0], v_c0), v_src0.val[1], v_c1), v_src0.val[2], v_c2);
            v_Y = vshrq_n_s32(vaddq_s32(v_Y, v_delta2), yuv_shift);
            int32x4_t v_Cr = vmlaq_s32(v_delta, vsubq_s32(v_src0.val[bidx^2], v_Y), v_c3);
            v_Cr = vshrq_n_s32(vaddq_s32(v_Cr, v_delta2), yuv_shift);
            int32x4_t v_Cb = vmlaq_s32(v_delta, vsubq_s32(v_src0.val[bidx], v_Y), v_c4);
            v_Cb = vshrq_n_s32(vaddq_s32(v_Cb, v_delta2), yuv_shift);

            v_dst.val[0] = vqmovun_s32(v_Y);
            v_dst.val[1+yuvOrder] = vqmovun_s32(v_Cr);
            v_dst.val[2-yuvOrder] = vqmovun_s32(v_Cb);

            vst3_u16(dst + i, v_dst);
        }

        for ( ; i < n; i += 3, src += scn)
        {
            int Y = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, yuv_shift);
            int Cr = CV_DESCALE((src[bidx^2] - Y)*C3 + delta, yuv_shift);
            int Cb = CV_DESCALE((src[bidx] - Y)*C4 + delta, yuv_shift);
            dst[i] = saturate_cast<ushort>(Y);
            dst[i+1+yuvOrder] = saturate_cast<ushort>(Cr);
            dst[i+2-yuvOrder] = saturate_cast<ushort>(Cb);
        }
    }
    int srccn, blueIdx, coeffs[5];
    bool isCrCb;
    int32x4_t v_c0, v_c1, v_c2, v_c3, v_c4, v_delta, v_delta2;
};

#elif CV_SSE4_1

template <>
struct RGB2YCrCb_i<uchar>
{
    typedef uchar channel_type;

    RGB2YCrCb_i(int _srccn, int _blueIdx, bool _isCrCb)
        : srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { R2Y, G2Y, B2Y, YCRI, YCBI };
        static const int coeffs_yuv[] = { R2Y, G2Y, B2Y, R2VI, B2UI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if (blueIdx==0)
            std::swap(coeffs[0], coeffs[2]);

        short delta = 1 << (yuv_shift - 1);
        v_delta_16 = _mm_set1_epi16(delta);
        v_delta_32 = _mm_set1_epi32(delta);
        short delta2 = 1 + ColorChannel<uchar>::half() * 2;
        v_coeff = _mm_set_epi16(delta2, (short)coeffs[4], delta2, (short)coeffs[3], delta2, (short)coeffs[4], delta2, (short)coeffs[3]);
        if(isCrCb)
            v_shuffle2 = _mm_set_epi8(0x0, 0x0, 0x0, 0x0, 0xf, 0xe, 0xc, 0xb, 0xa, 0x8, 0x7, 0x6, 0x4, 0x3, 0x2, 0x0);
        else //if YUV
            v_shuffle2 = _mm_set_epi8(0x0, 0x0, 0x0, 0x0, 0xe, 0xf, 0xc, 0xa, 0xb, 0x8, 0x6, 0x7, 0x4, 0x2, 0x3, 0x0);
        haveSIMD = checkHardwareSupport(CV_CPU_SSE4_1);
    }

    // 16u x 8
    void process(__m128i* v_rgb, __m128i & v_crgb,
                 __m128i* v_rb, uchar * dst) const
    {
        v_rgb[0] = _mm_madd_epi16(v_rgb[0], v_crgb);
        v_rgb[1] = _mm_madd_epi16(v_rgb[1], v_crgb);
        v_rgb[2] = _mm_madd_epi16(v_rgb[2], v_crgb);
        v_rgb[3] = _mm_madd_epi16(v_rgb[3], v_crgb);
        v_rgb[0] = _mm_hadd_epi32(v_rgb[0], v_rgb[1]);
        v_rgb[2] = _mm_hadd_epi32(v_rgb[2], v_rgb[3]);
        v_rgb[0] = _mm_add_epi32(v_rgb[0], v_delta_32);
        v_rgb[2] = _mm_add_epi32(v_rgb[2], v_delta_32);
        v_rgb[0] = _mm_srai_epi32(v_rgb[0], yuv_shift);
        v_rgb[2] = _mm_srai_epi32(v_rgb[2], yuv_shift);
        __m128i v_y = _mm_packs_epi32(v_rgb[0], v_rgb[2]);

        v_rb[0] = _mm_cvtepu8_epi16(v_rb[0]);
        v_rb[1] = _mm_cvtepu8_epi16(v_rb[1]);
        v_rb[0] = _mm_sub_epi16(v_rb[0], _mm_unpacklo_epi16(v_y, v_y));
        v_rb[1] = _mm_sub_epi16(v_rb[1], _mm_unpackhi_epi16(v_y, v_y));
        v_rgb[0] = _mm_unpacklo_epi16(v_rb[0], v_delta_16);
        v_rgb[1] = _mm_unpackhi_epi16(v_rb[0], v_delta_16);
        v_rgb[2] = _mm_unpacklo_epi16(v_rb[1], v_delta_16);
        v_rgb[3] = _mm_unpackhi_epi16(v_rb[1], v_delta_16);
        v_rgb[0] = _mm_madd_epi16(v_rgb[0], v_coeff);
        v_rgb[1] = _mm_madd_epi16(v_rgb[1], v_coeff);
        v_rgb[2] = _mm_madd_epi16(v_rgb[2], v_coeff);
        v_rgb[3] = _mm_madd_epi16(v_rgb[3], v_coeff);
        v_rgb[0] = _mm_srai_epi32(v_rgb[0], yuv_shift);
        v_rgb[1] = _mm_srai_epi32(v_rgb[1], yuv_shift);
        v_rgb[2] = _mm_srai_epi32(v_rgb[2], yuv_shift);
        v_rgb[3] = _mm_srai_epi32(v_rgb[3], yuv_shift);
        v_rgb[0] = _mm_packs_epi32(v_rgb[0], v_rgb[1]);
        v_rgb[2] = _mm_packs_epi32(v_rgb[2], v_rgb[3]);
        v_rgb[0] = _mm_packus_epi16(v_rgb[0], v_rgb[2]);

        v_rb[0] = _mm_unpacklo_epi16(v_y, v_rgb[0]);
        v_rb[1] = _mm_unpackhi_epi16(v_y, v_rgb[0]);

        v_rb[0] = _mm_shuffle_epi8(v_rb[0], v_shuffle2);
        v_rb[1] = _mm_shuffle_epi8(v_rb[1], v_shuffle2);
        v_rb[1] = _mm_alignr_epi8(v_rb[1], _mm_slli_si128(v_rb[0], 4), 12);

        _mm_storel_epi64((__m128i *)(dst), v_rb[0]);
        _mm_storeu_si128((__m128i *)(dst + 8), v_rb[1]);
    }

    void operator()(const uchar * src, uchar * dst, int n) const
    {
        int scn = srccn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        int delta = ColorChannel<uchar>::half()*(1 << yuv_shift);
        n *= 3;

        if (haveSIMD)
        {
            __m128i v_shuffle;
            __m128i v_crgb;
            if (scn == 4)
            {
                if (bidx == 0)
                {
                    v_shuffle = _mm_set_epi8(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xc, 0xe, 0x8, 0xa, 0x4, 0x6, 0x0, 0x2);
                }
                else
                {
                    v_shuffle = _mm_set_epi8(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xe, 0xc, 0xa, 0x8, 0x6, 0x4, 0x2, 0x0);
                }
                v_crgb = _mm_set_epi16(0, (short)C2, (short)C1, (short)C0, 0, (short)C2, (short)C1, (short)C0);
                for ( ; i <= n - 24; i += 24, src += scn * 8)
                {
                    __m128i v_src[2];
                    v_src[0] = _mm_loadu_si128((__m128i const *)(src));
                    v_src[1] = _mm_loadu_si128((__m128i const *)(src + 16));

                    __m128i v_rgb[4];
                    v_rgb[0] = _mm_cvtepu8_epi16(v_src[0]);
                    v_rgb[1] = _mm_cvtepu8_epi16(_mm_srli_si128(v_src[0], 8));
                    v_rgb[2] = _mm_cvtepu8_epi16(v_src[1]);
                    v_rgb[3] = _mm_cvtepu8_epi16(_mm_srli_si128(v_src[1], 8));

                    __m128i v_rb[2];
                    v_rb[0] = _mm_shuffle_epi8(v_src[0], v_shuffle);
                    v_rb[1] = _mm_shuffle_epi8(v_src[1], v_shuffle);

                    process(v_rgb, v_crgb, v_rb, dst + i);
                }
            }
            else
            {
                if (bidx == 0)
                {
                    v_shuffle = _mm_set_epi8(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x9, 0xb, 0x6, 0x8, 0x3, 0x5, 0x0, 0x2);
                }
                else
                {
                    v_shuffle = _mm_set_epi8(0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xb, 0x9, 0x8, 0x6, 0x5, 0x3, 0x2, 0x0);
                }
                v_crgb = _mm_set_epi16(0, (short)C2, (short)C1, (short)C0, (short)C2, (short)C1, (short)C0, 0);
                for ( ; i <= n - 24; i += 24, src += scn * 8)
                {
                    __m128i v_src[2];
                    v_src[0] = _mm_loadu_si128((__m128i const *)(src));
                    v_src[1] = _mm_loadl_epi64((__m128i const *)(src + 16));

                    __m128i v_rgb[4];
                    v_rgb[0] = _mm_cvtepu8_epi16(_mm_slli_si128(v_src[0], 1));
                    v_rgb[1] = _mm_cvtepu8_epi16(_mm_srli_si128(v_src[0], 5));
                    v_rgb[2] = _mm_cvtepu8_epi16(_mm_alignr_epi8(v_src[1], v_src[0], 11));
                    v_rgb[3] = _mm_cvtepu8_epi16(_mm_srli_si128(v_src[1], 1));

                    __m128i v_rb[2];
                    v_rb[0] = _mm_shuffle_epi8(v_src[0], v_shuffle);
                    v_rb[1] = _mm_shuffle_epi8(_mm_alignr_epi8(v_src[1], v_src[0], 12), v_shuffle);

                    process(v_rgb, v_crgb, v_rb, dst + i);
                }
            }
        }

        for ( ; i < n; i += 3, src += scn)
        {
            int Y = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, yuv_shift);
            int Cr = CV_DESCALE((src[bidx^2] - Y)*C3 + delta, yuv_shift);
            int Cb = CV_DESCALE((src[bidx] - Y)*C4 + delta, yuv_shift);
            dst[i] = saturate_cast<uchar>(Y);
            dst[i+1+yuvOrder] = saturate_cast<uchar>(Cr);
            dst[i+2-yuvOrder] = saturate_cast<uchar>(Cb);
        }
    }

    __m128i v_delta_16, v_delta_32;
    __m128i v_coeff;
    __m128i v_shuffle2;
    int srccn, blueIdx, coeffs[5];
    bool isCrCb;
    bool haveSIMD;
};

template <>
struct RGB2YCrCb_i<ushort>
{
    typedef ushort channel_type;

    RGB2YCrCb_i(int _srccn, int _blueIdx, bool _isCrCb)
        : srccn(_srccn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { R2Y, G2Y, B2Y, YCRI, YCBI };
        static const int coeffs_yuv[] = { R2Y, G2Y, B2Y, R2VI, B2UI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 5*sizeof(coeffs[0]));
        if (blueIdx==0)
            std::swap(coeffs[0], coeffs[2]);

        v_c0 = _mm_set1_epi32(coeffs[0]);
        v_c1 = _mm_set1_epi32(coeffs[1]);
        v_c2 = _mm_set1_epi32(coeffs[2]);
        v_c3 = _mm_set1_epi32(coeffs[3]);
        v_c4 = _mm_set1_epi32(coeffs[4]);
        v_delta2 = _mm_set1_epi32(1 << (yuv_shift - 1));
        v_delta = _mm_set1_epi32(ColorChannel<ushort>::half()*(1 << yuv_shift));
        v_delta = _mm_add_epi32(v_delta, v_delta2);
        v_zero = _mm_setzero_si128();

        haveSIMD = checkHardwareSupport(CV_CPU_SSE4_1);
    }

    // 16u x 8
    void process(__m128i v_r, __m128i v_g, __m128i v_b,
                 __m128i & v_y, __m128i & v_cr, __m128i & v_cb) const
    {
        __m128i v_r_p = _mm_unpacklo_epi16(v_r, v_zero);
        __m128i v_g_p = _mm_unpacklo_epi16(v_g, v_zero);
        __m128i v_b_p = _mm_unpacklo_epi16(v_b, v_zero);

        __m128i v_y0 = _mm_add_epi32(_mm_mullo_epi32(v_r_p, v_c0),
                       _mm_add_epi32(_mm_mullo_epi32(v_g_p, v_c1),
                                     _mm_mullo_epi32(v_b_p, v_c2)));
        v_y0 = _mm_srli_epi32(_mm_add_epi32(v_delta2, v_y0), yuv_shift);

        __m128i v_cr0 = _mm_mullo_epi32(_mm_sub_epi32(blueIdx == 2 ? v_r_p : v_b_p, v_y0), v_c3);
        __m128i v_cb0 = _mm_mullo_epi32(_mm_sub_epi32(blueIdx == 0 ? v_r_p : v_b_p, v_y0), v_c4);
        v_cr0 = _mm_srai_epi32(_mm_add_epi32(v_delta, v_cr0), yuv_shift);
        v_cb0 = _mm_srai_epi32(_mm_add_epi32(v_delta, v_cb0), yuv_shift);

        v_r_p = _mm_unpackhi_epi16(v_r, v_zero);
        v_g_p = _mm_unpackhi_epi16(v_g, v_zero);
        v_b_p = _mm_unpackhi_epi16(v_b, v_zero);

        __m128i v_y1 = _mm_add_epi32(_mm_mullo_epi32(v_r_p, v_c0),
                       _mm_add_epi32(_mm_mullo_epi32(v_g_p, v_c1),
                                     _mm_mullo_epi32(v_b_p, v_c2)));
        v_y1 = _mm_srli_epi32(_mm_add_epi32(v_delta2, v_y1), yuv_shift);

        __m128i v_cr1 = _mm_mullo_epi32(_mm_sub_epi32(blueIdx == 2 ? v_r_p : v_b_p, v_y1), v_c3);
        __m128i v_cb1 = _mm_mullo_epi32(_mm_sub_epi32(blueIdx == 0 ? v_r_p : v_b_p, v_y1), v_c4);
        v_cr1 = _mm_srai_epi32(_mm_add_epi32(v_delta, v_cr1), yuv_shift);
        v_cb1 = _mm_srai_epi32(_mm_add_epi32(v_delta, v_cb1), yuv_shift);

        v_y = _mm_packus_epi32(v_y0, v_y1);
        v_cr = _mm_packus_epi32(v_cr0, v_cr1);
        v_cb = _mm_packus_epi32(v_cb0, v_cb1);
    }

    void operator()(const ushort * src, ushort * dst, int n) const
    {
        int scn = srccn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3], C4 = coeffs[4];
        int delta = ColorChannel<ushort>::half()*(1 << yuv_shift);
        n *= 3;

        if (haveSIMD)
        {
            for ( ; i <= n - 48; i += 48, src += scn * 16)
            {
                __m128i v_r0 = _mm_loadu_si128((__m128i const *)(src));
                __m128i v_r1 = _mm_loadu_si128((__m128i const *)(src + 8));
                __m128i v_g0 = _mm_loadu_si128((__m128i const *)(src + 16));
                __m128i v_g1 = _mm_loadu_si128((__m128i const *)(src + 24));
                __m128i v_b0 = _mm_loadu_si128((__m128i const *)(src + 32));
                __m128i v_b1 = _mm_loadu_si128((__m128i const *)(src + 40));

                if (scn == 4)
                {
                    __m128i v_a0 = _mm_loadu_si128((__m128i const *)(src + 48));
                    __m128i v_a1 = _mm_loadu_si128((__m128i const *)(src + 56));

                    _mm_deinterleave_epi16(v_r0, v_r1, v_g0, v_g1,
                                           v_b0, v_b1, v_a0, v_a1);
                }
                else
                    _mm_deinterleave_epi16(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);

                __m128i v_y0 = v_zero, v_cr0 = v_zero, v_cb0 = v_zero;
                process(v_r0, v_g0, v_b0,
                        v_y0, v_cr0, v_cb0);

                __m128i v_y1 = v_zero, v_cr1 = v_zero, v_cb1 = v_zero;
                process(v_r1, v_g1, v_b1,
                        v_y1, v_cr1, v_cb1);

                if(isCrCb)
                    _mm_interleave_epi16(v_y0, v_y1, v_cr0, v_cr1, v_cb0, v_cb1);
                else //YUV
                    _mm_interleave_epi16(v_y0, v_y1, v_cb0, v_cb1, v_cr0, v_cr1);

                _mm_storeu_si128((__m128i *)(dst + i), v_y0);
                _mm_storeu_si128((__m128i *)(dst + i + 8), v_y1);
                _mm_storeu_si128((__m128i *)(dst + i + 16 + yuvOrder*16), v_cr0);
                _mm_storeu_si128((__m128i *)(dst + i + 24 + yuvOrder*16), v_cr1);
                _mm_storeu_si128((__m128i *)(dst + i + 32 - yuvOrder*16), v_cb0);
                _mm_storeu_si128((__m128i *)(dst + i + 40 - yuvOrder*16), v_cb1);
            }
        }

        for ( ; i < n; i += 3, src += scn)
        {
            int Y = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, yuv_shift);
            int Cr = CV_DESCALE((src[bidx^2] - Y)*C3 + delta, yuv_shift);
            int Cb = CV_DESCALE((src[bidx] - Y)*C4 + delta, yuv_shift);
            dst[i] = saturate_cast<ushort>(Y);
            dst[i+1+yuvOrder] = saturate_cast<ushort>(Cr);
            dst[i+2-yuvOrder] = saturate_cast<ushort>(Cb);
        }
    }

    int srccn, blueIdx, coeffs[5];
    bool isCrCb;
    __m128i v_c0, v_c1, v_c2;
    __m128i v_c3, v_c4, v_delta, v_delta2;
    __m128i v_zero;
    bool haveSIMD;
};

#endif // CV_SSE4_1

template<typename _Tp> struct YCrCb2RGB_f
{
    typedef _Tp channel_type;

    YCrCb2RGB_f(int _dstcn, int _blueIdx, bool _isCrCb)
        : dstcn(_dstcn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const float coeffs_cbr[] = {CR2RF, CR2GF, CB2GF, CB2BF};
        static const float coeffs_yuv[] = { V2RF,  V2GF,  U2GF,  U2BF};
        memcpy(coeffs, isCrCb ? coeffs_cbr : coeffs_yuv, 4*sizeof(coeffs[0]));
    }
    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const _Tp delta = ColorChannel<_Tp>::half(), alpha = ColorChannel<_Tp>::max();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3];
        n *= 3;
        for(int i = 0; i < n; i += 3, dst += dcn)
        {
            _Tp Y = src[i];
            _Tp Cr = src[i+1+yuvOrder];
            _Tp Cb = src[i+2-yuvOrder];

            _Tp b = saturate_cast<_Tp>(Y + (Cb - delta)*C3);
            _Tp g = saturate_cast<_Tp>(Y + (Cb - delta)*C2 + (Cr - delta)*C1);
            _Tp r = saturate_cast<_Tp>(Y + (Cr - delta)*C0);

            dst[bidx] = b; dst[1] = g; dst[bidx^2] = r;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    bool isCrCb;
    float coeffs[4];
};

#if CV_NEON

template <>
struct YCrCb2RGB_f<float>
{
    typedef float channel_type;

    YCrCb2RGB_f(int _dstcn, int _blueIdx, bool _isCrCb)
        : dstcn(_dstcn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const float coeffs_cbr[] = {CR2RF, CR2GF, CB2GF, CB2BF};
        static const float coeffs_yuv[] = { V2RF,  V2GF,  U2GF,  U2BF};
        memcpy(coeffs, isCrCb ? coeffs_cbr : coeffs_yuv, 4*sizeof(coeffs[0]));

        v_c0 = vdupq_n_f32(coeffs[0]);
        v_c1 = vdupq_n_f32(coeffs[1]);
        v_c2 = vdupq_n_f32(coeffs[2]);
        v_c3 = vdupq_n_f32(coeffs[3]);
        v_delta = vdupq_n_f32(ColorChannel<float>::half());
        v_alpha = vdupq_n_f32(ColorChannel<float>::max());
    }

    void operator()(const float* src, float* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const float delta = ColorChannel<float>::half(), alpha = ColorChannel<float>::max();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3];
        n *= 3;

        if (dcn == 3)
            for ( ; i <= n - 12; i += 12, dst += 12)
            {
                float32x4x3_t v_src = vld3q_f32(src + i), v_dst;
                float32x4_t v_Y = v_src.val[0], v_Cr = v_src.val[1+yuvOrder], v_Cb = v_src.val[2-yuvOrder];

                v_dst.val[bidx] = vmlaq_f32(v_Y, vsubq_f32(v_Cb, v_delta), v_c3);
                v_dst.val[1] = vaddq_f32(vmlaq_f32(vmulq_f32(vsubq_f32(v_Cb, v_delta), v_c2), vsubq_f32(v_Cr, v_delta), v_c1), v_Y);
                v_dst.val[bidx^2] = vmlaq_f32(v_Y, vsubq_f32(v_Cr, v_delta), v_c0);

                vst3q_f32(dst, v_dst);
            }
        else
            for ( ; i <= n - 12; i += 12, dst += 16)
            {
                float32x4x3_t v_src = vld3q_f32(src + i);
                float32x4x4_t v_dst;
                float32x4_t v_Y = v_src.val[0], v_Cr = v_src.val[1+yuvOrder], v_Cb = v_src.val[2-yuvOrder];

                v_dst.val[bidx] = vmlaq_f32(v_Y, vsubq_f32(v_Cb, v_delta), v_c3);
                v_dst.val[1] = vaddq_f32(vmlaq_f32(vmulq_f32(vsubq_f32(v_Cb, v_delta), v_c2), vsubq_f32(v_Cr, v_delta), v_c1), v_Y);
                v_dst.val[bidx^2] = vmlaq_f32(v_Y, vsubq_f32(v_Cr, v_delta), v_c0);
                v_dst.val[3] = v_alpha;

                vst4q_f32(dst, v_dst);
            }

        for ( ; i < n; i += 3, dst += dcn)
        {
            float Y = src[i], Cr = src[i+1+yuvOrder], Cb = src[i+2-yuvOrder];

            float b = Y + (Cb - delta)*C3;
            float g = Y + (Cb - delta)*C2 + (Cr - delta)*C1;
            float r = Y + (Cr - delta)*C0;

            dst[bidx] = b; dst[1] = g; dst[bidx^2] = r;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    bool isCrCb;
    float coeffs[4];
    float32x4_t v_c0, v_c1, v_c2, v_c3, v_alpha, v_delta;
};

#elif CV_SSE2

template <>
struct YCrCb2RGB_f<float>
{
    typedef float channel_type;

    YCrCb2RGB_f(int _dstcn, int _blueIdx, bool _isCrCb)
        : dstcn(_dstcn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const float coeffs_cbr[] = {CR2RF, CR2GF, CB2GF, CB2BF};
        static const float coeffs_yuv[] = { V2RF,  V2GF,  U2GF,  U2BF};
        memcpy(coeffs, isCrCb ? coeffs_cbr : coeffs_yuv, 4*sizeof(coeffs[0]));

        v_c0 = _mm_set1_ps(coeffs[0]);
        v_c1 = _mm_set1_ps(coeffs[1]);
        v_c2 = _mm_set1_ps(coeffs[2]);
        v_c3 = _mm_set1_ps(coeffs[3]);
        v_delta = _mm_set1_ps(ColorChannel<float>::half());
        v_alpha = _mm_set1_ps(ColorChannel<float>::max());

        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
    }

    void process(__m128 v_y, __m128 v_cr, __m128 v_cb,
                 __m128 & v_r, __m128 & v_g, __m128 & v_b) const
    {
        v_cb = _mm_sub_ps(v_cb, v_delta);
        v_cr = _mm_sub_ps(v_cr, v_delta);

        if (!isCrCb)
            std::swap(v_cb, v_cr);

        v_b = _mm_mul_ps(v_cb, v_c3);
        v_g = _mm_add_ps(_mm_mul_ps(v_cb, v_c2), _mm_mul_ps(v_cr, v_c1));
        v_r = _mm_mul_ps(v_cr, v_c0);

        v_b = _mm_add_ps(v_b, v_y);
        v_g = _mm_add_ps(v_g, v_y);
        v_r = _mm_add_ps(v_r, v_y);

        if (blueIdx == 0)
            std::swap(v_b, v_r);
    }

    void operator()(const float* src, float* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const float delta = ColorChannel<float>::half(), alpha = ColorChannel<float>::max();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3];
        n *= 3;

        if (haveSIMD)
        {
            for ( ; i <= n - 24; i += 24, dst += 8 * dcn)
            {
                __m128 v_y0 = _mm_loadu_ps(src + i);
                __m128 v_y1 = _mm_loadu_ps(src + i + 4);
                __m128 v_cr0 = _mm_loadu_ps(src + i + 8);
                __m128 v_cr1 = _mm_loadu_ps(src + i + 12);
                __m128 v_cb0 = _mm_loadu_ps(src + i + 16);
                __m128 v_cb1 = _mm_loadu_ps(src + i + 20);

                _mm_deinterleave_ps(v_y0, v_y1, v_cr0, v_cr1, v_cb0, v_cb1);

                __m128 v_r0, v_g0, v_b0;
                process(v_y0, v_cr0, v_cb0,
                        v_r0, v_g0, v_b0);

                __m128 v_r1, v_g1, v_b1;
                process(v_y1, v_cr1, v_cb1,
                        v_r1, v_g1, v_b1);

                __m128 v_a0 = v_alpha, v_a1 = v_alpha;

                if (dcn == 3)
                    _mm_interleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);
                else
                    _mm_interleave_ps(v_r0, v_r1, v_g0, v_g1,
                                      v_b0, v_b1, v_a0, v_a1);

                _mm_storeu_ps(dst, v_r0);
                _mm_storeu_ps(dst + 4, v_r1);
                _mm_storeu_ps(dst + 8, v_g0);
                _mm_storeu_ps(dst + 12, v_g1);
                _mm_storeu_ps(dst + 16, v_b0);
                _mm_storeu_ps(dst + 20, v_b1);

                if (dcn == 4)
                {
                    _mm_storeu_ps(dst + 24, v_a0);
                    _mm_storeu_ps(dst + 28, v_a1);
                }
            }
        }

        for ( ; i < n; i += 3, dst += dcn)
        {
            float Y = src[i], Cr = src[i+1+yuvOrder], Cb = src[i+2-yuvOrder];

            float b = Y + (Cb - delta)*C3;
            float g = Y + (Cb - delta)*C2 + (Cr - delta)*C1;
            float r = Y + (Cr - delta)*C0;

            dst[bidx] = b; dst[1] = g; dst[bidx^2] = r;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    bool isCrCb;
    float coeffs[4];

    __m128 v_c0, v_c1, v_c2, v_c3, v_alpha, v_delta;
    bool haveSIMD;
};

#endif

template<typename _Tp> struct YCrCb2RGB_i
{
    typedef _Tp channel_type;

    YCrCb2RGB_i(int _dstcn, int _blueIdx, bool _isCrCb)
        : dstcn(_dstcn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { CR2RI, CR2GI, CB2GI, CB2BI};
        static const int coeffs_yuv[] = {  V2RI,  V2GI,  U2GI, U2BI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 4*sizeof(coeffs[0]));
    }

    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const _Tp delta = ColorChannel<_Tp>::half(), alpha = ColorChannel<_Tp>::max();
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3];
        n *= 3;
        for(int i = 0; i < n; i += 3, dst += dcn)
        {
            _Tp Y = src[i];
            _Tp Cr = src[i+1+yuvOrder];
            _Tp Cb = src[i+2-yuvOrder];

            int b = Y + CV_DESCALE((Cb - delta)*C3, yuv_shift);
            int g = Y + CV_DESCALE((Cb - delta)*C2 + (Cr - delta)*C1, yuv_shift);
            int r = Y + CV_DESCALE((Cr - delta)*C0, yuv_shift);

            dst[bidx] = saturate_cast<_Tp>(b);
            dst[1] = saturate_cast<_Tp>(g);
            dst[bidx^2] = saturate_cast<_Tp>(r);
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    bool isCrCb;
    int coeffs[4];
};

#if CV_NEON

template <>
struct YCrCb2RGB_i<uchar>
{
    typedef uchar channel_type;

    YCrCb2RGB_i(int _dstcn, int _blueIdx, bool _isCrCb)
        : dstcn(_dstcn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { CR2RI, CR2GI, CB2GI, CB2BI};
        static const int coeffs_yuv[] = {  V2RI,  V2GI,  U2GI, U2BI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 4*sizeof(coeffs[0]));

        v_c0 = vdupq_n_s32(coeffs[0]);
        v_c1 = vdupq_n_s32(coeffs[1]);
        v_c2 = vdupq_n_s32(coeffs[2]);
        v_c3 = vdupq_n_s32(coeffs[3]);
        v_delta = vdup_n_s16(ColorChannel<uchar>::half());
        v_delta2 = vdupq_n_s32(1 << (yuv_shift - 1));
        v_alpha = vdup_n_u8(ColorChannel<uchar>::max());
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const uchar delta = ColorChannel<uchar>::half(), alpha = ColorChannel<uchar>::max();
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3];
        n *= 3;

        for ( ; i <= n - 24; i += 24, dst += dcn * 8)
        {
            uint8x8x3_t v_src = vld3_u8(src + i);
            int16x8x3_t v_src16;
            v_src16.val[0] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[0]));
            v_src16.val[1] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[1]));
            v_src16.val[2] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[2]));

            int16x4_t v_Y = vget_low_s16(v_src16.val[0]),
                      v_Cr = vget_low_s16(v_src16.val[1+yuvOrder]),
                      v_Cb = vget_low_s16(v_src16.val[2-yuvOrder]);

            int32x4_t v_b0 = vmulq_s32(v_c3, vsubl_s16(v_Cb, v_delta));
            v_b0 = vaddw_s16(vshrq_n_s32(vaddq_s32(v_b0, v_delta2), yuv_shift), v_Y);
            int32x4_t v_g0 = vmlaq_s32(vmulq_s32(vsubl_s16(v_Cr, v_delta), v_c1), vsubl_s16(v_Cb, v_delta), v_c2);
            v_g0 = vaddw_s16(vshrq_n_s32(vaddq_s32(v_g0, v_delta2), yuv_shift), v_Y);
            int32x4_t v_r0 = vmulq_s32(v_c0, vsubl_s16(v_Cr, v_delta));
            v_r0 = vaddw_s16(vshrq_n_s32(vaddq_s32(v_r0, v_delta2), yuv_shift), v_Y);

            v_Y = vget_high_s16(v_src16.val[0]);
            v_Cr = vget_high_s16(v_src16.val[1+yuvOrder]);
            v_Cb = vget_high_s16(v_src16.val[2-yuvOrder]);

            int32x4_t v_b1 = vmulq_s32(v_c3, vsubl_s16(v_Cb, v_delta));
            v_b1 = vaddw_s16(vshrq_n_s32(vaddq_s32(v_b1, v_delta2), yuv_shift), v_Y);
            int32x4_t v_g1 = vmlaq_s32(vmulq_s32(vsubl_s16(v_Cr, v_delta), v_c1), vsubl_s16(v_Cb, v_delta), v_c2);
            v_g1 = vaddw_s16(vshrq_n_s32(vaddq_s32(v_g1, v_delta2), yuv_shift), v_Y);
            int32x4_t v_r1 = vmulq_s32(v_c0, vsubl_s16(v_Cr, v_delta));
            v_r1 = vaddw_s16(vshrq_n_s32(vaddq_s32(v_r1, v_delta2), yuv_shift), v_Y);

            uint8x8_t v_b = vqmovun_s16(vcombine_s16(vmovn_s32(v_b0), vmovn_s32(v_b1)));
            uint8x8_t v_g = vqmovun_s16(vcombine_s16(vmovn_s32(v_g0), vmovn_s32(v_g1)));
            uint8x8_t v_r = vqmovun_s16(vcombine_s16(vmovn_s32(v_r0), vmovn_s32(v_r1)));

            if (dcn == 3)
            {
                uint8x8x3_t v_dst;
                v_dst.val[bidx] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[bidx^2] = v_r;
                vst3_u8(dst, v_dst);
            }
            else
            {
                uint8x8x4_t v_dst;
                v_dst.val[bidx] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[bidx^2] = v_r;
                v_dst.val[3] = v_alpha;
                vst4_u8(dst, v_dst);
            }
        }

        for ( ; i < n; i += 3, dst += dcn)
        {
            uchar Y = src[i];
            uchar Cr = src[i+1+yuvOrder];
            uchar Cb = src[i+2-yuvOrder];

            int b = Y + CV_DESCALE((Cb - delta)*C3, yuv_shift);
            int g = Y + CV_DESCALE((Cb - delta)*C2 + (Cr - delta)*C1, yuv_shift);
            int r = Y + CV_DESCALE((Cr - delta)*C0, yuv_shift);

            dst[bidx] = saturate_cast<uchar>(b);
            dst[1] = saturate_cast<uchar>(g);
            dst[bidx^2] = saturate_cast<uchar>(r);
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    bool isCrCb;
    int coeffs[4];

    int32x4_t v_c0, v_c1, v_c2, v_c3, v_delta2;
    int16x4_t v_delta;
    uint8x8_t v_alpha;
};

template <>
struct YCrCb2RGB_i<ushort>
{
    typedef ushort channel_type;

    YCrCb2RGB_i(int _dstcn, int _blueIdx, bool _isCrCb)
        : dstcn(_dstcn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { CR2RI, CR2GI, CB2GI, CB2BI};
        static const int coeffs_yuv[] = {  V2RI,  V2GI,  U2GI, U2BI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 4*sizeof(coeffs[0]));

        v_c0 = vdupq_n_s32(coeffs[0]);
        v_c1 = vdupq_n_s32(coeffs[1]);
        v_c2 = vdupq_n_s32(coeffs[2]);
        v_c3 = vdupq_n_s32(coeffs[3]);
        v_delta = vdupq_n_s32(ColorChannel<ushort>::half());
        v_delta2 = vdupq_n_s32(1 << (yuv_shift - 1));
        v_alpha = vdupq_n_u16(ColorChannel<ushort>::max());
        v_alpha2 = vget_low_u16(v_alpha);
    }

    void operator()(const ushort* src, ushort* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const ushort delta = ColorChannel<ushort>::half(), alpha = ColorChannel<ushort>::max();
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3];
        n *= 3;

        for ( ; i <= n - 24; i += 24, dst += dcn * 8)
        {
            uint16x8x3_t v_src = vld3q_u16(src + i);

            int32x4_t v_Y = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[0]))),
                      v_Cr = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[1+yuvOrder]))),
                      v_Cb = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[2-yuvOrder])));

            int32x4_t v_b0 = vmulq_s32(v_c3, vsubq_s32(v_Cb, v_delta));
            v_b0 = vaddq_s32(vshrq_n_s32(vaddq_s32(v_b0, v_delta2), yuv_shift), v_Y);
            int32x4_t v_g0 = vmlaq_s32(vmulq_s32(vsubq_s32(v_Cr, v_delta), v_c1), vsubq_s32(v_Cb, v_delta), v_c2);
            v_g0 = vaddq_s32(vshrq_n_s32(vaddq_s32(v_g0, v_delta2), yuv_shift), v_Y);
            int32x4_t v_r0 = vmulq_s32(v_c0, vsubq_s32(v_Cr, v_delta));
            v_r0 = vaddq_s32(vshrq_n_s32(vaddq_s32(v_r0, v_delta2), yuv_shift), v_Y);

            v_Y = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[0]))),
            v_Cr = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[1+yuvOrder]))),
            v_Cb = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[2-yuvOrder])));

            int32x4_t v_b1 = vmulq_s32(v_c3, vsubq_s32(v_Cb, v_delta));
            v_b1 = vaddq_s32(vshrq_n_s32(vaddq_s32(v_b1, v_delta2), yuv_shift), v_Y);
            int32x4_t v_g1 = vmlaq_s32(vmulq_s32(vsubq_s32(v_Cr, v_delta), v_c1), vsubq_s32(v_Cb, v_delta), v_c2);
            v_g1 = vaddq_s32(vshrq_n_s32(vaddq_s32(v_g1, v_delta2), yuv_shift), v_Y);
            int32x4_t v_r1 = vmulq_s32(v_c0, vsubq_s32(v_Cr, v_delta));
            v_r1 = vaddq_s32(vshrq_n_s32(vaddq_s32(v_r1, v_delta2), yuv_shift), v_Y);

            uint16x8_t v_b = vcombine_u16(vqmovun_s32(v_b0), vqmovun_s32(v_b1));
            uint16x8_t v_g = vcombine_u16(vqmovun_s32(v_g0), vqmovun_s32(v_g1));
            uint16x8_t v_r = vcombine_u16(vqmovun_s32(v_r0), vqmovun_s32(v_r1));

            if (dcn == 3)
            {
                uint16x8x3_t v_dst;
                v_dst.val[bidx] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[bidx^2] = v_r;
                vst3q_u16(dst, v_dst);
            }
            else
            {
                uint16x8x4_t v_dst;
                v_dst.val[bidx] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[bidx^2] = v_r;
                v_dst.val[3] = v_alpha;
                vst4q_u16(dst, v_dst);
            }
        }

        for ( ; i <= n - 12; i += 12, dst += dcn * 4)
        {
            uint16x4x3_t v_src = vld3_u16(src + i);

            int32x4_t v_Y = vreinterpretq_s32_u32(vmovl_u16(v_src.val[0])),
                      v_Cr = vreinterpretq_s32_u32(vmovl_u16(v_src.val[1+yuvOrder])),
                      v_Cb = vreinterpretq_s32_u32(vmovl_u16(v_src.val[2-yuvOrder]));

            int32x4_t v_b = vmulq_s32(v_c3, vsubq_s32(v_Cb, v_delta));
            v_b = vaddq_s32(vshrq_n_s32(vaddq_s32(v_b, v_delta2), yuv_shift), v_Y);
            int32x4_t v_g = vmlaq_s32(vmulq_s32(vsubq_s32(v_Cr, v_delta), v_c1), vsubq_s32(v_Cb, v_delta), v_c2);
            v_g = vaddq_s32(vshrq_n_s32(vaddq_s32(v_g, v_delta2), yuv_shift), v_Y);
            int32x4_t v_r = vmulq_s32(vsubq_s32(v_Cr, v_delta), v_c0);
            v_r = vaddq_s32(vshrq_n_s32(vaddq_s32(v_r, v_delta2), yuv_shift), v_Y);

            uint16x4_t v_bd = vqmovun_s32(v_b);
            uint16x4_t v_gd = vqmovun_s32(v_g);
            uint16x4_t v_rd = vqmovun_s32(v_r);

            if (dcn == 3)
            {
                uint16x4x3_t v_dst;
                v_dst.val[bidx] = v_bd;
                v_dst.val[1] = v_gd;
                v_dst.val[bidx^2] = v_rd;
                vst3_u16(dst, v_dst);
            }
            else
            {
                uint16x4x4_t v_dst;
                v_dst.val[bidx] = v_bd;
                v_dst.val[1] = v_gd;
                v_dst.val[bidx^2] = v_rd;
                v_dst.val[3] = v_alpha2;
                vst4_u16(dst, v_dst);
            }
        }

        for ( ; i < n; i += 3, dst += dcn)
        {
            ushort Y = src[i];
            ushort Cr = src[i+1+yuvOrder];
            ushort Cb = src[i+2-yuvOrder];

            int b = Y + CV_DESCALE((Cb - delta)*C3, yuv_shift);
            int g = Y + CV_DESCALE((Cb - delta)*C2 + (Cr - delta)*C1, yuv_shift);
            int r = Y + CV_DESCALE((Cr - delta)*C0, yuv_shift);

            dst[bidx] = saturate_cast<ushort>(b);
            dst[1] = saturate_cast<ushort>(g);
            dst[bidx^2] = saturate_cast<ushort>(r);
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    bool isCrCb;
    int coeffs[4];

    int32x4_t v_c0, v_c1, v_c2, v_c3, v_delta2, v_delta;
    uint16x8_t v_alpha;
    uint16x4_t v_alpha2;
};

#elif CV_SSE2

template <>
struct YCrCb2RGB_i<uchar>
{
    typedef uchar channel_type;

    YCrCb2RGB_i(int _dstcn, int _blueIdx, bool _isCrCb)
        : dstcn(_dstcn), blueIdx(_blueIdx), isCrCb(_isCrCb)
    {
        static const int coeffs_crb[] = { CR2RI, CR2GI, CB2GI, CB2BI};
        static const int coeffs_yuv[] = {  V2RI,  V2GI,  U2GI, U2BI };
        memcpy(coeffs, isCrCb ? coeffs_crb : coeffs_yuv, 4*sizeof(coeffs[0]));

        v_c0 = _mm_set1_epi16((short)coeffs[0]);
        v_c1 = _mm_set1_epi16((short)coeffs[1]);
        v_c2 = _mm_set1_epi16((short)coeffs[2]);
        v_c3 = _mm_set1_epi16((short)coeffs[3]);
        v_delta = _mm_set1_epi16(ColorChannel<uchar>::half());
        v_delta2 = _mm_set1_epi32(1 << (yuv_shift - 1));
        v_zero = _mm_setzero_si128();

        uchar alpha = ColorChannel<uchar>::max();
        v_alpha = _mm_set1_epi8(*(char *)&alpha);

        // when using YUV, one of coefficients is bigger than std::numeric_limits<short>::max(),
        //which is not appropriate for SSE
        useSSE = isCrCb;
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
    }

#if CV_SSE4_1
    // 16s x 8
    void process(__m128i* v_src, __m128i* v_shuffle,
                 __m128i* v_coeffs) const
    {
        __m128i v_ycrcb[3];
        v_ycrcb[0] = _mm_shuffle_epi8(v_src[0], v_shuffle[0]);
        v_ycrcb[1] = _mm_shuffle_epi8(_mm_alignr_epi8(v_src[1], v_src[0], 8), v_shuffle[0]);
        v_ycrcb[2] = _mm_shuffle_epi8(v_src[1], v_shuffle[0]);

        __m128i v_y[3];
        v_y[1] = _mm_shuffle_epi8(v_src[0], v_shuffle[1]);
        v_y[2] = _mm_srli_si128(_mm_shuffle_epi8(_mm_alignr_epi8(v_src[1], v_src[0], 15), v_shuffle[1]), 1);
        v_y[0] = _mm_unpacklo_epi8(v_y[1], v_zero);
        v_y[1] = _mm_unpackhi_epi8(v_y[1], v_zero);
        v_y[2] = _mm_unpacklo_epi8(v_y[2], v_zero);

        __m128i v_rgb[6];
        v_rgb[0] = _mm_unpacklo_epi8(v_ycrcb[0], v_zero);
        v_rgb[1] = _mm_unpackhi_epi8(v_ycrcb[0], v_zero);
        v_rgb[2] = _mm_unpacklo_epi8(v_ycrcb[1], v_zero);
        v_rgb[3] = _mm_unpackhi_epi8(v_ycrcb[1], v_zero);
        v_rgb[4] = _mm_unpacklo_epi8(v_ycrcb[2], v_zero);
        v_rgb[5] = _mm_unpackhi_epi8(v_ycrcb[2], v_zero);

        v_rgb[0] = _mm_sub_epi16(v_rgb[0], v_delta);
        v_rgb[1] = _mm_sub_epi16(v_rgb[1], v_delta);
        v_rgb[2] = _mm_sub_epi16(v_rgb[2], v_delta);
        v_rgb[3] = _mm_sub_epi16(v_rgb[3], v_delta);
        v_rgb[4] = _mm_sub_epi16(v_rgb[4], v_delta);
        v_rgb[5] = _mm_sub_epi16(v_rgb[5], v_delta);

        v_rgb[0] = _mm_madd_epi16(v_rgb[0], v_coeffs[0]);
        v_rgb[1] = _mm_madd_epi16(v_rgb[1], v_coeffs[1]);
        v_rgb[2] = _mm_madd_epi16(v_rgb[2], v_coeffs[2]);
        v_rgb[3] = _mm_madd_epi16(v_rgb[3], v_coeffs[0]);
        v_rgb[4] = _mm_madd_epi16(v_rgb[4], v_coeffs[1]);
        v_rgb[5] = _mm_madd_epi16(v_rgb[5], v_coeffs[2]);

        v_rgb[0] = _mm_add_epi32(v_rgb[0], v_delta2);
        v_rgb[1] = _mm_add_epi32(v_rgb[1], v_delta2);
        v_rgb[2] = _mm_add_epi32(v_rgb[2], v_delta2);
        v_rgb[3] = _mm_add_epi32(v_rgb[3], v_delta2);
        v_rgb[4] = _mm_add_epi32(v_rgb[4], v_delta2);
        v_rgb[5] = _mm_add_epi32(v_rgb[5], v_delta2);

        v_rgb[0] = _mm_srai_epi32(v_rgb[0], yuv_shift);
        v_rgb[1] = _mm_srai_epi32(v_rgb[1], yuv_shift);
        v_rgb[2] = _mm_srai_epi32(v_rgb[2], yuv_shift);
        v_rgb[3] = _mm_srai_epi32(v_rgb[3], yuv_shift);
        v_rgb[4] = _mm_srai_epi32(v_rgb[4], yuv_shift);
        v_rgb[5] = _mm_srai_epi32(v_rgb[5], yuv_shift);

        v_rgb[0] = _mm_packs_epi32(v_rgb[0], v_rgb[1]);
        v_rgb[2] = _mm_packs_epi32(v_rgb[2], v_rgb[3]);
        v_rgb[4] = _mm_packs_epi32(v_rgb[4], v_rgb[5]);

        v_rgb[0] = _mm_add_epi16(v_rgb[0], v_y[0]);
        v_rgb[2] = _mm_add_epi16(v_rgb[2], v_y[1]);
        v_rgb[4] = _mm_add_epi16(v_rgb[4], v_y[2]);

        v_src[0] = _mm_packus_epi16(v_rgb[0], v_rgb[2]);
        v_src[1] = _mm_packus_epi16(v_rgb[4], v_rgb[4]);
    }
#endif // CV_SSE4_1

    // 16s x 8
    void process(__m128i v_y, __m128i v_cr, __m128i v_cb,
                 __m128i & v_r, __m128i & v_g, __m128i & v_b) const
    {
        v_cr = _mm_sub_epi16(v_cr, v_delta);
        v_cb = _mm_sub_epi16(v_cb, v_delta);

        __m128i v_y_p = _mm_unpacklo_epi16(v_y, v_zero);

        __m128i v_mullo_3 = _mm_mullo_epi16(v_cb, v_c3);
        __m128i v_mullo_2 = _mm_mullo_epi16(v_cb, v_c2);
        __m128i v_mullo_1 = _mm_mullo_epi16(v_cr, v_c1);
        __m128i v_mullo_0 = _mm_mullo_epi16(v_cr, v_c0);

        __m128i v_mulhi_3 = _mm_mulhi_epi16(v_cb, v_c3);
        __m128i v_mulhi_2 = _mm_mulhi_epi16(v_cb, v_c2);
        __m128i v_mulhi_1 = _mm_mulhi_epi16(v_cr, v_c1);
        __m128i v_mulhi_0 = _mm_mulhi_epi16(v_cr, v_c0);

        __m128i v_b0 = _mm_srai_epi32(_mm_add_epi32(_mm_unpacklo_epi16(v_mullo_3, v_mulhi_3), v_delta2), yuv_shift);
        __m128i v_g0 = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_unpacklo_epi16(v_mullo_2, v_mulhi_2),
                                                                  _mm_unpacklo_epi16(v_mullo_1, v_mulhi_1)), v_delta2),
                                      yuv_shift);
        __m128i v_r0 = _mm_srai_epi32(_mm_add_epi32(_mm_unpacklo_epi16(v_mullo_0, v_mulhi_0), v_delta2), yuv_shift);

        v_r0 = _mm_add_epi32(v_r0, v_y_p);
        v_g0 = _mm_add_epi32(v_g0, v_y_p);
        v_b0 = _mm_add_epi32(v_b0, v_y_p);

        v_y_p = _mm_unpackhi_epi16(v_y, v_zero);

        __m128i v_b1 = _mm_srai_epi32(_mm_add_epi32(_mm_unpackhi_epi16(v_mullo_3, v_mulhi_3), v_delta2), yuv_shift);
        __m128i v_g1 = _mm_srai_epi32(_mm_add_epi32(_mm_add_epi32(_mm_unpackhi_epi16(v_mullo_2, v_mulhi_2),
                                                                  _mm_unpackhi_epi16(v_mullo_1, v_mulhi_1)), v_delta2),
                                      yuv_shift);
        __m128i v_r1 = _mm_srai_epi32(_mm_add_epi32(_mm_unpackhi_epi16(v_mullo_0, v_mulhi_0), v_delta2), yuv_shift);

        v_r1 = _mm_add_epi32(v_r1, v_y_p);
        v_g1 = _mm_add_epi32(v_g1, v_y_p);
        v_b1 = _mm_add_epi32(v_b1, v_y_p);

        v_r = _mm_packs_epi32(v_r0, v_r1);
        v_g = _mm_packs_epi32(v_g0, v_g1);
        v_b = _mm_packs_epi32(v_b0, v_b1);
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int dcn = dstcn, bidx = blueIdx, i = 0;
        int yuvOrder = !isCrCb; //1 if YUV, 0 if YCrCb
        const uchar delta = ColorChannel<uchar>::half(), alpha = ColorChannel<uchar>::max();
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2], C3 = coeffs[3];
        n *= 3;

#if CV_SSE4_1
        if (checkHardwareSupport(CV_CPU_SSE4_1) && useSSE)
        {
            __m128i v_shuffle[2];
            v_shuffle[0] = _mm_set_epi8(0x8, 0x7, 0x7, 0x6, 0x6, 0x5, 0x5, 0x4, 0x4, 0x3, 0x3, 0x2, 0x2, 0x1, 0x1, 0x0);
            v_shuffle[1] = _mm_set_epi8(0xf, 0xc, 0xc, 0xc, 0x9, 0x9, 0x9, 0x6, 0x6, 0x6, 0x3, 0x3, 0x3, 0x0, 0x0, 0x0);
            __m128i v_coeffs[3];
            v_coeffs[0] = _mm_set_epi16((short)C0, 0, 0, (short)C3, (short)C2, (short)C1, (short)C0, 0);
            v_coeffs[1] = _mm_set_epi16((short)C2, (short)C1, (short)C0, 0, 0, (short)C3, (short)C2, (short)C1);
            v_coeffs[2] = _mm_set_epi16(0, (short)C3, (short)C2, (short)C1, (short)C0, 0, 0, (short)C3);

            if (dcn == 3)
            {
                if (bidx == 0)
                {
                    __m128i v_shuffle_dst = _mm_set_epi8(0xf, 0xc, 0xd, 0xe, 0x9, 0xa, 0xb, 0x6, 0x7, 0x8, 0x3, 0x4, 0x5, 0x0, 0x1, 0x2);
                    for ( ; i <= n - 24; i += 24, dst += dcn * 8)
                    {
                        __m128i v_src[2];
                        v_src[0] = _mm_loadu_si128((__m128i const *)(src + i));
                        v_src[1] = _mm_loadl_epi64((__m128i const *)(src + i + 16));

                        process(v_src, v_shuffle, v_coeffs);

                        __m128i v_dst[2];
                        v_dst[0] = _mm_shuffle_epi8(v_src[0], v_shuffle_dst);
                        v_dst[1] = _mm_shuffle_epi8(_mm_alignr_epi8(v_src[1], v_src[0], 15), v_shuffle_dst);

                        _mm_storeu_si128((__m128i *)(dst), _mm_alignr_epi8(v_dst[1], _mm_slli_si128(v_dst[0], 1), 1));
                        _mm_storel_epi64((__m128i *)(dst + 16), _mm_srli_si128(v_dst[1], 1));
                    }
                }
                else
                {
                    for ( ; i <= n - 24; i += 24, dst += dcn * 8)
                    {
                        __m128i v_src[2];
                        v_src[0] = _mm_loadu_si128((__m128i const *)(src + i));
                        v_src[1] = _mm_loadl_epi64((__m128i const *)(src + i + 16));

                        process(v_src, v_shuffle, v_coeffs);

                        _mm_storeu_si128((__m128i *)(dst), v_src[0]);
                        _mm_storel_epi64((__m128i *)(dst + 16), v_src[1]);
                    }
                }
            }
            else
            {
                if (bidx == 0)
                {
                    __m128i v_shuffle_dst = _mm_set_epi8(0x0, 0xa, 0xb, 0xc, 0x0, 0x7, 0x8, 0x9, 0x0, 0x4, 0x5, 0x6, 0x0, 0x1, 0x2, 0x3);

                    for ( ; i <= n - 24; i += 24, dst += dcn * 8)
                    {
                        __m128i v_src[2];
                        v_src[0] = _mm_loadu_si128((__m128i const *)(src + i));
                        v_src[1] = _mm_loadl_epi64((__m128i const *)(src + i + 16));

                        process(v_src, v_shuffle, v_coeffs);

                        _mm_storeu_si128((__m128i *)(dst), _mm_shuffle_epi8(_mm_alignr_epi8(v_src[0], v_alpha, 15), v_shuffle_dst));
                        _mm_storeu_si128((__m128i *)(dst + 16), _mm_shuffle_epi8(_mm_alignr_epi8(_mm_alignr_epi8(v_src[1], v_src[0], 12), v_alpha, 15), v_shuffle_dst));
                    }
                }
                else
                {
                    __m128i v_shuffle_dst = _mm_set_epi8(0x0, 0xc, 0xb, 0xa, 0x0, 0x9, 0x8, 0x7, 0x0, 0x6, 0x5, 0x4, 0x0, 0x3, 0x2, 0x1);

                    for ( ; i <= n - 24; i += 24, dst += dcn * 8)
                    {
                        __m128i v_src[2];
                        v_src[0] = _mm_loadu_si128((__m128i const *)(src + i));
                        v_src[1] = _mm_loadl_epi64((__m128i const *)(src + i + 16));

                        process(v_src, v_shuffle, v_coeffs);

                        _mm_storeu_si128((__m128i *)(dst), _mm_shuffle_epi8(_mm_alignr_epi8(v_src[0], v_alpha, 15), v_shuffle_dst));
                        _mm_storeu_si128((__m128i *)(dst + 16), _mm_shuffle_epi8(_mm_alignr_epi8(_mm_alignr_epi8(v_src[1], v_src[0], 12), v_alpha, 15), v_shuffle_dst));
                    }
                }
            }
        }
        else
#endif // CV_SSE4_1
        if (haveSIMD && useSSE)
        {
            for ( ; i <= n - 96; i += 96, dst += dcn * 32)
            {
                __m128i v_y0 = _mm_loadu_si128((__m128i const *)(src + i));
                __m128i v_y1 = _mm_loadu_si128((__m128i const *)(src + i + 16));
                __m128i v_cr0 = _mm_loadu_si128((__m128i const *)(src + i + 32));
                __m128i v_cr1 = _mm_loadu_si128((__m128i const *)(src + i + 48));
                __m128i v_cb0 = _mm_loadu_si128((__m128i const *)(src + i + 64));
                __m128i v_cb1 = _mm_loadu_si128((__m128i const *)(src + i + 80));

                _mm_deinterleave_epi8(v_y0, v_y1, v_cr0, v_cr1, v_cb0, v_cb1);

                __m128i v_r_0 = v_zero, v_g_0 = v_zero, v_b_0 = v_zero;
                process(_mm_unpacklo_epi8(v_y0, v_zero),
                        _mm_unpacklo_epi8(v_cr0, v_zero),
                        _mm_unpacklo_epi8(v_cb0, v_zero),
                        v_r_0, v_g_0, v_b_0);

                __m128i v_r_1 = v_zero, v_g_1 = v_zero, v_b_1 = v_zero;
                process(_mm_unpackhi_epi8(v_y0, v_zero),
                        _mm_unpackhi_epi8(v_cr0, v_zero),
                        _mm_unpackhi_epi8(v_cb0, v_zero),
                        v_r_1, v_g_1, v_b_1);

                __m128i v_r0 = _mm_packus_epi16(v_r_0, v_r_1);
                __m128i v_g0 = _mm_packus_epi16(v_g_0, v_g_1);
                __m128i v_b0 = _mm_packus_epi16(v_b_0, v_b_1);

                process(_mm_unpacklo_epi8(v_y1, v_zero),
                        _mm_unpacklo_epi8(v_cr1, v_zero),
                        _mm_unpacklo_epi8(v_cb1, v_zero),
                        v_r_0, v_g_0, v_b_0);

                process(_mm_unpackhi_epi8(v_y1, v_zero),
                        _mm_unpackhi_epi8(v_cr1, v_zero),
                        _mm_unpackhi_epi8(v_cb1, v_zero),
                        v_r_1, v_g_1, v_b_1);

                __m128i v_r1 = _mm_packus_epi16(v_r_0, v_r_1);
                __m128i v_g1 = _mm_packus_epi16(v_g_0, v_g_1);
                __m128i v_b1 = _mm_packus_epi16(v_b_0, v_b_1);

                if (bidx == 0)
                {
                    std::swap(v_r0, v_b0);
                    std::swap(v_r1, v_b1);
                }

                __m128i v_a0 = v_alpha, v_a1 = v_alpha;

                if (dcn == 3)
                    _mm_interleave_epi8(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);
                else
                    _mm_interleave_epi8(v_r0, v_r1, v_g0, v_g1,
                                        v_b0, v_b1, v_a0, v_a1);

                _mm_storeu_si128((__m128i *)(dst), v_r0);
                _mm_storeu_si128((__m128i *)(dst + 16), v_r1);
                _mm_storeu_si128((__m128i *)(dst + 32), v_g0);
                _mm_storeu_si128((__m128i *)(dst + 48), v_g1);
                _mm_storeu_si128((__m128i *)(dst + 64), v_b0);
                _mm_storeu_si128((__m128i *)(dst + 80), v_b1);

                if (dcn == 4)
                {
                    _mm_storeu_si128((__m128i *)(dst + 96), v_a0);
                    _mm_storeu_si128((__m128i *)(dst + 112), v_a1);
                }
            }
        }

        for ( ; i < n; i += 3, dst += dcn)
        {
            uchar Y = src[i];
            uchar Cr = src[i+1+yuvOrder];
            uchar Cb = src[i+2-yuvOrder];

            int b = Y + CV_DESCALE((Cb - delta)*C3, yuv_shift);
            int g = Y + CV_DESCALE((Cb - delta)*C2 + (Cr - delta)*C1, yuv_shift);
            int r = Y + CV_DESCALE((Cr - delta)*C0, yuv_shift);

            dst[bidx] = saturate_cast<uchar>(b);
            dst[1] = saturate_cast<uchar>(g);
            dst[bidx^2] = saturate_cast<uchar>(r);
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    int coeffs[4];
    bool isCrCb;
    bool useSSE, haveSIMD;

    __m128i v_c0, v_c1, v_c2, v_c3, v_delta2;
    __m128i v_delta, v_alpha, v_zero;
};

#endif // CV_SSE2

////////////////////////////////////// RGB <-> XYZ ///////////////////////////////////////

static const float sRGB2XYZ_D65[] =
{
    0.412453f, 0.357580f, 0.180423f,
    0.212671f, 0.715160f, 0.072169f,
    0.019334f, 0.119193f, 0.950227f
};

static const float XYZ2sRGB_D65[] =
{
    3.240479f, -1.53715f, -0.498535f,
    -0.969256f, 1.875991f, 0.041556f,
    0.055648f, -0.204043f, 1.057311f
};

template<typename _Tp> struct RGB2XYZ_f
{
    typedef _Tp channel_type;

    RGB2XYZ_f(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        memcpy(coeffs, _coeffs ? _coeffs : sRGB2XYZ_D65, 9*sizeof(coeffs[0]));
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[2]);
            std::swap(coeffs[3], coeffs[5]);
            std::swap(coeffs[6], coeffs[8]);
        }
    }
    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int scn = srccn;
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];

        n *= 3;
        for(int i = 0; i < n; i += 3, src += scn)
        {
            _Tp X = saturate_cast<_Tp>(src[0]*C0 + src[1]*C1 + src[2]*C2);
            _Tp Y = saturate_cast<_Tp>(src[0]*C3 + src[1]*C4 + src[2]*C5);
            _Tp Z = saturate_cast<_Tp>(src[0]*C6 + src[1]*C7 + src[2]*C8);
            dst[i] = X; dst[i+1] = Y; dst[i+2] = Z;
        }
    }
    int srccn;
    float coeffs[9];
};

#if CV_NEON

template <>
struct RGB2XYZ_f<float>
{
    typedef float channel_type;

    RGB2XYZ_f(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        memcpy(coeffs, _coeffs ? _coeffs : sRGB2XYZ_D65, 9*sizeof(coeffs[0]));
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[2]);
            std::swap(coeffs[3], coeffs[5]);
            std::swap(coeffs[6], coeffs[8]);
        }

        v_c0 = vdupq_n_f32(coeffs[0]);
        v_c1 = vdupq_n_f32(coeffs[1]);
        v_c2 = vdupq_n_f32(coeffs[2]);
        v_c3 = vdupq_n_f32(coeffs[3]);
        v_c4 = vdupq_n_f32(coeffs[4]);
        v_c5 = vdupq_n_f32(coeffs[5]);
        v_c6 = vdupq_n_f32(coeffs[6]);
        v_c7 = vdupq_n_f32(coeffs[7]);
        v_c8 = vdupq_n_f32(coeffs[8]);
    }

    void operator()(const float* src, float* dst, int n) const
    {
        int scn = srccn, i = 0;
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];

        n *= 3;

        if (scn == 3)
            for ( ; i <= n - 12; i += 12, src += 12)
            {
                float32x4x3_t v_src = vld3q_f32(src), v_dst;
                v_dst.val[0] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c0), v_src.val[1], v_c1), v_src.val[2], v_c2);
                v_dst.val[1] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c3), v_src.val[1], v_c4), v_src.val[2], v_c5);
                v_dst.val[2] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c6), v_src.val[1], v_c7), v_src.val[2], v_c8);
                vst3q_f32(dst + i, v_dst);
            }
        else
            for ( ; i <= n - 12; i += 12, src += 16)
            {
                float32x4x4_t v_src = vld4q_f32(src);
                float32x4x3_t v_dst;
                v_dst.val[0] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c0), v_src.val[1], v_c1), v_src.val[2], v_c2);
                v_dst.val[1] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c3), v_src.val[1], v_c4), v_src.val[2], v_c5);
                v_dst.val[2] = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], v_c6), v_src.val[1], v_c7), v_src.val[2], v_c8);
                vst3q_f32(dst + i, v_dst);
            }

        for ( ; i < n; i += 3, src += scn)
        {
            float X = saturate_cast<float>(src[0]*C0 + src[1]*C1 + src[2]*C2);
            float Y = saturate_cast<float>(src[0]*C3 + src[1]*C4 + src[2]*C5);
            float Z = saturate_cast<float>(src[0]*C6 + src[1]*C7 + src[2]*C8);
            dst[i] = X; dst[i+1] = Y; dst[i+2] = Z;
        }
    }

    int srccn;
    float coeffs[9];
    float32x4_t v_c0, v_c1, v_c2, v_c3, v_c4, v_c5, v_c6, v_c7, v_c8;
};

#elif CV_SSE2

template <>
struct RGB2XYZ_f<float>
{
    typedef float channel_type;

    RGB2XYZ_f(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        memcpy(coeffs, _coeffs ? _coeffs : sRGB2XYZ_D65, 9*sizeof(coeffs[0]));
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[2]);
            std::swap(coeffs[3], coeffs[5]);
            std::swap(coeffs[6], coeffs[8]);
        }

        v_c0 = _mm_set1_ps(coeffs[0]);
        v_c1 = _mm_set1_ps(coeffs[1]);
        v_c2 = _mm_set1_ps(coeffs[2]);
        v_c3 = _mm_set1_ps(coeffs[3]);
        v_c4 = _mm_set1_ps(coeffs[4]);
        v_c5 = _mm_set1_ps(coeffs[5]);
        v_c6 = _mm_set1_ps(coeffs[6]);
        v_c7 = _mm_set1_ps(coeffs[7]);
        v_c8 = _mm_set1_ps(coeffs[8]);

        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
    }

    void process(__m128 v_r, __m128 v_g, __m128 v_b,
                 __m128 & v_x, __m128 & v_y, __m128 & v_z) const
    {
        v_x = _mm_mul_ps(v_r, v_c0);
        v_x = _mm_add_ps(v_x, _mm_mul_ps(v_g, v_c1));
        v_x = _mm_add_ps(v_x, _mm_mul_ps(v_b, v_c2));

        v_y = _mm_mul_ps(v_r, v_c3);
        v_y = _mm_add_ps(v_y, _mm_mul_ps(v_g, v_c4));
        v_y = _mm_add_ps(v_y, _mm_mul_ps(v_b, v_c5));

        v_z = _mm_mul_ps(v_r, v_c6);
        v_z = _mm_add_ps(v_z, _mm_mul_ps(v_g, v_c7));
        v_z = _mm_add_ps(v_z, _mm_mul_ps(v_b, v_c8));
    }

    void operator()(const float* src, float* dst, int n) const
    {
        int scn = srccn, i = 0;
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];

        n *= 3;

        if (haveSIMD)
        {
            for ( ; i <= n - 24; i += 24, src += 8 * scn)
            {
                __m128 v_r0 = _mm_loadu_ps(src);
                __m128 v_r1 = _mm_loadu_ps(src + 4);
                __m128 v_g0 = _mm_loadu_ps(src + 8);
                __m128 v_g1 = _mm_loadu_ps(src + 12);
                __m128 v_b0 = _mm_loadu_ps(src + 16);
                __m128 v_b1 = _mm_loadu_ps(src + 20);

                if (scn == 4)
                {
                    __m128 v_a0 = _mm_loadu_ps(src + 24);
                    __m128 v_a1 = _mm_loadu_ps(src + 28);

                    _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1,
                                        v_b0, v_b1, v_a0, v_a1);
                }
                else
                    _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);

                __m128 v_x0, v_y0, v_z0;
                process(v_r0, v_g0, v_b0,
                        v_x0, v_y0, v_z0);

                __m128 v_x1, v_y1, v_z1;
                process(v_r1, v_g1, v_b1,
                        v_x1, v_y1, v_z1);

                _mm_interleave_ps(v_x0, v_x1, v_y0, v_y1, v_z0, v_z1);

                _mm_storeu_ps(dst + i, v_x0);
                _mm_storeu_ps(dst + i + 4, v_x1);
                _mm_storeu_ps(dst + i + 8, v_y0);
                _mm_storeu_ps(dst + i + 12, v_y1);
                _mm_storeu_ps(dst + i + 16, v_z0);
                _mm_storeu_ps(dst + i + 20, v_z1);
            }
        }

        for ( ; i < n; i += 3, src += scn)
        {
            float X = saturate_cast<float>(src[0]*C0 + src[1]*C1 + src[2]*C2);
            float Y = saturate_cast<float>(src[0]*C3 + src[1]*C4 + src[2]*C5);
            float Z = saturate_cast<float>(src[0]*C6 + src[1]*C7 + src[2]*C8);
            dst[i] = X; dst[i+1] = Y; dst[i+2] = Z;
        }
    }

    int srccn;
    float coeffs[9];
    __m128 v_c0, v_c1, v_c2, v_c3, v_c4, v_c5, v_c6, v_c7, v_c8;
    bool haveSIMD;
};


#endif

template<typename _Tp> struct RGB2XYZ_i
{
    typedef _Tp channel_type;

    RGB2XYZ_i(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        static const int coeffs0[] =
        {
            1689,    1465,    739,
            871,     2929,    296,
            79,      488,     3892
        };
        for( int i = 0; i < 9; i++ )
            coeffs[i] = _coeffs ? cvRound(_coeffs[i]*(1 << xyz_shift)) : coeffs0[i];
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[2]);
            std::swap(coeffs[3], coeffs[5]);
            std::swap(coeffs[6], coeffs[8]);
        }
    }
    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int scn = srccn;
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
            C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
            C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        for(int i = 0; i < n; i += 3, src += scn)
        {
            int X = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, xyz_shift);
            int Y = CV_DESCALE(src[0]*C3 + src[1]*C4 + src[2]*C5, xyz_shift);
            int Z = CV_DESCALE(src[0]*C6 + src[1]*C7 + src[2]*C8, xyz_shift);
            dst[i] = saturate_cast<_Tp>(X); dst[i+1] = saturate_cast<_Tp>(Y);
            dst[i+2] = saturate_cast<_Tp>(Z);
        }
    }
    int srccn;
    int coeffs[9];
};

#if CV_NEON

template <>
struct RGB2XYZ_i<uchar>
{
    typedef uchar channel_type;

    RGB2XYZ_i(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        static const int coeffs0[] =
        {
            1689,    1465,    739,
            871,     2929,    296,
            79,      488,     3892
        };
        for( int i = 0; i < 9; i++ )
            coeffs[i] = _coeffs ? cvRound(_coeffs[i]*(1 << xyz_shift)) : coeffs0[i];
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[2]);
            std::swap(coeffs[3], coeffs[5]);
            std::swap(coeffs[6], coeffs[8]);
        }

        v_c0 = vdup_n_u16(coeffs[0]);
        v_c1 = vdup_n_u16(coeffs[1]);
        v_c2 = vdup_n_u16(coeffs[2]);
        v_c3 = vdup_n_u16(coeffs[3]);
        v_c4 = vdup_n_u16(coeffs[4]);
        v_c5 = vdup_n_u16(coeffs[5]);
        v_c6 = vdup_n_u16(coeffs[6]);
        v_c7 = vdup_n_u16(coeffs[7]);
        v_c8 = vdup_n_u16(coeffs[8]);
        v_delta = vdupq_n_u32(1 << (xyz_shift - 1));
    }
    void operator()(const uchar * src, uchar * dst, int n) const
    {
        int scn = srccn, i = 0;
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
            C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
            C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        for ( ; i <= n - 24; i += 24, src += scn * 8)
        {
            uint8x8x3_t v_dst;
            uint16x8x3_t v_src16;

            if (scn == 3)
            {
                uint8x8x3_t v_src = vld3_u8(src);
                v_src16.val[0] = vmovl_u8(v_src.val[0]);
                v_src16.val[1] = vmovl_u8(v_src.val[1]);
                v_src16.val[2] = vmovl_u8(v_src.val[2]);
            }
            else
            {
                uint8x8x4_t v_src = vld4_u8(src);
                v_src16.val[0] = vmovl_u8(v_src.val[0]);
                v_src16.val[1] = vmovl_u8(v_src.val[1]);
                v_src16.val[2] = vmovl_u8(v_src.val[2]);
            }

            uint16x4_t v_s0 = vget_low_u16(v_src16.val[0]),
                       v_s1 = vget_low_u16(v_src16.val[1]),
                       v_s2 = vget_low_u16(v_src16.val[2]);

            uint32x4_t v_X0 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            uint32x4_t v_Y0 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            uint32x4_t v_Z0 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X0 = vshrq_n_u32(vaddq_u32(v_X0, v_delta), xyz_shift);
            v_Y0 = vshrq_n_u32(vaddq_u32(v_Y0, v_delta), xyz_shift);
            v_Z0 = vshrq_n_u32(vaddq_u32(v_Z0, v_delta), xyz_shift);

            v_s0 = vget_high_u16(v_src16.val[0]),
            v_s1 = vget_high_u16(v_src16.val[1]),
            v_s2 = vget_high_u16(v_src16.val[2]);

            uint32x4_t v_X1 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            uint32x4_t v_Y1 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            uint32x4_t v_Z1 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X1 = vshrq_n_u32(vaddq_u32(v_X1, v_delta), xyz_shift);
            v_Y1 = vshrq_n_u32(vaddq_u32(v_Y1, v_delta), xyz_shift);
            v_Z1 = vshrq_n_u32(vaddq_u32(v_Z1, v_delta), xyz_shift);

            v_dst.val[0] = vqmovn_u16(vcombine_u16(vmovn_u32(v_X0), vmovn_u32(v_X1)));
            v_dst.val[1] = vqmovn_u16(vcombine_u16(vmovn_u32(v_Y0), vmovn_u32(v_Y1)));
            v_dst.val[2] = vqmovn_u16(vcombine_u16(vmovn_u32(v_Z0), vmovn_u32(v_Z1)));

            vst3_u8(dst + i, v_dst);
        }

        for ( ; i < n; i += 3, src += scn)
        {
            int X = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, xyz_shift);
            int Y = CV_DESCALE(src[0]*C3 + src[1]*C4 + src[2]*C5, xyz_shift);
            int Z = CV_DESCALE(src[0]*C6 + src[1]*C7 + src[2]*C8, xyz_shift);
            dst[i] = saturate_cast<uchar>(X);
            dst[i+1] = saturate_cast<uchar>(Y);
            dst[i+2] = saturate_cast<uchar>(Z);
        }
    }

    int srccn, coeffs[9];
    uint16x4_t v_c0, v_c1, v_c2, v_c3, v_c4, v_c5, v_c6, v_c7, v_c8;
    uint32x4_t v_delta;
};

template <>
struct RGB2XYZ_i<ushort>
{
    typedef ushort channel_type;

    RGB2XYZ_i(int _srccn, int blueIdx, const float* _coeffs) : srccn(_srccn)
    {
        static const int coeffs0[] =
        {
            1689,    1465,    739,
            871,     2929,    296,
            79,      488,     3892
        };
        for( int i = 0; i < 9; i++ )
            coeffs[i] = _coeffs ? cvRound(_coeffs[i]*(1 << xyz_shift)) : coeffs0[i];
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[2]);
            std::swap(coeffs[3], coeffs[5]);
            std::swap(coeffs[6], coeffs[8]);
        }

        v_c0 = vdup_n_u16(coeffs[0]);
        v_c1 = vdup_n_u16(coeffs[1]);
        v_c2 = vdup_n_u16(coeffs[2]);
        v_c3 = vdup_n_u16(coeffs[3]);
        v_c4 = vdup_n_u16(coeffs[4]);
        v_c5 = vdup_n_u16(coeffs[5]);
        v_c6 = vdup_n_u16(coeffs[6]);
        v_c7 = vdup_n_u16(coeffs[7]);
        v_c8 = vdup_n_u16(coeffs[8]);
        v_delta = vdupq_n_u32(1 << (xyz_shift - 1));
    }

    void operator()(const ushort * src, ushort * dst, int n) const
    {
        int scn = srccn, i = 0;
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
            C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
            C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        for ( ; i <= n - 24; i += 24, src += scn * 8)
        {
            uint16x8x3_t v_src, v_dst;

            if (scn == 3)
                v_src = vld3q_u16(src);
            else
            {
                uint16x8x4_t v_src4 = vld4q_u16(src);
                v_src.val[0] = v_src4.val[0];
                v_src.val[1] = v_src4.val[1];
                v_src.val[2] = v_src4.val[2];
            }

            uint16x4_t v_s0 = vget_low_u16(v_src.val[0]),
                       v_s1 = vget_low_u16(v_src.val[1]),
                       v_s2 = vget_low_u16(v_src.val[2]);

            uint32x4_t v_X0 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            uint32x4_t v_Y0 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            uint32x4_t v_Z0 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X0 = vshrq_n_u32(vaddq_u32(v_X0, v_delta), xyz_shift);
            v_Y0 = vshrq_n_u32(vaddq_u32(v_Y0, v_delta), xyz_shift);
            v_Z0 = vshrq_n_u32(vaddq_u32(v_Z0, v_delta), xyz_shift);

            v_s0 = vget_high_u16(v_src.val[0]),
            v_s1 = vget_high_u16(v_src.val[1]),
            v_s2 = vget_high_u16(v_src.val[2]);

            uint32x4_t v_X1 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            uint32x4_t v_Y1 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            uint32x4_t v_Z1 = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X1 = vshrq_n_u32(vaddq_u32(v_X1, v_delta), xyz_shift);
            v_Y1 = vshrq_n_u32(vaddq_u32(v_Y1, v_delta), xyz_shift);
            v_Z1 = vshrq_n_u32(vaddq_u32(v_Z1, v_delta), xyz_shift);

            v_dst.val[0] = vcombine_u16(vqmovn_u32(v_X0), vqmovn_u32(v_X1));
            v_dst.val[1] = vcombine_u16(vqmovn_u32(v_Y0), vqmovn_u32(v_Y1));
            v_dst.val[2] = vcombine_u16(vqmovn_u32(v_Z0), vqmovn_u32(v_Z1));

            vst3q_u16(dst + i, v_dst);
        }

        for ( ; i <= n - 12; i += 12, src += scn * 4)
        {
            uint16x4x3_t v_dst;
            uint16x4_t v_s0, v_s1, v_s2;

            if (scn == 3)
            {
                uint16x4x3_t v_src = vld3_u16(src);
                v_s0 = v_src.val[0];
                v_s1 = v_src.val[1];
                v_s2 = v_src.val[2];
            }
            else
            {
                uint16x4x4_t v_src = vld4_u16(src);
                v_s0 = v_src.val[0];
                v_s1 = v_src.val[1];
                v_s2 = v_src.val[2];
            }

            uint32x4_t v_X = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            uint32x4_t v_Y = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            uint32x4_t v_Z = vmlal_u16(vmlal_u16(vmull_u16(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);

            v_dst.val[0] = vqmovn_u32(vshrq_n_u32(vaddq_u32(v_X, v_delta), xyz_shift));
            v_dst.val[1] = vqmovn_u32(vshrq_n_u32(vaddq_u32(v_Y, v_delta), xyz_shift));
            v_dst.val[2] = vqmovn_u32(vshrq_n_u32(vaddq_u32(v_Z, v_delta), xyz_shift));

            vst3_u16(dst + i, v_dst);
        }

        for ( ; i < n; i += 3, src += scn)
        {
            int X = CV_DESCALE(src[0]*C0 + src[1]*C1 + src[2]*C2, xyz_shift);
            int Y = CV_DESCALE(src[0]*C3 + src[1]*C4 + src[2]*C5, xyz_shift);
            int Z = CV_DESCALE(src[0]*C6 + src[1]*C7 + src[2]*C8, xyz_shift);
            dst[i] = saturate_cast<ushort>(X);
            dst[i+1] = saturate_cast<ushort>(Y);
            dst[i+2] = saturate_cast<ushort>(Z);
        }
    }

    int srccn, coeffs[9];
    uint16x4_t v_c0, v_c1, v_c2, v_c3, v_c4, v_c5, v_c6, v_c7, v_c8;
    uint32x4_t v_delta;
};

#endif

template<typename _Tp> struct XYZ2RGB_f
{
    typedef _Tp channel_type;

    XYZ2RGB_f(int _dstcn, int _blueIdx, const float* _coeffs)
    : dstcn(_dstcn), blueIdx(_blueIdx)
    {
        memcpy(coeffs, _coeffs ? _coeffs : XYZ2sRGB_D65, 9*sizeof(coeffs[0]));
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[6]);
            std::swap(coeffs[1], coeffs[7]);
            std::swap(coeffs[2], coeffs[8]);
        }
    }

    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int dcn = dstcn;
        _Tp alpha = ColorChannel<_Tp>::max();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;
        for(int i = 0; i < n; i += 3, dst += dcn)
        {
            _Tp B = saturate_cast<_Tp>(src[i]*C0 + src[i+1]*C1 + src[i+2]*C2);
            _Tp G = saturate_cast<_Tp>(src[i]*C3 + src[i+1]*C4 + src[i+2]*C5);
            _Tp R = saturate_cast<_Tp>(src[i]*C6 + src[i+1]*C7 + src[i+2]*C8);
            dst[0] = B; dst[1] = G; dst[2] = R;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    float coeffs[9];
};

#if CV_SSE2

template <>
struct XYZ2RGB_f<float>
{
    typedef float channel_type;

    XYZ2RGB_f(int _dstcn, int _blueIdx, const float* _coeffs)
    : dstcn(_dstcn), blueIdx(_blueIdx)
    {
        memcpy(coeffs, _coeffs ? _coeffs : XYZ2sRGB_D65, 9*sizeof(coeffs[0]));
        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[6]);
            std::swap(coeffs[1], coeffs[7]);
            std::swap(coeffs[2], coeffs[8]);
        }

        v_c0 = _mm_set1_ps(coeffs[0]);
        v_c1 = _mm_set1_ps(coeffs[1]);
        v_c2 = _mm_set1_ps(coeffs[2]);
        v_c3 = _mm_set1_ps(coeffs[3]);
        v_c4 = _mm_set1_ps(coeffs[4]);
        v_c5 = _mm_set1_ps(coeffs[5]);
        v_c6 = _mm_set1_ps(coeffs[6]);
        v_c7 = _mm_set1_ps(coeffs[7]);
        v_c8 = _mm_set1_ps(coeffs[8]);

        v_alpha = _mm_set1_ps(ColorChannel<float>::max());

        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
    }

    void process(__m128 v_x, __m128 v_y, __m128 v_z,
                 __m128 & v_r, __m128 & v_g, __m128 & v_b) const
    {
        v_b = _mm_mul_ps(v_x, v_c0);
        v_b = _mm_add_ps(v_b, _mm_mul_ps(v_y, v_c1));
        v_b = _mm_add_ps(v_b, _mm_mul_ps(v_z, v_c2));

        v_g = _mm_mul_ps(v_x, v_c3);
        v_g = _mm_add_ps(v_g, _mm_mul_ps(v_y, v_c4));
        v_g = _mm_add_ps(v_g, _mm_mul_ps(v_z, v_c5));

        v_r = _mm_mul_ps(v_x, v_c6);
        v_r = _mm_add_ps(v_r, _mm_mul_ps(v_y, v_c7));
        v_r = _mm_add_ps(v_r, _mm_mul_ps(v_z, v_c8));
    }

    void operator()(const float* src, float* dst, int n) const
    {
        int dcn = dstcn;
        float alpha = ColorChannel<float>::max();
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;
        int i = 0;

        if (haveSIMD)
        {
            for ( ; i <= n - 24; i += 24, dst += 8 * dcn)
            {
                __m128 v_x0 = _mm_loadu_ps(src + i);
                __m128 v_x1 = _mm_loadu_ps(src + i + 4);
                __m128 v_y0 = _mm_loadu_ps(src + i + 8);
                __m128 v_y1 = _mm_loadu_ps(src + i + 12);
                __m128 v_z0 = _mm_loadu_ps(src + i + 16);
                __m128 v_z1 = _mm_loadu_ps(src + i + 20);

                _mm_deinterleave_ps(v_x0, v_x1, v_y0, v_y1, v_z0, v_z1);

                __m128 v_r0, v_g0, v_b0;
                process(v_x0, v_y0, v_z0,
                        v_r0, v_g0, v_b0);

                __m128 v_r1, v_g1, v_b1;
                process(v_x1, v_y1, v_z1,
                        v_r1, v_g1, v_b1);

                __m128 v_a0 = v_alpha, v_a1 = v_alpha;

                if (dcn == 4)
                    _mm_interleave_ps(v_b0, v_b1, v_g0, v_g1,
                                      v_r0, v_r1, v_a0, v_a1);
                else
                    _mm_interleave_ps(v_b0, v_b1, v_g0, v_g1, v_r0, v_r1);

                _mm_storeu_ps(dst, v_b0);
                _mm_storeu_ps(dst + 4, v_b1);
                _mm_storeu_ps(dst + 8, v_g0);
                _mm_storeu_ps(dst + 12, v_g1);
                _mm_storeu_ps(dst + 16, v_r0);
                _mm_storeu_ps(dst + 20, v_r1);

                if (dcn == 4)
                {
                    _mm_storeu_ps(dst + 24, v_a0);
                    _mm_storeu_ps(dst + 28, v_a1);
                }
            }

        }

        for( ; i < n; i += 3, dst += dcn)
        {
            float B = src[i]*C0 + src[i+1]*C1 + src[i+2]*C2;
            float G = src[i]*C3 + src[i+1]*C4 + src[i+2]*C5;
            float R = src[i]*C6 + src[i+1]*C7 + src[i+2]*C8;
            dst[0] = B; dst[1] = G; dst[2] = R;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    float coeffs[9];

    __m128 v_c0, v_c1, v_c2, v_c3, v_c4, v_c5, v_c6, v_c7, v_c8;
    __m128 v_alpha;
    bool haveSIMD;
};

#endif // CV_SSE2


template<typename _Tp> struct XYZ2RGB_i
{
    typedef _Tp channel_type;

    XYZ2RGB_i(int _dstcn, int _blueIdx, const int* _coeffs)
    : dstcn(_dstcn), blueIdx(_blueIdx)
    {
        static const int coeffs0[] =
        {
            13273,  -6296,  -2042,
            -3970,   7684,    170,
              228,   -836,   4331
        };
        for(int i = 0; i < 9; i++)
            coeffs[i] = _coeffs ? cvRound(_coeffs[i]*(1 << xyz_shift)) : coeffs0[i];

        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[6]);
            std::swap(coeffs[1], coeffs[7]);
            std::swap(coeffs[2], coeffs[8]);
        }
    }
    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        int dcn = dstcn;
        _Tp alpha = ColorChannel<_Tp>::max();
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
            C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
            C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;
        for(int i = 0; i < n; i += 3, dst += dcn)
        {
            int B = CV_DESCALE(src[i]*C0 + src[i+1]*C1 + src[i+2]*C2, xyz_shift);
            int G = CV_DESCALE(src[i]*C3 + src[i+1]*C4 + src[i+2]*C5, xyz_shift);
            int R = CV_DESCALE(src[i]*C6 + src[i+1]*C7 + src[i+2]*C8, xyz_shift);
            dst[0] = saturate_cast<_Tp>(B); dst[1] = saturate_cast<_Tp>(G);
            dst[2] = saturate_cast<_Tp>(R);
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    int coeffs[9];
};

#if CV_NEON

template <>
struct XYZ2RGB_i<uchar>
{
    typedef uchar channel_type;

    XYZ2RGB_i(int _dstcn, int _blueIdx, const int* _coeffs)
    : dstcn(_dstcn), blueIdx(_blueIdx)
    {
        static const int coeffs0[] =
        {
            13273,  -6296,  -2042,
            -3970,   7684,    170,
              228,   -836,   4331
        };
        for(int i = 0; i < 9; i++)
            coeffs[i] = _coeffs ? cvRound(_coeffs[i]*(1 << xyz_shift)) : coeffs0[i];

        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[6]);
            std::swap(coeffs[1], coeffs[7]);
            std::swap(coeffs[2], coeffs[8]);
        }

        v_c0 = vdup_n_s16(coeffs[0]);
        v_c1 = vdup_n_s16(coeffs[1]);
        v_c2 = vdup_n_s16(coeffs[2]);
        v_c3 = vdup_n_s16(coeffs[3]);
        v_c4 = vdup_n_s16(coeffs[4]);
        v_c5 = vdup_n_s16(coeffs[5]);
        v_c6 = vdup_n_s16(coeffs[6]);
        v_c7 = vdup_n_s16(coeffs[7]);
        v_c8 = vdup_n_s16(coeffs[8]);
        v_delta = vdupq_n_s32(1 << (xyz_shift - 1));
        v_alpha = vmovn_u16(vdupq_n_u16(ColorChannel<uchar>::max()));
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int dcn = dstcn, i = 0;
        uchar alpha = ColorChannel<uchar>::max();
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
            C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
            C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        for ( ; i <= n - 24; i += 24, dst += dcn * 8)
        {
            uint8x8x3_t v_src = vld3_u8(src + i);
            int16x8x3_t v_src16;
            v_src16.val[0] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[0]));
            v_src16.val[1] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[1]));
            v_src16.val[2] = vreinterpretq_s16_u16(vmovl_u8(v_src.val[2]));

            int16x4_t v_s0 = vget_low_s16(v_src16.val[0]),
                       v_s1 = vget_low_s16(v_src16.val[1]),
                       v_s2 = vget_low_s16(v_src16.val[2]);

            int32x4_t v_X0 = vmlal_s16(vmlal_s16(vmull_s16(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            int32x4_t v_Y0 = vmlal_s16(vmlal_s16(vmull_s16(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            int32x4_t v_Z0 = vmlal_s16(vmlal_s16(vmull_s16(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X0 = vshrq_n_s32(vaddq_s32(v_X0, v_delta), xyz_shift);
            v_Y0 = vshrq_n_s32(vaddq_s32(v_Y0, v_delta), xyz_shift);
            v_Z0 = vshrq_n_s32(vaddq_s32(v_Z0, v_delta), xyz_shift);

            v_s0 = vget_high_s16(v_src16.val[0]),
            v_s1 = vget_high_s16(v_src16.val[1]),
            v_s2 = vget_high_s16(v_src16.val[2]);

            int32x4_t v_X1 = vmlal_s16(vmlal_s16(vmull_s16(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            int32x4_t v_Y1 = vmlal_s16(vmlal_s16(vmull_s16(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            int32x4_t v_Z1 = vmlal_s16(vmlal_s16(vmull_s16(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X1 = vshrq_n_s32(vaddq_s32(v_X1, v_delta), xyz_shift);
            v_Y1 = vshrq_n_s32(vaddq_s32(v_Y1, v_delta), xyz_shift);
            v_Z1 = vshrq_n_s32(vaddq_s32(v_Z1, v_delta), xyz_shift);

            uint8x8_t v_b = vqmovun_s16(vcombine_s16(vqmovn_s32(v_X0), vqmovn_s32(v_X1)));
            uint8x8_t v_g = vqmovun_s16(vcombine_s16(vqmovn_s32(v_Y0), vqmovn_s32(v_Y1)));
            uint8x8_t v_r = vqmovun_s16(vcombine_s16(vqmovn_s32(v_Z0), vqmovn_s32(v_Z1)));

            if (dcn == 3)
            {
                uint8x8x3_t v_dst;
                v_dst.val[0] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[2] = v_r;
                vst3_u8(dst, v_dst);
            }
            else
            {
                uint8x8x4_t v_dst;
                v_dst.val[0] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[2] = v_r;
                v_dst.val[3] = v_alpha;
                vst4_u8(dst, v_dst);
            }
        }

        for ( ; i < n; i += 3, dst += dcn)
        {
            int B = CV_DESCALE(src[i]*C0 + src[i+1]*C1 + src[i+2]*C2, xyz_shift);
            int G = CV_DESCALE(src[i]*C3 + src[i+1]*C4 + src[i+2]*C5, xyz_shift);
            int R = CV_DESCALE(src[i]*C6 + src[i+1]*C7 + src[i+2]*C8, xyz_shift);
            dst[0] = saturate_cast<uchar>(B); dst[1] = saturate_cast<uchar>(G);
            dst[2] = saturate_cast<uchar>(R);
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    int coeffs[9];

    int16x4_t v_c0, v_c1, v_c2, v_c3, v_c4, v_c5, v_c6, v_c7, v_c8;
    uint8x8_t v_alpha;
    int32x4_t v_delta;
};

template <>
struct XYZ2RGB_i<ushort>
{
    typedef ushort channel_type;

    XYZ2RGB_i(int _dstcn, int _blueIdx, const int* _coeffs)
    : dstcn(_dstcn), blueIdx(_blueIdx)
    {
        static const int coeffs0[] =
        {
            13273,  -6296,  -2042,
            -3970,   7684,    170,
              228,   -836,   4331
        };
        for(int i = 0; i < 9; i++)
            coeffs[i] = _coeffs ? cvRound(_coeffs[i]*(1 << xyz_shift)) : coeffs0[i];

        if(blueIdx == 0)
        {
            std::swap(coeffs[0], coeffs[6]);
            std::swap(coeffs[1], coeffs[7]);
            std::swap(coeffs[2], coeffs[8]);
        }

        v_c0 = vdupq_n_s32(coeffs[0]);
        v_c1 = vdupq_n_s32(coeffs[1]);
        v_c2 = vdupq_n_s32(coeffs[2]);
        v_c3 = vdupq_n_s32(coeffs[3]);
        v_c4 = vdupq_n_s32(coeffs[4]);
        v_c5 = vdupq_n_s32(coeffs[5]);
        v_c6 = vdupq_n_s32(coeffs[6]);
        v_c7 = vdupq_n_s32(coeffs[7]);
        v_c8 = vdupq_n_s32(coeffs[8]);
        v_delta = vdupq_n_s32(1 << (xyz_shift - 1));
        v_alpha = vdupq_n_u16(ColorChannel<ushort>::max());
        v_alpha2 = vget_low_u16(v_alpha);
    }

    void operator()(const ushort* src, ushort* dst, int n) const
    {
        int dcn = dstcn, i = 0;
        ushort alpha = ColorChannel<ushort>::max();
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
            C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
            C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        for ( ; i <= n - 24; i += 24, dst += dcn * 8)
        {
            uint16x8x3_t v_src = vld3q_u16(src + i);
            int32x4_t v_s0 = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[0]))),
                      v_s1 = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[1]))),
                      v_s2 = vreinterpretq_s32_u32(vmovl_u16(vget_low_u16(v_src.val[2])));

            int32x4_t v_X0 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            int32x4_t v_Y0 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            int32x4_t v_Z0 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X0 = vshrq_n_s32(vaddq_s32(v_X0, v_delta), xyz_shift);
            v_Y0 = vshrq_n_s32(vaddq_s32(v_Y0, v_delta), xyz_shift);
            v_Z0 = vshrq_n_s32(vaddq_s32(v_Z0, v_delta), xyz_shift);

            v_s0 = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[0])));
            v_s1 = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[1])));
            v_s2 = vreinterpretq_s32_u32(vmovl_u16(vget_high_u16(v_src.val[2])));

            int32x4_t v_X1 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            int32x4_t v_Y1 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            int32x4_t v_Z1 = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X1 = vshrq_n_s32(vaddq_s32(v_X1, v_delta), xyz_shift);
            v_Y1 = vshrq_n_s32(vaddq_s32(v_Y1, v_delta), xyz_shift);
            v_Z1 = vshrq_n_s32(vaddq_s32(v_Z1, v_delta), xyz_shift);

            uint16x8_t v_b = vcombine_u16(vqmovun_s32(v_X0), vqmovun_s32(v_X1));
            uint16x8_t v_g = vcombine_u16(vqmovun_s32(v_Y0), vqmovun_s32(v_Y1));
            uint16x8_t v_r = vcombine_u16(vqmovun_s32(v_Z0), vqmovun_s32(v_Z1));

            if (dcn == 3)
            {
                uint16x8x3_t v_dst;
                v_dst.val[0] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[2] = v_r;
                vst3q_u16(dst, v_dst);
            }
            else
            {
                uint16x8x4_t v_dst;
                v_dst.val[0] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[2] = v_r;
                v_dst.val[3] = v_alpha;
                vst4q_u16(dst, v_dst);
            }
        }

        for ( ; i <= n - 12; i += 12, dst += dcn * 4)
        {
            uint16x4x3_t v_src = vld3_u16(src + i);
            int32x4_t v_s0 = vreinterpretq_s32_u32(vmovl_u16(v_src.val[0])),
                      v_s1 = vreinterpretq_s32_u32(vmovl_u16(v_src.val[1])),
                      v_s2 = vreinterpretq_s32_u32(vmovl_u16(v_src.val[2]));

            int32x4_t v_X = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c0), v_s1, v_c1), v_s2, v_c2);
            int32x4_t v_Y = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c3), v_s1, v_c4), v_s2, v_c5);
            int32x4_t v_Z = vmlaq_s32(vmlaq_s32(vmulq_s32(v_s0, v_c6), v_s1, v_c7), v_s2, v_c8);
            v_X = vshrq_n_s32(vaddq_s32(v_X, v_delta), xyz_shift);
            v_Y = vshrq_n_s32(vaddq_s32(v_Y, v_delta), xyz_shift);
            v_Z = vshrq_n_s32(vaddq_s32(v_Z, v_delta), xyz_shift);

            uint16x4_t v_b = vqmovun_s32(v_X);
            uint16x4_t v_g = vqmovun_s32(v_Y);
            uint16x4_t v_r = vqmovun_s32(v_Z);

            if (dcn == 3)
            {
                uint16x4x3_t v_dst;
                v_dst.val[0] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[2] = v_r;
                vst3_u16(dst, v_dst);
            }
            else
            {
                uint16x4x4_t v_dst;
                v_dst.val[0] = v_b;
                v_dst.val[1] = v_g;
                v_dst.val[2] = v_r;
                v_dst.val[3] = v_alpha2;
                vst4_u16(dst, v_dst);
            }
        }

        for ( ; i < n; i += 3, dst += dcn)
        {
            int B = CV_DESCALE(src[i]*C0 + src[i+1]*C1 + src[i+2]*C2, xyz_shift);
            int G = CV_DESCALE(src[i]*C3 + src[i+1]*C4 + src[i+2]*C5, xyz_shift);
            int R = CV_DESCALE(src[i]*C6 + src[i+1]*C7 + src[i+2]*C8, xyz_shift);
            dst[0] = saturate_cast<ushort>(B); dst[1] = saturate_cast<ushort>(G);
            dst[2] = saturate_cast<ushort>(R);
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }
    int dstcn, blueIdx;
    int coeffs[9];

    int32x4_t v_c0, v_c1, v_c2, v_c3, v_c4, v_c5, v_c6, v_c7, v_c8, v_delta;
    uint16x4_t v_alpha2;
    uint16x8_t v_alpha;
};

#endif

////////////////////////////////////// RGB <-> HSV ///////////////////////////////////////


struct RGB2HSV_b
{
    typedef uchar channel_type;

    RGB2HSV_b(int _srccn, int _blueIdx, int _hrange)
    : srccn(_srccn), blueIdx(_blueIdx), hrange(_hrange)
    {
        CV_Assert( hrange == 180 || hrange == 256 );
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i, bidx = blueIdx, scn = srccn;
        const int hsv_shift = 12;

        static int sdiv_table[256];
        static int hdiv_table180[256];
        static int hdiv_table256[256];
        static volatile bool initialized = false;

        int hr = hrange;
        const int* hdiv_table = hr == 180 ? hdiv_table180 : hdiv_table256;
        n *= 3;

        if( !initialized )
        {
            sdiv_table[0] = hdiv_table180[0] = hdiv_table256[0] = 0;
            for( i = 1; i < 256; i++ )
            {
                sdiv_table[i] = saturate_cast<int>((255 << hsv_shift)/(1.*i));
                hdiv_table180[i] = saturate_cast<int>((180 << hsv_shift)/(6.*i));
                hdiv_table256[i] = saturate_cast<int>((256 << hsv_shift)/(6.*i));
            }
            initialized = true;
        }

        for( i = 0; i < n; i += 3, src += scn )
        {
            int b = src[bidx], g = src[1], r = src[bidx^2];
            int h, s, v = b;
            int vmin = b, diff;
            int vr, vg;

            CV_CALC_MAX_8U( v, g );
            CV_CALC_MAX_8U( v, r );
            CV_CALC_MIN_8U( vmin, g );
            CV_CALC_MIN_8U( vmin, r );

            diff = v - vmin;
            vr = v == r ? -1 : 0;
            vg = v == g ? -1 : 0;

            s = (diff * sdiv_table[v] + (1 << (hsv_shift-1))) >> hsv_shift;
            h = (vr & (g - b)) +
                (~vr & ((vg & (b - r + 2 * diff)) + ((~vg) & (r - g + 4 * diff))));
            h = (h * hdiv_table[diff] + (1 << (hsv_shift-1))) >> hsv_shift;
            h += h < 0 ? hr : 0;

            dst[i] = saturate_cast<uchar>(h);
            dst[i+1] = (uchar)s;
            dst[i+2] = (uchar)v;
        }
    }

    int srccn, blueIdx, hrange;
};


struct RGB2HSV_f
{
    typedef float channel_type;

    RGB2HSV_f(int _srccn, int _blueIdx, float _hrange)
    : srccn(_srccn), blueIdx(_blueIdx), hrange(_hrange) {}

    void operator()(const float* src, float* dst, int n) const
    {
        int i, bidx = blueIdx, scn = srccn;
        float hscale = hrange*(1.f/360.f);
        n *= 3;

        for( i = 0; i < n; i += 3, src += scn )
        {
            float b = src[bidx], g = src[1], r = src[bidx^2];
            float h, s, v;

            float vmin, diff;

            v = vmin = r;
            if( v < g ) v = g;
            if( v < b ) v = b;
            if( vmin > g ) vmin = g;
            if( vmin > b ) vmin = b;

            diff = v - vmin;
            s = diff/(float)(fabs(v) + FLT_EPSILON);
            diff = (float)(60./(diff + FLT_EPSILON));
            if( v == r )
                h = (g - b)*diff;
            else if( v == g )
                h = (b - r)*diff + 120.f;
            else
                h = (r - g)*diff + 240.f;

            if( h < 0 ) h += 360.f;

            dst[i] = h*hscale;
            dst[i+1] = s;
            dst[i+2] = v;
        }
    }

    int srccn, blueIdx;
    float hrange;
};


struct HSV2RGB_f
{
    typedef float channel_type;

    HSV2RGB_f(int _dstcn, int _blueIdx, float _hrange)
    : dstcn(_dstcn), blueIdx(_blueIdx), hscale(6.f/_hrange) {
        #if CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(__m128& v_h0, __m128& v_h1, __m128& v_s0,
                 __m128& v_s1, __m128& v_v0, __m128& v_v1) const
    {
        v_h0 = _mm_mul_ps(v_h0, _mm_set1_ps(hscale));
        v_h1 = _mm_mul_ps(v_h1, _mm_set1_ps(hscale));

        __m128 v_pre_sector0 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_h0));
        __m128 v_pre_sector1 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_h1));

        v_h0 = _mm_sub_ps(v_h0, v_pre_sector0);
        v_h1 = _mm_sub_ps(v_h1, v_pre_sector1);

        __m128 v_tab00 = v_v0;
        __m128 v_tab01 = v_v1;
        __m128 v_tab10 = _mm_mul_ps(v_v0, _mm_sub_ps(_mm_set1_ps(1.0f), v_s0));
        __m128 v_tab11 = _mm_mul_ps(v_v1, _mm_sub_ps(_mm_set1_ps(1.0f), v_s1));
        __m128 v_tab20 = _mm_mul_ps(v_v0, _mm_sub_ps(_mm_set1_ps(1.0f), _mm_mul_ps(v_s0, v_h0)));
        __m128 v_tab21 = _mm_mul_ps(v_v1, _mm_sub_ps(_mm_set1_ps(1.0f), _mm_mul_ps(v_s1, v_h1)));
        __m128 v_tab30 = _mm_mul_ps(v_v0, _mm_sub_ps(_mm_set1_ps(1.0f), _mm_mul_ps(v_s0, _mm_sub_ps(_mm_set1_ps(1.0f), v_h0))));
        __m128 v_tab31 = _mm_mul_ps(v_v1, _mm_sub_ps(_mm_set1_ps(1.0f), _mm_mul_ps(v_s1, _mm_sub_ps(_mm_set1_ps(1.0f), v_h1))));

        __m128 v_sector0 = _mm_div_ps(v_pre_sector0, _mm_set1_ps(6.0f));
        __m128 v_sector1 = _mm_div_ps(v_pre_sector1, _mm_set1_ps(6.0f));
        v_sector0 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_sector0));
        v_sector1 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_sector1));
        v_sector0 = _mm_mul_ps(v_sector0, _mm_set1_ps(6.0f));
        v_sector1 = _mm_mul_ps(v_sector1, _mm_set1_ps(6.0f));
        v_sector0 = _mm_sub_ps(v_pre_sector0, v_sector0);
        v_sector1 = _mm_sub_ps(v_pre_sector1, v_sector1);

        v_h0 = _mm_and_ps(v_tab10, _mm_cmplt_ps(v_sector0, _mm_set1_ps(2.0f)));
        v_h1 = _mm_and_ps(v_tab11, _mm_cmplt_ps(v_sector1, _mm_set1_ps(2.0f)));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_tab30, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(2.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_tab31, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(2.0f))));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_tab00, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_tab01, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_tab00, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_tab01, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(4.0f))));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_tab20, _mm_cmpgt_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_tab21, _mm_cmpgt_ps(v_sector1, _mm_set1_ps(4.0f))));
        v_s0 = _mm_and_ps(v_tab30, _mm_cmplt_ps(v_sector0, _mm_set1_ps(1.0f)));
        v_s1 = _mm_and_ps(v_tab31, _mm_cmplt_ps(v_sector1, _mm_set1_ps(1.0f)));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_tab00, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(1.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_tab01, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(1.0f))));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_tab00, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(2.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_tab01, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(2.0f))));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_tab20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_tab21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_tab10, _mm_cmpgt_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_tab11, _mm_cmpgt_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_v0 = _mm_and_ps(v_tab00, _mm_cmplt_ps(v_sector0, _mm_set1_ps(1.0f)));
        v_v1 = _mm_and_ps(v_tab01, _mm_cmplt_ps(v_sector1, _mm_set1_ps(1.0f)));
        v_v0 = _mm_or_ps(v_v0, _mm_and_ps(v_tab20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(1.0f))));
        v_v1 = _mm_or_ps(v_v1, _mm_and_ps(v_tab21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(1.0f))));
        v_v0 = _mm_or_ps(v_v0, _mm_and_ps(v_tab10, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(2.0f))));
        v_v1 = _mm_or_ps(v_v1, _mm_and_ps(v_tab11, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(2.0f))));
        v_v0 = _mm_or_ps(v_v0, _mm_and_ps(v_tab10, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_v1 = _mm_or_ps(v_v1, _mm_and_ps(v_tab11, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_v0 = _mm_or_ps(v_v0, _mm_and_ps(v_tab30, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_v1 = _mm_or_ps(v_v1, _mm_and_ps(v_tab31, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(4.0f))));
        v_v0 = _mm_or_ps(v_v0, _mm_and_ps(v_tab00, _mm_cmpgt_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_v1 = _mm_or_ps(v_v1, _mm_and_ps(v_tab01, _mm_cmpgt_ps(v_sector1, _mm_set1_ps(4.0f))));
    }
    #endif

    void operator()(const float* src, float* dst, int n) const
    {
        int i = 0, bidx = blueIdx, dcn = dstcn;
        float _hscale = hscale;
        float alpha = ColorChannel<float>::max();
        n *= 3;

        #if CV_SSE2
        if (haveSIMD)
        {
            for( ; i <= n - 24; i += 24, dst += dcn * 8 )
            {
                __m128 v_h0 = _mm_loadu_ps(src + i +  0);
                __m128 v_h1 = _mm_loadu_ps(src + i +  4);
                __m128 v_s0 = _mm_loadu_ps(src + i +  8);
                __m128 v_s1 = _mm_loadu_ps(src + i + 12);
                __m128 v_v0 = _mm_loadu_ps(src + i + 16);
                __m128 v_v1 = _mm_loadu_ps(src + i + 20);

                _mm_deinterleave_ps(v_h0, v_h1, v_s0, v_s1, v_v0, v_v1);

                process(v_h0, v_h1, v_s0, v_s1, v_v0, v_v1);

                if (dcn == 3)
                {
                    if (bidx)
                    {
                        _mm_interleave_ps(v_v0, v_v1, v_s0, v_s1, v_h0, v_h1);

                        _mm_storeu_ps(dst +  0, v_v0);
                        _mm_storeu_ps(dst +  4, v_v1);
                        _mm_storeu_ps(dst +  8, v_s0);
                        _mm_storeu_ps(dst + 12, v_s1);
                        _mm_storeu_ps(dst + 16, v_h0);
                        _mm_storeu_ps(dst + 20, v_h1);
                    }
                    else
                    {
                        _mm_interleave_ps(v_h0, v_h1, v_s0, v_s1, v_v0, v_v1);

                        _mm_storeu_ps(dst +  0, v_h0);
                        _mm_storeu_ps(dst +  4, v_h1);
                        _mm_storeu_ps(dst +  8, v_s0);
                        _mm_storeu_ps(dst + 12, v_s1);
                        _mm_storeu_ps(dst + 16, v_v0);
                        _mm_storeu_ps(dst + 20, v_v1);
                    }
                }
                else
                {
                    __m128 v_a0 = _mm_set1_ps(alpha);
                    __m128 v_a1 = _mm_set1_ps(alpha);
                    if (bidx)
                    {
                        _mm_interleave_ps(v_v0, v_v1, v_s0, v_s1, v_h0, v_h1, v_a0, v_a1);

                        _mm_storeu_ps(dst +  0, v_v0);
                        _mm_storeu_ps(dst +  4, v_v1);
                        _mm_storeu_ps(dst +  8, v_s0);
                        _mm_storeu_ps(dst + 12, v_s1);
                        _mm_storeu_ps(dst + 16, v_h0);
                        _mm_storeu_ps(dst + 20, v_h1);
                        _mm_storeu_ps(dst + 24, v_a0);
                        _mm_storeu_ps(dst + 28, v_a1);
                    }
                    else
                    {
                        _mm_interleave_ps(v_h0, v_h1, v_s0, v_s1, v_v0, v_v1, v_a0, v_a1);

                        _mm_storeu_ps(dst +  0, v_h0);
                        _mm_storeu_ps(dst +  4, v_h1);
                        _mm_storeu_ps(dst +  8, v_s0);
                        _mm_storeu_ps(dst + 12, v_s1);
                        _mm_storeu_ps(dst + 16, v_v0);
                        _mm_storeu_ps(dst + 20, v_v1);
                        _mm_storeu_ps(dst + 24, v_a0);
                        _mm_storeu_ps(dst + 28, v_a1);
                    }
                }
            }
        }
        #endif
        for( ; i < n; i += 3, dst += dcn )
        {
            float h = src[i], s = src[i+1], v = src[i+2];
            float b, g, r;

            if( s == 0 )
                b = g = r = v;
            else
            {
                static const int sector_data[][3]=
                    {{1,3,0}, {1,0,2}, {3,0,1}, {0,2,1}, {0,1,3}, {2,1,0}};
                float tab[4];
                int sector;
                h *= _hscale;
                if( h < 0 )
                    do h += 6; while( h < 0 );
                else if( h >= 6 )
                    do h -= 6; while( h >= 6 );
                sector = cvFloor(h);
                h -= sector;
                if( (unsigned)sector >= 6u )
                {
                    sector = 0;
                    h = 0.f;
                }

                tab[0] = v;
                tab[1] = v*(1.f - s);
                tab[2] = v*(1.f - s*h);
                tab[3] = v*(1.f - s*(1.f - h));

                b = tab[sector_data[sector][0]];
                g = tab[sector_data[sector][1]];
                r = tab[sector_data[sector][2]];
            }

            dst[bidx] = b;
            dst[1] = g;
            dst[bidx^2] = r;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }

    int dstcn, blueIdx;
    float hscale;
    #if CV_SSE2
    bool haveSIMD;
    #endif
};


struct HSV2RGB_b
{
    typedef uchar channel_type;

    HSV2RGB_b(int _dstcn, int _blueIdx, int _hrange)
    : dstcn(_dstcn), cvt(3, _blueIdx, (float)_hrange)
    {
        #if CV_NEON
        v_scale_inv = vdupq_n_f32(1.f/255.f);
        v_scale = vdupq_n_f32(255.f);
        v_alpha = vdup_n_u8(ColorChannel<uchar>::max());
        #elif CV_SSE2
        v_scale = _mm_set1_ps(255.0f);
        v_alpha = _mm_set1_ps(ColorChannel<uchar>::max());
        v_zero = _mm_setzero_si128();
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(__m128i v_r, __m128i v_g, __m128i v_b,
                 const __m128& v_coeffs_,
                 float * buf) const
    {
        __m128 v_r0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_r, v_zero));
        __m128 v_g0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_g, v_zero));
        __m128 v_b0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_b, v_zero));

        __m128 v_r1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_r, v_zero));
        __m128 v_g1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_g, v_zero));
        __m128 v_b1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_b, v_zero));

        __m128 v_coeffs = v_coeffs_;

        v_r0 = _mm_mul_ps(v_r0, v_coeffs);
        v_g1 = _mm_mul_ps(v_g1, v_coeffs);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));

        v_r1 = _mm_mul_ps(v_r1, v_coeffs);
        v_b0 = _mm_mul_ps(v_b0, v_coeffs);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));

        v_g0 = _mm_mul_ps(v_g0, v_coeffs);
        v_b1 = _mm_mul_ps(v_b1, v_coeffs);

        _mm_store_ps(buf, v_r0);
        _mm_store_ps(buf + 4, v_r1);
        _mm_store_ps(buf + 8, v_g0);
        _mm_store_ps(buf + 12, v_g1);
        _mm_store_ps(buf + 16, v_b0);
        _mm_store_ps(buf + 20, v_b1);
    }
    #endif

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i, j, dcn = dstcn;
        uchar alpha = ColorChannel<uchar>::max();
        float CV_DECL_ALIGNED(16) buf[3*BLOCK_SIZE];
        #if CV_SSE2
        __m128 v_coeffs = _mm_set_ps(1.f, 1.f/255.f, 1.f/255.f, 1.f);
        #endif

        for( i = 0; i < n; i += BLOCK_SIZE, src += BLOCK_SIZE*3 )
        {
            int dn = std::min(n - i, (int)BLOCK_SIZE);
            j = 0;

            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24)
            {
                uint8x8x3_t v_src = vld3_u8(src + j);
                uint16x8_t v_t0 = vmovl_u8(v_src.val[0]),
                           v_t1 = vmovl_u8(v_src.val[1]),
                           v_t2 = vmovl_u8(v_src.val[2]);

                float32x4x3_t v_dst;
                v_dst.val[0] = vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t0)));
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j, v_dst);

                v_dst.val[0] = vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t0)));
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j + 12, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; j <= (dn - 8) * 3; j += 24)
                {
                    __m128i v_src0 = _mm_loadu_si128((__m128i const *)(src + j));
                    __m128i v_src1 = _mm_loadl_epi64((__m128i const *)(src + j + 16));

                    process(_mm_unpacklo_epi8(v_src0, v_zero),
                            _mm_unpackhi_epi8(v_src0, v_zero),
                            _mm_unpacklo_epi8(v_src1, v_zero),
                            v_coeffs,
                            buf + j);
                }
            }
            #endif

            for( ; j < dn*3; j += 3 )
            {
                buf[j] = src[j];
                buf[j+1] = src[j+1]*(1.f/255.f);
                buf[j+2] = src[j+2]*(1.f/255.f);
            }
            cvt(buf, buf, dn);

            j = 0;
            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24, dst += dcn * 8)
            {
                float32x4x3_t v_src0 = vld3q_f32(buf + j), v_src1 = vld3q_f32(buf + j + 12);
                uint8x8_t v_dst0 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[0], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[0], v_scale)))));
                uint8x8_t v_dst1 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[1], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[1], v_scale)))));
                uint8x8_t v_dst2 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[2], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[2], v_scale)))));

                if (dcn == 4)
                {
                    uint8x8x4_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    v_dst.val[3] = v_alpha;
                    vst4_u8(dst, v_dst);
                }
                else
                {
                    uint8x8x3_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    vst3_u8(dst, v_dst);
                }
            }
            #elif CV_SSE2
            if (dcn == 3 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 16); j += 16, dst += 16)
                {
                    __m128 v_src0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_src1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_src2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);
                    __m128 v_src3 = _mm_mul_ps(_mm_load_ps(buf + j + 12), v_scale);

                    __m128i v_dst0 = _mm_packs_epi32(_mm_cvtps_epi32(v_src0),
                                                     _mm_cvtps_epi32(v_src1));
                    __m128i v_dst1 = _mm_packs_epi32(_mm_cvtps_epi32(v_src2),
                                                     _mm_cvtps_epi32(v_src3));

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            else if (dcn == 4 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 12); j += 12, dst += 16)
                {
                    __m128 v_buf0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_buf1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_buf2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);

                    __m128 v_ba0 = _mm_unpackhi_ps(v_buf0, v_alpha);
                    __m128 v_ba1 = _mm_unpacklo_ps(v_buf2, v_alpha);

                    __m128i v_src0 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf0, v_ba0, 0x44));
                    __m128i v_src1 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba0, v_buf1, 0x4e)), 0x78);
                    __m128i v_src2 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf1, v_ba1, 0x4e));
                    __m128i v_src3 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba1, v_buf2, 0xee)), 0x78);

                    __m128i v_dst0 = _mm_packs_epi32(v_src0, v_src1);
                    __m128i v_dst1 = _mm_packs_epi32(v_src2, v_src3);

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            #endif

            for( ; j < dn*3; j += 3, dst += dcn )
            {
                dst[0] = saturate_cast<uchar>(buf[j]*255.f);
                dst[1] = saturate_cast<uchar>(buf[j+1]*255.f);
                dst[2] = saturate_cast<uchar>(buf[j+2]*255.f);
                if( dcn == 4 )
                    dst[3] = alpha;
            }
        }
    }

    int dstcn;
    HSV2RGB_f cvt;
    #if CV_NEON
    float32x4_t v_scale, v_scale_inv;
    uint8x8_t v_alpha;
    #elif CV_SSE2
    __m128 v_scale;
    __m128 v_alpha;
    __m128i v_zero;
    bool haveSIMD;
    #endif
};


///////////////////////////////////// RGB <-> HLS ////////////////////////////////////////

struct RGB2HLS_f
{
    typedef float channel_type;

    RGB2HLS_f(int _srccn, int _blueIdx, float _hrange)
    : srccn(_srccn), blueIdx(_blueIdx), hscale(_hrange/360.f) {
        #if CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(__m128& v_b0, __m128& v_b1, __m128& v_g0,
                 __m128& v_g1, __m128& v_r0, __m128& v_r1) const
    {
        __m128 v_max0 = _mm_max_ps(_mm_max_ps(v_b0, v_g0), v_r0);
        __m128 v_max1 = _mm_max_ps(_mm_max_ps(v_b1, v_g1), v_r1);
        __m128 v_min0 = _mm_min_ps(_mm_min_ps(v_b0, v_g0), v_r0);
        __m128 v_min1 = _mm_min_ps(_mm_min_ps(v_b1, v_g1), v_r1);
        __m128 v_diff0 = _mm_sub_ps(v_max0, v_min0);
        __m128 v_diff1 = _mm_sub_ps(v_max1, v_min1);
        __m128 v_sum0 = _mm_add_ps(v_max0, v_min0);
        __m128 v_sum1 = _mm_add_ps(v_max1, v_min1);
        __m128 v_l0 = _mm_mul_ps(v_sum0, _mm_set1_ps(0.5f));
        __m128 v_l1 = _mm_mul_ps(v_sum1, _mm_set1_ps(0.5f));

        __m128 v_gel0 = _mm_cmpge_ps(v_l0, _mm_set1_ps(0.5f));
        __m128 v_gel1 = _mm_cmpge_ps(v_l1, _mm_set1_ps(0.5f));
        __m128 v_s0 = _mm_and_ps(v_gel0, _mm_sub_ps(_mm_set1_ps(2.0f), v_sum0));
        __m128 v_s1 = _mm_and_ps(v_gel1, _mm_sub_ps(_mm_set1_ps(2.0f), v_sum1));
        v_s0 = _mm_or_ps(v_s0, _mm_andnot_ps(v_gel0, v_sum0));
        v_s1 = _mm_or_ps(v_s1, _mm_andnot_ps(v_gel1, v_sum1));
        v_s0 = _mm_div_ps(v_diff0, v_s0);
        v_s1 = _mm_div_ps(v_diff1, v_s1);

        __m128 v_gteps0 = _mm_cmpgt_ps(v_diff0, _mm_set1_ps(FLT_EPSILON));
        __m128 v_gteps1 = _mm_cmpgt_ps(v_diff1, _mm_set1_ps(FLT_EPSILON));

        v_diff0 = _mm_div_ps(_mm_set1_ps(60.f), v_diff0);
        v_diff1 = _mm_div_ps(_mm_set1_ps(60.f), v_diff1);

        __m128 v_eqr0 = _mm_cmpeq_ps(v_max0, v_r0);
        __m128 v_eqr1 = _mm_cmpeq_ps(v_max1, v_r1);
        __m128 v_h0 = _mm_and_ps(v_eqr0, _mm_mul_ps(_mm_sub_ps(v_g0, v_b0), v_diff0));
        __m128 v_h1 = _mm_and_ps(v_eqr1, _mm_mul_ps(_mm_sub_ps(v_g1, v_b1), v_diff1));
        __m128 v_eqg0 = _mm_cmpeq_ps(v_max0, v_g0);
        __m128 v_eqg1 = _mm_cmpeq_ps(v_max1, v_g1);
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(_mm_andnot_ps(v_eqr0, v_eqg0), _mm_add_ps(_mm_mul_ps(_mm_sub_ps(v_b0, v_r0), v_diff0), _mm_set1_ps(120.f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(_mm_andnot_ps(v_eqr1, v_eqg1), _mm_add_ps(_mm_mul_ps(_mm_sub_ps(v_b1, v_r1), v_diff1), _mm_set1_ps(120.f))));
        v_h0 = _mm_or_ps(v_h0, _mm_andnot_ps(_mm_or_ps(v_eqr0, v_eqg0), _mm_add_ps(_mm_mul_ps(_mm_sub_ps(v_r0, v_g0), v_diff0), _mm_set1_ps(240.f))));
        v_h1 = _mm_or_ps(v_h1, _mm_andnot_ps(_mm_or_ps(v_eqr1, v_eqg1), _mm_add_ps(_mm_mul_ps(_mm_sub_ps(v_r1, v_g1), v_diff1), _mm_set1_ps(240.f))));
        v_h0 = _mm_add_ps(v_h0, _mm_and_ps(_mm_cmplt_ps(v_h0, _mm_setzero_ps()), _mm_set1_ps(360.f)));
        v_h1 = _mm_add_ps(v_h1, _mm_and_ps(_mm_cmplt_ps(v_h1, _mm_setzero_ps()), _mm_set1_ps(360.f)));
        v_h0 = _mm_mul_ps(v_h0, _mm_set1_ps(hscale));
        v_h1 = _mm_mul_ps(v_h1, _mm_set1_ps(hscale));

        v_b0 = _mm_and_ps(v_gteps0, v_h0);
        v_b1 = _mm_and_ps(v_gteps1, v_h1);
        v_g0 = v_l0;
        v_g1 = v_l1;
        v_r0 = _mm_and_ps(v_gteps0, v_s0);
        v_r1 = _mm_and_ps(v_gteps1, v_s1);
    }
    #endif

    void operator()(const float* src, float* dst, int n) const
    {
        int i = 0, bidx = blueIdx, scn = srccn;
        n *= 3;

        #if CV_SSE2
        if (haveSIMD)
        {
            for( ; i <= n - 24; i += 24, src += scn * 8 )
            {
                __m128 v_b0 = _mm_loadu_ps(src +  0);
                __m128 v_b1 = _mm_loadu_ps(src +  4);
                __m128 v_g0 = _mm_loadu_ps(src +  8);
                __m128 v_g1 = _mm_loadu_ps(src + 12);
                __m128 v_r0 = _mm_loadu_ps(src + 16);
                __m128 v_r1 = _mm_loadu_ps(src + 20);

                if (scn == 3)
                {
                    _mm_deinterleave_ps(v_b0, v_b1, v_g0, v_g1, v_r0, v_r1);
                }
                else
                {
                    __m128 v_a0 = _mm_loadu_ps(src + 24);
                    __m128 v_a1 = _mm_loadu_ps(src + 28);
                    _mm_deinterleave_ps(v_b0, v_b1, v_g0, v_g1, v_r0, v_r1, v_a0, v_a1);
                }

                if (bidx)
                {
                    __m128 v_tmp0 = v_b0;
                    __m128 v_tmp1 = v_b1;
                    v_b0 = v_r0;
                    v_b1 = v_r1;
                    v_r0 = v_tmp0;
                    v_r1 = v_tmp1;
                }

                process(v_b0, v_b1, v_g0, v_g1, v_r0, v_r1);

                _mm_interleave_ps(v_b0, v_b1, v_g0, v_g1, v_r0, v_r1);

                _mm_storeu_ps(dst + i +  0, v_b0);
                _mm_storeu_ps(dst + i +  4, v_b1);
                _mm_storeu_ps(dst + i +  8, v_g0);
                _mm_storeu_ps(dst + i + 12, v_g1);
                _mm_storeu_ps(dst + i + 16, v_r0);
                _mm_storeu_ps(dst + i + 20, v_r1);
            }
        }
        #endif

        for( ; i < n; i += 3, src += scn )
        {
            float b = src[bidx], g = src[1], r = src[bidx^2];
            float h = 0.f, s = 0.f, l;
            float vmin, vmax, diff;

            vmax = vmin = r;
            if( vmax < g ) vmax = g;
            if( vmax < b ) vmax = b;
            if( vmin > g ) vmin = g;
            if( vmin > b ) vmin = b;

            diff = vmax - vmin;
            l = (vmax + vmin)*0.5f;

            if( diff > FLT_EPSILON )
            {
                s = l < 0.5f ? diff/(vmax + vmin) : diff/(2 - vmax - vmin);
                diff = 60.f/diff;

                if( vmax == r )
                    h = (g - b)*diff;
                else if( vmax == g )
                    h = (b - r)*diff + 120.f;
                else
                    h = (r - g)*diff + 240.f;

                if( h < 0.f ) h += 360.f;
            }

            dst[i] = h*hscale;
            dst[i+1] = l;
            dst[i+2] = s;
        }
    }

    int srccn, blueIdx;
    float hscale;
    #if CV_SSE2
    bool haveSIMD;
    #endif
};


struct RGB2HLS_b
{
    typedef uchar channel_type;

    RGB2HLS_b(int _srccn, int _blueIdx, int _hrange)
    : srccn(_srccn), cvt(3, _blueIdx, (float)_hrange)
    {
        #if CV_NEON
        v_scale_inv = vdupq_n_f32(1.f/255.f);
        v_scale = vdupq_n_f32(255.f);
        v_alpha = vdup_n_u8(ColorChannel<uchar>::max());
        #elif CV_SSE2
        v_scale_inv = _mm_set1_ps(1.f/255.f);
        v_zero = _mm_setzero_si128();
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(const float * buf,
                 __m128 & v_coeffs, uchar * dst) const
    {
        __m128 v_l0f = _mm_load_ps(buf);
        __m128 v_l1f = _mm_load_ps(buf + 4);
        __m128 v_u0f = _mm_load_ps(buf + 8);
        __m128 v_u1f = _mm_load_ps(buf + 12);

        v_l0f = _mm_mul_ps(v_l0f, v_coeffs);
        v_u1f = _mm_mul_ps(v_u1f, v_coeffs);
        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x92));
        v_u0f = _mm_mul_ps(v_u0f, v_coeffs);
        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x92));
        v_l1f = _mm_mul_ps(v_l1f, v_coeffs);

        __m128i v_l = _mm_packs_epi32(_mm_cvtps_epi32(v_l0f), _mm_cvtps_epi32(v_l1f));
        __m128i v_u = _mm_packs_epi32(_mm_cvtps_epi32(v_u0f), _mm_cvtps_epi32(v_u1f));
        __m128i v_l0 = _mm_packus_epi16(v_l, v_u);

        _mm_storeu_si128((__m128i *)(dst), v_l0);
    }
    #endif

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i, j, scn = srccn;
        float CV_DECL_ALIGNED(16) buf[3*BLOCK_SIZE];
        #if CV_SSE2
        __m128 v_coeffs = _mm_set_ps(1.f, 255.f, 255.f, 1.f);
        #endif

        for( i = 0; i < n; i += BLOCK_SIZE, dst += BLOCK_SIZE*3 )
        {
            int dn = std::min(n - i, (int)BLOCK_SIZE);
            j = 0;

            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24, src += 8 * scn)
            {
                uint16x8_t v_t0, v_t1, v_t2;

                if (scn == 3)
                {
                    uint8x8x3_t v_src = vld3_u8(src);
                    v_t0 = vmovl_u8(v_src.val[0]);
                    v_t1 = vmovl_u8(v_src.val[1]);
                    v_t2 = vmovl_u8(v_src.val[2]);
                }
                else
                {
                    uint8x8x4_t v_src = vld4_u8(src);
                    v_t0 = vmovl_u8(v_src.val[0]);
                    v_t1 = vmovl_u8(v_src.val[1]);
                    v_t2 = vmovl_u8(v_src.val[2]);
                }

                float32x4x3_t v_dst;
                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j, v_dst);

                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j + 12, v_dst);
            }
            #elif CV_SSE2
            if (scn == 3 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 16); j += 16, src += 16)
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)src);

                    __m128i v_src_p = _mm_unpacklo_epi8(v_src, v_zero);
                    _mm_store_ps(buf + j, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_p, v_zero)), v_scale_inv));
                    _mm_store_ps(buf + j + 4, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(v_src_p, v_zero)), v_scale_inv));

                    v_src_p = _mm_unpackhi_epi8(v_src, v_zero);
                    _mm_store_ps(buf + j + 8, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_p, v_zero)), v_scale_inv));
                    _mm_store_ps(buf + j + 12, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(v_src_p, v_zero)), v_scale_inv));
                }

                int jr = j % 3;
                if (jr)
                    src -= jr, j -= jr;
            }
            else if (scn == 4 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 12); j += 12, src += 16)
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)src);

                    __m128i v_src_lo = _mm_unpacklo_epi8(v_src, v_zero);
                    __m128i v_src_hi = _mm_unpackhi_epi8(v_src, v_zero);
                    _mm_storeu_ps(buf + j, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_lo, v_zero)), v_scale_inv));
                    _mm_storeu_ps(buf + j + 3, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(v_src_lo, v_zero)), v_scale_inv));
                    _mm_storeu_ps(buf + j + 6, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_hi, v_zero)), v_scale_inv));
                    float tmp = buf[j + 8];
                    _mm_storeu_ps(buf + j + 8, _mm_mul_ps(_mm_cvtepi32_ps(_mm_shuffle_epi32(_mm_unpackhi_epi16(v_src_hi, v_zero), 0x90)), v_scale_inv));
                    buf[j + 8] = tmp;
                }

                int jr = j % 3;
                if (jr)
                    src -= jr, j -= jr;
            }
            #endif
            for( ; j < dn*3; j += 3, src += scn )
            {
                buf[j] = src[0]*(1.f/255.f);
                buf[j+1] = src[1]*(1.f/255.f);
                buf[j+2] = src[2]*(1.f/255.f);
            }
            cvt(buf, buf, dn);

            j = 0;
            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24)
            {
                float32x4x3_t v_src0 = vld3q_f32(buf + j), v_src1 = vld3q_f32(buf + j + 12);

                uint8x8x3_t v_dst;
                v_dst.val[0] = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(v_src0.val[0])),
                                                       vqmovn_u32(cv_vrndq_u32_f32(v_src1.val[0]))));
                v_dst.val[1] = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[1], v_scale))),
                                                       vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[1], v_scale)))));
                v_dst.val[2] = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[2], v_scale))),
                                                       vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[2], v_scale)))));
                vst3_u8(dst + j, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; j <= (dn - 16) * 3; j += 48)
                {
                    process(buf + j,
                            v_coeffs, dst + j);

                    process(buf + j + 16,
                            v_coeffs, dst + j + 16);

                    process(buf + j + 32,
                            v_coeffs, dst + j + 32);
                }
            }
            #endif
            for( ; j < dn*3; j += 3 )
            {
                dst[j] = saturate_cast<uchar>(buf[j]);
                dst[j+1] = saturate_cast<uchar>(buf[j+1]*255.f);
                dst[j+2] = saturate_cast<uchar>(buf[j+2]*255.f);
            }
        }
    }

    int srccn;
    RGB2HLS_f cvt;
    #if CV_NEON
    float32x4_t v_scale, v_scale_inv;
    uint8x8_t v_alpha;
    #elif CV_SSE2
    __m128 v_scale_inv;
    __m128i v_zero;
    bool haveSIMD;
    #endif
};


struct HLS2RGB_f
{
    typedef float channel_type;

    HLS2RGB_f(int _dstcn, int _blueIdx, float _hrange)
    : dstcn(_dstcn), blueIdx(_blueIdx), hscale(6.f/_hrange) {
        #if CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(__m128& v_h0, __m128& v_h1, __m128& v_l0,
                 __m128& v_l1, __m128& v_s0, __m128& v_s1) const
    {
        __m128 v_lel0 = _mm_cmple_ps(v_l0, _mm_set1_ps(0.5f));
        __m128 v_lel1 = _mm_cmple_ps(v_l1, _mm_set1_ps(0.5f));
        __m128 v_p20 = _mm_andnot_ps(v_lel0, _mm_sub_ps(_mm_add_ps(v_l0, v_s0), _mm_mul_ps(v_l0, v_s0)));
        __m128 v_p21 = _mm_andnot_ps(v_lel1, _mm_sub_ps(_mm_add_ps(v_l1, v_s1), _mm_mul_ps(v_l1, v_s1)));
        v_p20 = _mm_or_ps(v_p20, _mm_and_ps(v_lel0, _mm_mul_ps(v_l0, _mm_add_ps(_mm_set1_ps(1.0f), v_s0))));
        v_p21 = _mm_or_ps(v_p21, _mm_and_ps(v_lel1, _mm_mul_ps(v_l1, _mm_add_ps(_mm_set1_ps(1.0f), v_s1))));

        __m128 v_p10 = _mm_sub_ps(_mm_mul_ps(_mm_set1_ps(2.0f), v_l0), v_p20);
        __m128 v_p11 = _mm_sub_ps(_mm_mul_ps(_mm_set1_ps(2.0f), v_l1), v_p21);

        v_h0 = _mm_mul_ps(v_h0, _mm_set1_ps(hscale));
        v_h1 = _mm_mul_ps(v_h1, _mm_set1_ps(hscale));

        __m128 v_pre_sector0 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_h0));
        __m128 v_pre_sector1 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_h1));

        v_h0 = _mm_sub_ps(v_h0, v_pre_sector0);
        v_h1 = _mm_sub_ps(v_h1, v_pre_sector1);

        __m128 v_p2_p10 = _mm_sub_ps(v_p20, v_p10);
        __m128 v_p2_p11 = _mm_sub_ps(v_p21, v_p11);
        __m128 v_tab20 = _mm_add_ps(v_p10, _mm_mul_ps(v_p2_p10, _mm_sub_ps(_mm_set1_ps(1.0f), v_h0)));
        __m128 v_tab21 = _mm_add_ps(v_p11, _mm_mul_ps(v_p2_p11, _mm_sub_ps(_mm_set1_ps(1.0f), v_h1)));
        __m128 v_tab30 = _mm_add_ps(v_p10, _mm_mul_ps(v_p2_p10, v_h0));
        __m128 v_tab31 = _mm_add_ps(v_p11, _mm_mul_ps(v_p2_p11, v_h1));

        __m128 v_sector0 = _mm_div_ps(v_pre_sector0, _mm_set1_ps(6.0f));
        __m128 v_sector1 = _mm_div_ps(v_pre_sector1, _mm_set1_ps(6.0f));
        v_sector0 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_sector0));
        v_sector1 = _mm_cvtepi32_ps(_mm_cvttps_epi32(v_sector1));
        v_sector0 = _mm_mul_ps(v_sector0, _mm_set1_ps(6.0f));
        v_sector1 = _mm_mul_ps(v_sector1, _mm_set1_ps(6.0f));
        v_sector0 = _mm_sub_ps(v_pre_sector0, v_sector0);
        v_sector1 = _mm_sub_ps(v_pre_sector1, v_sector1);

        v_h0 = _mm_and_ps(v_p10, _mm_cmplt_ps(v_sector0, _mm_set1_ps(2.0f)));
        v_h1 = _mm_and_ps(v_p11, _mm_cmplt_ps(v_sector1, _mm_set1_ps(2.0f)));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_tab30, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(2.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_tab31, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(2.0f))));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_p20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_p21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_p20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_p21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(4.0f))));
        v_h0 = _mm_or_ps(v_h0, _mm_and_ps(v_tab20, _mm_cmpgt_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_h1 = _mm_or_ps(v_h1, _mm_and_ps(v_tab21, _mm_cmpgt_ps(v_sector1, _mm_set1_ps(4.0f))));
        v_l0 = _mm_and_ps(v_tab30, _mm_cmplt_ps(v_sector0, _mm_set1_ps(1.0f)));
        v_l1 = _mm_and_ps(v_tab31, _mm_cmplt_ps(v_sector1, _mm_set1_ps(1.0f)));
        v_l0 = _mm_or_ps(v_l0, _mm_and_ps(v_p20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(1.0f))));
        v_l1 = _mm_or_ps(v_l1, _mm_and_ps(v_p21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(1.0f))));
        v_l0 = _mm_or_ps(v_l0, _mm_and_ps(v_p20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(2.0f))));
        v_l1 = _mm_or_ps(v_l1, _mm_and_ps(v_p21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(2.0f))));
        v_l0 = _mm_or_ps(v_l0, _mm_and_ps(v_tab20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_l1 = _mm_or_ps(v_l1, _mm_and_ps(v_tab21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_l0 = _mm_or_ps(v_l0, _mm_and_ps(v_p10, _mm_cmpgt_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_l1 = _mm_or_ps(v_l1, _mm_and_ps(v_p11, _mm_cmpgt_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_s0 = _mm_and_ps(v_p20, _mm_cmplt_ps(v_sector0, _mm_set1_ps(1.0f)));
        v_s1 = _mm_and_ps(v_p21, _mm_cmplt_ps(v_sector1, _mm_set1_ps(1.0f)));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_tab20, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(1.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_tab21, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(1.0f))));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_p10, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(2.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_p11, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(2.0f))));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_p10, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(3.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_p11, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(3.0f))));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_tab30, _mm_cmpeq_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_tab31, _mm_cmpeq_ps(v_sector1, _mm_set1_ps(4.0f))));
        v_s0 = _mm_or_ps(v_s0, _mm_and_ps(v_p20, _mm_cmpgt_ps(v_sector0, _mm_set1_ps(4.0f))));
        v_s1 = _mm_or_ps(v_s1, _mm_and_ps(v_p21, _mm_cmpgt_ps(v_sector1, _mm_set1_ps(4.0f))));
    }
    #endif

    void operator()(const float* src, float* dst, int n) const
    {
        int i = 0, bidx = blueIdx, dcn = dstcn;
        float _hscale = hscale;
        float alpha = ColorChannel<float>::max();
        n *= 3;

        #if CV_SSE2
        if (haveSIMD)
        {
            for( ; i <= n - 24; i += 24, dst += dcn * 8 )
            {
                __m128 v_h0 = _mm_loadu_ps(src + i +  0);
                __m128 v_h1 = _mm_loadu_ps(src + i +  4);
                __m128 v_l0 = _mm_loadu_ps(src + i +  8);
                __m128 v_l1 = _mm_loadu_ps(src + i + 12);
                __m128 v_s0 = _mm_loadu_ps(src + i + 16);
                __m128 v_s1 = _mm_loadu_ps(src + i + 20);

                _mm_deinterleave_ps(v_h0, v_h1, v_l0, v_l1, v_s0, v_s1);

                process(v_h0, v_h1, v_l0, v_l1, v_s0, v_s1);

                if (dcn == 3)
                {
                    if (bidx)
                    {
                        _mm_interleave_ps(v_s0, v_s1, v_l0, v_l1, v_h0, v_h1);

                        _mm_storeu_ps(dst +  0, v_s0);
                        _mm_storeu_ps(dst +  4, v_s1);
                        _mm_storeu_ps(dst +  8, v_l0);
                        _mm_storeu_ps(dst + 12, v_l1);
                        _mm_storeu_ps(dst + 16, v_h0);
                        _mm_storeu_ps(dst + 20, v_h1);
                    }
                    else
                    {
                        _mm_interleave_ps(v_h0, v_h1, v_l0, v_l1, v_s0, v_s1);

                        _mm_storeu_ps(dst +  0, v_h0);
                        _mm_storeu_ps(dst +  4, v_h1);
                        _mm_storeu_ps(dst +  8, v_l0);
                        _mm_storeu_ps(dst + 12, v_l1);
                        _mm_storeu_ps(dst + 16, v_s0);
                        _mm_storeu_ps(dst + 20, v_s1);
                    }
                }
                else
                {
                    __m128 v_a0 = _mm_set1_ps(alpha);
                    __m128 v_a1 = _mm_set1_ps(alpha);
                    if (bidx)
                    {
                        _mm_interleave_ps(v_s0, v_s1, v_l0, v_l1, v_h0, v_h1, v_a0, v_a1);

                        _mm_storeu_ps(dst +  0, v_s0);
                        _mm_storeu_ps(dst +  4, v_s1);
                        _mm_storeu_ps(dst +  8, v_l0);
                        _mm_storeu_ps(dst + 12, v_l1);
                        _mm_storeu_ps(dst + 16, v_h0);
                        _mm_storeu_ps(dst + 20, v_h1);
                        _mm_storeu_ps(dst + 24, v_a0);
                        _mm_storeu_ps(dst + 28, v_a1);
                    }
                    else
                    {
                        _mm_interleave_ps(v_h0, v_h1, v_l0, v_l1, v_s0, v_s1, v_a0, v_a1);

                        _mm_storeu_ps(dst +  0, v_h0);
                        _mm_storeu_ps(dst +  4, v_h1);
                        _mm_storeu_ps(dst +  8, v_l0);
                        _mm_storeu_ps(dst + 12, v_l1);
                        _mm_storeu_ps(dst + 16, v_s0);
                        _mm_storeu_ps(dst + 20, v_s1);
                        _mm_storeu_ps(dst + 24, v_a0);
                        _mm_storeu_ps(dst + 28, v_a1);
                    }
                }
            }
        }
        #endif
        for( ; i < n; i += 3, dst += dcn )
        {
            float h = src[i], l = src[i+1], s = src[i+2];
            float b, g, r;

            if( s == 0 )
                b = g = r = l;
            else
            {
                static const int sector_data[][3]=
                {{1,3,0}, {1,0,2}, {3,0,1}, {0,2,1}, {0,1,3}, {2,1,0}};
                float tab[4];
                int sector;

                float p2 = l <= 0.5f ? l*(1 + s) : l + s - l*s;
                float p1 = 2*l - p2;

                h *= _hscale;
                if( h < 0 )
                    do h += 6; while( h < 0 );
                else if( h >= 6 )
                    do h -= 6; while( h >= 6 );

                assert( 0 <= h && h < 6 );
                sector = cvFloor(h);
                h -= sector;

                tab[0] = p2;
                tab[1] = p1;
                tab[2] = p1 + (p2 - p1)*(1-h);
                tab[3] = p1 + (p2 - p1)*h;

                b = tab[sector_data[sector][0]];
                g = tab[sector_data[sector][1]];
                r = tab[sector_data[sector][2]];
            }

            dst[bidx] = b;
            dst[1] = g;
            dst[bidx^2] = r;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }

    int dstcn, blueIdx;
    float hscale;
    #if CV_SSE2
    bool haveSIMD;
    #endif
};


struct HLS2RGB_b
{
    typedef uchar channel_type;

    HLS2RGB_b(int _dstcn, int _blueIdx, int _hrange)
    : dstcn(_dstcn), cvt(3, _blueIdx, (float)_hrange)
    {
        #if CV_NEON
        v_scale_inv = vdupq_n_f32(1.f/255.f);
        v_scale = vdupq_n_f32(255.f);
        v_alpha = vdup_n_u8(ColorChannel<uchar>::max());
        #elif CV_SSE2
        v_scale = _mm_set1_ps(255.f);
        v_alpha = _mm_set1_ps(ColorChannel<uchar>::max());
        v_zero = _mm_setzero_si128();
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(__m128i v_r, __m128i v_g, __m128i v_b,
                 const __m128& v_coeffs_,
                 float * buf) const
    {
        __m128 v_r0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_r, v_zero));
        __m128 v_g0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_g, v_zero));
        __m128 v_b0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_b, v_zero));

        __m128 v_r1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_r, v_zero));
        __m128 v_g1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_g, v_zero));
        __m128 v_b1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_b, v_zero));

        __m128 v_coeffs = v_coeffs_;

        v_r0 = _mm_mul_ps(v_r0, v_coeffs);
        v_g1 = _mm_mul_ps(v_g1, v_coeffs);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));

        v_r1 = _mm_mul_ps(v_r1, v_coeffs);
        v_b0 = _mm_mul_ps(v_b0, v_coeffs);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));

        v_g0 = _mm_mul_ps(v_g0, v_coeffs);
        v_b1 = _mm_mul_ps(v_b1, v_coeffs);

        _mm_store_ps(buf, v_r0);
        _mm_store_ps(buf + 4, v_r1);
        _mm_store_ps(buf + 8, v_g0);
        _mm_store_ps(buf + 12, v_g1);
        _mm_store_ps(buf + 16, v_b0);
        _mm_store_ps(buf + 20, v_b1);
    }
    #endif

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i, j, dcn = dstcn;
        uchar alpha = ColorChannel<uchar>::max();
        float CV_DECL_ALIGNED(16) buf[3*BLOCK_SIZE];
        #if CV_SSE2
        __m128 v_coeffs = _mm_set_ps(1.f, 1.f/255.f, 1.f/255.f, 1.f);
        #endif

        for( i = 0; i < n; i += BLOCK_SIZE, src += BLOCK_SIZE*3 )
        {
            int dn = std::min(n - i, (int)BLOCK_SIZE);
            j = 0;

            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24)
            {
                uint8x8x3_t v_src = vld3_u8(src + j);
                uint16x8_t v_t0 = vmovl_u8(v_src.val[0]),
                           v_t1 = vmovl_u8(v_src.val[1]),
                           v_t2 = vmovl_u8(v_src.val[2]);

                float32x4x3_t v_dst;
                v_dst.val[0] = vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t0)));
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j, v_dst);

                v_dst.val[0] = vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t0)));
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j + 12, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; j <= (dn - 8) * 3; j += 24)
                {
                    __m128i v_src0 = _mm_loadu_si128((__m128i const *)(src + j));
                    __m128i v_src1 = _mm_loadl_epi64((__m128i const *)(src + j + 16));

                    process(_mm_unpacklo_epi8(v_src0, v_zero),
                            _mm_unpackhi_epi8(v_src0, v_zero),
                            _mm_unpacklo_epi8(v_src1, v_zero),
                            v_coeffs,
                            buf + j);
                }
            }
            #endif
            for( ; j < dn*3; j += 3 )
            {
                buf[j] = src[j];
                buf[j+1] = src[j+1]*(1.f/255.f);
                buf[j+2] = src[j+2]*(1.f/255.f);
            }
            cvt(buf, buf, dn);

            j = 0;
            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24, dst += dcn * 8)
            {
                float32x4x3_t v_src0 = vld3q_f32(buf + j), v_src1 = vld3q_f32(buf + j + 12);
                uint8x8_t v_dst0 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[0], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[0], v_scale)))));
                uint8x8_t v_dst1 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[1], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[1], v_scale)))));
                uint8x8_t v_dst2 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[2], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[2], v_scale)))));

                if (dcn == 4)
                {
                    uint8x8x4_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    v_dst.val[3] = v_alpha;
                    vst4_u8(dst, v_dst);
                }
                else
                {
                    uint8x8x3_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    vst3_u8(dst, v_dst);
                }
            }
            #elif CV_SSE2
            if (dcn == 3 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 16); j += 16, dst += 16)
                {
                    __m128 v_src0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_src1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_src2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);
                    __m128 v_src3 = _mm_mul_ps(_mm_load_ps(buf + j + 12), v_scale);

                    __m128i v_dst0 = _mm_packs_epi32(_mm_cvtps_epi32(v_src0),
                                                     _mm_cvtps_epi32(v_src1));
                    __m128i v_dst1 = _mm_packs_epi32(_mm_cvtps_epi32(v_src2),
                                                     _mm_cvtps_epi32(v_src3));

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            else if (dcn == 4 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 12); j += 12, dst += 16)
                {
                    __m128 v_buf0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_buf1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_buf2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);

                    __m128 v_ba0 = _mm_unpackhi_ps(v_buf0, v_alpha);
                    __m128 v_ba1 = _mm_unpacklo_ps(v_buf2, v_alpha);

                    __m128i v_src0 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf0, v_ba0, 0x44));
                    __m128i v_src1 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba0, v_buf1, 0x4e)), 0x78);
                    __m128i v_src2 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf1, v_ba1, 0x4e));
                    __m128i v_src3 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba1, v_buf2, 0xee)), 0x78);

                    __m128i v_dst0 = _mm_packs_epi32(v_src0, v_src1);
                    __m128i v_dst1 = _mm_packs_epi32(v_src2, v_src3);

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            #endif

            for( ; j < dn*3; j += 3, dst += dcn )
            {
                dst[0] = saturate_cast<uchar>(buf[j]*255.f);
                dst[1] = saturate_cast<uchar>(buf[j+1]*255.f);
                dst[2] = saturate_cast<uchar>(buf[j+2]*255.f);
                if( dcn == 4 )
                    dst[3] = alpha;
            }
        }
    }

    int dstcn;
    HLS2RGB_f cvt;
    #if CV_NEON
    float32x4_t v_scale, v_scale_inv;
    uint8x8_t v_alpha;
    #elif CV_SSE2
    __m128 v_scale;
    __m128 v_alpha;
    __m128i v_zero;
    bool haveSIMD;
    #endif
};


///////////////////////////////////// RGB <-> L*a*b* /////////////////////////////////////

static const float D65[] = { 0.950456f, 1.f, 1.088754f };

enum { LAB_CBRT_TAB_SIZE = 1024, GAMMA_TAB_SIZE = 1024 };
static float LabCbrtTab[LAB_CBRT_TAB_SIZE*4];
static const float LabCbrtTabScale = LAB_CBRT_TAB_SIZE/1.5f;

static float sRGBGammaTab[GAMMA_TAB_SIZE*4], sRGBInvGammaTab[GAMMA_TAB_SIZE*4];
static const float GammaTabScale = (float)GAMMA_TAB_SIZE;

static ushort sRGBGammaTab_b[256], linearGammaTab_b[256];
#undef lab_shift
#define lab_shift xyz_shift
#define gamma_shift 3
#define lab_shift2 (lab_shift + gamma_shift)
#define LAB_CBRT_TAB_SIZE_B (256*3/2*(1<<gamma_shift))
static ushort LabCbrtTab_b[LAB_CBRT_TAB_SIZE_B];

static void initLabTabs()
{
    static bool initialized = false;
    if(!initialized)
    {
        float f[LAB_CBRT_TAB_SIZE+1], g[GAMMA_TAB_SIZE+1], ig[GAMMA_TAB_SIZE+1], scale = 1.f/LabCbrtTabScale;
        int i;
        for(i = 0; i <= LAB_CBRT_TAB_SIZE; i++)
        {
            float x = i*scale;
            f[i] = x < 0.008856f ? x*7.787f + 0.13793103448275862f : cvCbrt(x);
        }
        splineBuild(f, LAB_CBRT_TAB_SIZE, LabCbrtTab);

        scale = 1.f/GammaTabScale;
        for(i = 0; i <= GAMMA_TAB_SIZE; i++)
        {
            float x = i*scale;
            g[i] = x <= 0.04045f ? x*(1.f/12.92f) : (float)std::pow((double)(x + 0.055)*(1./1.055), 2.4);
            ig[i] = x <= 0.0031308 ? x*12.92f : (float)(1.055*std::pow((double)x, 1./2.4) - 0.055);
        }
        splineBuild(g, GAMMA_TAB_SIZE, sRGBGammaTab);
        splineBuild(ig, GAMMA_TAB_SIZE, sRGBInvGammaTab);

        for(i = 0; i < 256; i++)
        {
            float x = i*(1.f/255.f);
            sRGBGammaTab_b[i] = saturate_cast<ushort>(255.f*(1 << gamma_shift)*(x <= 0.04045f ? x*(1.f/12.92f) : (float)std::pow((double)(x + 0.055)*(1./1.055), 2.4)));
            linearGammaTab_b[i] = (ushort)(i*(1 << gamma_shift));
        }

        for(i = 0; i < LAB_CBRT_TAB_SIZE_B; i++)
        {
            float x = i*(1.f/(255.f*(1 << gamma_shift)));
            LabCbrtTab_b[i] = saturate_cast<ushort>((1 << lab_shift2)*(x < 0.008856f ? x*7.787f + 0.13793103448275862f : cvCbrt(x)));
        }
        initialized = true;
    }
}

struct RGB2Lab_b
{
    typedef uchar channel_type;

    RGB2Lab_b(int _srccn, int blueIdx, const float* _coeffs,
              const float* _whitept, bool _srgb)
    : srccn(_srccn), srgb(_srgb)
    {
        static volatile int _3 = 3;
        initLabTabs();

        if (!_coeffs)
            _coeffs = sRGB2XYZ_D65;
        if (!_whitept)
            _whitept = D65;

        float scale[] =
        {
            (1 << lab_shift)/_whitept[0],
            (float)(1 << lab_shift),
            (1 << lab_shift)/_whitept[2]
        };

        for( int i = 0; i < _3; i++ )
        {
            coeffs[i*3+(blueIdx^2)] = cvRound(_coeffs[i*3]*scale[i]);
            coeffs[i*3+1] = cvRound(_coeffs[i*3+1]*scale[i]);
            coeffs[i*3+blueIdx] = cvRound(_coeffs[i*3+2]*scale[i]);

            CV_Assert( coeffs[i] >= 0 && coeffs[i*3+1] >= 0 && coeffs[i*3+2] >= 0 &&
                      coeffs[i*3] + coeffs[i*3+1] + coeffs[i*3+2] < 2*(1 << lab_shift) );
        }
    }

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        const int Lscale = (116*255+50)/100;
        const int Lshift = -((16*255*(1 << lab_shift2) + 50)/100);
        const ushort* tab = srgb ? sRGBGammaTab_b : linearGammaTab_b;
        int i, scn = srccn;
        int C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
            C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
            C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        for( i = 0; i < n; i += 3, src += scn )
        {
            int R = tab[src[0]], G = tab[src[1]], B = tab[src[2]];
            int fX = LabCbrtTab_b[CV_DESCALE(R*C0 + G*C1 + B*C2, lab_shift)];
            int fY = LabCbrtTab_b[CV_DESCALE(R*C3 + G*C4 + B*C5, lab_shift)];
            int fZ = LabCbrtTab_b[CV_DESCALE(R*C6 + G*C7 + B*C8, lab_shift)];

            int L = CV_DESCALE( Lscale*fY + Lshift, lab_shift2 );
            int a = CV_DESCALE( 500*(fX - fY) + 128*(1 << lab_shift2), lab_shift2 );
            int b = CV_DESCALE( 200*(fY - fZ) + 128*(1 << lab_shift2), lab_shift2 );

            dst[i] = saturate_cast<uchar>(L);
            dst[i+1] = saturate_cast<uchar>(a);
            dst[i+2] = saturate_cast<uchar>(b);
        }
    }

    int srccn;
    int coeffs[9];
    bool srgb;
};


#define clip(value) \
    value < 0.0f ? 0.0f : value > 1.0f ? 1.0f : value;

struct RGB2Lab_f
{
    typedef float channel_type;

    RGB2Lab_f(int _srccn, int blueIdx, const float* _coeffs,
              const float* _whitept, bool _srgb)
    : srccn(_srccn), srgb(_srgb)
    {
        volatile int _3 = 3;
        initLabTabs();

        if (!_coeffs)
            _coeffs = sRGB2XYZ_D65;
        if (!_whitept)
            _whitept = D65;

        float scale[] = { 1.0f / _whitept[0], 1.0f, 1.0f / _whitept[2] };

        for( int i = 0; i < _3; i++ )
        {
            int j = i * 3;
            coeffs[j + (blueIdx ^ 2)] = _coeffs[j] * scale[i];
            coeffs[j + 1] = _coeffs[j + 1] * scale[i];
            coeffs[j + blueIdx] = _coeffs[j + 2] * scale[i];

            CV_Assert( coeffs[j] >= 0 && coeffs[j + 1] >= 0 && coeffs[j + 2] >= 0 &&
                       coeffs[j] + coeffs[j + 1] + coeffs[j + 2] < 1.5f*LabCbrtTabScale );
        }
    }

    void operator()(const float* src, float* dst, int n) const
    {
        int i, scn = srccn;
        float gscale = GammaTabScale;
        const float* gammaTab = srgb ? sRGBGammaTab : 0;
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        static const float _1_3 = 1.0f / 3.0f;
        static const float _a = 16.0f / 116.0f;
        for (i = 0; i < n; i += 3, src += scn )
        {
            float R = clip(src[0]);
            float G = clip(src[1]);
            float B = clip(src[2]);

            if (gammaTab)
            {
                R = splineInterpolate(R * gscale, gammaTab, GAMMA_TAB_SIZE);
                G = splineInterpolate(G * gscale, gammaTab, GAMMA_TAB_SIZE);
                B = splineInterpolate(B * gscale, gammaTab, GAMMA_TAB_SIZE);
            }
            float X = R*C0 + G*C1 + B*C2;
            float Y = R*C3 + G*C4 + B*C5;
            float Z = R*C6 + G*C7 + B*C8;

            float FX = X > 0.008856f ? std::pow(X, _1_3) : (7.787f * X + _a);
            float FY = Y > 0.008856f ? std::pow(Y, _1_3) : (7.787f * Y + _a);
            float FZ = Z > 0.008856f ? std::pow(Z, _1_3) : (7.787f * Z + _a);

            float L = Y > 0.008856f ? (116.f * FY - 16.f) : (903.3f * Y);
            float a = 500.f * (FX - FY);
            float b = 200.f * (FY - FZ);

            dst[i] = L;
            dst[i + 1] = a;
            dst[i + 2] = b;
        }
    }

    int srccn;
    float coeffs[9];
    bool srgb;
};

struct Lab2RGB_f
{
    typedef float channel_type;

    Lab2RGB_f( int _dstcn, int blueIdx, const float* _coeffs,
              const float* _whitept, bool _srgb )
    : dstcn(_dstcn), srgb(_srgb)
    {
        initLabTabs();

        if(!_coeffs)
            _coeffs = XYZ2sRGB_D65;
        if(!_whitept)
            _whitept = D65;

        for( int i = 0; i < 3; i++ )
        {
            coeffs[i+(blueIdx^2)*3] = _coeffs[i]*_whitept[i];
            coeffs[i+3] = _coeffs[i+3]*_whitept[i];
            coeffs[i+blueIdx*3] = _coeffs[i+6]*_whitept[i];
        }

        lThresh = 0.008856f * 903.3f;
        fThresh = 7.787f * 0.008856f + 16.0f / 116.0f;
        #if CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(__m128& v_li0, __m128& v_li1, __m128& v_ai0,
                 __m128& v_ai1, __m128& v_bi0, __m128& v_bi1) const
    {
        __m128 v_y00 = _mm_mul_ps(v_li0, _mm_set1_ps(1.0f/903.3f));
        __m128 v_y01 = _mm_mul_ps(v_li1, _mm_set1_ps(1.0f/903.3f));
        __m128 v_fy00 = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(7.787f), v_y00), _mm_set1_ps(16.0f/116.0f));
        __m128 v_fy01 = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(7.787f), v_y01), _mm_set1_ps(16.0f/116.0f));

        __m128 v_fy10 = _mm_mul_ps(_mm_add_ps(v_li0, _mm_set1_ps(16.0f)), _mm_set1_ps(1.0f/116.0f));
        __m128 v_fy11 = _mm_mul_ps(_mm_add_ps(v_li1, _mm_set1_ps(16.0f)), _mm_set1_ps(1.0f/116.0f));
        __m128 v_y10 = _mm_mul_ps(_mm_mul_ps(v_fy10, v_fy10), v_fy10);
        __m128 v_y11 = _mm_mul_ps(_mm_mul_ps(v_fy11, v_fy11), v_fy11);

        __m128 v_cmpli0 = _mm_cmple_ps(v_li0, _mm_set1_ps(lThresh));
        __m128 v_cmpli1 = _mm_cmple_ps(v_li1, _mm_set1_ps(lThresh));
        v_y00 = _mm_and_ps(v_cmpli0, v_y00);
        v_y01 = _mm_and_ps(v_cmpli1, v_y01);
        v_fy00 = _mm_and_ps(v_cmpli0, v_fy00);
        v_fy01 = _mm_and_ps(v_cmpli1, v_fy01);
        v_y10 = _mm_andnot_ps(v_cmpli0, v_y10);
        v_y11 = _mm_andnot_ps(v_cmpli1, v_y11);
        v_fy10 = _mm_andnot_ps(v_cmpli0, v_fy10);
        v_fy11 = _mm_andnot_ps(v_cmpli1, v_fy11);
        __m128 v_y0 = _mm_or_ps(v_y00, v_y10);
        __m128 v_y1 = _mm_or_ps(v_y01, v_y11);
        __m128 v_fy0 = _mm_or_ps(v_fy00, v_fy10);
        __m128 v_fy1 = _mm_or_ps(v_fy01, v_fy11);

        __m128 v_fxz00 = _mm_add_ps(v_fy0, _mm_mul_ps(v_ai0, _mm_set1_ps(0.002f)));
        __m128 v_fxz01 = _mm_add_ps(v_fy1, _mm_mul_ps(v_ai1, _mm_set1_ps(0.002f)));
        __m128 v_fxz10 = _mm_sub_ps(v_fy0, _mm_mul_ps(v_bi0, _mm_set1_ps(0.005f)));
        __m128 v_fxz11 = _mm_sub_ps(v_fy1, _mm_mul_ps(v_bi1, _mm_set1_ps(0.005f)));

        __m128 v_fxz000 = _mm_mul_ps(_mm_sub_ps(v_fxz00, _mm_set1_ps(16.0f/116.0f)), _mm_set1_ps(1.0f/7.787f));
        __m128 v_fxz001 = _mm_mul_ps(_mm_sub_ps(v_fxz01, _mm_set1_ps(16.0f/116.0f)), _mm_set1_ps(1.0f/7.787f));
        __m128 v_fxz010 = _mm_mul_ps(_mm_sub_ps(v_fxz10, _mm_set1_ps(16.0f/116.0f)), _mm_set1_ps(1.0f/7.787f));
        __m128 v_fxz011 = _mm_mul_ps(_mm_sub_ps(v_fxz11, _mm_set1_ps(16.0f/116.0f)), _mm_set1_ps(1.0f/7.787f));

        __m128 v_fxz100 = _mm_mul_ps(_mm_mul_ps(v_fxz00, v_fxz00), v_fxz00);
        __m128 v_fxz101 = _mm_mul_ps(_mm_mul_ps(v_fxz01, v_fxz01), v_fxz01);
        __m128 v_fxz110 = _mm_mul_ps(_mm_mul_ps(v_fxz10, v_fxz10), v_fxz10);
        __m128 v_fxz111 = _mm_mul_ps(_mm_mul_ps(v_fxz11, v_fxz11), v_fxz11);

        __m128 v_cmpfxz00 = _mm_cmple_ps(v_fxz00, _mm_set1_ps(fThresh));
        __m128 v_cmpfxz01 = _mm_cmple_ps(v_fxz01, _mm_set1_ps(fThresh));
        __m128 v_cmpfxz10 = _mm_cmple_ps(v_fxz10, _mm_set1_ps(fThresh));
        __m128 v_cmpfxz11 = _mm_cmple_ps(v_fxz11, _mm_set1_ps(fThresh));
        v_fxz000 = _mm_and_ps(v_cmpfxz00, v_fxz000);
        v_fxz001 = _mm_and_ps(v_cmpfxz01, v_fxz001);
        v_fxz010 = _mm_and_ps(v_cmpfxz10, v_fxz010);
        v_fxz011 = _mm_and_ps(v_cmpfxz11, v_fxz011);
        v_fxz100 = _mm_andnot_ps(v_cmpfxz00, v_fxz100);
        v_fxz101 = _mm_andnot_ps(v_cmpfxz01, v_fxz101);
        v_fxz110 = _mm_andnot_ps(v_cmpfxz10, v_fxz110);
        v_fxz111 = _mm_andnot_ps(v_cmpfxz11, v_fxz111);
        __m128 v_x0 = _mm_or_ps(v_fxz000, v_fxz100);
        __m128 v_x1 = _mm_or_ps(v_fxz001, v_fxz101);
        __m128 v_z0 = _mm_or_ps(v_fxz010, v_fxz110);
        __m128 v_z1 = _mm_or_ps(v_fxz011, v_fxz111);

        __m128 v_ro0 = _mm_mul_ps(_mm_set1_ps(coeffs[0]), v_x0);
        __m128 v_ro1 = _mm_mul_ps(_mm_set1_ps(coeffs[0]), v_x1);
        __m128 v_go0 = _mm_mul_ps(_mm_set1_ps(coeffs[3]), v_x0);
        __m128 v_go1 = _mm_mul_ps(_mm_set1_ps(coeffs[3]), v_x1);
        __m128 v_bo0 = _mm_mul_ps(_mm_set1_ps(coeffs[6]), v_x0);
        __m128 v_bo1 = _mm_mul_ps(_mm_set1_ps(coeffs[6]), v_x1);
        v_ro0 = _mm_add_ps(v_ro0, _mm_mul_ps(_mm_set1_ps(coeffs[1]), v_y0));
        v_ro1 = _mm_add_ps(v_ro1, _mm_mul_ps(_mm_set1_ps(coeffs[1]), v_y1));
        v_go0 = _mm_add_ps(v_go0, _mm_mul_ps(_mm_set1_ps(coeffs[4]), v_y0));
        v_go1 = _mm_add_ps(v_go1, _mm_mul_ps(_mm_set1_ps(coeffs[4]), v_y1));
        v_bo0 = _mm_add_ps(v_bo0, _mm_mul_ps(_mm_set1_ps(coeffs[7]), v_y0));
        v_bo1 = _mm_add_ps(v_bo1, _mm_mul_ps(_mm_set1_ps(coeffs[7]), v_y1));
        v_ro0 = _mm_add_ps(v_ro0, _mm_mul_ps(_mm_set1_ps(coeffs[2]), v_z0));
        v_ro1 = _mm_add_ps(v_ro1, _mm_mul_ps(_mm_set1_ps(coeffs[2]), v_z1));
        v_go0 = _mm_add_ps(v_go0, _mm_mul_ps(_mm_set1_ps(coeffs[5]), v_z0));
        v_go1 = _mm_add_ps(v_go1, _mm_mul_ps(_mm_set1_ps(coeffs[5]), v_z1));
        v_bo0 = _mm_add_ps(v_bo0, _mm_mul_ps(_mm_set1_ps(coeffs[8]), v_z0));
        v_bo1 = _mm_add_ps(v_bo1, _mm_mul_ps(_mm_set1_ps(coeffs[8]), v_z1));

        v_li0 = _mm_min_ps(_mm_max_ps(v_ro0, _mm_setzero_ps()), _mm_set1_ps(1.0f));
        v_li1 = _mm_min_ps(_mm_max_ps(v_ro1, _mm_setzero_ps()), _mm_set1_ps(1.0f));
        v_ai0 = _mm_min_ps(_mm_max_ps(v_go0, _mm_setzero_ps()), _mm_set1_ps(1.0f));
        v_ai1 = _mm_min_ps(_mm_max_ps(v_go1, _mm_setzero_ps()), _mm_set1_ps(1.0f));
        v_bi0 = _mm_min_ps(_mm_max_ps(v_bo0, _mm_setzero_ps()), _mm_set1_ps(1.0f));
        v_bi1 = _mm_min_ps(_mm_max_ps(v_bo1, _mm_setzero_ps()), _mm_set1_ps(1.0f));
    }
    #endif

    void operator()(const float* src, float* dst, int n) const
    {
        int i = 0, dcn = dstcn;
        const float* gammaTab = srgb ? sRGBInvGammaTab : 0;
        float gscale = GammaTabScale;
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
        C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
        C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        float alpha = ColorChannel<float>::max();
        n *= 3;

        #if CV_SSE2
        if (haveSIMD)
        {
            for (; i <= n - 24; i += 24, dst += dcn * 8)
            {
                __m128 v_li0 = _mm_loadu_ps(src + i +  0);
                __m128 v_li1 = _mm_loadu_ps(src + i +  4);
                __m128 v_ai0 = _mm_loadu_ps(src + i +  8);
                __m128 v_ai1 = _mm_loadu_ps(src + i + 12);
                __m128 v_bi0 = _mm_loadu_ps(src + i + 16);
                __m128 v_bi1 = _mm_loadu_ps(src + i + 20);

                _mm_deinterleave_ps(v_li0, v_li1, v_ai0, v_ai1, v_bi0, v_bi1);

                process(v_li0, v_li1, v_ai0, v_ai1, v_bi0, v_bi1);

                if (gammaTab)
                {
                    __m128 v_gscale = _mm_set1_ps(gscale);
                    v_li0 = _mm_mul_ps(v_li0, v_gscale);
                    v_li1 = _mm_mul_ps(v_li1, v_gscale);
                    v_ai0 = _mm_mul_ps(v_ai0, v_gscale);
                    v_ai1 = _mm_mul_ps(v_ai1, v_gscale);
                    v_bi0 = _mm_mul_ps(v_bi0, v_gscale);
                    v_bi1 = _mm_mul_ps(v_bi1, v_gscale);

                    splineInterpolate(v_li0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_li1, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_ai0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_ai1, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_bi0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_bi1, gammaTab, GAMMA_TAB_SIZE);
                }

                if( dcn == 4 )
                {
                    __m128 v_a0 = _mm_set1_ps(alpha);
                    __m128 v_a1 = _mm_set1_ps(alpha);
                    _mm_interleave_ps(v_li0, v_li1, v_ai0, v_ai1, v_bi0, v_bi1, v_a0, v_a1);

                    _mm_storeu_ps(dst +  0, v_li0);
                    _mm_storeu_ps(dst +  4, v_li1);
                    _mm_storeu_ps(dst +  8, v_ai0);
                    _mm_storeu_ps(dst + 12, v_ai1);
                    _mm_storeu_ps(dst + 16, v_bi0);
                    _mm_storeu_ps(dst + 20, v_bi1);
                    _mm_storeu_ps(dst + 24, v_a0);
                    _mm_storeu_ps(dst + 28, v_a1);
                }
                else
                {
                    _mm_interleave_ps(v_li0, v_li1, v_ai0, v_ai1, v_bi0, v_bi1);

                    _mm_storeu_ps(dst +  0, v_li0);
                    _mm_storeu_ps(dst +  4, v_li1);
                    _mm_storeu_ps(dst +  8, v_ai0);
                    _mm_storeu_ps(dst + 12, v_ai1);
                    _mm_storeu_ps(dst + 16, v_bi0);
                    _mm_storeu_ps(dst + 20, v_bi1);
                }
            }
        }
        #endif
        for (; i < n; i += 3, dst += dcn)
        {
            float li = src[i];
            float ai = src[i + 1];
            float bi = src[i + 2];

            float y, fy;
            if (li <= lThresh)
            {
                y = li / 903.3f;
                fy = 7.787f * y + 16.0f / 116.0f;
            }
            else
            {
                fy = (li + 16.0f) / 116.0f;
                y = fy * fy * fy;
            }

            float fxz[] = { ai / 500.0f + fy, fy - bi / 200.0f };

            for (int j = 0; j < 2; j++)
                if (fxz[j] <= fThresh)
                    fxz[j] = (fxz[j] - 16.0f / 116.0f) / 7.787f;
                else
                    fxz[j] = fxz[j] * fxz[j] * fxz[j];


            float x = fxz[0], z = fxz[1];
            float ro = C0 * x + C1 * y + C2 * z;
            float go = C3 * x + C4 * y + C5 * z;
            float bo = C6 * x + C7 * y + C8 * z;
            ro = clip(ro);
            go = clip(go);
            bo = clip(bo);

            if (gammaTab)
            {
                ro = splineInterpolate(ro * gscale, gammaTab, GAMMA_TAB_SIZE);
                go = splineInterpolate(go * gscale, gammaTab, GAMMA_TAB_SIZE);
                bo = splineInterpolate(bo * gscale, gammaTab, GAMMA_TAB_SIZE);
            }

            dst[0] = ro, dst[1] = go, dst[2] = bo;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }

    int dstcn;
    float coeffs[9];
    bool srgb;
    float lThresh;
    float fThresh;
    #if CV_SSE2
    bool haveSIMD;
    #endif
};

#undef clip

struct Lab2RGB_b
{
    typedef uchar channel_type;

    Lab2RGB_b( int _dstcn, int blueIdx, const float* _coeffs,
               const float* _whitept, bool _srgb )
    : dstcn(_dstcn), cvt(3, blueIdx, _coeffs, _whitept, _srgb )
    {
        #if CV_NEON
        v_scale_inv = vdupq_n_f32(100.f/255.f);
        v_scale = vdupq_n_f32(255.f);
        v_alpha = vdup_n_u8(ColorChannel<uchar>::max());
        v_128 = vdupq_n_f32(128.0f);
        #elif CV_SSE2
        v_scale = _mm_set1_ps(255.f);
        v_alpha = _mm_set1_ps(ColorChannel<uchar>::max());
        v_zero = _mm_setzero_si128();
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    // 16s x 8
    void process(__m128i v_r, __m128i v_g, __m128i v_b,
                 const __m128& v_coeffs_, const __m128& v_res_,
                 float * buf) const
    {
        __m128 v_r0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_r, v_zero));
        __m128 v_g0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_g, v_zero));
        __m128 v_b0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_b, v_zero));

        __m128 v_r1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_r, v_zero));
        __m128 v_g1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_g, v_zero));
        __m128 v_b1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_b, v_zero));

        __m128 v_coeffs = v_coeffs_;
        __m128 v_res = v_res_;

        v_r0 = _mm_sub_ps(_mm_mul_ps(v_r0, v_coeffs), v_res);
        v_g1 = _mm_sub_ps(_mm_mul_ps(v_g1, v_coeffs), v_res);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));
        v_res = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_res), 0x49));

        v_r1 = _mm_sub_ps(_mm_mul_ps(v_r1, v_coeffs), v_res);
        v_b0 = _mm_sub_ps(_mm_mul_ps(v_b0, v_coeffs), v_res);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));
        v_res = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_res), 0x49));

        v_g0 = _mm_sub_ps(_mm_mul_ps(v_g0, v_coeffs), v_res);
        v_b1 = _mm_sub_ps(_mm_mul_ps(v_b1, v_coeffs), v_res);

        _mm_store_ps(buf, v_r0);
        _mm_store_ps(buf + 4, v_r1);
        _mm_store_ps(buf + 8, v_g0);
        _mm_store_ps(buf + 12, v_g1);
        _mm_store_ps(buf + 16, v_b0);
        _mm_store_ps(buf + 20, v_b1);
    }
    #endif

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i, j, dcn = dstcn;
        uchar alpha = ColorChannel<uchar>::max();
        float CV_DECL_ALIGNED(16) buf[3*BLOCK_SIZE];
        #if CV_SSE2
        __m128 v_coeffs = _mm_set_ps(100.f/255.f, 1.f, 1.f, 100.f/255.f);
        __m128 v_res = _mm_set_ps(0.f, 128.f, 128.f, 0.f);
        #endif

        for( i = 0; i < n; i += BLOCK_SIZE, src += BLOCK_SIZE*3 )
        {
            int dn = std::min(n - i, (int)BLOCK_SIZE);
            j = 0;

            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24)
            {
                uint8x8x3_t v_src = vld3_u8(src + j);
                uint16x8_t v_t0 = vmovl_u8(v_src.val[0]),
                           v_t1 = vmovl_u8(v_src.val[1]),
                           v_t2 = vmovl_u8(v_src.val[2]);

                float32x4x3_t v_dst;
                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vsubq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t1))), v_128);
                v_dst.val[2] = vsubq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t2))), v_128);
                vst3q_f32(buf + j, v_dst);

                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vsubq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t1))), v_128);
                v_dst.val[2] = vsubq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t2))), v_128);
                vst3q_f32(buf + j + 12, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; j <= (dn - 8) * 3; j += 24)
                {
                    __m128i v_src0 = _mm_loadu_si128((__m128i const *)(src + j));
                    __m128i v_src1 = _mm_loadl_epi64((__m128i const *)(src + j + 16));

                    process(_mm_unpacklo_epi8(v_src0, v_zero),
                            _mm_unpackhi_epi8(v_src0, v_zero),
                            _mm_unpacklo_epi8(v_src1, v_zero),
                            v_coeffs, v_res,
                            buf + j);
                }
            }
            #endif

            for( ; j < dn*3; j += 3 )
            {
                buf[j] = src[j]*(100.f/255.f);
                buf[j+1] = (float)(src[j+1] - 128);
                buf[j+2] = (float)(src[j+2] - 128);
            }
            cvt(buf, buf, dn);
            j = 0;

            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24, dst += dcn * 8)
            {
                float32x4x3_t v_src0 = vld3q_f32(buf + j), v_src1 = vld3q_f32(buf + j + 12);
                uint8x8_t v_dst0 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[0], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[0], v_scale)))));
                uint8x8_t v_dst1 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[1], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[1], v_scale)))));
                uint8x8_t v_dst2 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[2], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[2], v_scale)))));

                if (dcn == 4)
                {
                    uint8x8x4_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    v_dst.val[3] = v_alpha;
                    vst4_u8(dst, v_dst);
                }
                else
                {
                    uint8x8x3_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    vst3_u8(dst, v_dst);
                }
            }
            #elif CV_SSE2
            if (dcn == 3 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 16); j += 16, dst += 16)
                {
                    __m128 v_src0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_src1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_src2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);
                    __m128 v_src3 = _mm_mul_ps(_mm_load_ps(buf + j + 12), v_scale);

                    __m128i v_dst0 = _mm_packs_epi32(_mm_cvtps_epi32(v_src0),
                                                     _mm_cvtps_epi32(v_src1));
                    __m128i v_dst1 = _mm_packs_epi32(_mm_cvtps_epi32(v_src2),
                                                     _mm_cvtps_epi32(v_src3));

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            else if (dcn == 4 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 12); j += 12, dst += 16)
                {
                    __m128 v_buf0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_buf1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_buf2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);

                    __m128 v_ba0 = _mm_unpackhi_ps(v_buf0, v_alpha);
                    __m128 v_ba1 = _mm_unpacklo_ps(v_buf2, v_alpha);

                    __m128i v_src0 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf0, v_ba0, 0x44));
                    __m128i v_src1 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba0, v_buf1, 0x4e)), 0x78);
                    __m128i v_src2 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf1, v_ba1, 0x4e));
                    __m128i v_src3 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba1, v_buf2, 0xee)), 0x78);

                    __m128i v_dst0 = _mm_packs_epi32(v_src0, v_src1);
                    __m128i v_dst1 = _mm_packs_epi32(v_src2, v_src3);

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            #endif

            for( ; j < dn*3; j += 3, dst += dcn )
            {
                dst[0] = saturate_cast<uchar>(buf[j]*255.f);
                dst[1] = saturate_cast<uchar>(buf[j+1]*255.f);
                dst[2] = saturate_cast<uchar>(buf[j+2]*255.f);
                if( dcn == 4 )
                    dst[3] = alpha;
            }
        }
    }

    int dstcn;
    Lab2RGB_f cvt;

    #if CV_NEON
    float32x4_t v_scale, v_scale_inv, v_128;
    uint8x8_t v_alpha;
    #elif CV_SSE2
    __m128 v_scale;
    __m128 v_alpha;
    __m128i v_zero;
    bool haveSIMD;
    #endif
};


///////////////////////////////////// RGB <-> L*u*v* /////////////////////////////////////

struct RGB2Luv_f
{
    typedef float channel_type;

    RGB2Luv_f( int _srccn, int blueIdx, const float* _coeffs,
               const float* whitept, bool _srgb )
    : srccn(_srccn), srgb(_srgb)
    {
        volatile int i;
        initLabTabs();

        if(!_coeffs) _coeffs = sRGB2XYZ_D65;
        if(!whitept) whitept = D65;

        for( i = 0; i < 3; i++ )
        {
            coeffs[i*3] = _coeffs[i*3];
            coeffs[i*3+1] = _coeffs[i*3+1];
            coeffs[i*3+2] = _coeffs[i*3+2];
            if( blueIdx == 0 )
                std::swap(coeffs[i*3], coeffs[i*3+2]);
            CV_Assert( coeffs[i*3] >= 0 && coeffs[i*3+1] >= 0 && coeffs[i*3+2] >= 0 &&
                      coeffs[i*3] + coeffs[i*3+1] + coeffs[i*3+2] < 1.5f );
        }

        float d = 1.f/(whitept[0] + whitept[1]*15 + whitept[2]*3);
        un = 4*whitept[0]*d*13;
        vn = 9*whitept[1]*d*13;

        #if CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif

        CV_Assert(whitept[1] == 1.f);
    }

    #if CV_NEON
    void process(float32x4x3_t& v_src) const
    {
        float32x4_t v_x = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], vdupq_n_f32(coeffs[0])), v_src.val[1], vdupq_n_f32(coeffs[1])), v_src.val[2], vdupq_n_f32(coeffs[2]));
        float32x4_t v_y = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], vdupq_n_f32(coeffs[3])), v_src.val[1], vdupq_n_f32(coeffs[4])), v_src.val[2], vdupq_n_f32(coeffs[5]));
        float32x4_t v_z = vmlaq_f32(vmlaq_f32(vmulq_f32(v_src.val[0], vdupq_n_f32(coeffs[6])), v_src.val[1], vdupq_n_f32(coeffs[7])), v_src.val[2], vdupq_n_f32(coeffs[8]));

        v_src.val[0] = vmulq_f32(v_y, vdupq_n_f32(LabCbrtTabScale));
        splineInterpolate(v_src.val[0], LabCbrtTab, LAB_CBRT_TAB_SIZE);

        v_src.val[0] = vmlaq_f32(vdupq_n_f32(-16.f), v_src.val[0], vdupq_n_f32(116.f));

        float32x4_t v_div = vmaxq_f32(vmlaq_f32(vmlaq_f32(v_x, vdupq_n_f32(15.f), v_y), vdupq_n_f32(3.f), v_z), vdupq_n_f32(FLT_EPSILON));
        float32x4_t v_reciprocal = vrecpeq_f32(v_div);
        v_reciprocal = vmulq_f32(vrecpsq_f32(v_div, v_reciprocal), v_reciprocal);
        v_reciprocal = vmulq_f32(vrecpsq_f32(v_div, v_reciprocal), v_reciprocal);
        float32x4_t v_d = vmulq_f32(vdupq_n_f32(52.f), v_reciprocal);

        v_src.val[1] = vmulq_f32(v_src.val[0], vmlaq_f32(vdupq_n_f32(-un), v_x, v_d));
        v_src.val[2] = vmulq_f32(v_src.val[0], vmlaq_f32(vdupq_n_f32(-vn), vmulq_f32(vdupq_n_f32(2.25f), v_y), v_d));
    }
    #elif CV_SSE2
    void process(__m128& v_r0, __m128& v_r1, __m128& v_g0,
                 __m128& v_g1, __m128& v_b0, __m128& v_b1) const
    {
        __m128 v_x0 = _mm_mul_ps(v_r0, _mm_set1_ps(coeffs[0]));
        __m128 v_x1 = _mm_mul_ps(v_r1, _mm_set1_ps(coeffs[0]));
        __m128 v_y0 = _mm_mul_ps(v_r0, _mm_set1_ps(coeffs[3]));
        __m128 v_y1 = _mm_mul_ps(v_r1, _mm_set1_ps(coeffs[3]));
        __m128 v_z0 = _mm_mul_ps(v_r0, _mm_set1_ps(coeffs[6]));
        __m128 v_z1 = _mm_mul_ps(v_r1, _mm_set1_ps(coeffs[6]));

        v_x0 = _mm_add_ps(v_x0, _mm_mul_ps(v_g0, _mm_set1_ps(coeffs[1])));
        v_x1 = _mm_add_ps(v_x1, _mm_mul_ps(v_g1, _mm_set1_ps(coeffs[1])));
        v_y0 = _mm_add_ps(v_y0, _mm_mul_ps(v_g0, _mm_set1_ps(coeffs[4])));
        v_y1 = _mm_add_ps(v_y1, _mm_mul_ps(v_g1, _mm_set1_ps(coeffs[4])));
        v_z0 = _mm_add_ps(v_z0, _mm_mul_ps(v_g0, _mm_set1_ps(coeffs[7])));
        v_z1 = _mm_add_ps(v_z1, _mm_mul_ps(v_g1, _mm_set1_ps(coeffs[7])));

        v_x0 = _mm_add_ps(v_x0, _mm_mul_ps(v_b0, _mm_set1_ps(coeffs[2])));
        v_x1 = _mm_add_ps(v_x1, _mm_mul_ps(v_b1, _mm_set1_ps(coeffs[2])));
        v_y0 = _mm_add_ps(v_y0, _mm_mul_ps(v_b0, _mm_set1_ps(coeffs[5])));
        v_y1 = _mm_add_ps(v_y1, _mm_mul_ps(v_b1, _mm_set1_ps(coeffs[5])));
        v_z0 = _mm_add_ps(v_z0, _mm_mul_ps(v_b0, _mm_set1_ps(coeffs[8])));
        v_z1 = _mm_add_ps(v_z1, _mm_mul_ps(v_b1, _mm_set1_ps(coeffs[8])));

        __m128 v_l0 = _mm_mul_ps(v_y0, _mm_set1_ps(LabCbrtTabScale));
        __m128 v_l1 = _mm_mul_ps(v_y1, _mm_set1_ps(LabCbrtTabScale));
        splineInterpolate(v_l0, LabCbrtTab, LAB_CBRT_TAB_SIZE);
        splineInterpolate(v_l1, LabCbrtTab, LAB_CBRT_TAB_SIZE);

        v_l0 = _mm_mul_ps(v_l0, _mm_set1_ps(116.0f));
        v_l1 = _mm_mul_ps(v_l1, _mm_set1_ps(116.0f));
        v_r0 = _mm_sub_ps(v_l0, _mm_set1_ps(16.0f));
        v_r1 = _mm_sub_ps(v_l1, _mm_set1_ps(16.0f));

        v_z0 = _mm_mul_ps(v_z0, _mm_set1_ps(3.0f));
        v_z1 = _mm_mul_ps(v_z1, _mm_set1_ps(3.0f));
        v_z0 = _mm_add_ps(v_z0, v_x0);
        v_z1 = _mm_add_ps(v_z1, v_x1);
        v_z0 = _mm_add_ps(v_z0, _mm_mul_ps(v_y0, _mm_set1_ps(15.0f)));
        v_z1 = _mm_add_ps(v_z1, _mm_mul_ps(v_y1, _mm_set1_ps(15.0f)));
        v_z0 = _mm_max_ps(v_z0, _mm_set1_ps(FLT_EPSILON));
        v_z1 = _mm_max_ps(v_z1, _mm_set1_ps(FLT_EPSILON));
        __m128 v_d0 = _mm_div_ps(_mm_set1_ps(52.0f), v_z0);
        __m128 v_d1 = _mm_div_ps(_mm_set1_ps(52.0f), v_z1);

        v_x0 = _mm_mul_ps(v_x0, v_d0);
        v_x1 = _mm_mul_ps(v_x1, v_d1);
        v_x0 = _mm_sub_ps(v_x0, _mm_set1_ps(un));
        v_x1 = _mm_sub_ps(v_x1, _mm_set1_ps(un));
        v_g0 = _mm_mul_ps(v_x0, v_r0);
        v_g1 = _mm_mul_ps(v_x1, v_r1);

        v_y0 = _mm_mul_ps(v_y0, v_d0);
        v_y1 = _mm_mul_ps(v_y1, v_d1);
        v_y0 = _mm_mul_ps(v_y0, _mm_set1_ps(2.25f));
        v_y1 = _mm_mul_ps(v_y1, _mm_set1_ps(2.25f));
        v_y0 = _mm_sub_ps(v_y0, _mm_set1_ps(vn));
        v_y1 = _mm_sub_ps(v_y1, _mm_set1_ps(vn));
        v_b0 = _mm_mul_ps(v_y0, v_r0);
        v_b1 = _mm_mul_ps(v_y1, v_r1);
    }
    #endif

    void operator()(const float* src, float* dst, int n) const
    {
        int i = 0, scn = srccn;
        float gscale = GammaTabScale;
        const float* gammaTab = srgb ? sRGBGammaTab : 0;
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        n *= 3;

        #if CV_NEON
        if (scn == 3)
        {
            for( ; i <= n - 12; i += 12, src += scn * 4 )
            {
                float32x4x3_t v_src = vld3q_f32(src);
                if( gammaTab )
                {
                    v_src.val[0] = vmulq_f32(v_src.val[0], vdupq_n_f32(gscale));
                    v_src.val[1] = vmulq_f32(v_src.val[1], vdupq_n_f32(gscale));
                    v_src.val[2] = vmulq_f32(v_src.val[2], vdupq_n_f32(gscale));
                    splineInterpolate(v_src.val[0], gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_src.val[1], gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_src.val[2], gammaTab, GAMMA_TAB_SIZE);
                }

                process(v_src);

                vst3q_f32(dst + i, v_src);
            }
        }
        else
        {
            for( ; i <= n - 12; i += 12, src += scn * 4 )
            {
                float32x4x4_t v_src = vld4q_f32(src);
                if( gammaTab )
                {
                    v_src.val[0] = vmulq_f32(v_src.val[0], vdupq_n_f32(gscale));
                    v_src.val[1] = vmulq_f32(v_src.val[1], vdupq_n_f32(gscale));
                    v_src.val[2] = vmulq_f32(v_src.val[2], vdupq_n_f32(gscale));
                    splineInterpolate(v_src.val[0], gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_src.val[1], gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_src.val[2], gammaTab, GAMMA_TAB_SIZE);
                }

                float32x4x3_t v_dst;
                v_dst.val[0] = v_src.val[0];
                v_dst.val[1] = v_src.val[1];
                v_dst.val[2] = v_src.val[2];
                process(v_dst);

                vst3q_f32(dst + i, v_dst);
            }
        }
        #elif CV_SSE2
        if (haveSIMD)
        {
            for( ; i <= n - 24; i += 24, src += scn * 8 )
            {
                __m128 v_r0 = _mm_loadu_ps(src +  0);
                __m128 v_r1 = _mm_loadu_ps(src +  4);
                __m128 v_g0 = _mm_loadu_ps(src +  8);
                __m128 v_g1 = _mm_loadu_ps(src + 12);
                __m128 v_b0 = _mm_loadu_ps(src + 16);
                __m128 v_b1 = _mm_loadu_ps(src + 20);

                if (scn == 3)
                {
                    _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);
                }
                else
                {
                    __m128 v_a0 = _mm_loadu_ps(src + 24);
                    __m128 v_a1 = _mm_loadu_ps(src + 28);

                    _mm_deinterleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1, v_a0, v_a1);
                }

                if ( gammaTab )
                {
                    __m128 v_gscale = _mm_set1_ps(gscale);
                    v_r0 = _mm_mul_ps(v_r0, v_gscale);
                    v_r1 = _mm_mul_ps(v_r1, v_gscale);
                    v_g0 = _mm_mul_ps(v_g0, v_gscale);
                    v_g1 = _mm_mul_ps(v_g1, v_gscale);
                    v_b0 = _mm_mul_ps(v_b0, v_gscale);
                    v_b1 = _mm_mul_ps(v_b1, v_gscale);

                    splineInterpolate(v_r0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_r1, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_g0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_g1, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_b0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_b1, gammaTab, GAMMA_TAB_SIZE);
                }

                process(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);

                _mm_interleave_ps(v_r0, v_r1, v_g0, v_g1, v_b0, v_b1);

                _mm_storeu_ps(dst + i +  0, v_r0);
                _mm_storeu_ps(dst + i +  4, v_r1);
                _mm_storeu_ps(dst + i +  8, v_g0);
                _mm_storeu_ps(dst + i + 12, v_g1);
                _mm_storeu_ps(dst + i + 16, v_b0);
                _mm_storeu_ps(dst + i + 20, v_b1);
            }
        }
        #endif
        for( ; i < n; i += 3, src += scn )
        {
            float R = src[0], G = src[1], B = src[2];
            if( gammaTab )
            {
                R = splineInterpolate(R*gscale, gammaTab, GAMMA_TAB_SIZE);
                G = splineInterpolate(G*gscale, gammaTab, GAMMA_TAB_SIZE);
                B = splineInterpolate(B*gscale, gammaTab, GAMMA_TAB_SIZE);
            }

            float X = R*C0 + G*C1 + B*C2;
            float Y = R*C3 + G*C4 + B*C5;
            float Z = R*C6 + G*C7 + B*C8;

            float L = splineInterpolate(Y*LabCbrtTabScale, LabCbrtTab, LAB_CBRT_TAB_SIZE);
            L = 116.f*L - 16.f;

            float d = (4*13) / std::max(X + 15 * Y + 3 * Z, FLT_EPSILON);
            float u = L*(X*d - un);
            float v = L*((9*0.25f)*Y*d - vn);

            dst[i] = L; dst[i+1] = u; dst[i+2] = v;
        }
    }

    int srccn;
    float coeffs[9], un, vn;
    bool srgb;
    #if CV_SSE2
    bool haveSIMD;
    #endif
};


struct Luv2RGB_f
{
    typedef float channel_type;

    Luv2RGB_f( int _dstcn, int blueIdx, const float* _coeffs,
              const float* whitept, bool _srgb )
    : dstcn(_dstcn), srgb(_srgb)
    {
        initLabTabs();

        if(!_coeffs) _coeffs = XYZ2sRGB_D65;
        if(!whitept) whitept = D65;

        for( int i = 0; i < 3; i++ )
        {
            coeffs[i+(blueIdx^2)*3] = _coeffs[i];
            coeffs[i+3] = _coeffs[i+3];
            coeffs[i+blueIdx*3] = _coeffs[i+6];
        }

        float d = 1.f/(whitept[0] + whitept[1]*15 + whitept[2]*3);
        un = 4*whitept[0]*d;
        vn = 9*whitept[1]*d;
        #if CV_SSE2
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif

        CV_Assert(whitept[1] == 1.f);
    }

    #if CV_SSE2
    void process(__m128& v_l0, __m128& v_l1, __m128& v_u0,
                 __m128& v_u1, __m128& v_v0, __m128& v_v1) const
    {
        __m128 v_y0 = _mm_mul_ps(_mm_add_ps(v_l0, _mm_set1_ps(16.0f)), _mm_set1_ps(1.f/116.f));
        __m128 v_y1 = _mm_mul_ps(_mm_add_ps(v_l1, _mm_set1_ps(16.0f)), _mm_set1_ps(1.f/116.f));
        v_y0 = _mm_mul_ps(_mm_mul_ps(v_y0, v_y0), v_y0);
        v_y1 = _mm_mul_ps(_mm_mul_ps(v_y1, v_y1), v_y1);
        __m128 v_d0 = _mm_div_ps(_mm_set1_ps(1.f/13.f), v_l0);
        __m128 v_d1 = _mm_div_ps(_mm_set1_ps(1.f/13.f), v_l1);
        v_u0 = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(v_u0, v_d0), _mm_set1_ps(un)), _mm_set1_ps(3.f));
        v_u1 = _mm_mul_ps(_mm_add_ps(_mm_mul_ps(v_u1, v_d1), _mm_set1_ps(un)), _mm_set1_ps(3.f));
        v_v0 = _mm_add_ps(_mm_mul_ps(v_v0, v_d0), _mm_set1_ps(vn));
        v_v1 = _mm_add_ps(_mm_mul_ps(v_v1, v_d1), _mm_set1_ps(vn));
        __m128 v_iv0 = _mm_div_ps(_mm_set1_ps(0.25f), v_v0);
        __m128 v_iv1 = _mm_div_ps(_mm_set1_ps(0.25f), v_v1);
        __m128 v_x0 = _mm_mul_ps(_mm_mul_ps(_mm_set1_ps(3.f), v_u0), v_iv0);
        __m128 v_x1 = _mm_mul_ps(_mm_mul_ps(_mm_set1_ps(3.f), v_u1), v_iv1);
        __m128 v_z0 = _mm_mul_ps(_mm_sub_ps(_mm_sub_ps(_mm_set1_ps(12.f), v_u0), _mm_mul_ps(_mm_set1_ps(20.f), v_v0)), v_iv0);
        __m128 v_z1 = _mm_mul_ps(_mm_sub_ps(_mm_sub_ps(_mm_set1_ps(12.f), v_u1), _mm_mul_ps(_mm_set1_ps(20.f), v_v1)), v_iv1);

        v_l0 = _mm_mul_ps(v_x0, _mm_set1_ps(coeffs[0]));
        v_l1 = _mm_mul_ps(v_x1, _mm_set1_ps(coeffs[0]));
        v_u0 = _mm_mul_ps(v_x0, _mm_set1_ps(coeffs[3]));
        v_u1 = _mm_mul_ps(v_x1, _mm_set1_ps(coeffs[3]));
        v_v0 = _mm_mul_ps(v_x0, _mm_set1_ps(coeffs[6]));
        v_v1 = _mm_mul_ps(v_x1, _mm_set1_ps(coeffs[6]));
        v_l0 = _mm_add_ps(v_l0, _mm_set1_ps(coeffs[1]));
        v_l1 = _mm_add_ps(v_l1, _mm_set1_ps(coeffs[1]));
        v_u0 = _mm_add_ps(v_u0, _mm_set1_ps(coeffs[4]));
        v_u1 = _mm_add_ps(v_u1, _mm_set1_ps(coeffs[4]));
        v_v0 = _mm_add_ps(v_v0, _mm_set1_ps(coeffs[7]));
        v_v1 = _mm_add_ps(v_v1, _mm_set1_ps(coeffs[7]));
        v_l0 = _mm_add_ps(v_l0, _mm_mul_ps(v_z0, _mm_set1_ps(coeffs[2])));
        v_l1 = _mm_add_ps(v_l1, _mm_mul_ps(v_z1, _mm_set1_ps(coeffs[2])));
        v_u0 = _mm_add_ps(v_u0, _mm_mul_ps(v_z0, _mm_set1_ps(coeffs[5])));
        v_u1 = _mm_add_ps(v_u1, _mm_mul_ps(v_z1, _mm_set1_ps(coeffs[5])));
        v_v0 = _mm_add_ps(v_v0, _mm_mul_ps(v_z0, _mm_set1_ps(coeffs[8])));
        v_v1 = _mm_add_ps(v_v1, _mm_mul_ps(v_z1, _mm_set1_ps(coeffs[8])));
        v_l0 = _mm_mul_ps(v_l0, v_y0);
        v_l1 = _mm_mul_ps(v_l1, v_y1);
        v_u0 = _mm_mul_ps(v_u0, v_y0);
        v_u1 = _mm_mul_ps(v_u1, v_y1);
        v_v0 = _mm_mul_ps(v_v0, v_y0);
        v_v1 = _mm_mul_ps(v_v1, v_y1);

        v_l0 = _mm_max_ps(v_l0, _mm_setzero_ps());
        v_l1 = _mm_max_ps(v_l1, _mm_setzero_ps());
        v_u0 = _mm_max_ps(v_u0, _mm_setzero_ps());
        v_u1 = _mm_max_ps(v_u1, _mm_setzero_ps());
        v_v0 = _mm_max_ps(v_v0, _mm_setzero_ps());
        v_v1 = _mm_max_ps(v_v1, _mm_setzero_ps());
        v_l0 = _mm_min_ps(v_l0, _mm_set1_ps(1.f));
        v_l1 = _mm_min_ps(v_l1, _mm_set1_ps(1.f));
        v_u0 = _mm_min_ps(v_u0, _mm_set1_ps(1.f));
        v_u1 = _mm_min_ps(v_u1, _mm_set1_ps(1.f));
        v_v0 = _mm_min_ps(v_v0, _mm_set1_ps(1.f));
        v_v1 = _mm_min_ps(v_v1, _mm_set1_ps(1.f));
    }
    #endif

    void operator()(const float* src, float* dst, int n) const
    {
        int i = 0, dcn = dstcn;
        const float* gammaTab = srgb ? sRGBInvGammaTab : 0;
        float gscale = GammaTabScale;
        float C0 = coeffs[0], C1 = coeffs[1], C2 = coeffs[2],
              C3 = coeffs[3], C4 = coeffs[4], C5 = coeffs[5],
              C6 = coeffs[6], C7 = coeffs[7], C8 = coeffs[8];
        float alpha = ColorChannel<float>::max();
        float _un = un, _vn = vn;
        n *= 3;

        #if CV_SSE2
        if (haveSIMD)
        {
            for( ; i <= n - 24; i += 24, dst += dcn * 8 )
            {
                __m128 v_l0 = _mm_loadu_ps(src + i +  0);
                __m128 v_l1 = _mm_loadu_ps(src + i +  4);
                __m128 v_u0 = _mm_loadu_ps(src + i +  8);
                __m128 v_u1 = _mm_loadu_ps(src + i + 12);
                __m128 v_v0 = _mm_loadu_ps(src + i + 16);
                __m128 v_v1 = _mm_loadu_ps(src + i + 20);

                _mm_deinterleave_ps(v_l0, v_l1, v_u0, v_u1, v_v0, v_v1);

                process(v_l0, v_l1, v_u0, v_u1, v_v0, v_v1);

                if( gammaTab )
                {
                    __m128 v_gscale = _mm_set1_ps(gscale);
                    v_l0 = _mm_mul_ps(v_l0, v_gscale);
                    v_l1 = _mm_mul_ps(v_l1, v_gscale);
                    v_u0 = _mm_mul_ps(v_u0, v_gscale);
                    v_u1 = _mm_mul_ps(v_u1, v_gscale);
                    v_v0 = _mm_mul_ps(v_v0, v_gscale);
                    v_v1 = _mm_mul_ps(v_v1, v_gscale);
                    splineInterpolate(v_l0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_l1, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_u0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_u1, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_v0, gammaTab, GAMMA_TAB_SIZE);
                    splineInterpolate(v_v1, gammaTab, GAMMA_TAB_SIZE);
                }

                if( dcn == 4 )
                {
                    __m128 v_a0 = _mm_set1_ps(alpha);
                    __m128 v_a1 = _mm_set1_ps(alpha);
                    _mm_interleave_ps(v_l0, v_l1, v_u0, v_u1, v_v0, v_v1, v_a0, v_a1);

                    _mm_storeu_ps(dst +  0, v_l0);
                    _mm_storeu_ps(dst +  4, v_l1);
                    _mm_storeu_ps(dst +  8, v_u0);
                    _mm_storeu_ps(dst + 12, v_u1);
                    _mm_storeu_ps(dst + 16, v_v0);
                    _mm_storeu_ps(dst + 20, v_v1);
                    _mm_storeu_ps(dst + 24, v_a0);
                    _mm_storeu_ps(dst + 28, v_a1);
                }
                else
                {
                    _mm_interleave_ps(v_l0, v_l1, v_u0, v_u1, v_v0, v_v1);

                    _mm_storeu_ps(dst +  0, v_l0);
                    _mm_storeu_ps(dst +  4, v_l1);
                    _mm_storeu_ps(dst +  8, v_u0);
                    _mm_storeu_ps(dst + 12, v_u1);
                    _mm_storeu_ps(dst + 16, v_v0);
                    _mm_storeu_ps(dst + 20, v_v1);
                }
            }
        }
        #endif
        for( ; i < n; i += 3, dst += dcn )
        {
            float L = src[i], u = src[i+1], v = src[i+2], d, X, Y, Z;
            Y = (L + 16.f) * (1.f/116.f);
            Y = Y*Y*Y;
            d = (1.f/13.f)/L;
            u = u*d + _un;
            v = v*d + _vn;
            float iv = 1.f/v;
            X = 2.25f * u * Y * iv ;
            Z = (12 - 3 * u - 20 * v) * Y * 0.25f * iv;

            float R = X*C0 + Y*C1 + Z*C2;
            float G = X*C3 + Y*C4 + Z*C5;
            float B = X*C6 + Y*C7 + Z*C8;

            R = std::min(std::max(R, 0.f), 1.f);
            G = std::min(std::max(G, 0.f), 1.f);
            B = std::min(std::max(B, 0.f), 1.f);

            if( gammaTab )
            {
                R = splineInterpolate(R*gscale, gammaTab, GAMMA_TAB_SIZE);
                G = splineInterpolate(G*gscale, gammaTab, GAMMA_TAB_SIZE);
                B = splineInterpolate(B*gscale, gammaTab, GAMMA_TAB_SIZE);
            }

            dst[0] = R; dst[1] = G; dst[2] = B;
            if( dcn == 4 )
                dst[3] = alpha;
        }
    }

    int dstcn;
    float coeffs[9], un, vn;
    bool srgb;
    #if CV_SSE2
    bool haveSIMD;
    #endif
};


struct RGB2Luv_b
{
    typedef uchar channel_type;

    RGB2Luv_b( int _srccn, int blueIdx, const float* _coeffs,
               const float* _whitept, bool _srgb )
    : srccn(_srccn), cvt(3, blueIdx, _coeffs, _whitept, _srgb)
    {
        #if CV_NEON
        v_scale_inv = vdupq_n_f32(1.f/255.f);
        v_scale = vdupq_n_f32(2.55f);
        v_coeff1 = vdupq_n_f32(0.72033898305084743f);
        v_coeff2 = vdupq_n_f32(96.525423728813564f);
        v_coeff3 = vdupq_n_f32(0.9732824427480916f);
        v_coeff4 = vdupq_n_f32(136.259541984732824f);
        v_alpha = vdup_n_u8(ColorChannel<uchar>::max());
        #elif CV_SSE2
        v_zero = _mm_setzero_si128();
        v_scale_inv = _mm_set1_ps(1.f/255.f);
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    void process(const float * buf,
                 __m128 & v_coeffs, __m128 & v_res, uchar * dst) const
    {
        __m128 v_l0f = _mm_load_ps(buf);
        __m128 v_l1f = _mm_load_ps(buf + 4);
        __m128 v_u0f = _mm_load_ps(buf + 8);
        __m128 v_u1f = _mm_load_ps(buf + 12);

        v_l0f = _mm_add_ps(_mm_mul_ps(v_l0f, v_coeffs), v_res);
        v_u1f = _mm_add_ps(_mm_mul_ps(v_u1f, v_coeffs), v_res);
        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x92));
        v_res = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_res), 0x92));
        v_u0f = _mm_add_ps(_mm_mul_ps(v_u0f, v_coeffs), v_res);
        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x92));
        v_res = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_res), 0x92));
        v_l1f = _mm_add_ps(_mm_mul_ps(v_l1f, v_coeffs), v_res);

        __m128i v_l = _mm_packs_epi32(_mm_cvtps_epi32(v_l0f), _mm_cvtps_epi32(v_l1f));
        __m128i v_u = _mm_packs_epi32(_mm_cvtps_epi32(v_u0f), _mm_cvtps_epi32(v_u1f));
        __m128i v_l0 = _mm_packus_epi16(v_l, v_u);

        _mm_storeu_si128((__m128i *)(dst), v_l0);
    }
    #endif

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i, j, scn = srccn;
        float CV_DECL_ALIGNED(16) buf[3*BLOCK_SIZE];

        #if CV_SSE2
        __m128 v_coeffs = _mm_set_ps(2.55f, 0.9732824427480916f, 0.72033898305084743f, 2.55f);
        __m128 v_res = _mm_set_ps(0.f, 136.259541984732824f, 96.525423728813564f, 0.f);
        #endif

        for( i = 0; i < n; i += BLOCK_SIZE, dst += BLOCK_SIZE*3 )
        {
            int dn = std::min(n - i, (int)BLOCK_SIZE);
            j = 0;

            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24, src += 8 * scn)
            {
                uint16x8_t v_t0, v_t1, v_t2;

                if (scn == 3)
                {
                    uint8x8x3_t v_src = vld3_u8(src);
                    v_t0 = vmovl_u8(v_src.val[0]);
                    v_t1 = vmovl_u8(v_src.val[1]);
                    v_t2 = vmovl_u8(v_src.val[2]);
                }
                else
                {
                    uint8x8x4_t v_src = vld4_u8(src);
                    v_t0 = vmovl_u8(v_src.val[0]);
                    v_t1 = vmovl_u8(v_src.val[1]);
                    v_t2 = vmovl_u8(v_src.val[2]);
                }

                float32x4x3_t v_dst;
                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j, v_dst);

                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t1))), v_scale_inv);
                v_dst.val[2] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t2))), v_scale_inv);
                vst3q_f32(buf + j + 12, v_dst);
            }
            #elif CV_SSE2
            if (scn == 3 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 16); j += 16, src += 16)
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)src);

                    __m128i v_src_p = _mm_unpacklo_epi8(v_src, v_zero);
                    _mm_store_ps(buf + j, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_p, v_zero)), v_scale_inv));
                    _mm_store_ps(buf + j + 4, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(v_src_p, v_zero)), v_scale_inv));

                    v_src_p = _mm_unpackhi_epi8(v_src, v_zero);
                    _mm_store_ps(buf + j + 8, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_p, v_zero)), v_scale_inv));
                    _mm_store_ps(buf + j + 12, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(v_src_p, v_zero)), v_scale_inv));
                }

                int jr = j % 3;
                if (jr)
                    src -= jr, j -= jr;
            }
            else if (scn == 4 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 12); j += 12, src += 16)
                {
                    __m128i v_src = _mm_loadu_si128((__m128i const *)src);

                    __m128i v_src_lo = _mm_unpacklo_epi8(v_src, v_zero);
                    __m128i v_src_hi = _mm_unpackhi_epi8(v_src, v_zero);
                    _mm_storeu_ps(buf + j, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_lo, v_zero)), v_scale_inv));
                    _mm_storeu_ps(buf + j + 3, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpackhi_epi16(v_src_lo, v_zero)), v_scale_inv));
                    _mm_storeu_ps(buf + j + 6, _mm_mul_ps(_mm_cvtepi32_ps(_mm_unpacklo_epi16(v_src_hi, v_zero)), v_scale_inv));
                    float tmp = buf[j + 8];
                    _mm_storeu_ps(buf + j + 8, _mm_mul_ps(_mm_cvtepi32_ps(_mm_shuffle_epi32(_mm_unpackhi_epi16(v_src_hi, v_zero), 0x90)), v_scale_inv));
                    buf[j + 8] = tmp;
                }

                int jr = j % 3;
                if (jr)
                    src -= jr, j -= jr;
            }
            #endif
            for( ; j < dn*3; j += 3, src += scn )
            {
                buf[j] = src[0]*(1.f/255.f);
                buf[j+1] = (float)(src[1]*(1.f/255.f));
                buf[j+2] = (float)(src[2]*(1.f/255.f));
            }
            cvt(buf, buf, dn);

            j = 0;
            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24)
            {
                float32x4x3_t v_src0 = vld3q_f32(buf + j), v_src1 = vld3q_f32(buf + j + 12);

                uint8x8x3_t v_dst;
                v_dst.val[0] = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[0], v_scale))),
                                                       vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[0], v_scale)))));
                v_dst.val[1] = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vaddq_f32(vmulq_f32(v_src0.val[1], v_coeff1), v_coeff2))),
                                                       vqmovn_u32(cv_vrndq_u32_f32(vaddq_f32(vmulq_f32(v_src1.val[1], v_coeff1), v_coeff2)))));
                v_dst.val[2] = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vaddq_f32(vmulq_f32(v_src0.val[2], v_coeff3), v_coeff4))),
                                                       vqmovn_u32(cv_vrndq_u32_f32(vaddq_f32(vmulq_f32(v_src1.val[2], v_coeff3), v_coeff4)))));

                vst3_u8(dst + j, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; j <= (dn - 16) * 3; j += 48)
                {
                    process(buf + j,
                            v_coeffs, v_res, dst + j);

                    process(buf + j + 16,
                            v_coeffs, v_res, dst + j + 16);

                    process(buf + j + 32,
                            v_coeffs, v_res, dst + j + 32);
                }
            }
            #endif

            for( ; j < dn*3; j += 3 )
            {
                dst[j] = saturate_cast<uchar>(buf[j]*2.55f);
                dst[j+1] = saturate_cast<uchar>(buf[j+1]*0.72033898305084743f + 96.525423728813564f);
                dst[j+2] = saturate_cast<uchar>(buf[j+2]*0.9732824427480916f + 136.259541984732824f);
            }
        }
    }

    int srccn;
    RGB2Luv_f cvt;

    #if CV_NEON
    float32x4_t v_scale, v_scale_inv, v_coeff1, v_coeff2, v_coeff3, v_coeff4;
    uint8x8_t v_alpha;
    #elif CV_SSE2
    __m128 v_scale_inv;
    __m128i v_zero;
    bool haveSIMD;
    #endif
};


struct Luv2RGB_b
{
    typedef uchar channel_type;

    Luv2RGB_b( int _dstcn, int blueIdx, const float* _coeffs,
               const float* _whitept, bool _srgb )
    : dstcn(_dstcn), cvt(3, blueIdx, _coeffs, _whitept, _srgb )
    {
        #if CV_NEON
        v_scale_inv = vdupq_n_f32(100.f/255.f);
        v_coeff1 = vdupq_n_f32(1.388235294117647f);
        v_coeff2 = vdupq_n_f32(1.027450980392157f);
        v_134 = vdupq_n_f32(134.f);
        v_140 = vdupq_n_f32(140.f);
        v_scale = vdupq_n_f32(255.f);
        v_alpha = vdup_n_u8(ColorChannel<uchar>::max());
        #elif CV_SSE2
        v_scale = _mm_set1_ps(255.f);
        v_zero = _mm_setzero_si128();
        v_alpha = _mm_set1_ps(ColorChannel<uchar>::max());
        haveSIMD = checkHardwareSupport(CV_CPU_SSE2);
        #endif
    }

    #if CV_SSE2
    // 16s x 8
    void process(__m128i v_l, __m128i v_u, __m128i v_v,
                 const __m128& v_coeffs_, const __m128& v_res_,
                 float * buf) const
    {
        __m128 v_l0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_l, v_zero));
        __m128 v_u0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_u, v_zero));
        __m128 v_v0 = _mm_cvtepi32_ps(_mm_unpacklo_epi16(v_v, v_zero));

        __m128 v_l1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_l, v_zero));
        __m128 v_u1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_u, v_zero));
        __m128 v_v1 = _mm_cvtepi32_ps(_mm_unpackhi_epi16(v_v, v_zero));

        __m128 v_coeffs = v_coeffs_;
        __m128 v_res = v_res_;

        v_l0 = _mm_mul_ps(v_l0, v_coeffs);
        v_u1 = _mm_mul_ps(v_u1, v_coeffs);
        v_l0 = _mm_sub_ps(v_l0, v_res);
        v_u1 = _mm_sub_ps(v_u1, v_res);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));
        v_res = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_res), 0x49));

        v_l1 = _mm_mul_ps(v_l1, v_coeffs);
        v_v0 = _mm_mul_ps(v_v0, v_coeffs);
        v_l1 = _mm_sub_ps(v_l1, v_res);
        v_v0 = _mm_sub_ps(v_v0, v_res);

        v_coeffs = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_coeffs), 0x49));
        v_res = _mm_castsi128_ps(_mm_shuffle_epi32(_mm_castps_si128(v_res), 0x49));

        v_u0 = _mm_mul_ps(v_u0, v_coeffs);
        v_v1 = _mm_mul_ps(v_v1, v_coeffs);
        v_u0 = _mm_sub_ps(v_u0, v_res);
        v_v1 = _mm_sub_ps(v_v1, v_res);

        _mm_store_ps(buf, v_l0);
        _mm_store_ps(buf + 4, v_l1);
        _mm_store_ps(buf + 8, v_u0);
        _mm_store_ps(buf + 12, v_u1);
        _mm_store_ps(buf + 16, v_v0);
        _mm_store_ps(buf + 20, v_v1);
    }
    #endif

    void operator()(const uchar* src, uchar* dst, int n) const
    {
        int i, j, dcn = dstcn;
        uchar alpha = ColorChannel<uchar>::max();
        float CV_DECL_ALIGNED(16) buf[3*BLOCK_SIZE];

        #if CV_SSE2
        __m128 v_coeffs = _mm_set_ps(100.f/255.f, 1.027450980392157f, 1.388235294117647f, 100.f/255.f);
        __m128 v_res = _mm_set_ps(0.f, 140.f, 134.f, 0.f);
        #endif

        for( i = 0; i < n; i += BLOCK_SIZE, src += BLOCK_SIZE*3 )
        {
            int dn = std::min(n - i, (int)BLOCK_SIZE);
            j = 0;

            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24)
            {
                uint8x8x3_t v_src = vld3_u8(src + j);
                uint16x8_t v_t0 = vmovl_u8(v_src.val[0]),
                           v_t1 = vmovl_u8(v_src.val[1]),
                           v_t2 = vmovl_u8(v_src.val[2]);

                float32x4x3_t v_dst;
                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vsubq_f32(vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t1))), v_coeff1), v_134);
                v_dst.val[2] = vsubq_f32(vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_low_u16(v_t2))), v_coeff2), v_140);
                vst3q_f32(buf + j, v_dst);

                v_dst.val[0] = vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t0))), v_scale_inv);
                v_dst.val[1] = vsubq_f32(vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t1))), v_coeff1), v_134);
                v_dst.val[2] = vsubq_f32(vmulq_f32(vcvtq_f32_u32(vmovl_u16(vget_high_u16(v_t2))), v_coeff2), v_140);
                vst3q_f32(buf + j + 12, v_dst);
            }
            #elif CV_SSE2
            if (haveSIMD)
            {
                for ( ; j <= (dn - 8) * 3; j += 24)
                {
                    __m128i v_src0 = _mm_loadu_si128((__m128i const *)(src + j));
                    __m128i v_src1 = _mm_loadl_epi64((__m128i const *)(src + j + 16));

                    process(_mm_unpacklo_epi8(v_src0, v_zero),
                            _mm_unpackhi_epi8(v_src0, v_zero),
                            _mm_unpacklo_epi8(v_src1, v_zero),
                            v_coeffs, v_res,
                            buf + j);
                }
            }
            #endif
            for( ; j < dn*3; j += 3 )
            {
                buf[j] = src[j]*(100.f/255.f);
                buf[j+1] = (float)(src[j+1]*1.388235294117647f - 134.f);
                buf[j+2] = (float)(src[j+2]*1.027450980392157f - 140.f);
            }
            cvt(buf, buf, dn);

            j = 0;
            #if CV_NEON
            for ( ; j <= (dn - 8) * 3; j += 24, dst += dcn * 8)
            {
                float32x4x3_t v_src0 = vld3q_f32(buf + j), v_src1 = vld3q_f32(buf + j + 12);
                uint8x8_t v_dst0 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[0], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[0], v_scale)))));
                uint8x8_t v_dst1 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[1], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[1], v_scale)))));
                uint8x8_t v_dst2 = vqmovn_u16(vcombine_u16(vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src0.val[2], v_scale))),
                                                           vqmovn_u32(cv_vrndq_u32_f32(vmulq_f32(v_src1.val[2], v_scale)))));

                if (dcn == 4)
                {
                    uint8x8x4_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    v_dst.val[3] = v_alpha;
                    vst4_u8(dst, v_dst);
                }
                else
                {
                    uint8x8x3_t v_dst;
                    v_dst.val[0] = v_dst0;
                    v_dst.val[1] = v_dst1;
                    v_dst.val[2] = v_dst2;
                    vst3_u8(dst, v_dst);
                }
            }
            #elif CV_SSE2
            if (dcn == 3 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 16); j += 16, dst += 16)
                {
                    __m128 v_src0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_src1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_src2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);
                    __m128 v_src3 = _mm_mul_ps(_mm_load_ps(buf + j + 12), v_scale);

                    __m128i v_dst0 = _mm_packs_epi32(_mm_cvtps_epi32(v_src0),
                                                     _mm_cvtps_epi32(v_src1));
                    __m128i v_dst1 = _mm_packs_epi32(_mm_cvtps_epi32(v_src2),
                                                     _mm_cvtps_epi32(v_src3));

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            else if (dcn == 4 && haveSIMD)
            {
                for ( ; j <= (dn * 3 - 12); j += 12, dst += 16)
                {
                    __m128 v_buf0 = _mm_mul_ps(_mm_load_ps(buf + j), v_scale);
                    __m128 v_buf1 = _mm_mul_ps(_mm_load_ps(buf + j + 4), v_scale);
                    __m128 v_buf2 = _mm_mul_ps(_mm_load_ps(buf + j + 8), v_scale);

                    __m128 v_ba0 = _mm_unpackhi_ps(v_buf0, v_alpha);
                    __m128 v_ba1 = _mm_unpacklo_ps(v_buf2, v_alpha);

                    __m128i v_src0 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf0, v_ba0, 0x44));
                    __m128i v_src1 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba0, v_buf1, 0x4e)), 0x78);
                    __m128i v_src2 = _mm_cvtps_epi32(_mm_shuffle_ps(v_buf1, v_ba1, 0x4e));
                    __m128i v_src3 = _mm_shuffle_epi32(_mm_cvtps_epi32(_mm_shuffle_ps(v_ba1, v_buf2, 0xee)), 0x78);

                    __m128i v_dst0 = _mm_packs_epi32(v_src0, v_src1);
                    __m128i v_dst1 = _mm_packs_epi32(v_src2, v_src3);

                    _mm_storeu_si128((__m128i *)dst, _mm_packus_epi16(v_dst0, v_dst1));
                }

                int jr = j % 3;
                if (jr)
                    dst -= jr, j -= jr;
            }
            #endif

            for( ; j < dn*3; j += 3, dst += dcn )
            {
                dst[0] = saturate_cast<uchar>(buf[j]*255.f);
                dst[1] = saturate_cast<uchar>(buf[j+1]*255.f);
                dst[2] = saturate_cast<uchar>(buf[j+2]*255.f);
                if( dcn == 4 )
                    dst[3] = alpha;
            }
        }
    }

    int dstcn;
    Luv2RGB_f cvt;

    #if CV_NEON
    float32x4_t v_scale, v_scale_inv, v_coeff1, v_coeff2, v_134, v_140;
    uint8x8_t v_alpha;
    #elif CV_SSE2
    __m128 v_scale;
    __m128 v_alpha;
    __m128i v_zero;
    bool haveSIMD;
    #endif
};


///////////////////////////////////// YUV420 -> RGB /////////////////////////////////////

const int ITUR_BT_601_CY = 1220542;
const int ITUR_BT_601_CUB = 2116026;
const int ITUR_BT_601_CUG = -409993;
const int ITUR_BT_601_CVG = -852492;
const int ITUR_BT_601_CVR = 1673527;
const int ITUR_BT_601_SHIFT = 20;

// Coefficients for RGB to YUV420p conversion
const int ITUR_BT_601_CRY =  269484;
const int ITUR_BT_601_CGY =  528482;
const int ITUR_BT_601_CBY =  102760;
const int ITUR_BT_601_CRU = -155188;
const int ITUR_BT_601_CGU = -305135;
const int ITUR_BT_601_CBU =  460324;
const int ITUR_BT_601_CGV = -385875;
const int ITUR_BT_601_CBV = -74448;

template<int bIdx, int uIdx>
struct YUV420sp2RGB888Invoker : ParallelLoopBody
{
    uchar * dst_data;
    size_t dst_step;
    int width;
    const uchar* my1, *muv;
    size_t stride;

    YUV420sp2RGB888Invoker(uchar * _dst_data, size_t _dst_step, int _dst_width, size_t _stride, const uchar* _y1, const uchar* _uv)
        : dst_data(_dst_data), dst_step(_dst_step), width(_dst_width), my1(_y1), muv(_uv), stride(_stride) {}

    void operator()(const Range& range) const
    {
        int rangeBegin = range.start * 2;
        int rangeEnd = range.end * 2;

        //R = 1.164(Y - 16) + 1.596(V - 128)
        //G = 1.164(Y - 16) - 0.813(V - 128) - 0.391(U - 128)
        //B = 1.164(Y - 16)                  + 2.018(U - 128)

        //R = (1220542(Y - 16) + 1673527(V - 128)                  + (1 << 19)) >> 20
        //G = (1220542(Y - 16) - 852492(V - 128) - 409993(U - 128) + (1 << 19)) >> 20
        //B = (1220542(Y - 16)                  + 2116026(U - 128) + (1 << 19)) >> 20

        const uchar* y1 = my1 + rangeBegin * stride, *uv = muv + rangeBegin * stride / 2;

        for (int j = rangeBegin; j < rangeEnd; j += 2, y1 += stride * 2, uv += stride)
        {
            uchar* row1 = dst_data + dst_step * j;
            uchar* row2 = dst_data + dst_step * (j + 1);
            const uchar* y2 = y1 + stride;

            for (int i = 0; i < width; i += 2, row1 += 6, row2 += 6)
            {
                int u = int(uv[i + 0 + uIdx]) - 128;
                int v = int(uv[i + 1 - uIdx]) - 128;

                int ruv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVR * v;
                int guv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVG * v + ITUR_BT_601_CUG * u;
                int buv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CUB * u;

                int y00 = std::max(0, int(y1[i]) - 16) * ITUR_BT_601_CY;
                row1[2-bIdx] = saturate_cast<uchar>((y00 + ruv) >> ITUR_BT_601_SHIFT);
                row1[1]      = saturate_cast<uchar>((y00 + guv) >> ITUR_BT_601_SHIFT);
                row1[bIdx]   = saturate_cast<uchar>((y00 + buv) >> ITUR_BT_601_SHIFT);

                int y01 = std::max(0, int(y1[i + 1]) - 16) * ITUR_BT_601_CY;
                row1[5-bIdx] = saturate_cast<uchar>((y01 + ruv) >> ITUR_BT_601_SHIFT);
                row1[4]      = saturate_cast<uchar>((y01 + guv) >> ITUR_BT_601_SHIFT);
                row1[3+bIdx] = saturate_cast<uchar>((y01 + buv) >> ITUR_BT_601_SHIFT);

                int y10 = std::max(0, int(y2[i]) - 16) * ITUR_BT_601_CY;
                row2[2-bIdx] = saturate_cast<uchar>((y10 + ruv) >> ITUR_BT_601_SHIFT);
                row2[1]      = saturate_cast<uchar>((y10 + guv) >> ITUR_BT_601_SHIFT);
                row2[bIdx]   = saturate_cast<uchar>((y10 + buv) >> ITUR_BT_601_SHIFT);

                int y11 = std::max(0, int(y2[i + 1]) - 16) * ITUR_BT_601_CY;
                row2[5-bIdx] = saturate_cast<uchar>((y11 + ruv) >> ITUR_BT_601_SHIFT);
                row2[4]      = saturate_cast<uchar>((y11 + guv) >> ITUR_BT_601_SHIFT);
                row2[3+bIdx] = saturate_cast<uchar>((y11 + buv) >> ITUR_BT_601_SHIFT);
            }
        }
    }
};

template<int bIdx, int uIdx>
struct YUV420sp2RGBA8888Invoker : ParallelLoopBody
{
    uchar * dst_data;
    size_t dst_step;
    int width;
    const uchar* my1, *muv;
    size_t stride;

    YUV420sp2RGBA8888Invoker(uchar * _dst_data, size_t _dst_step, int _dst_width, size_t _stride, const uchar* _y1, const uchar* _uv)
        : dst_data(_dst_data), dst_step(_dst_step), width(_dst_width), my1(_y1), muv(_uv), stride(_stride) {}

    void operator()(const Range& range) const
    {
        int rangeBegin = range.start * 2;
        int rangeEnd = range.end * 2;

        //R = 1.164(Y - 16) + 1.596(V - 128)
        //G = 1.164(Y - 16) - 0.813(V - 128) - 0.391(U - 128)
        //B = 1.164(Y - 16)                  + 2.018(U - 128)

        //R = (1220542(Y - 16) + 1673527(V - 128)                  + (1 << 19)) >> 20
        //G = (1220542(Y - 16) - 852492(V - 128) - 409993(U - 128) + (1 << 19)) >> 20
        //B = (1220542(Y - 16)                  + 2116026(U - 128) + (1 << 19)) >> 20

        const uchar* y1 = my1 + rangeBegin * stride, *uv = muv + rangeBegin * stride / 2;

        for (int j = rangeBegin; j < rangeEnd; j += 2, y1 += stride * 2, uv += stride)
        {
            uchar* row1 = dst_data + dst_step * j;
            uchar* row2 = dst_data + dst_step * (j + 1);
            const uchar* y2 = y1 + stride;

            for (int i = 0; i < width; i += 2, row1 += 8, row2 += 8)
            {
                int u = int(uv[i + 0 + uIdx]) - 128;
                int v = int(uv[i + 1 - uIdx]) - 128;

                int ruv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVR * v;
                int guv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVG * v + ITUR_BT_601_CUG * u;
                int buv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CUB * u;

                int y00 = std::max(0, int(y1[i]) - 16) * ITUR_BT_601_CY;
                row1[2-bIdx] = saturate_cast<uchar>((y00 + ruv) >> ITUR_BT_601_SHIFT);
                row1[1]      = saturate_cast<uchar>((y00 + guv) >> ITUR_BT_601_SHIFT);
                row1[bIdx]   = saturate_cast<uchar>((y00 + buv) >> ITUR_BT_601_SHIFT);
                row1[3]      = uchar(0xff);

                int y01 = std::max(0, int(y1[i + 1]) - 16) * ITUR_BT_601_CY;
                row1[6-bIdx] = saturate_cast<uchar>((y01 + ruv) >> ITUR_BT_601_SHIFT);
                row1[5]      = saturate_cast<uchar>((y01 + guv) >> ITUR_BT_601_SHIFT);
                row1[4+bIdx] = saturate_cast<uchar>((y01 + buv) >> ITUR_BT_601_SHIFT);
                row1[7]      = uchar(0xff);

                int y10 = std::max(0, int(y2[i]) - 16) * ITUR_BT_601_CY;
                row2[2-bIdx] = saturate_cast<uchar>((y10 + ruv) >> ITUR_BT_601_SHIFT);
                row2[1]      = saturate_cast<uchar>((y10 + guv) >> ITUR_BT_601_SHIFT);
                row2[bIdx]   = saturate_cast<uchar>((y10 + buv) >> ITUR_BT_601_SHIFT);
                row2[3]      = uchar(0xff);

                int y11 = std::max(0, int(y2[i + 1]) - 16) * ITUR_BT_601_CY;
                row2[6-bIdx] = saturate_cast<uchar>((y11 + ruv) >> ITUR_BT_601_SHIFT);
                row2[5]      = saturate_cast<uchar>((y11 + guv) >> ITUR_BT_601_SHIFT);
                row2[4+bIdx] = saturate_cast<uchar>((y11 + buv) >> ITUR_BT_601_SHIFT);
                row2[7]      = uchar(0xff);
            }
        }
    }
};

template<int bIdx>
struct YUV420p2RGB888Invoker : ParallelLoopBody
{
    uchar * dst_data;
    size_t dst_step;
    int width;
    const uchar* my1, *mu, *mv;
    size_t stride;
    int ustepIdx, vstepIdx;

    YUV420p2RGB888Invoker(uchar * _dst_data, size_t _dst_step, int _dst_width, size_t _stride, const uchar* _y1, const uchar* _u, const uchar* _v, int _ustepIdx, int _vstepIdx)
        : dst_data(_dst_data), dst_step(_dst_step), width(_dst_width), my1(_y1), mu(_u), mv(_v), stride(_stride), ustepIdx(_ustepIdx), vstepIdx(_vstepIdx) {}

    void operator()(const Range& range) const
    {
        const int rangeBegin = range.start * 2;
        const int rangeEnd = range.end * 2;

        int uvsteps[2] = {width/2, static_cast<int>(stride) - width/2};
        int usIdx = ustepIdx, vsIdx = vstepIdx;

        const uchar* y1 = my1 + rangeBegin * stride;
        const uchar* u1 = mu + (range.start / 2) * stride;
        const uchar* v1 = mv + (range.start / 2) * stride;

        if(range.start % 2 == 1)
        {
            u1 += uvsteps[(usIdx++) & 1];
            v1 += uvsteps[(vsIdx++) & 1];
        }

        for (int j = rangeBegin; j < rangeEnd; j += 2, y1 += stride * 2, u1 += uvsteps[(usIdx++) & 1], v1 += uvsteps[(vsIdx++) & 1])
        {
            uchar* row1 = dst_data + dst_step * j;
            uchar* row2 = dst_data + dst_step * (j + 1);
            const uchar* y2 = y1 + stride;

            for (int i = 0; i < width / 2; i += 1, row1 += 6, row2 += 6)
            {
                int u = int(u1[i]) - 128;
                int v = int(v1[i]) - 128;

                int ruv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVR * v;
                int guv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVG * v + ITUR_BT_601_CUG * u;
                int buv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CUB * u;

                int y00 = std::max(0, int(y1[2 * i]) - 16) * ITUR_BT_601_CY;
                row1[2-bIdx] = saturate_cast<uchar>((y00 + ruv) >> ITUR_BT_601_SHIFT);
                row1[1]      = saturate_cast<uchar>((y00 + guv) >> ITUR_BT_601_SHIFT);
                row1[bIdx]   = saturate_cast<uchar>((y00 + buv) >> ITUR_BT_601_SHIFT);

                int y01 = std::max(0, int(y1[2 * i + 1]) - 16) * ITUR_BT_601_CY;
                row1[5-bIdx] = saturate_cast<uchar>((y01 + ruv) >> ITUR_BT_601_SHIFT);
                row1[4]      = saturate_cast<uchar>((y01 + guv) >> ITUR_BT_601_SHIFT);
                row1[3+bIdx] = saturate_cast<uchar>((y01 + buv) >> ITUR_BT_601_SHIFT);

                int y10 = std::max(0, int(y2[2 * i]) - 16) * ITUR_BT_601_CY;
                row2[2-bIdx] = saturate_cast<uchar>((y10 + ruv) >> ITUR_BT_601_SHIFT);
                row2[1]      = saturate_cast<uchar>((y10 + guv) >> ITUR_BT_601_SHIFT);
                row2[bIdx]   = saturate_cast<uchar>((y10 + buv) >> ITUR_BT_601_SHIFT);

                int y11 = std::max(0, int(y2[2 * i + 1]) - 16) * ITUR_BT_601_CY;
                row2[5-bIdx] = saturate_cast<uchar>((y11 + ruv) >> ITUR_BT_601_SHIFT);
                row2[4]      = saturate_cast<uchar>((y11 + guv) >> ITUR_BT_601_SHIFT);
                row2[3+bIdx] = saturate_cast<uchar>((y11 + buv) >> ITUR_BT_601_SHIFT);
            }
        }
    }
};

template<int bIdx>
struct YUV420p2RGBA8888Invoker : ParallelLoopBody
{
    uchar * dst_data;
    size_t dst_step;
    int width;
    const uchar* my1, *mu, *mv;
    size_t  stride;
    int ustepIdx, vstepIdx;

    YUV420p2RGBA8888Invoker(uchar * _dst_data, size_t _dst_step, int _dst_width, size_t _stride, const uchar* _y1, const uchar* _u, const uchar* _v, int _ustepIdx, int _vstepIdx)
        : dst_data(_dst_data), dst_step(_dst_step), width(_dst_width), my1(_y1), mu(_u), mv(_v), stride(_stride), ustepIdx(_ustepIdx), vstepIdx(_vstepIdx) {}

    void operator()(const Range& range) const
    {
        int rangeBegin = range.start * 2;
        int rangeEnd = range.end * 2;

        int uvsteps[2] = {width/2, static_cast<int>(stride) - width/2};
        int usIdx = ustepIdx, vsIdx = vstepIdx;

        const uchar* y1 = my1 + rangeBegin * stride;
        const uchar* u1 = mu + (range.start / 2) * stride;
        const uchar* v1 = mv + (range.start / 2) * stride;

        if(range.start % 2 == 1)
        {
            u1 += uvsteps[(usIdx++) & 1];
            v1 += uvsteps[(vsIdx++) & 1];
        }

        for (int j = rangeBegin; j < rangeEnd; j += 2, y1 += stride * 2, u1 += uvsteps[(usIdx++) & 1], v1 += uvsteps[(vsIdx++) & 1])
        {
            uchar* row1 = dst_data + dst_step * j;
            uchar* row2 = dst_data + dst_step * (j + 1);
            const uchar* y2 = y1 + stride;

            for (int i = 0; i < width / 2; i += 1, row1 += 8, row2 += 8)
            {
                int u = int(u1[i]) - 128;
                int v = int(v1[i]) - 128;

                int ruv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVR * v;
                int guv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVG * v + ITUR_BT_601_CUG * u;
                int buv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CUB * u;

                int y00 = std::max(0, int(y1[2 * i]) - 16) * ITUR_BT_601_CY;
                row1[2-bIdx] = saturate_cast<uchar>((y00 + ruv) >> ITUR_BT_601_SHIFT);
                row1[1]      = saturate_cast<uchar>((y00 + guv) >> ITUR_BT_601_SHIFT);
                row1[bIdx]   = saturate_cast<uchar>((y00 + buv) >> ITUR_BT_601_SHIFT);
                row1[3]      = uchar(0xff);

                int y01 = std::max(0, int(y1[2 * i + 1]) - 16) * ITUR_BT_601_CY;
                row1[6-bIdx] = saturate_cast<uchar>((y01 + ruv) >> ITUR_BT_601_SHIFT);
                row1[5]      = saturate_cast<uchar>((y01 + guv) >> ITUR_BT_601_SHIFT);
                row1[4+bIdx] = saturate_cast<uchar>((y01 + buv) >> ITUR_BT_601_SHIFT);
                row1[7]      = uchar(0xff);

                int y10 = std::max(0, int(y2[2 * i]) - 16) * ITUR_BT_601_CY;
                row2[2-bIdx] = saturate_cast<uchar>((y10 + ruv) >> ITUR_BT_601_SHIFT);
                row2[1]      = saturate_cast<uchar>((y10 + guv) >> ITUR_BT_601_SHIFT);
                row2[bIdx]   = saturate_cast<uchar>((y10 + buv) >> ITUR_BT_601_SHIFT);
                row2[3]      = uchar(0xff);

                int y11 = std::max(0, int(y2[2 * i + 1]) - 16) * ITUR_BT_601_CY;
                row2[6-bIdx] = saturate_cast<uchar>((y11 + ruv) >> ITUR_BT_601_SHIFT);
                row2[5]      = saturate_cast<uchar>((y11 + guv) >> ITUR_BT_601_SHIFT);
                row2[4+bIdx] = saturate_cast<uchar>((y11 + buv) >> ITUR_BT_601_SHIFT);
                row2[7]      = uchar(0xff);
            }
        }
    }
};

#define MIN_SIZE_FOR_PARALLEL_YUV420_CONVERSION (320*240)

template<int bIdx, int uIdx>
inline void cvtYUV420sp2RGB(uchar * dst_data, size_t dst_step, int dst_width, int dst_height, size_t _stride, const uchar* _y1, const uchar* _uv)
{
    YUV420sp2RGB888Invoker<bIdx, uIdx> converter(dst_data, dst_step, dst_width, _stride, _y1,  _uv);
    if (dst_width * dst_height >= MIN_SIZE_FOR_PARALLEL_YUV420_CONVERSION)
        parallel_for_(Range(0, dst_height/2), converter);
    else
        converter(Range(0, dst_height/2));
}

template<int bIdx, int uIdx>
inline void cvtYUV420sp2RGBA(uchar * dst_data, size_t dst_step, int dst_width, int dst_height, size_t _stride, const uchar* _y1, const uchar* _uv)
{
    YUV420sp2RGBA8888Invoker<bIdx, uIdx> converter(dst_data, dst_step, dst_width, _stride, _y1,  _uv);
    if (dst_width * dst_height >= MIN_SIZE_FOR_PARALLEL_YUV420_CONVERSION)
        parallel_for_(Range(0, dst_height/2), converter);
    else
        converter(Range(0, dst_height/2));
}

template<int bIdx>
inline void cvtYUV420p2RGB(uchar * dst_data, size_t dst_step, int dst_width, int dst_height, size_t _stride, const uchar* _y1, const uchar* _u, const uchar* _v, int ustepIdx, int vstepIdx)
{
    YUV420p2RGB888Invoker<bIdx> converter(dst_data, dst_step, dst_width, _stride, _y1,  _u, _v, ustepIdx, vstepIdx);
    if (dst_width * dst_height >= MIN_SIZE_FOR_PARALLEL_YUV420_CONVERSION)
        parallel_for_(Range(0, dst_height/2), converter);
    else
        converter(Range(0, dst_height/2));
}

template<int bIdx>
inline void cvtYUV420p2RGBA(uchar * dst_data, size_t dst_step, int dst_width, int dst_height, size_t _stride, const uchar* _y1, const uchar* _u, const uchar* _v, int ustepIdx, int vstepIdx)
{
    YUV420p2RGBA8888Invoker<bIdx> converter(dst_data, dst_step, dst_width, _stride, _y1,  _u, _v, ustepIdx, vstepIdx);
    if (dst_width * dst_height >= MIN_SIZE_FOR_PARALLEL_YUV420_CONVERSION)
        parallel_for_(Range(0, dst_height/2), converter);
    else
        converter(Range(0, dst_height/2));
}

///////////////////////////////////// RGB -> YUV420p /////////////////////////////////////

template<int uIdx>
inline void swapUV(uchar * &, uchar * &) {}
template<>
inline void swapUV<2>(uchar * & u, uchar * & v) { std::swap(u, v); }

template<int bIdx, int uIdx>
struct RGB888toYUV420pInvoker: public ParallelLoopBody
{
    RGB888toYUV420pInvoker(const uchar * _src_data, size_t _src_step, uchar * _dst_data, size_t _dst_step,
                           int _src_width, int _src_height, int _scn)
        : src_data(_src_data), src_step(_src_step),
          dst_data(_dst_data), dst_step(_dst_step),
          src_width(_src_width), src_height(_src_height),
          scn(_scn) { }

    void operator()(const Range& rowRange) const
    {
        const int w = src_width;
        const int h = src_height;

        const int cn = scn;
        for( int i = rowRange.start; i < rowRange.end; i++ )
        {
            const uchar* row0 = src_data + src_step * (2 * i);
            const uchar* row1 = src_data + src_step * (2 * i + 1);

            uchar* y = dst_data + dst_step * (2*i);
            uchar* u = dst_data + dst_step * (h + i/2) + (i % 2) * (w/2);
            uchar* v = dst_data + dst_step * (h + (i + h/2)/2) + ((i + h/2) % 2) * (w/2);

            swapUV<uIdx>(u, v);

            for( int j = 0, k = 0; j < w * cn; j += 2 * cn, k++ )
            {
                int r00 = row0[2-bIdx + j];      int g00 = row0[1 + j];      int b00 = row0[bIdx + j];
                int r01 = row0[2-bIdx + cn + j]; int g01 = row0[1 + cn + j]; int b01 = row0[bIdx + cn + j];
                int r10 = row1[2-bIdx + j];      int g10 = row1[1 + j];      int b10 = row1[bIdx + j];
                int r11 = row1[2-bIdx + cn + j]; int g11 = row1[1 + cn + j]; int b11 = row1[bIdx + cn + j];

                const int shifted16 = (16 << ITUR_BT_601_SHIFT);
                const int halfShift = (1 << (ITUR_BT_601_SHIFT - 1));
                int y00 = ITUR_BT_601_CRY * r00 + ITUR_BT_601_CGY * g00 + ITUR_BT_601_CBY * b00 + halfShift + shifted16;
                int y01 = ITUR_BT_601_CRY * r01 + ITUR_BT_601_CGY * g01 + ITUR_BT_601_CBY * b01 + halfShift + shifted16;
                int y10 = ITUR_BT_601_CRY * r10 + ITUR_BT_601_CGY * g10 + ITUR_BT_601_CBY * b10 + halfShift + shifted16;
                int y11 = ITUR_BT_601_CRY * r11 + ITUR_BT_601_CGY * g11 + ITUR_BT_601_CBY * b11 + halfShift + shifted16;

                y[2*k + 0]            = saturate_cast<uchar>(y00 >> ITUR_BT_601_SHIFT);
                y[2*k + 1]            = saturate_cast<uchar>(y01 >> ITUR_BT_601_SHIFT);
                y[2*k + dst_step + 0] = saturate_cast<uchar>(y10 >> ITUR_BT_601_SHIFT);
                y[2*k + dst_step + 1] = saturate_cast<uchar>(y11 >> ITUR_BT_601_SHIFT);

                const int shifted128 = (128 << ITUR_BT_601_SHIFT);
                int u00 = ITUR_BT_601_CRU * r00 + ITUR_BT_601_CGU * g00 + ITUR_BT_601_CBU * b00 + halfShift + shifted128;
                int v00 = ITUR_BT_601_CBU * r00 + ITUR_BT_601_CGV * g00 + ITUR_BT_601_CBV * b00 + halfShift + shifted128;

                u[k] = saturate_cast<uchar>(u00 >> ITUR_BT_601_SHIFT);
                v[k] = saturate_cast<uchar>(v00 >> ITUR_BT_601_SHIFT);
            }
        }
    }

    static bool isFit( int src_width, int src_height )
    {
        return (src_width * src_height >= 320*240);
    }

private:
    RGB888toYUV420pInvoker& operator=(const RGB888toYUV420pInvoker&);

    const uchar * src_data;
    size_t src_step;
    uchar * dst_data;
    size_t dst_step;
    int src_width;
    int src_height;
    const int scn;
};

template<int bIdx, int uIdx>
static void cvtRGBtoYUV420p(const uchar * src_data, size_t src_step,
                            uchar * dst_data, size_t dst_step,
                            int src_width, int src_height, int scn)
{
    RGB888toYUV420pInvoker<bIdx, uIdx> colorConverter(src_data, src_step, dst_data, dst_step, src_width, src_height, scn);
    if( RGB888toYUV420pInvoker<bIdx, uIdx>::isFit(src_width, src_height) )
        parallel_for_(Range(0, src_height/2), colorConverter);
    else
        colorConverter(Range(0, src_height/2));
}

///////////////////////////////////// YUV422 -> RGB /////////////////////////////////////

template<int bIdx, int uIdx, int yIdx>
struct YUV422toRGB888Invoker : ParallelLoopBody
{
    uchar * dst_data;
    size_t dst_step;
    const uchar * src_data;
    size_t src_step;
    int width;

    YUV422toRGB888Invoker(uchar * _dst_data, size_t _dst_step,
                          const uchar * _src_data, size_t _src_step,
                          int _width)
        : dst_data(_dst_data), dst_step(_dst_step), src_data(_src_data), src_step(_src_step), width(_width) {}

    void operator()(const Range& range) const
    {
        int rangeBegin = range.start;
        int rangeEnd = range.end;

        const int uidx = 1 - yIdx + uIdx * 2;
        const int vidx = (2 + uidx) % 4;
        const uchar* yuv_src = src_data + rangeBegin * src_step;

        for (int j = rangeBegin; j < rangeEnd; j++, yuv_src += src_step)
        {
            uchar* row = dst_data + dst_step * j;

            for (int i = 0; i < 2 * width; i += 4, row += 6)
            {
                int u = int(yuv_src[i + uidx]) - 128;
                int v = int(yuv_src[i + vidx]) - 128;

                int ruv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVR * v;
                int guv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVG * v + ITUR_BT_601_CUG * u;
                int buv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CUB * u;

                int y00 = std::max(0, int(yuv_src[i + yIdx]) - 16) * ITUR_BT_601_CY;
                row[2-bIdx] = saturate_cast<uchar>((y00 + ruv) >> ITUR_BT_601_SHIFT);
                row[1]      = saturate_cast<uchar>((y00 + guv) >> ITUR_BT_601_SHIFT);
                row[bIdx]   = saturate_cast<uchar>((y00 + buv) >> ITUR_BT_601_SHIFT);

                int y01 = std::max(0, int(yuv_src[i + yIdx + 2]) - 16) * ITUR_BT_601_CY;
                row[5-bIdx] = saturate_cast<uchar>((y01 + ruv) >> ITUR_BT_601_SHIFT);
                row[4]      = saturate_cast<uchar>((y01 + guv) >> ITUR_BT_601_SHIFT);
                row[3+bIdx] = saturate_cast<uchar>((y01 + buv) >> ITUR_BT_601_SHIFT);
            }
        }
    }
};

template<int bIdx, int uIdx, int yIdx>
struct YUV422toRGBA8888Invoker : ParallelLoopBody
{
    uchar * dst_data;
    size_t dst_step;
    const uchar * src_data;
    size_t src_step;
    int width;

    YUV422toRGBA8888Invoker(uchar * _dst_data, size_t _dst_step,
                            const uchar * _src_data, size_t _src_step,
                            int _width)
        : dst_data(_dst_data), dst_step(_dst_step), src_data(_src_data), src_step(_src_step), width(_width) {}

    void operator()(const Range& range) const
    {
        int rangeBegin = range.start;
        int rangeEnd = range.end;

        const int uidx = 1 - yIdx + uIdx * 2;
        const int vidx = (2 + uidx) % 4;
        const uchar* yuv_src = src_data + rangeBegin * src_step;

        for (int j = rangeBegin; j < rangeEnd; j++, yuv_src += src_step)
        {
            uchar* row = dst_data + dst_step * j;

            for (int i = 0; i < 2 * width; i += 4, row += 8)
            {
                int u = int(yuv_src[i + uidx]) - 128;
                int v = int(yuv_src[i + vidx]) - 128;

                int ruv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVR * v;
                int guv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CVG * v + ITUR_BT_601_CUG * u;
                int buv = (1 << (ITUR_BT_601_SHIFT - 1)) + ITUR_BT_601_CUB * u;

                int y00 = std::max(0, int(yuv_src[i + yIdx]) - 16) * ITUR_BT_601_CY;
                row[2-bIdx] = saturate_cast<uchar>((y00 + ruv) >> ITUR_BT_601_SHIFT);
                row[1]      = saturate_cast<uchar>((y00 + guv) >> ITUR_BT_601_SHIFT);
                row[bIdx]   = saturate_cast<uchar>((y00 + buv) >> ITUR_BT_601_SHIFT);
                row[3]      = uchar(0xff);

                int y01 = std::max(0, int(yuv_src[i + yIdx + 2]) - 16) * ITUR_BT_601_CY;
                row[6-bIdx] = saturate_cast<uchar>((y01 + ruv) >> ITUR_BT_601_SHIFT);
                row[5]      = saturate_cast<uchar>((y01 + guv) >> ITUR_BT_601_SHIFT);
                row[4+bIdx] = saturate_cast<uchar>((y01 + buv) >> ITUR_BT_601_SHIFT);
                row[7]      = uchar(0xff);
            }
        }
    }
};

#define MIN_SIZE_FOR_PARALLEL_YUV422_CONVERSION (320*240)

template<int bIdx, int uIdx, int yIdx>
inline void cvtYUV422toRGB(uchar * dst_data, size_t dst_step, const uchar * src_data, size_t src_step,
                           int width, int height)
{
    YUV422toRGB888Invoker<bIdx, uIdx, yIdx> converter(dst_data, dst_step, src_data, src_step, width);
    if (width * height >= MIN_SIZE_FOR_PARALLEL_YUV422_CONVERSION)
        parallel_for_(Range(0, height), converter);
    else
        converter(Range(0, height));
}

template<int bIdx, int uIdx, int yIdx>
inline void cvtYUV422toRGBA(uchar * dst_data, size_t dst_step, const uchar * src_data, size_t src_step,
                           int width, int height)
{
    YUV422toRGBA8888Invoker<bIdx, uIdx, yIdx> converter(dst_data, dst_step, src_data, src_step, width);
    if (width * height >= MIN_SIZE_FOR_PARALLEL_YUV422_CONVERSION)
        parallel_for_(Range(0, height), converter);
    else
        converter(Range(0, height));
}

/////////////////////////// RGBA <-> mRGBA (alpha premultiplied) //////////////

template<typename _Tp>
struct RGBA2mRGBA
{
    typedef _Tp channel_type;

    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        _Tp max_val  = ColorChannel<_Tp>::max();
        _Tp half_val = ColorChannel<_Tp>::half();
        for( int i = 0; i < n; i++ )
        {
            _Tp v0 = *src++;
            _Tp v1 = *src++;
            _Tp v2 = *src++;
            _Tp v3 = *src++;

            *dst++ = (v0 * v3 + half_val) / max_val;
            *dst++ = (v1 * v3 + half_val) / max_val;
            *dst++ = (v2 * v3 + half_val) / max_val;
            *dst++ = v3;
        }
    }
};


template<typename _Tp>
struct mRGBA2RGBA
{
    typedef _Tp channel_type;

    void operator()(const _Tp* src, _Tp* dst, int n) const
    {
        _Tp max_val = ColorChannel<_Tp>::max();
        for( int i = 0; i < n; i++ )
        {
            _Tp v0 = *src++;
            _Tp v1 = *src++;
            _Tp v2 = *src++;
            _Tp v3 = *src++;
            _Tp v3_half = v3 / 2;

            *dst++ = (v3==0)? 0 : (v0 * max_val + v3_half) / v3;
            *dst++ = (v3==0)? 0 : (v1 * max_val + v3_half) / v3;
            *dst++ = (v3==0)? 0 : (v2 * max_val + v3_half) / v3;
            *dst++ = v3;
        }
    }
};

#ifdef HAVE_OPENCL

static bool ocl_cvtColor( InputArray _src, OutputArray _dst, int code, int dcn )
{
    bool ok = false;
    UMat src = _src.getUMat(), dst;
    Size sz = src.size(), dstSz = sz;
    int scn = src.channels(), depth = src.depth(), bidx, uidx, yidx;
    int dims = 2, stripeSize = 1;
    ocl::Kernel k;

    if (depth != CV_8U && depth != CV_16U && depth != CV_32F)
        return false;

    ocl::Device dev = ocl::Device::getDefault();
    int pxPerWIy = dev.isIntel() && (dev.type() & ocl::Device::TYPE_GPU) ? 4 : 1;
    int pxPerWIx = 1;

    size_t globalsize[] = { (size_t)src.cols, ((size_t)src.rows + pxPerWIy - 1) / pxPerWIy };
    cv::String opts = format("-D depth=%d -D scn=%d -D PIX_PER_WI_Y=%d ",
                             depth, scn, pxPerWIy);

    switch (code)
    {
    case COLOR_BGR2BGRA: case COLOR_RGB2BGRA: case COLOR_BGRA2BGR:
    case COLOR_RGBA2BGR: case COLOR_RGB2BGR: case COLOR_BGRA2RGBA:
    {
        CV_Assert(scn == 3 || scn == 4);
        dcn = code == COLOR_BGR2BGRA || code == COLOR_RGB2BGRA || code == COLOR_BGRA2RGBA ? 4 : 3;
        bool reverse = !(code == COLOR_BGR2BGRA || code == COLOR_BGRA2BGR);
        k.create("RGB", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=0 -D %s", dcn,
                        reverse ? "REVERSE" : "ORDER"));
        break;
    }
    case COLOR_BGR5652BGR: case COLOR_BGR5552BGR: case COLOR_BGR5652RGB: case COLOR_BGR5552RGB:
    case COLOR_BGR5652BGRA: case COLOR_BGR5552BGRA: case COLOR_BGR5652RGBA: case COLOR_BGR5552RGBA:
    {
        dcn = code == COLOR_BGR5652BGRA || code == COLOR_BGR5552BGRA || code == COLOR_BGR5652RGBA || code == COLOR_BGR5552RGBA ? 4 : 3;
        CV_Assert((dcn == 3 || dcn == 4) && scn == 2 && depth == CV_8U);
        bidx = code == COLOR_BGR5652BGR || code == COLOR_BGR5552BGR ||
            code == COLOR_BGR5652BGRA || code == COLOR_BGR5552BGRA ? 0 : 2;
        int greenbits = code == COLOR_BGR5652BGR || code == COLOR_BGR5652RGB ||
            code == COLOR_BGR5652BGRA || code == COLOR_BGR5652RGBA ? 6 : 5;
        k.create("RGB5x52RGB", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d -D greenbits=%d", dcn, bidx, greenbits));
        break;
    }
    case COLOR_BGR2BGR565: case COLOR_BGR2BGR555: case COLOR_RGB2BGR565: case COLOR_RGB2BGR555:
    case COLOR_BGRA2BGR565: case COLOR_BGRA2BGR555: case COLOR_RGBA2BGR565: case COLOR_RGBA2BGR555:
    {
        CV_Assert((scn == 3 || scn == 4) && depth == CV_8U );
        bidx = code == COLOR_BGR2BGR565 || code == COLOR_BGR2BGR555 ||
            code == COLOR_BGRA2BGR565 || code == COLOR_BGRA2BGR555 ? 0 : 2;
        int greenbits = code == COLOR_BGR2BGR565 || code == COLOR_RGB2BGR565 ||
            code == COLOR_BGRA2BGR565 || code == COLOR_RGBA2BGR565 ? 6 : 5;
        dcn = 2;
        k.create("RGB2RGB5x5", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=2 -D bidx=%d -D greenbits=%d", bidx, greenbits));
        break;
    }
    case COLOR_BGR5652GRAY: case COLOR_BGR5552GRAY:
    {
        CV_Assert(scn == 2 && depth == CV_8U);
        dcn = 1;
        int greenbits = code == COLOR_BGR5652GRAY ? 6 : 5;
        k.create("BGR5x52Gray", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=1 -D bidx=0 -D greenbits=%d", greenbits));
        break;
    }
    case COLOR_GRAY2BGR565: case COLOR_GRAY2BGR555:
    {
        CV_Assert(scn == 1 && depth == CV_8U);
        dcn = 2;
        int greenbits = code == COLOR_GRAY2BGR565 ? 6 : 5;
        k.create("Gray2BGR5x5", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=2 -D bidx=0 -D greenbits=%d", greenbits));
        break;
    }
    case COLOR_BGR2GRAY: case COLOR_BGRA2GRAY:
    case COLOR_RGB2GRAY: case COLOR_RGBA2GRAY:
    {
        CV_Assert(scn == 3 || scn == 4);
        bidx = code == COLOR_BGR2GRAY || code == COLOR_BGRA2GRAY ? 0 : 2;
        dcn = 1;
        k.create("RGB2Gray", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=1 -D bidx=%d -D STRIPE_SIZE=%d",
                               bidx, stripeSize));
        globalsize[0] = (src.cols + stripeSize-1)/stripeSize;
        break;
    }
    case COLOR_GRAY2BGR:
    case COLOR_GRAY2BGRA:
    {
        CV_Assert(scn == 1);
        dcn = code == COLOR_GRAY2BGRA ? 4 : 3;
        k.create("Gray2RGB", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D bidx=0 -D dcn=%d", dcn));
        break;
    }
    case COLOR_BGR2YUV:
    case COLOR_RGB2YUV:
    {
        CV_Assert(scn == 3 || scn == 4);
        bidx = code == COLOR_RGB2YUV ? 2 : 0;
        dcn = 3;
        k.create("RGB2YUV", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=3 -D bidx=%d", bidx));
        break;
    }
    case COLOR_YUV2BGR:
    case COLOR_YUV2RGB:
    {
        if(dcn <= 0) dcn = 3;
        CV_Assert(dcn == 3 || dcn == 4);
        bidx = code == COLOR_YUV2RGB ? 2 : 0;
        k.create("YUV2RGB", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d", dcn, bidx));
        break;
    }
    case COLOR_YUV2RGB_NV12: case COLOR_YUV2BGR_NV12: case COLOR_YUV2RGB_NV21: case COLOR_YUV2BGR_NV21:
    case COLOR_YUV2RGBA_NV12: case COLOR_YUV2BGRA_NV12: case COLOR_YUV2RGBA_NV21: case COLOR_YUV2BGRA_NV21:
    {
        CV_Assert( scn == 1 );
        CV_Assert( sz.width % 2 == 0 && sz.height % 3 == 0 && depth == CV_8U );
        dcn  = code == COLOR_YUV2BGRA_NV12 || code == COLOR_YUV2RGBA_NV12 ||
               code == COLOR_YUV2BGRA_NV21 || code == COLOR_YUV2RGBA_NV21 ? 4 : 3;
        bidx = code == COLOR_YUV2BGRA_NV12 || code == COLOR_YUV2BGR_NV12 ||
               code == COLOR_YUV2BGRA_NV21 || code == COLOR_YUV2BGR_NV21 ? 0 : 2;
        uidx = code == COLOR_YUV2RGBA_NV21 || code == COLOR_YUV2RGB_NV21 ||
               code == COLOR_YUV2BGRA_NV21 || code == COLOR_YUV2BGR_NV21 ? 1 : 0;

        dstSz = Size(sz.width, sz.height * 2 / 3);
        globalsize[0] = dstSz.width / 2; globalsize[1] = (dstSz.height/2 + pxPerWIy - 1) / pxPerWIy;
        k.create("YUV2RGB_NVx", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d -D uidx=%d", dcn, bidx, uidx));
        break;
    }
    case COLOR_YUV2BGR_YV12: case COLOR_YUV2RGB_YV12: case COLOR_YUV2BGRA_YV12: case COLOR_YUV2RGBA_YV12:
    case COLOR_YUV2BGR_IYUV: case COLOR_YUV2RGB_IYUV: case COLOR_YUV2BGRA_IYUV: case COLOR_YUV2RGBA_IYUV:
    {
        CV_Assert( scn == 1 );
        CV_Assert( sz.width % 2 == 0 && sz.height % 3 == 0 && depth == CV_8U );
        dcn  = code == COLOR_YUV2BGRA_YV12 || code == COLOR_YUV2RGBA_YV12 ||
               code == COLOR_YUV2BGRA_IYUV || code == COLOR_YUV2RGBA_IYUV ? 4 : 3;
        bidx = code == COLOR_YUV2BGRA_YV12 || code == COLOR_YUV2BGR_YV12 ||
               code == COLOR_YUV2BGRA_IYUV || code == COLOR_YUV2BGR_IYUV ? 0 : 2;
        uidx = code == COLOR_YUV2BGRA_YV12 || code == COLOR_YUV2BGR_YV12 ||
               code == COLOR_YUV2RGBA_YV12 || code == COLOR_YUV2RGB_YV12 ? 1 : 0;

        dstSz = Size(sz.width, sz.height * 2 / 3);
        globalsize[0] = dstSz.width / 2; globalsize[1] = (dstSz.height/2 + pxPerWIy - 1) / pxPerWIy;
        k.create("YUV2RGB_YV12_IYUV", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d -D uidx=%d%s", dcn, bidx, uidx,
                 src.isContinuous() ? " -D SRC_CONT" : ""));
        break;
    }
    case COLOR_YUV2GRAY_420:
    {
        if (dcn <= 0) dcn = 1;

        CV_Assert( dcn == 1 );
        CV_Assert( sz.width % 2 == 0 && sz.height % 3 == 0 && depth == CV_8U );

        dstSz = Size(sz.width, sz.height * 2 / 3);
        _dst.create(dstSz, CV_MAKETYPE(depth, dcn));
        dst = _dst.getUMat();

        src.rowRange(0, dstSz.height).copyTo(dst);
        return true;
    }
    case COLOR_RGB2YUV_YV12: case COLOR_BGR2YUV_YV12: case COLOR_RGBA2YUV_YV12: case COLOR_BGRA2YUV_YV12:
    case COLOR_RGB2YUV_IYUV: case COLOR_BGR2YUV_IYUV: case COLOR_RGBA2YUV_IYUV: case COLOR_BGRA2YUV_IYUV:
    {
        if (dcn <= 0) dcn = 1;
        bidx = code == COLOR_BGRA2YUV_YV12 || code == COLOR_BGR2YUV_YV12 ||
               code == COLOR_BGRA2YUV_IYUV || code == COLOR_BGR2YUV_IYUV ? 0 : 2;
        uidx = code == COLOR_RGBA2YUV_YV12 || code == COLOR_RGB2YUV_YV12 ||
               code == COLOR_BGRA2YUV_YV12 || code == COLOR_BGR2YUV_YV12 ? 1 : 0;

        CV_Assert( (scn == 3 || scn == 4) && depth == CV_8U );
        CV_Assert( dcn == 1 );
        CV_Assert( sz.width % 2 == 0 && sz.height % 2 == 0 );

        dstSz = Size(sz.width, sz.height / 2 * 3);
        _dst.create(dstSz, CV_MAKETYPE(depth, dcn));
        dst = _dst.getUMat();

        if (dev.isIntel() && src.cols % 4 == 0 && src.step % 4 == 0 && src.offset % 4 == 0 &&
            dst.step % 4 == 0 && dst.offset % 4 == 0)
        {
            pxPerWIx = 2;
        }
        globalsize[0] = dstSz.width / (2 * pxPerWIx); globalsize[1] = (dstSz.height/3 + pxPerWIy - 1) / pxPerWIy;

        k.create("RGB2YUV_YV12_IYUV", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d -D uidx=%d -D PIX_PER_WI_X=%d", dcn, bidx, uidx, pxPerWIx));
        k.args(ocl::KernelArg::ReadOnlyNoSize(src), ocl::KernelArg::WriteOnly(dst));
        return k.run(2, globalsize, NULL, false);
    }
    case COLOR_YUV2RGB_UYVY: case COLOR_YUV2BGR_UYVY: case COLOR_YUV2RGBA_UYVY: case COLOR_YUV2BGRA_UYVY:
    case COLOR_YUV2RGB_YUY2: case COLOR_YUV2BGR_YUY2: case COLOR_YUV2RGB_YVYU: case COLOR_YUV2BGR_YVYU:
    case COLOR_YUV2RGBA_YUY2: case COLOR_YUV2BGRA_YUY2: case COLOR_YUV2RGBA_YVYU: case COLOR_YUV2BGRA_YVYU:
    {
        if (dcn <= 0)
            dcn = (code==COLOR_YUV2RGBA_UYVY || code==COLOR_YUV2BGRA_UYVY || code==COLOR_YUV2RGBA_YUY2 ||
                   code==COLOR_YUV2BGRA_YUY2 || code==COLOR_YUV2RGBA_YVYU || code==COLOR_YUV2BGRA_YVYU) ? 4 : 3;

        bidx = (code==COLOR_YUV2BGR_UYVY || code==COLOR_YUV2BGRA_UYVY || code==COLOR_YUV2BGRA_YUY2 ||
                code==COLOR_YUV2BGR_YUY2 || code==COLOR_YUV2BGRA_YVYU || code==COLOR_YUV2BGR_YVYU) ? 0 : 2;
        yidx = (code==COLOR_YUV2RGB_UYVY || code==COLOR_YUV2RGBA_UYVY || code==COLOR_YUV2BGR_UYVY || code==COLOR_YUV2BGRA_UYVY) ? 1 : 0;
        uidx = (code==COLOR_YUV2RGB_YVYU || code==COLOR_YUV2RGBA_YVYU ||
                code==COLOR_YUV2BGR_YVYU || code==COLOR_YUV2BGRA_YVYU) ? 2 : 0;
        uidx = 1 - yidx + uidx;

        CV_Assert( dcn == 3 || dcn == 4 );
        CV_Assert( scn == 2 && depth == CV_8U );

        k.create("YUV2RGB_422", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d -D uidx=%d -D yidx=%d%s", dcn, bidx, uidx, yidx,
                                src.offset % 4 == 0 && src.step % 4 == 0 ? " -D USE_OPTIMIZED_LOAD" : ""));
        break;
    }
    case COLOR_BGR2YCrCb:
    case COLOR_RGB2YCrCb:
    {
        CV_Assert(scn == 3 || scn == 4);
        bidx = code == COLOR_BGR2YCrCb ? 0 : 2;
        dcn = 3;
        k.create("RGB2YCrCb", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=3 -D bidx=%d", bidx));
        break;
    }
    case COLOR_YCrCb2BGR:
    case COLOR_YCrCb2RGB:
    {
        if( dcn <= 0 )
            dcn = 3;
        CV_Assert(scn == 3 && (dcn == 3 || dcn == 4));
        bidx = code == COLOR_YCrCb2BGR ? 0 : 2;
        k.create("YCrCb2RGB", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d", dcn, bidx));
        break;
    }
    case COLOR_BGR2XYZ: case COLOR_RGB2XYZ:
    {
        CV_Assert(scn == 3 || scn == 4);
        bidx = code == COLOR_BGR2XYZ ? 0 : 2;

        UMat c;
        if (depth == CV_32F)
        {
            float coeffs[] =
            {
                0.412453f, 0.357580f, 0.180423f,
                0.212671f, 0.715160f, 0.072169f,
                0.019334f, 0.119193f, 0.950227f
            };
            if (bidx == 0)
            {
                std::swap(coeffs[0], coeffs[2]);
                std::swap(coeffs[3], coeffs[5]);
                std::swap(coeffs[6], coeffs[8]);
            }
            Mat(1, 9, CV_32FC1, &coeffs[0]).copyTo(c);
        }
        else
        {
            int coeffs[] =
            {
                1689,    1465,    739,
                871,     2929,    296,
                79,      488,     3892
            };
            if (bidx == 0)
            {
                std::swap(coeffs[0], coeffs[2]);
                std::swap(coeffs[3], coeffs[5]);
                std::swap(coeffs[6], coeffs[8]);
            }
            Mat(1, 9, CV_32SC1, &coeffs[0]).copyTo(c);
        }

        _dst.create(dstSz, CV_MAKETYPE(depth, 3));
        dst = _dst.getUMat();

        k.create("RGB2XYZ", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=3 -D bidx=%d", bidx));
        if (k.empty())
            return false;
        k.args(ocl::KernelArg::ReadOnlyNoSize(src), ocl::KernelArg::WriteOnly(dst), ocl::KernelArg::PtrReadOnly(c));
        return k.run(2, globalsize, 0, false);
    }
    case COLOR_XYZ2BGR: case COLOR_XYZ2RGB:
    {
        if (dcn <= 0)
            dcn = 3;
        CV_Assert(scn == 3 && (dcn == 3 || dcn == 4));
        bidx = code == COLOR_XYZ2BGR ? 0 : 2;

        UMat c;
        if (depth == CV_32F)
        {
            float coeffs[] =
            {
                3.240479f, -1.53715f, -0.498535f,
                -0.969256f, 1.875991f, 0.041556f,
                0.055648f, -0.204043f, 1.057311f
            };
            if (bidx == 0)
            {
                std::swap(coeffs[0], coeffs[6]);
                std::swap(coeffs[1], coeffs[7]);
                std::swap(coeffs[2], coeffs[8]);
            }
            Mat(1, 9, CV_32FC1, &coeffs[0]).copyTo(c);
        }
        else
        {
            int coeffs[] =
            {
                13273,  -6296,  -2042,
                -3970,   7684,    170,
                  228,   -836,   4331
            };
            if (bidx == 0)
            {
                std::swap(coeffs[0], coeffs[6]);
                std::swap(coeffs[1], coeffs[7]);
                std::swap(coeffs[2], coeffs[8]);
            }
            Mat(1, 9, CV_32SC1, &coeffs[0]).copyTo(c);
        }

        _dst.create(dstSz, CV_MAKETYPE(depth, dcn));
        dst = _dst.getUMat();

        k.create("XYZ2RGB", ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d", dcn, bidx));
        if (k.empty())
            return false;
        k.args(ocl::KernelArg::ReadOnlyNoSize(src), ocl::KernelArg::WriteOnly(dst), ocl::KernelArg::PtrReadOnly(c));
        return k.run(2, globalsize, 0, false);
    }
    case COLOR_BGR2HSV: case COLOR_RGB2HSV: case COLOR_BGR2HSV_FULL: case COLOR_RGB2HSV_FULL:
    case COLOR_BGR2HLS: case COLOR_RGB2HLS: case COLOR_BGR2HLS_FULL: case COLOR_RGB2HLS_FULL:
    {
        CV_Assert((scn == 3 || scn == 4) && (depth == CV_8U || depth == CV_32F));
        bidx = code == COLOR_BGR2HSV || code == COLOR_BGR2HLS ||
            code == COLOR_BGR2HSV_FULL || code == COLOR_BGR2HLS_FULL ? 0 : 2;
        int hrange = depth == CV_32F ? 360 : code == COLOR_BGR2HSV || code == COLOR_RGB2HSV ||
            code == COLOR_BGR2HLS || code == COLOR_RGB2HLS ? 180 : 256;
        bool is_hsv = code == COLOR_BGR2HSV || code == COLOR_RGB2HSV || code == COLOR_BGR2HSV_FULL || code == COLOR_RGB2HSV_FULL;
        String kernelName = String("RGB2") + (is_hsv ? "HSV" : "HLS");
        dcn = 3;

        if (is_hsv && depth == CV_8U)
        {
            static UMat sdiv_data;
            static UMat hdiv_data180;
            static UMat hdiv_data256;
            static int sdiv_table[256];
            static int hdiv_table180[256];
            static int hdiv_table256[256];
            static volatile bool initialized180 = false, initialized256 = false;
            volatile bool & initialized = hrange == 180 ? initialized180 : initialized256;

            if (!initialized)
            {
                int * const hdiv_table = hrange == 180 ? hdiv_table180 : hdiv_table256, hsv_shift = 12;
                UMat & hdiv_data = hrange == 180 ? hdiv_data180 : hdiv_data256;

                sdiv_table[0] = hdiv_table180[0] = hdiv_table256[0] = 0;

                int v = 255 << hsv_shift;
                if (!initialized180 && !initialized256)
                {
                    for(int i = 1; i < 256; i++ )
                        sdiv_table[i] = saturate_cast<int>(v/(1.*i));
                    Mat(1, 256, CV_32SC1, sdiv_table).copyTo(sdiv_data);
                }

                v = hrange << hsv_shift;
                for (int i = 1; i < 256; i++ )
                    hdiv_table[i] = saturate_cast<int>(v/(6.*i));

                Mat(1, 256, CV_32SC1, hdiv_table).copyTo(hdiv_data);
                initialized = true;
            }

            _dst.create(dstSz, CV_8UC3);
            dst = _dst.getUMat();

            k.create("RGB2HSV", ocl::imgproc::cvtcolor_oclsrc,
                     opts + format("-D hrange=%d -D bidx=%d -D dcn=3",
                                   hrange, bidx));
            if (k.empty())
                return false;

            k.args(ocl::KernelArg::ReadOnlyNoSize(src), ocl::KernelArg::WriteOnly(dst),
                   ocl::KernelArg::PtrReadOnly(sdiv_data), hrange == 256 ? ocl::KernelArg::PtrReadOnly(hdiv_data256) :
                                                                       ocl::KernelArg::PtrReadOnly(hdiv_data180));

            return k.run(2, globalsize, NULL, false);
        }
        else
            k.create(kernelName.c_str(), ocl::imgproc::cvtcolor_oclsrc,
                     opts + format("-D hscale=%ff -D bidx=%d -D dcn=3",
                                   hrange*(1.f/360.f), bidx));
        break;
    }
    case COLOR_HSV2BGR: case COLOR_HSV2RGB: case COLOR_HSV2BGR_FULL: case COLOR_HSV2RGB_FULL:
    case COLOR_HLS2BGR: case COLOR_HLS2RGB: case COLOR_HLS2BGR_FULL: case COLOR_HLS2RGB_FULL:
    {
        if (dcn <= 0)
            dcn = 3;
        CV_Assert(scn == 3 && (dcn == 3 || dcn == 4) && (depth == CV_8U || depth == CV_32F));
        bidx = code == COLOR_HSV2BGR || code == COLOR_HLS2BGR ||
            code == COLOR_HSV2BGR_FULL || code == COLOR_HLS2BGR_FULL ? 0 : 2;
        int hrange = depth == CV_32F ? 360 : code == COLOR_HSV2BGR || code == COLOR_HSV2RGB ||
            code == COLOR_HLS2BGR || code == COLOR_HLS2RGB ? 180 : 255;
        bool is_hsv = code == COLOR_HSV2BGR || code == COLOR_HSV2RGB ||
                code == COLOR_HSV2BGR_FULL || code == COLOR_HSV2RGB_FULL;

        String kernelName = String(is_hsv ? "HSV" : "HLS") + "2RGB";
        k.create(kernelName.c_str(), ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d -D hrange=%d -D hscale=%ff",
                               dcn, bidx, hrange, 6.f/hrange));
        break;
    }
    case COLOR_RGBA2mRGBA: case COLOR_mRGBA2RGBA:
    {
        CV_Assert(scn == 4 && depth == CV_8U);
        dcn = 4;

        k.create(code == COLOR_RGBA2mRGBA ? "RGBA2mRGBA" : "mRGBA2RGBA", ocl::imgproc::cvtcolor_oclsrc,
                 opts + "-D dcn=4 -D bidx=3");
        break;
    }
    case CV_BGR2Lab: case CV_RGB2Lab: case CV_LBGR2Lab: case CV_LRGB2Lab:
    case CV_BGR2Luv: case CV_RGB2Luv: case CV_LBGR2Luv: case CV_LRGB2Luv:
    {
        CV_Assert( (scn == 3 || scn == 4) && (depth == CV_8U || depth == CV_32F) );

        bidx = code == CV_BGR2Lab || code == CV_LBGR2Lab || code == CV_BGR2Luv || code == CV_LBGR2Luv ? 0 : 2;
        bool srgb = code == CV_BGR2Lab || code == CV_RGB2Lab || code == CV_RGB2Luv || code == CV_BGR2Luv;
        bool lab = code == CV_BGR2Lab || code == CV_RGB2Lab || code == CV_LBGR2Lab || code == CV_LRGB2Lab;
        float un, vn;
        dcn = 3;

        k.create(format("BGR2%s", lab ? "Lab" : "Luv").c_str(),
                 ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d%s",
                               dcn, bidx, srgb ? " -D SRGB" : ""));
        if (k.empty())
            return false;

        initLabTabs();

        _dst.create(dstSz, CV_MAKETYPE(depth, dcn));
        dst = _dst.getUMat();

        ocl::KernelArg srcarg = ocl::KernelArg::ReadOnlyNoSize(src),
                dstarg = ocl::KernelArg::WriteOnly(dst);

        if (depth == CV_8U && lab)
        {
            static UMat usRGBGammaTab, ulinearGammaTab, uLabCbrtTab, ucoeffs;

            if (srgb && usRGBGammaTab.empty())
                Mat(1, 256, CV_16UC1, sRGBGammaTab_b).copyTo(usRGBGammaTab);
            else if (ulinearGammaTab.empty())
                Mat(1, 256, CV_16UC1, linearGammaTab_b).copyTo(ulinearGammaTab);
            if (uLabCbrtTab.empty())
                Mat(1, LAB_CBRT_TAB_SIZE_B, CV_16UC1, LabCbrtTab_b).copyTo(uLabCbrtTab);

            {
                int coeffs[9];
                const float * const _coeffs = sRGB2XYZ_D65, * const _whitept = D65;
                const float scale[] =
                {
                    (1 << lab_shift)/_whitept[0],
                    (float)(1 << lab_shift),
                    (1 << lab_shift)/_whitept[2]
                };

                for (int i = 0; i < 3; i++ )
                {
                    coeffs[i*3+(bidx^2)] = cvRound(_coeffs[i*3]*scale[i]);
                    coeffs[i*3+1] = cvRound(_coeffs[i*3+1]*scale[i]);
                    coeffs[i*3+bidx] = cvRound(_coeffs[i*3+2]*scale[i]);

                    CV_Assert( coeffs[i] >= 0 && coeffs[i*3+1] >= 0 && coeffs[i*3+2] >= 0 &&
                              coeffs[i*3] + coeffs[i*3+1] + coeffs[i*3+2] < 2*(1 << lab_shift) );
                }
                Mat(1, 9, CV_32SC1, coeffs).copyTo(ucoeffs);
            }

            const int Lscale = (116*255+50)/100;
            const int Lshift = -((16*255*(1 << lab_shift2) + 50)/100);

            k.args(srcarg, dstarg,
                   ocl::KernelArg::PtrReadOnly(srgb ? usRGBGammaTab : ulinearGammaTab),
                   ocl::KernelArg::PtrReadOnly(uLabCbrtTab), ocl::KernelArg::PtrReadOnly(ucoeffs),
                   Lscale, Lshift);
        }
        else
        {
            static UMat usRGBGammaTab, ucoeffs, uLabCbrtTab;

            if (srgb && usRGBGammaTab.empty())
                Mat(1, GAMMA_TAB_SIZE * 4, CV_32FC1, sRGBGammaTab).copyTo(usRGBGammaTab);
            if (!lab && uLabCbrtTab.empty())
                Mat(1, LAB_CBRT_TAB_SIZE * 4, CV_32FC1, LabCbrtTab).copyTo(uLabCbrtTab);

            {
                float coeffs[9];
                const float * const _coeffs = sRGB2XYZ_D65, * const _whitept = D65;
                float scale[] = { 1.0f / _whitept[0], 1.0f, 1.0f / _whitept[2] };

                for (int i = 0; i < 3; i++)
                {
                    int j = i * 3;
                    coeffs[j + (bidx ^ 2)] = _coeffs[j] * (lab ? scale[i] : 1);
                    coeffs[j + 1] = _coeffs[j + 1] * (lab ? scale[i] : 1);
                    coeffs[j + bidx] = _coeffs[j + 2] * (lab ? scale[i] : 1);

                    CV_Assert( coeffs[j] >= 0 && coeffs[j + 1] >= 0 && coeffs[j + 2] >= 0 &&
                               coeffs[j] + coeffs[j + 1] + coeffs[j + 2] < 1.5f*(lab ? LabCbrtTabScale : 1) );
                }

                float d = 1.f/(_whitept[0] + _whitept[1]*15 + _whitept[2]*3);
                un = 13*4*_whitept[0]*d;
                vn = 13*9*_whitept[1]*d;

                Mat(1, 9, CV_32FC1, coeffs).copyTo(ucoeffs);
            }

            float _1_3 = 1.0f / 3.0f, _a = 16.0f / 116.0f;
            ocl::KernelArg ucoeffsarg = ocl::KernelArg::PtrReadOnly(ucoeffs);

            if (lab)
            {
                if (srgb)
                    k.args(srcarg, dstarg, ocl::KernelArg::PtrReadOnly(usRGBGammaTab),
                           ucoeffsarg, _1_3, _a);
                else
                    k.args(srcarg, dstarg, ucoeffsarg, _1_3, _a);
            }
            else
            {
                ocl::KernelArg LabCbrtTabarg = ocl::KernelArg::PtrReadOnly(uLabCbrtTab);
                if (srgb)
                    k.args(srcarg, dstarg, ocl::KernelArg::PtrReadOnly(usRGBGammaTab),
                           LabCbrtTabarg, ucoeffsarg, un, vn);
                else
                    k.args(srcarg, dstarg, LabCbrtTabarg, ucoeffsarg, un, vn);
            }
        }

        return k.run(dims, globalsize, NULL, false);
    }
    case CV_Lab2BGR: case CV_Lab2RGB: case CV_Lab2LBGR: case CV_Lab2LRGB:
    case CV_Luv2BGR: case CV_Luv2RGB: case CV_Luv2LBGR: case CV_Luv2LRGB:
    {
        if( dcn <= 0 )
            dcn = 3;
        CV_Assert( scn == 3 && (dcn == 3 || dcn == 4) && (depth == CV_8U || depth == CV_32F) );

        bidx = code == CV_Lab2BGR || code == CV_Lab2LBGR || code == CV_Luv2BGR || code == CV_Luv2LBGR ? 0 : 2;
        bool srgb = code == CV_Lab2BGR || code == CV_Lab2RGB || code == CV_Luv2BGR || code == CV_Luv2RGB;
        bool lab = code == CV_Lab2BGR || code == CV_Lab2RGB || code == CV_Lab2LBGR || code == CV_Lab2LRGB;
        float un, vn;

        k.create(format("%s2BGR", lab ? "Lab" : "Luv").c_str(),
                 ocl::imgproc::cvtcolor_oclsrc,
                 opts + format("-D dcn=%d -D bidx=%d%s",
                               dcn, bidx, srgb ? " -D SRGB" : ""));
        if (k.empty())
            return false;

        initLabTabs();
        static UMat ucoeffs, usRGBInvGammaTab;

        if (srgb && usRGBInvGammaTab.empty())
            Mat(1, GAMMA_TAB_SIZE*4, CV_32FC1, sRGBInvGammaTab).copyTo(usRGBInvGammaTab);

        {
            float coeffs[9];
            const float * const _coeffs = XYZ2sRGB_D65, * const _whitept = D65;

            for( int i = 0; i < 3; i++ )
            {
                coeffs[i+(bidx^2)*3] = _coeffs[i] * (lab ? _whitept[i] : 1);
                coeffs[i+3] = _coeffs[i+3] * (lab ? _whitept[i] : 1);
                coeffs[i+bidx*3] = _coeffs[i+6] * (lab ? _whitept[i] : 1);
            }

            float d = 1.f/(_whitept[0] + _whitept[1]*15 + _whitept[2]*3);
            un = 4*_whitept[0]*d;
            vn = 9*_whitept[1]*d;

            Mat(1, 9, CV_32FC1, coeffs).copyTo(ucoeffs);
        }

        _dst.create(sz, CV_MAKETYPE(depth, dcn));
        dst = _dst.getUMat();

        float lThresh = 0.008856f * 903.3f;
        float fThresh = 7.787f * 0.008856f + 16.0f / 116.0f;

        ocl::KernelArg srcarg = ocl::KernelArg::ReadOnlyNoSize(src),
                dstarg = ocl::KernelArg::WriteOnly(dst),
                coeffsarg = ocl::KernelArg::PtrReadOnly(ucoeffs);

        if (lab)
        {
            if (srgb)
                k.args(srcarg, dstarg, ocl::KernelArg::PtrReadOnly(usRGBInvGammaTab),
                       coeffsarg, lThresh, fThresh);
            else
                k.args(srcarg, dstarg, coeffsarg, lThresh, fThresh);
        }
        else
        {
            if (srgb)
                k.args(srcarg, dstarg, ocl::KernelArg::PtrReadOnly(usRGBInvGammaTab),
                       coeffsarg, un, vn);
            else
                k.args(srcarg, dstarg, coeffsarg, un, vn);
        }

        return k.run(dims, globalsize, NULL, false);
    }
    default:
        break;
    }

    if( !k.empty() )
    {
        _dst.create(dstSz, CV_MAKETYPE(depth, dcn));
        dst = _dst.getUMat();
        k.args(ocl::KernelArg::ReadOnlyNoSize(src), ocl::KernelArg::WriteOnly(dst));
        ok = k.run(dims, globalsize, NULL, false);
    }
    return ok;
}

#endif

}

//
// HAL functions
//

namespace cv {
namespace hal {

// 8u, 16u, 32f
void cvtBGRtoBGR(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int scn, int dcn, bool swapBlue)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoBGR, cv_hal_cvtBGRtoBGR, src_data, src_step, dst_data, dst_step, width, height, depth, scn, dcn, swapBlue);

#if defined(HAVE_IPP) && IPP_VERSION_X100 >= 700
    CV_IPP_CHECK()
    {
    if(scn == 3 && dcn == 4 && !swapBlue)
    {
        if ( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                             IPPReorderFunctor(ippiSwapChannelsC3C4RTab[depth], 0, 1, 2)) )
            return;
    }
    else if(scn == 4 && dcn == 3 && !swapBlue)
    {
        if ( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                             IPPGeneralFunctor(ippiCopyAC4C3RTab[depth])) )
            return;
    }
    else if(scn == 3 && dcn == 4 && swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPReorderFunctor(ippiSwapChannelsC3C4RTab[depth], 2, 1, 0)) )
            return;
    }
    else if(scn == 4 && dcn == 3 && swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPReorderFunctor(ippiSwapChannelsC4C3RTab[depth], 2, 1, 0)) )
            return;
    }
    else if(scn == 3 && dcn == 3 && swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, scn), dst_data, dst_step, width, height,
                                IPPReorderFunctor(ippiSwapChannelsC3RTab[depth], 2, 1, 0)) )
            return;
    }
#if IPP_VERSION_X100 >= 810
    else if(scn == 4 && dcn == 4 && swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, scn), dst_data, dst_step, width, height,
                                IPPReorderFunctor(ippiSwapChannelsC4RTab[depth], 2, 1, 0)) )
            return;
    }
    }
#endif
#endif

    int blueIdx = swapBlue ? 2 : 0;
    if( depth == CV_8U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2RGB<uchar>(scn, dcn, blueIdx));
    else if( depth == CV_16U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2RGB<ushort>(scn, dcn, blueIdx));
    else
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2RGB<float>(scn, dcn, blueIdx));
}

// only 8u
void cvtBGRtoBGR5x5(const uchar * src_data, size_t src_step,
                    uchar * dst_data, size_t dst_step,
                    int width, int height,
                    int scn, bool swapBlue, int greenBits)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoBGR5x5, cv_hal_cvtBGRtoBGR5x5, src_data, src_step, dst_data, dst_step, width, height, scn, swapBlue, greenBits);

#if defined(HAVE_IPP) && IPP_DISABLE_BLOCK // breaks OCL accuracy tests
    CV_IPP_CHECK()
    {
    CV_SUPPRESS_DEPRECATED_START;
    if (scn == 3 && greenBits == 6 && !swapBlue)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralFunctor((ippiGeneralFunc)ippiBGRToBGR565_8u16u_C3R)))
            return;
    }
    else if (scn == 4 && greenBits == 6 && !swapBlue)
    {
        if (CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(CV_8U, scn), dst_data, dst_step, width, height,
                                IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[CV_8U],
                                                         (ippiGeneralFunc)ippiBGRToBGR565_8u16u_C3R, 0, 1, 2, CV_8U)))
            return;
    }
    else if (scn == 3 && greenBits == 6 && swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(CV_8U, scn), dst_data, dst_step, width, height,
                                IPPReorderGeneralFunctor(ippiSwapChannelsC3RTab[CV_8U],
                                                         (ippiGeneralFunc)ippiBGRToBGR565_8u16u_C3R, 2, 1, 0, CV_8U)) )
            return;
    }
    else if (scn == 4 && greenBits == 6 && swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(CV_8U, scn), dst_data, dst_step, width, height,
                                IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[CV_8U],
                                                         (ippiGeneralFunc)ippiBGRToBGR565_8u16u_C3R, 2, 1, 0, CV_8U)) )
            return;
    }
    CV_SUPPRESS_DEPRECATED_END;
    }
#endif

    CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2RGB5x5(scn, swapBlue ? 2 : 0, greenBits));
}

// only 8u
void cvtBGR5x5toBGR(const uchar * src_data, size_t src_step,
                    uchar * dst_data, size_t dst_step,
                    int width, int height,
                    int dcn, bool swapBlue, int greenBits)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGR5x5toBGR, cv_hal_cvtBGR5x5toBGR, src_data, src_step, dst_data, dst_step, width, height, dcn, swapBlue, greenBits);

#if defined(HAVE_IPP) && IPP_VERSION_X100 < 900
    CV_IPP_CHECK()
    {
    CV_SUPPRESS_DEPRECATED_START;
    if (dcn == 3 && greenBits == 6 && !swapBlue)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralFunctor((ippiGeneralFunc)ippiBGR565ToBGR_16u8u_C3R)))
            return;
    }
    else if (dcn == 3 && greenBits == 6 && swapBlue)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor((ippiGeneralFunc)ippiBGR565ToBGR_16u8u_C3R,
                                                     ippiSwapChannelsC3RTab[CV_8U], 2, 1, 0, CV_8U)))
            return;
    }
    else if (dcn == 4 && greenBits == 6 && !swapBlue)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor((ippiGeneralFunc)ippiBGR565ToBGR_16u8u_C3R,
                                                     ippiSwapChannelsC3C4RTab[CV_8U], 0, 1, 2, CV_8U)))
            return;
    }
    else if (dcn == 4 && greenBits == 6 && swapBlue)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor((ippiGeneralFunc)ippiBGR565ToBGR_16u8u_C3R,
                                                     ippiSwapChannelsC3C4RTab[CV_8U], 2, 1, 0, CV_8U)))
            return;
    }
    CV_SUPPRESS_DEPRECATED_END;
    }
#endif

    CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB5x52RGB(dcn, swapBlue ? 2 : 0, greenBits));
}

// 8u, 16u, 32f
void cvtBGRtoGray(const uchar * src_data, size_t src_step,
                  uchar * dst_data, size_t dst_step,
                  int width, int height,
                  int depth, int scn, bool swapBlue)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoGray, cv_hal_cvtBGRtoGray, src_data, src_step, dst_data, dst_step, width, height, depth, scn, swapBlue);

#if defined(HAVE_IPP) && IPP_VERSION_X100 >= 700
    CV_IPP_CHECK()
    {
    if(depth == CV_32F && scn == 3 && !swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPColor2GrayFunctor(ippiColor2GrayC3Tab[depth])) )
            return;
    }
    else if(depth == CV_32F && scn == 3 && swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralFunctor(ippiRGB2GrayC3Tab[depth])) )
            return;
    }
    else if(depth == CV_32F && scn == 4 && !swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPColor2GrayFunctor(ippiColor2GrayC4Tab[depth])) )
            return;
    }
    else if(depth == CV_32F && scn == 4 && swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralFunctor(ippiRGB2GrayC4Tab[depth])) )
            return;
    }
    }
#endif

    int blueIdx = swapBlue ? 2 : 0;
    if( depth == CV_8U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2Gray<uchar>(scn, blueIdx, 0));
    else if( depth == CV_16U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2Gray<ushort>(scn, blueIdx, 0));
    else
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2Gray<float>(scn, blueIdx, 0));
}

// 8u, 16u, 32f
void cvtGraytoBGR(const uchar * src_data, size_t src_step,
                  uchar * dst_data, size_t dst_step,
                  int width, int height,
                  int depth, int dcn)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtGraytoBGR, cv_hal_cvtGraytoBGR, src_data, src_step, dst_data, dst_step, width, height, depth, dcn);

#if defined(HAVE_IPP) && IPP_VERSION_X100 >= 700
    CV_IPP_CHECK()
    {
    if(dcn == 3)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGray2BGRFunctor(ippiCopyP3C3RTab[depth])) )
            return;
    }
    else if(dcn == 4)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGray2BGRAFunctor(ippiCopyP3C3RTab[depth], ippiSwapChannelsC3C4RTab[depth], depth)) )
            return;
    }
    }
#endif

    if( depth == CV_8U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Gray2RGB<uchar>(dcn));
    else if( depth == CV_16U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Gray2RGB<ushort>(dcn));
    else
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Gray2RGB<float>(dcn));
}

// only 8u
void cvtBGR5x5toGray(const uchar * src_data, size_t src_step,
                     uchar * dst_data, size_t dst_step,
                     int width, int height,
                     int greenBits)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGR5x5toGray, cv_hal_cvtBGR5x5toGray, src_data, src_step, dst_data, dst_step, width, height, greenBits);
    CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB5x52Gray(greenBits));
}

// only 8u
void cvtGraytoBGR5x5(const uchar * src_data, size_t src_step,
                     uchar * dst_data, size_t dst_step,
                     int width, int height,
                     int greenBits)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtGraytoBGR5x5, cv_hal_cvtGraytoBGR5x5, src_data, src_step, dst_data, dst_step, width, height, greenBits);
    CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Gray2RGB5x5(greenBits));
}

// 8u, 16u, 32f
void cvtBGRtoYUV(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int scn, bool swapBlue, bool isCbCr)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoYUV, cv_hal_cvtBGRtoYUV, src_data, src_step, dst_data, dst_step, width, height, depth, scn, swapBlue, isCbCr);

#if defined(HAVE_IPP) && IPP_DISABLE_BLOCK
    CV_IPP_CHECK()
    {
    if (scn == 3 && depth == CV_8U && swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralFunctor((ippiGeneralFunc)ippiRGBToYUV_8u_C3R)))
            return;
    }
    else if (scn == 3 && depth == CV_8U && !swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPReorderGeneralFunctor(ippiSwapChannelsC3RTab[depth],
                                                     (ippiGeneralFunc)ippiRGBToYUV_8u_C3R, 2, 1, 0, depth)))
            return;
    }
    else if (scn == 4 && depth == CV_8U && swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth],
                                                     (ippiGeneralFunc)ippiRGBToYUV_8u_C3R, 0, 1, 2, depth)))
            return;
    }
    else if (scn == 4 && depth == CV_8U && !swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth],
                                                     (ippiGeneralFunc)ippiRGBToYUV_8u_C3R, 2, 1, 0, depth)))
            return;
    }
    }
#endif

    int blueIdx = swapBlue ? 2 : 0;
    if( depth == CV_8U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2YCrCb_i<uchar>(scn, blueIdx, isCbCr));
    else if( depth == CV_16U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2YCrCb_i<ushort>(scn, blueIdx, isCbCr));
    else
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2YCrCb_f<float>(scn, blueIdx, isCbCr));
}

void cvtYUVtoBGR(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int dcn, bool swapBlue, bool isCbCr)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtYUVtoBGR, cv_hal_cvtYUVtoBGR, src_data, src_step, dst_data, dst_step, width, height, depth, dcn, swapBlue, isCbCr);


#if defined(HAVE_IPP) && IPP_DISABLE_BLOCK
    CV_IPP_CHECK()
    {
    if (dcn == 3 && depth == CV_8U && swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralFunctor((ippiGeneralFunc)ippiYUVToRGB_8u_C3R)))
            return;
    }
    else if (dcn == 3 && depth == CV_8U && !swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor((ippiGeneralFunc)ippiYUVToRGB_8u_C3R,
                                                               ippiSwapChannelsC3RTab[depth], 2, 1, 0, depth)))
            return;
    }
    else if (dcn == 4 && depth == CV_8U && swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor((ippiGeneralFunc)ippiYUVToRGB_8u_C3R,
                                                               ippiSwapChannelsC3C4RTab[depth], 0, 1, 2, depth)))
            return;
    }
    else if (dcn == 4 && depth == CV_8U && !swapBlue && !isCbCr)
    {
        if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor((ippiGeneralFunc)ippiYUVToRGB_8u_C3R,
                                                               ippiSwapChannelsC3C4RTab[depth], 2, 1, 0, depth)))
            return;
    }
    }
#endif

    int blueIdx = swapBlue ? 2 : 0;
    if( depth == CV_8U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, YCrCb2RGB_i<uchar>(dcn, blueIdx, isCbCr));
    else if( depth == CV_16U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, YCrCb2RGB_i<ushort>(dcn, blueIdx, isCbCr));
    else
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, YCrCb2RGB_f<float>(dcn, blueIdx, isCbCr));
}

void cvtBGRtoXYZ(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int scn, bool swapBlue)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoXYZ, cv_hal_cvtBGRtoXYZ, src_data, src_step, dst_data, dst_step, width, height, depth, scn, swapBlue);

#if defined(HAVE_IPP) && IPP_VERSION_X100 >= 700
    CV_IPP_CHECK()
    {
    if(scn == 3 && depth != CV_32F && !swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, scn), dst_data, dst_step, width, height,
                                IPPReorderGeneralFunctor(ippiSwapChannelsC3RTab[depth], ippiRGB2XYZTab[depth], 2, 1, 0, depth)) )
            return;
    }
    else if(scn == 4 && depth != CV_32F && !swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth], ippiRGB2XYZTab[depth], 2, 1, 0, depth)) )
            return;
    }
    else if(scn == 3 && depth != CV_32F && swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, scn), dst_data, dst_step, width, height,
                                IPPGeneralFunctor(ippiRGB2XYZTab[depth])) )
            return;
    }
    else if(scn == 4 && depth != CV_32F && swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth], ippiRGB2XYZTab[depth], 0, 1, 2, depth)) )
            return;
    }
    }
#endif

    int blueIdx = swapBlue ? 2 : 0;
    if( depth == CV_8U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2XYZ_i<uchar>(scn, blueIdx, 0));
    else if( depth == CV_16U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2XYZ_i<ushort>(scn, blueIdx, 0));
    else
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2XYZ_f<float>(scn, blueIdx, 0));
}

void cvtXYZtoBGR(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int dcn, bool swapBlue)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtXYZtoBGR, cv_hal_cvtXYZtoBGR, src_data, src_step, dst_data, dst_step, width, height, depth, dcn, swapBlue);

#if defined(HAVE_IPP) && IPP_VERSION_X100 >= 700
    CV_IPP_CHECK()
    {
    if(dcn == 3 && depth != CV_32F && !swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, 3), dst_data, dst_step, width, height,
                                IPPGeneralReorderFunctor(ippiXYZ2RGBTab[depth], ippiSwapChannelsC3RTab[depth], 2, 1, 0, depth)) )
            return;
    }
    else if(dcn == 4 && depth != CV_32F && !swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor(ippiXYZ2RGBTab[depth], ippiSwapChannelsC3C4RTab[depth], 2, 1, 0, depth)) )
            return;
    }
    if(dcn == 3 && depth != CV_32F && swapBlue)
    {
        if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, 3), dst_data, dst_step, width, height,
                                IPPGeneralFunctor(ippiXYZ2RGBTab[depth])) )
            return;
    }
    else if(dcn == 4 && depth != CV_32F && swapBlue)
    {
        if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                            IPPGeneralReorderFunctor(ippiXYZ2RGBTab[depth], ippiSwapChannelsC3C4RTab[depth], 0, 1, 2, depth)) )
            return;
    }
    }
#endif

    int blueIdx = swapBlue ? 2 : 0;
    if( depth == CV_8U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, XYZ2RGB_i<uchar>(dcn, blueIdx, 0));
    else if( depth == CV_16U )
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, XYZ2RGB_i<ushort>(dcn, blueIdx, 0));
    else
        CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, XYZ2RGB_f<float>(dcn, blueIdx, 0));
}

// 8u, 32f
void cvtBGRtoHSV(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int scn, bool swapBlue, bool isFullRange, bool isHSV)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoHSV, cv_hal_cvtBGRtoHSV, src_data, src_step, dst_data, dst_step, width, height, depth, scn, swapBlue, isFullRange, isHSV);

#if defined(HAVE_IPP) && IPP_VERSION_X100 >= 700
    CV_IPP_CHECK()
    {
    if (depth == CV_8U && isFullRange)
    {
        if (isHSV)
        {
#if IPP_DISABLE_BLOCK // breaks OCL accuracy tests
            if(scn == 3 && !swapBlue)
            {
                if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKE_TYPE(depth, scn), dst_data, dst_step, width, height,
                                        IPPReorderGeneralFunctor(ippiSwapChannelsC3RTab[depth], ippiRGB2HSVTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(scn == 4 && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth], ippiRGB2HSVTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(scn == 4 && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth], ippiRGB2HSVTab[depth], 0, 1, 2, depth)) )
                    return;
            }
#endif
        }
        else
        {
            if(scn == 3 && !swapBlue)
            {
                if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKE_TYPE(depth, scn), dst_data, dst_step, width, height,
                                        IPPReorderGeneralFunctor(ippiSwapChannelsC3RTab[depth], ippiRGB2HLSTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(scn == 4 && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth], ippiRGB2HLSTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(scn == 3 && swapBlue)
            {
                if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKE_TYPE(depth, scn), dst_data, dst_step, width, height,
                                        IPPGeneralFunctor(ippiRGB2HLSTab[depth])) )
                    return;
            }
            else if(scn == 4 && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth], ippiRGB2HLSTab[depth], 0, 1, 2, depth)) )
                    return;
            }
        }
    }
    }
#endif

    int hrange = depth == CV_32F ? 360 : isFullRange ? 256 : 180;
    int blueIdx = swapBlue ? 2 : 0;
    if(isHSV)
    {
        if(depth == CV_8U)
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2HSV_b(scn, blueIdx, hrange));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2HSV_f(scn, blueIdx, static_cast<float>(hrange)));
    }
    else
    {
        if( depth == CV_8U )
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2HLS_b(scn, blueIdx, hrange));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2HLS_f(scn, blueIdx, static_cast<float>(hrange)));
    }
}

// 8u, 32f
void cvtHSVtoBGR(const uchar * src_data, size_t src_step,
                        uchar * dst_data, size_t dst_step,
                        int width, int height,
                        int depth, int dcn, bool swapBlue, bool isFullRange, bool isHSV)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtHSVtoBGR, cv_hal_cvtHSVtoBGR, src_data, src_step, dst_data, dst_step, width, height, depth, dcn, swapBlue, isFullRange, isHSV);

#if defined(HAVE_IPP) && IPP_VERSION_X100 >= 700
    CV_IPP_CHECK()
    {
    if (depth == CV_8U && isFullRange)
    {
        if (isHSV)
        {
            if(dcn == 3 && !swapBlue)
            {
                if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, 3), dst_data, dst_step, width, height,
                                        IPPGeneralReorderFunctor(ippiHSV2RGBTab[depth], ippiSwapChannelsC3RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(dcn == 4 && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPGeneralReorderFunctor(ippiHSV2RGBTab[depth], ippiSwapChannelsC3C4RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(dcn == 3 && swapBlue)
            {
                if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, 3), dst_data, dst_step, width, height,
                                        IPPGeneralFunctor(ippiHSV2RGBTab[depth])) )
                    return;
            }
            else if(dcn == 4 && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPGeneralReorderFunctor(ippiHSV2RGBTab[depth], ippiSwapChannelsC3C4RTab[depth], 0, 1, 2, depth)) )
                    return;
            }
        }
        else
        {
            if(dcn == 3 && !swapBlue)
            {
                if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, 3), dst_data, dst_step, width, height,
                                        IPPGeneralReorderFunctor(ippiHLS2RGBTab[depth], ippiSwapChannelsC3RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(dcn == 4 && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPGeneralReorderFunctor(ippiHLS2RGBTab[depth], ippiSwapChannelsC3C4RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if(dcn == 3 && swapBlue)
            {
                if( CvtColorIPPLoopCopy(src_data, src_step, CV_MAKETYPE(depth, 3), dst_data, dst_step, width, height,
                                        IPPGeneralFunctor(ippiHLS2RGBTab[depth])) )
                    return;
            }
            else if(dcn == 4 && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                                    IPPGeneralReorderFunctor(ippiHLS2RGBTab[depth], ippiSwapChannelsC3C4RTab[depth], 0, 1, 2, depth)) )
                    return;
            }
        }
    }
    }
#endif

    int hrange = depth == CV_32F ? 360 : isFullRange ? 255 : 180;
    int blueIdx = swapBlue ? 2 : 0;
    if(isHSV)
    {
        if( depth == CV_8U )
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, HSV2RGB_b(dcn, blueIdx, hrange));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, HSV2RGB_f(dcn, blueIdx, static_cast<float>(hrange)));
    }
    else
    {
        if( depth == CV_8U )
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, HLS2RGB_b(dcn, blueIdx, hrange));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, HLS2RGB_f(dcn, blueIdx, static_cast<float>(hrange)));
    }
}

// 8u, 32f
void cvtBGRtoLab(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int scn, bool swapBlue, bool isLab, bool srgb)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoLab, cv_hal_cvtBGRtoLab, src_data, src_step, dst_data, dst_step, width, height, depth, scn, swapBlue, isLab, srgb);

#if defined(HAVE_IPP) && IPP_DISABLE_BLOCK
    CV_IPP_CHECK()
    {
    if (!srgb)
    {
        if (isLab)
        {
            if (scn == 3 && depth == CV_8U && !swapBlue)
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralFunctor((ippiGeneralFunc)ippiBGRToLab_8u_C3R)))
                    return;
            }
            else if (scn == 4 && depth == CV_8U && !swapBlue)
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth],
                                                             (ippiGeneralFunc)ippiBGRToLab_8u_C3R, 0, 1, 2, depth)))
                    return;
            }
            else if (scn == 3 && depth == CV_8U && swapBlue) // slower than OpenCV
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC3RTab[depth],
                                                             (ippiGeneralFunc)ippiBGRToLab_8u_C3R, 2, 1, 0, depth)))
                    return;
            }
            else if (scn == 4 && depth == CV_8U && swapBlue) // slower than OpenCV
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth],
                                                             (ippiGeneralFunc)ippiBGRToLab_8u_C3R, 2, 1, 0, depth)))
                    return;
            }
        }
        else
        {
            if (scn == 3 && swapBlue)
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralFunctor(ippiRGBToLUVTab[depth])))
                    return;
            }
            else if (scn == 4 && swapBlue)
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth],
                                                             ippiRGBToLUVTab[depth], 0, 1, 2, depth)))
                    return;
            }
            else if (scn == 3 && !swapBlue)
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC3RTab[depth],
                                                             ippiRGBToLUVTab[depth], 2, 1, 0, depth)))
                    return;
            }
            else if (scn == 4 && !swapBlue)
            {
                if (CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPReorderGeneralFunctor(ippiSwapChannelsC4C3RTab[depth],
                                                             ippiRGBToLUVTab[depth], 2, 1, 0, depth)))
                    return;
            }
        }
    }
    }
#endif


    int blueIdx = swapBlue ? 2 : 0;
    if(isLab)
    {
        if( depth == CV_8U )
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2Lab_b(scn, blueIdx, 0, 0, srgb));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2Lab_f(scn, blueIdx, 0, 0, srgb));
    }
    else
    {
        if( depth == CV_8U )
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2Luv_b(scn, blueIdx, 0, 0, srgb));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGB2Luv_f(scn, blueIdx, 0, 0, srgb));
    }
}

// 8u, 32f
void cvtLabtoBGR(const uchar * src_data, size_t src_step,
                 uchar * dst_data, size_t dst_step,
                 int width, int height,
                 int depth, int dcn, bool swapBlue, bool isLab, bool srgb)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtLabtoBGR, cv_hal_cvtLabtoBGR, src_data, src_step, dst_data, dst_step, width, height, depth, dcn, swapBlue, isLab, srgb);

#if defined(HAVE_IPP) && IPP_DISABLE_BLOCK
    CV_IPP_CHECK()
    {
    if (!srgb)
    {
        if (isLab)
        {
            if( dcn == 3 && depth == CV_8U && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralFunctor((ippiGeneralFunc)ippiLabToBGR_8u_C3R)) )
                    return;
            }
            else if( dcn == 4 && depth == CV_8U && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralReorderFunctor((ippiGeneralFunc)ippiLabToBGR_8u_C3R,
                                                             ippiSwapChannelsC3C4RTab[depth], 0, 1, 2, depth)) )
                    return;
            }
            if( dcn == 3 && depth == CV_8U && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralReorderFunctor((ippiGeneralFunc)ippiLabToBGR_8u_C3R,
                                                             ippiSwapChannelsC3RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if( dcn == 4 && depth == CV_8U && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralReorderFunctor((ippiGeneralFunc)ippiLabToBGR_8u_C3R,
                                                             ippiSwapChannelsC3C4RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
        }
        else
        {
            if( dcn == 3 && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralFunctor(ippiLUVToRGBTab[depth])) )
                    return;
            }
            else if( dcn == 4 && swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralReorderFunctor(ippiLUVToRGBTab[depth],
                                                             ippiSwapChannelsC3C4RTab[depth], 0, 1, 2, depth)) )
                    return;
            }
            if( dcn == 3 && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralReorderFunctor(ippiLUVToRGBTab[depth],
                                                             ippiSwapChannelsC3RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
            else if( dcn == 4 && !swapBlue)
            {
                if( CvtColorIPPLoop(src_data, src_step, dst_data,dst_step, width, height,
                                    IPPGeneralReorderFunctor(ippiLUVToRGBTab[depth],
                                                             ippiSwapChannelsC3C4RTab[depth], 2, 1, 0, depth)) )
                    return;
            }
        }
    }
    }
#endif

    int blueIdx = swapBlue ? 2 : 0;
    if(isLab)
    {
        if( depth == CV_8U )
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Lab2RGB_b(dcn, blueIdx, 0, 0, srgb));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Lab2RGB_f(dcn, blueIdx, 0, 0, srgb));
    }
    else
    {
        if( depth == CV_8U )
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Luv2RGB_b(dcn, blueIdx, 0, 0, srgb));
        else
            CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, Luv2RGB_f(dcn, blueIdx, 0, 0, srgb));
    }
}

void cvtTwoPlaneYUVtoBGR(const uchar * src_data, size_t src_step,
                                uchar * dst_data, size_t dst_step,
                                int dst_width, int dst_height,
                                int dcn, bool swapBlue, int uIdx)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtTwoPlaneYUVtoBGR, cv_hal_cvtTwoPlaneYUVtoBGR, src_data, src_step, dst_data, dst_step, dst_width, dst_height, dcn, swapBlue, uIdx);
    int blueIdx = swapBlue ? 2 : 0;
    const uchar* uv = src_data + src_step * static_cast<size_t>(dst_height);
    switch(dcn*100 + blueIdx * 10 + uIdx)
    {
    case 300: cvtYUV420sp2RGB<0, 0> (dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    case 301: cvtYUV420sp2RGB<0, 1> (dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    case 320: cvtYUV420sp2RGB<2, 0> (dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    case 321: cvtYUV420sp2RGB<2, 1> (dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    case 400: cvtYUV420sp2RGBA<0, 0>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    case 401: cvtYUV420sp2RGBA<0, 1>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    case 420: cvtYUV420sp2RGBA<2, 0>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    case 421: cvtYUV420sp2RGBA<2, 1>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, uv); break;
    default: CV_Error( CV_StsBadFlag, "Unknown/unsupported color conversion code" ); break;
    };
}

void cvtThreePlaneYUVtoBGR(const uchar * src_data, size_t src_step,
                                  uchar * dst_data, size_t dst_step,
                                  int dst_width, int dst_height,
                                  int dcn, bool swapBlue, int uIdx)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtThreePlaneYUVtoBGR, cv_hal_cvtThreePlaneYUVtoBGR, src_data, src_step, dst_data, dst_step, dst_width, dst_height, dcn, swapBlue, uIdx);
    const uchar* u = src_data + src_step * static_cast<size_t>(dst_height);
    const uchar* v = src_data + src_step * static_cast<size_t>(dst_height + dst_height/4) + (dst_width/2) * ((dst_height % 4)/2);

    int ustepIdx = 0;
    int vstepIdx = dst_height % 4 == 2 ? 1 : 0;

    if(uIdx == 1) { std::swap(u ,v), std::swap(ustepIdx, vstepIdx); }
    int blueIdx = swapBlue ? 2 : 0;

    switch(dcn*10 + blueIdx)
    {
    case 30: cvtYUV420p2RGB<0>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, u, v, ustepIdx, vstepIdx); break;
    case 32: cvtYUV420p2RGB<2>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, u, v, ustepIdx, vstepIdx); break;
    case 40: cvtYUV420p2RGBA<0>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, u, v, ustepIdx, vstepIdx); break;
    case 42: cvtYUV420p2RGBA<2>(dst_data, dst_step, dst_width, dst_height, src_step, src_data, u, v, ustepIdx, vstepIdx); break;
    default: CV_Error( CV_StsBadFlag, "Unknown/unsupported color conversion code" ); break;
    };
}

void cvtBGRtoThreePlaneYUV(const uchar * src_data, size_t src_step,
                           uchar * dst_data, size_t dst_step,
                           int width, int height,
                           int scn, bool swapBlue, int uIdx)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtBGRtoThreePlaneYUV, cv_hal_cvtBGRtoThreePlaneYUV, src_data, src_step, dst_data, dst_step, width, height, scn, swapBlue, uIdx);
    int blueIdx = swapBlue ? 2 : 0;
    switch(blueIdx + uIdx*10)
    {
    case 10: cvtRGBtoYUV420p<0, 1>(src_data, src_step, dst_data, dst_step, width, height, scn); break;
    case 12: cvtRGBtoYUV420p<2, 1>(src_data, src_step, dst_data, dst_step, width, height, scn); break;
    case 20: cvtRGBtoYUV420p<0, 2>(src_data, src_step, dst_data, dst_step, width, height, scn); break;
    case 22: cvtRGBtoYUV420p<2, 2>(src_data, src_step, dst_data, dst_step, width, height, scn); break;
    default: CV_Error( CV_StsBadFlag, "Unknown/unsupported color conversion code" ); break;
    };
}

void cvtOnePlaneYUVtoBGR(const uchar * src_data, size_t src_step,
                         uchar * dst_data, size_t dst_step,
                         int width, int height,
                         int dcn, bool swapBlue, int uIdx, int ycn)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtOnePlaneYUVtoBGR, cv_hal_cvtOnePlaneYUVtoBGR, src_data, src_step, dst_data, dst_step, width, height, dcn, swapBlue, uIdx, ycn);
    int blueIdx = swapBlue ? 2 : 0;
    switch(dcn*1000 + blueIdx*100 + uIdx*10 + ycn)
    {
    case 3000: cvtYUV422toRGB<0,0,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 3001: cvtYUV422toRGB<0,0,1>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 3010: cvtYUV422toRGB<0,1,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 3200: cvtYUV422toRGB<2,0,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 3201: cvtYUV422toRGB<2,0,1>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 3210: cvtYUV422toRGB<2,1,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 4000: cvtYUV422toRGBA<0,0,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 4001: cvtYUV422toRGBA<0,0,1>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 4010: cvtYUV422toRGBA<0,1,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 4200: cvtYUV422toRGBA<2,0,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 4201: cvtYUV422toRGBA<2,0,1>(dst_data, dst_step, src_data, src_step, width, height); break;
    case 4210: cvtYUV422toRGBA<2,1,0>(dst_data, dst_step, src_data, src_step, width, height); break;
    default: CV_Error( CV_StsBadFlag, "Unknown/unsupported color conversion code" ); break;
    };
}

void cvtRGBAtoMultipliedRGBA(const uchar * src_data, size_t src_step,
                             uchar * dst_data, size_t dst_step,
                             int width, int height)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtRGBAtoMultipliedRGBA, cv_hal_cvtRGBAtoMultipliedRGBA, src_data, src_step, dst_data, dst_step, width, height);

#ifdef HAVE_IPP
    CV_IPP_CHECK()
    {
    if (CvtColorIPPLoop(src_data, src_step, dst_data, dst_step, width, height,
                        IPPGeneralFunctor((ippiGeneralFunc)ippiAlphaPremul_8u_AC4R)))
        return;
    }
#endif

    CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, RGBA2mRGBA<uchar>());
}

void cvtMultipliedRGBAtoRGBA(const uchar * src_data, size_t src_step,
                             uchar * dst_data, size_t dst_step,
                             int width, int height)
{
    CV_INSTRUMENT_REGION()

    CALL_HAL(cvtMultipliedRGBAtoRGBA, cv_hal_cvtMultipliedRGBAtoRGBA, src_data, src_step, dst_data, dst_step, width, height);
    CvtColorLoop(src_data, src_step, dst_data, dst_step, width, height, mRGBA2RGBA<uchar>());
}

}} // cv::hal::

//
// Helper functions
//

inline bool isHSV(int code)
{
    switch(code)
    {
    case CV_HSV2BGR: case CV_HSV2RGB: case CV_HSV2BGR_FULL: case CV_HSV2RGB_FULL:
    case CV_BGR2HSV: case CV_RGB2HSV: case CV_BGR2HSV_FULL: case CV_RGB2HSV_FULL:
        return true;
    default:
        return false;
    }
}

inline bool isLab(int code)
{
    switch (code)
    {
    case CV_Lab2BGR: case CV_Lab2RGB: case CV_Lab2LBGR: case CV_Lab2LRGB:
    case CV_BGR2Lab: case CV_RGB2Lab: case CV_LBGR2Lab: case CV_LRGB2Lab:
        return true;
    default:
        return false;
    }
}

inline bool issRGB(int code)
{
    switch (code)
    {
    case CV_BGR2Lab: case CV_RGB2Lab: case CV_BGR2Luv: case CV_RGB2Luv:
    case CV_Lab2BGR: case CV_Lab2RGB: case CV_Luv2BGR: case CV_Luv2RGB:
        return true;
    default:
        return false;
    }
}

inline bool swapBlue(int code)
{
    switch (code)
    {
    case CV_BGR2BGRA: case CV_BGRA2BGR:
    case CV_BGR2BGR565: case CV_BGR2BGR555: case CV_BGRA2BGR565: case CV_BGRA2BGR555:
    case CV_BGR5652BGR: case CV_BGR5552BGR: case CV_BGR5652BGRA: case CV_BGR5552BGRA:
    case CV_BGR2GRAY: case CV_BGRA2GRAY:
    case CV_BGR2YCrCb: case CV_BGR2YUV:
    case CV_YCrCb2BGR: case CV_YUV2BGR:
    case CV_BGR2XYZ: case CV_XYZ2BGR:
    case CV_BGR2HSV: case CV_BGR2HLS: case CV_BGR2HSV_FULL: case CV_BGR2HLS_FULL:
    case CV_YUV2BGR_YV12: case CV_YUV2BGRA_YV12: case CV_YUV2BGR_IYUV: case CV_YUV2BGRA_IYUV:
    case CV_YUV2BGR_NV21: case CV_YUV2BGRA_NV21: case CV_YUV2BGR_NV12: case CV_YUV2BGRA_NV12:
    case CV_Lab2BGR: case CV_Luv2BGR: case CV_Lab2LBGR: case CV_Luv2LBGR:
    case CV_BGR2Lab: case CV_BGR2Luv: case CV_LBGR2Lab: case CV_LBGR2Luv:
    case CV_HSV2BGR: case CV_HLS2BGR: case CV_HSV2BGR_FULL: case CV_HLS2BGR_FULL:
    case CV_YUV2BGR_UYVY: case CV_YUV2BGRA_UYVY: case CV_YUV2BGR_YUY2:
    case CV_YUV2BGRA_YUY2:  case CV_YUV2BGR_YVYU: case CV_YUV2BGRA_YVYU:
    case CV_BGR2YUV_IYUV: case CV_BGRA2YUV_IYUV: case CV_BGR2YUV_YV12: case CV_BGRA2YUV_YV12:
        return false;
    default:
        return true;
    }
}

inline bool isFullRange(int code)
{
    switch (code)
    {
    case CV_BGR2HSV_FULL: case CV_RGB2HSV_FULL: case CV_BGR2HLS_FULL: case CV_RGB2HLS_FULL:
    case CV_HSV2BGR_FULL: case CV_HSV2RGB_FULL: case CV_HLS2BGR_FULL: case CV_HLS2RGB_FULL:
        return true;
    default:
        return false;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////
//                                   The main function                                  //
//////////////////////////////////////////////////////////////////////////////////////////

void cv::cvtColor( InputArray _src, OutputArray _dst, int code, int dcn )
{
    CV_INSTRUMENT_REGION()

    int stype = _src.type();
    int scn = CV_MAT_CN(stype), depth = CV_MAT_DEPTH(stype), uidx, gbits, ycn;

    CV_OCL_RUN( _src.dims() <= 2 && _dst.isUMat() && !(depth == CV_8U && (code == CV_Luv2BGR || code == CV_Luv2RGB)),
                ocl_cvtColor(_src, _dst, code, dcn) )

    Mat src, dst;
    if (_src.getObj() == _dst.getObj()) // inplace processing (#6653)
        _src.copyTo(src);
    else
        src = _src.getMat();
    Size sz = src.size();
    CV_Assert( depth == CV_8U || depth == CV_16U || depth == CV_32F );

    switch( code )
    {
        case CV_BGR2BGRA: case CV_RGB2BGRA: case CV_BGRA2BGR:
        case CV_RGBA2BGR: case CV_RGB2BGR: case CV_BGRA2RGBA:
            CV_Assert( scn == 3 || scn == 4 );
            dcn = code == CV_BGR2BGRA || code == CV_RGB2BGRA || code == CV_BGRA2RGBA ? 4 : 3;
            _dst.create( sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtBGRtoBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                             depth, scn, dcn, swapBlue(code));
            break;

        case CV_BGR2BGR565: case CV_BGR2BGR555: case CV_RGB2BGR565: case CV_RGB2BGR555:
        case CV_BGRA2BGR565: case CV_BGRA2BGR555: case CV_RGBA2BGR565: case CV_RGBA2BGR555:
            CV_Assert( (scn == 3 || scn == 4) && depth == CV_8U );
            gbits = code == CV_BGR2BGR565 || code == CV_RGB2BGR565 ||
                    code == CV_BGRA2BGR565 || code == CV_RGBA2BGR565 ? 6 : 5;
            _dst.create(sz, CV_8UC2);
            dst = _dst.getMat();
            hal::cvtBGRtoBGR5x5(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                                scn, swapBlue(code), gbits);
            break;

        case CV_BGR5652BGR: case CV_BGR5552BGR: case CV_BGR5652RGB: case CV_BGR5552RGB:
        case CV_BGR5652BGRA: case CV_BGR5552BGRA: case CV_BGR5652RGBA: case CV_BGR5552RGBA:
            if(dcn <= 0) dcn = (code==CV_BGR5652BGRA || code==CV_BGR5552BGRA || code==CV_BGR5652RGBA || code==CV_BGR5552RGBA) ? 4 : 3;
            CV_Assert( (dcn == 3 || dcn == 4) && scn == 2 && depth == CV_8U );
            gbits = code == CV_BGR5652BGR || code == CV_BGR5652RGB ||
                    code == CV_BGR5652BGRA || code == CV_BGR5652RGBA ? 6 : 5;
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtBGR5x5toBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                                dcn, swapBlue(code), gbits);
            break;

        case CV_BGR2GRAY: case CV_BGRA2GRAY: case CV_RGB2GRAY: case CV_RGBA2GRAY:
            CV_Assert( scn == 3 || scn == 4 );
            _dst.create(sz, CV_MAKETYPE(depth, 1));
            dst = _dst.getMat();
            hal::cvtBGRtoGray(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                              depth, scn, swapBlue(code));
            break;

        case CV_BGR5652GRAY: case CV_BGR5552GRAY:
            CV_Assert( scn == 2 && depth == CV_8U );
            gbits = code == CV_BGR5652GRAY ? 6 : 5;
            _dst.create(sz, CV_8UC1);
            dst = _dst.getMat();
            hal::cvtBGR5x5toGray(src.data, src.step, dst.data, dst.step, src.cols, src.rows, gbits);
            break;

        case CV_GRAY2BGR: case CV_GRAY2BGRA:
            if( dcn <= 0 ) dcn = (code==CV_GRAY2BGRA) ? 4 : 3;
            CV_Assert( scn == 1 && (dcn == 3 || dcn == 4));
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtGraytoBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows, depth, dcn);
            break;

        case CV_GRAY2BGR565: case CV_GRAY2BGR555:
            CV_Assert( scn == 1 && depth == CV_8U );
            gbits = code == CV_GRAY2BGR565 ? 6 : 5;
            _dst.create(sz, CV_8UC2);
            dst = _dst.getMat();
            hal::cvtGraytoBGR5x5(src.data, src.step, dst.data, dst.step, src.cols, src.rows, gbits);
            break;

        case CV_BGR2YCrCb: case CV_RGB2YCrCb:
        case CV_BGR2YUV: case CV_RGB2YUV:
            CV_Assert( scn == 3 || scn == 4 );
            _dst.create(sz, CV_MAKETYPE(depth, 3));
            dst = _dst.getMat();
            hal::cvtBGRtoYUV(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                             depth, scn, swapBlue(code), code == CV_BGR2YCrCb || code == CV_RGB2YCrCb);
            break;

        case CV_YCrCb2BGR: case CV_YCrCb2RGB:
        case CV_YUV2BGR: case CV_YUV2RGB:
            if( dcn <= 0 ) dcn = 3;
            CV_Assert( scn == 3 && (dcn == 3 || dcn == 4) );
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtYUVtoBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                             depth, dcn, swapBlue(code), code == CV_YCrCb2BGR || code == CV_YCrCb2RGB);
            break;

        case CV_BGR2XYZ: case CV_RGB2XYZ:
            CV_Assert( scn == 3 || scn == 4 );
            _dst.create(sz, CV_MAKETYPE(depth, 3));
            dst = _dst.getMat();
            hal::cvtBGRtoXYZ(src.data, src.step, dst.data, dst.step, src.cols, src.rows, depth, scn, swapBlue(code));
            break;

        case CV_XYZ2BGR: case CV_XYZ2RGB:
            if( dcn <= 0 ) dcn = 3;
            CV_Assert( scn == 3 && (dcn == 3 || dcn == 4) );
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtXYZtoBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows, depth, dcn, swapBlue(code));
            break;

        case CV_BGR2HSV: case CV_RGB2HSV: case CV_BGR2HSV_FULL: case CV_RGB2HSV_FULL:
        case CV_BGR2HLS: case CV_RGB2HLS: case CV_BGR2HLS_FULL: case CV_RGB2HLS_FULL:
            CV_Assert( (scn == 3 || scn == 4) && (depth == CV_8U || depth == CV_32F) );
            _dst.create(sz, CV_MAKETYPE(depth, 3));
            dst = _dst.getMat();
            hal::cvtBGRtoHSV(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                             depth, scn, swapBlue(code), isFullRange(code), isHSV(code));
            break;

        case CV_HSV2BGR: case CV_HSV2RGB: case CV_HSV2BGR_FULL: case CV_HSV2RGB_FULL:
        case CV_HLS2BGR: case CV_HLS2RGB: case CV_HLS2BGR_FULL: case CV_HLS2RGB_FULL:
            if( dcn <= 0 ) dcn = 3;
            CV_Assert( scn == 3 && (dcn == 3 || dcn == 4) && (depth == CV_8U || depth == CV_32F) );
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtHSVtoBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                             depth, dcn, swapBlue(code), isFullRange(code), isHSV(code));
            break;

        case CV_BGR2Lab: case CV_RGB2Lab: case CV_LBGR2Lab: case CV_LRGB2Lab:
        case CV_BGR2Luv: case CV_RGB2Luv: case CV_LBGR2Luv: case CV_LRGB2Luv:
            CV_Assert( (scn == 3 || scn == 4) && (depth == CV_8U || depth == CV_32F) );
            _dst.create(sz, CV_MAKETYPE(depth, 3));
            dst = _dst.getMat();
            hal::cvtBGRtoLab(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                             depth, scn, swapBlue(code), isLab(code), issRGB(code));
            break;

        case CV_Lab2BGR: case CV_Lab2RGB: case CV_Lab2LBGR: case CV_Lab2LRGB:
        case CV_Luv2BGR: case CV_Luv2RGB: case CV_Luv2LBGR: case CV_Luv2LRGB:
            if( dcn <= 0 ) dcn = 3;
            CV_Assert( scn == 3 && (dcn == 3 || dcn == 4) && (depth == CV_8U || depth == CV_32F) );
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtLabtoBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                             depth, dcn, swapBlue(code), isLab(code), issRGB(code));
            break;

        case CV_BayerBG2GRAY: case CV_BayerGB2GRAY: case CV_BayerRG2GRAY: case CV_BayerGR2GRAY:
        case CV_BayerBG2BGR: case CV_BayerGB2BGR: case CV_BayerRG2BGR: case CV_BayerGR2BGR:
        case CV_BayerBG2BGR_VNG: case CV_BayerGB2BGR_VNG: case CV_BayerRG2BGR_VNG: case CV_BayerGR2BGR_VNG:
        case CV_BayerBG2BGR_EA: case CV_BayerGB2BGR_EA: case CV_BayerRG2BGR_EA: case CV_BayerGR2BGR_EA:
            demosaicing(src, _dst, code, dcn);
            break;

        case CV_YUV2BGR_NV21:  case CV_YUV2RGB_NV21:  case CV_YUV2BGR_NV12:  case CV_YUV2RGB_NV12:
        case CV_YUV2BGRA_NV21: case CV_YUV2RGBA_NV21: case CV_YUV2BGRA_NV12: case CV_YUV2RGBA_NV12:
            // http://www.fourcc.org/yuv.php#NV21 == yuv420sp -> a plane of 8 bit Y samples followed by an interleaved V/U plane containing 8 bit 2x2 subsampled chroma samples
            // http://www.fourcc.org/yuv.php#NV12 -> a plane of 8 bit Y samples followed by an interleaved U/V plane containing 8 bit 2x2 subsampled colour difference samples
            if (dcn <= 0) dcn = (code==CV_YUV420sp2BGRA || code==CV_YUV420sp2RGBA || code==CV_YUV2BGRA_NV12 || code==CV_YUV2RGBA_NV12) ? 4 : 3;
            uidx = (code==CV_YUV2BGR_NV21 || code==CV_YUV2BGRA_NV21 || code==CV_YUV2RGB_NV21 || code==CV_YUV2RGBA_NV21) ? 1 : 0;
            CV_Assert( dcn == 3 || dcn == 4 );
            CV_Assert( sz.width % 2 == 0 && sz.height % 3 == 0 && depth == CV_8U );
            _dst.create(Size(sz.width, sz.height * 2 / 3), CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtTwoPlaneYUVtoBGR(src.data, src.step, dst.data, dst.step, dst.cols, dst.rows,
                                     dcn, swapBlue(code), uidx);
            break;
        case CV_YUV2BGR_YV12: case CV_YUV2RGB_YV12: case CV_YUV2BGRA_YV12: case CV_YUV2RGBA_YV12:
        case CV_YUV2BGR_IYUV: case CV_YUV2RGB_IYUV: case CV_YUV2BGRA_IYUV: case CV_YUV2RGBA_IYUV:
            //http://www.fourcc.org/yuv.php#YV12 == yuv420p -> It comprises an NxM Y plane followed by (N/2)x(M/2) V and U planes.
            //http://www.fourcc.org/yuv.php#IYUV == I420 -> It comprises an NxN Y plane followed by (N/2)x(N/2) U and V planes
            if (dcn <= 0) dcn = (code==CV_YUV2BGRA_YV12 || code==CV_YUV2RGBA_YV12 || code==CV_YUV2RGBA_IYUV || code==CV_YUV2BGRA_IYUV) ? 4 : 3;
            uidx  = (code==CV_YUV2BGR_YV12 || code==CV_YUV2RGB_YV12 || code==CV_YUV2BGRA_YV12 || code==CV_YUV2RGBA_YV12) ? 1 : 0;
            CV_Assert( dcn == 3 || dcn == 4 );
            CV_Assert( sz.width % 2 == 0 && sz.height % 3 == 0 && depth == CV_8U );
            _dst.create(Size(sz.width, sz.height * 2 / 3), CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtThreePlaneYUVtoBGR(src.data, src.step, dst.data, dst.step, dst.cols, dst.rows,
                                       dcn, swapBlue(code), uidx);
            break;

        case CV_YUV2GRAY_420:
            {
                if (dcn <= 0) dcn = 1;

                CV_Assert( dcn == 1 );
                CV_Assert( sz.width % 2 == 0 && sz.height % 3 == 0 && depth == CV_8U );

                Size dstSz(sz.width, sz.height * 2 / 3);
                _dst.create(dstSz, CV_MAKETYPE(depth, dcn));
                dst = _dst.getMat();
#ifdef HAVE_IPP
                if (CV_INSTRUMENT_FUN_IPP(ippiCopy_8u_C1R, src.data, (int)src.step, dst.data, (int)dst.step,
                                                   ippiSize(dstSz.width, dstSz.height)) >= 0)
                    break;
#endif
                src(Range(0, dstSz.height), Range::all()).copyTo(dst);
            }
            break;
        case CV_RGB2YUV_YV12: case CV_BGR2YUV_YV12: case CV_RGBA2YUV_YV12: case CV_BGRA2YUV_YV12:
        case CV_RGB2YUV_IYUV: case CV_BGR2YUV_IYUV: case CV_RGBA2YUV_IYUV: case CV_BGRA2YUV_IYUV:
            if (dcn <= 0) dcn = 1;
            uidx = (code == CV_BGR2YUV_IYUV || code == CV_BGRA2YUV_IYUV || code == CV_RGB2YUV_IYUV || code == CV_RGBA2YUV_IYUV) ? 1 : 2;
            CV_Assert( (scn == 3 || scn == 4) && depth == CV_8U );
            CV_Assert( dcn == 1 );
            CV_Assert( sz.width % 2 == 0 && sz.height % 2 == 0 );
            _dst.create(Size(sz.width, sz.height / 2 * 3), CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtBGRtoThreePlaneYUV(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                                       scn, swapBlue(code), uidx);
            break;
        case CV_YUV2RGB_UYVY: case CV_YUV2BGR_UYVY: case CV_YUV2RGBA_UYVY: case CV_YUV2BGRA_UYVY:
        case CV_YUV2RGB_YUY2: case CV_YUV2BGR_YUY2: case CV_YUV2RGB_YVYU: case CV_YUV2BGR_YVYU:
        case CV_YUV2RGBA_YUY2: case CV_YUV2BGRA_YUY2: case CV_YUV2RGBA_YVYU: case CV_YUV2BGRA_YVYU:
            //http://www.fourcc.org/yuv.php#UYVY
            //http://www.fourcc.org/yuv.php#YUY2
            //http://www.fourcc.org/yuv.php#YVYU
            if (dcn <= 0) dcn = (code==CV_YUV2RGBA_UYVY || code==CV_YUV2BGRA_UYVY || code==CV_YUV2RGBA_YUY2 || code==CV_YUV2BGRA_YUY2 || code==CV_YUV2RGBA_YVYU || code==CV_YUV2BGRA_YVYU) ? 4 : 3;
            ycn  = (code==CV_YUV2RGB_UYVY || code==CV_YUV2BGR_UYVY || code==CV_YUV2RGBA_UYVY || code==CV_YUV2BGRA_UYVY) ? 1 : 0;
            uidx = (code==CV_YUV2RGB_YVYU || code==CV_YUV2BGR_YVYU || code==CV_YUV2RGBA_YVYU || code==CV_YUV2BGRA_YVYU) ? 1 : 0;
            CV_Assert( dcn == 3 || dcn == 4 );
            CV_Assert( scn == 2 && depth == CV_8U );
            _dst.create(sz, CV_8UC(dcn));
            dst = _dst.getMat();
            hal::cvtOnePlaneYUVtoBGR(src.data, src.step, dst.data, dst.step, src.cols, src.rows,
                                     dcn, swapBlue(code), uidx, ycn);
            break;
        case CV_YUV2GRAY_UYVY: case CV_YUV2GRAY_YUY2:
            {
                if (dcn <= 0) dcn = 1;

                CV_Assert( dcn == 1 );
                CV_Assert( scn == 2 && depth == CV_8U );

                src.release(); // T-API datarace fixup
                extractChannel(_src, _dst, code == CV_YUV2GRAY_UYVY ? 1 : 0);
            }
            break;
        case CV_RGBA2mRGBA:
            if (dcn <= 0) dcn = 4;
            CV_Assert( scn == 4 && dcn == 4 && depth == CV_8U );
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtRGBAtoMultipliedRGBA(src.data, src.step, dst.data, dst.step, src.cols, src.rows);
            break;
        case CV_mRGBA2RGBA:
            if (dcn <= 0) dcn = 4;
            CV_Assert( scn == 4 && dcn == 4 && depth == CV_8U );
            _dst.create(sz, CV_MAKETYPE(depth, dcn));
            dst = _dst.getMat();
            hal::cvtMultipliedRGBAtoRGBA(src.data, src.step, dst.data, dst.step, src.cols, src.rows);
            break;
        default:
            CV_Error( CV_StsBadFlag, "Unknown/unsupported color conversion code" );
        }
}

CV_IMPL void
cvCvtColor( const CvArr* srcarr, CvArr* dstarr, int code )
{
    cv::Mat src = cv::cvarrToMat(srcarr), dst0 = cv::cvarrToMat(dstarr), dst = dst0;
    CV_Assert( src.depth() == dst.depth() );

    cv::cvtColor(src, dst, code, dst.channels());
    CV_Assert( dst.data == dst0.data );
}


/* End of file. */
