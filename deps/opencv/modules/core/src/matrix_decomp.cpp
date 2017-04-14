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
// Copyright (C) 2009-2011, Willow Garage Inc., all rights reserved.
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

namespace cv { namespace hal {

/****************************************************************************************\
*                     LU & Cholesky implementation for small matrices                    *
\****************************************************************************************/

template<typename _Tp> static inline int
LUImpl(_Tp* A, size_t astep, int m, _Tp* b, size_t bstep, int n, _Tp eps)
{
    int i, j, k, p = 1;
    astep /= sizeof(A[0]);
    bstep /= sizeof(b[0]);

    for( i = 0; i < m; i++ )
    {
        k = i;

        for( j = i+1; j < m; j++ )
            if( std::abs(A[j*astep + i]) > std::abs(A[k*astep + i]) )
                k = j;

        if( std::abs(A[k*astep + i]) < eps )
            return 0;

        if( k != i )
        {
            for( j = i; j < m; j++ )
                std::swap(A[i*astep + j], A[k*astep + j]);
            if( b )
                for( j = 0; j < n; j++ )
                    std::swap(b[i*bstep + j], b[k*bstep + j]);
            p = -p;
        }

        _Tp d = -1/A[i*astep + i];

        for( j = i+1; j < m; j++ )
        {
            _Tp alpha = A[j*astep + i]*d;

            for( k = i+1; k < m; k++ )
                A[j*astep + k] += alpha*A[i*astep + k];

            if( b )
                for( k = 0; k < n; k++ )
                    b[j*bstep + k] += alpha*b[i*bstep + k];
        }
    }

    if( b )
    {
        for( i = m-1; i >= 0; i-- )
            for( j = 0; j < n; j++ )
            {
                _Tp s = b[i*bstep + j];
                for( k = i+1; k < m; k++ )
                    s -= A[i*astep + k]*b[k*bstep + j];
                b[i*bstep + j] = s/A[i*astep + i];
            }
    }

    return p;
}


int LU32f(float* A, size_t astep, int m, float* b, size_t bstep, int n)
{
    CV_INSTRUMENT_REGION()

    int output;
    CALL_HAL_RET(LU32f, cv_hal_LU32f, output, A, astep, m, b, bstep, n)
    output = LUImpl(A, astep, m, b, bstep, n, FLT_EPSILON*10);
    return output;
}


int LU64f(double* A, size_t astep, int m, double* b, size_t bstep, int n)
{
    CV_INSTRUMENT_REGION()

    int output;
    CALL_HAL_RET(LU64f, cv_hal_LU64f, output, A, astep, m, b, bstep, n)
    output = LUImpl(A, astep, m, b, bstep, n, DBL_EPSILON*100);
    return output;
}

template<typename _Tp> static inline bool
CholImpl(_Tp* A, size_t astep, int m, _Tp* b, size_t bstep, int n)
{
    _Tp* L = A;
    int i, j, k;
    double s;
    astep /= sizeof(A[0]);
    bstep /= sizeof(b[0]);

    for( i = 0; i < m; i++ )
    {
        for( j = 0; j < i; j++ )
        {
            s = A[i*astep + j];
            for( k = 0; k < j; k++ )
                s -= L[i*astep + k]*L[j*astep + k];
            L[i*astep + j] = (_Tp)(s*L[j*astep + j]);
        }
        s = A[i*astep + i];
        for( k = 0; k < j; k++ )
        {
            double t = L[i*astep + k];
            s -= t*t;
        }
        if( s < std::numeric_limits<_Tp>::epsilon() )
            return false;
        L[i*astep + i] = (_Tp)(1./std::sqrt(s));
    }

    if (!b)
    {
        for( i = 0; i < m; i++ )
             L[i*astep + i]=1/L[i*astep + i];
        return true;
   }

    // LLt x = b
    // 1: L y = b
    // 2. Lt x = y

    /*
     [ L00             ]  y0   b0
     [ L10 L11         ]  y1 = b1
     [ L20 L21 L22     ]  y2   b2
     [ L30 L31 L32 L33 ]  y3   b3

     [ L00 L10 L20 L30 ]  x0   y0
     [     L11 L21 L31 ]  x1 = y1
     [         L22 L32 ]  x2   y2
     [             L33 ]  x3   y3
     */

    for( i = 0; i < m; i++ )
    {
        for( j = 0; j < n; j++ )
        {
            s = b[i*bstep + j];
            for( k = 0; k < i; k++ )
                s -= L[i*astep + k]*b[k*bstep + j];
            b[i*bstep + j] = (_Tp)(s*L[i*astep + i]);
        }
    }

    for( i = m-1; i >= 0; i-- )
    {
        for( j = 0; j < n; j++ )
        {
            s = b[i*bstep + j];
            for( k = m-1; k > i; k-- )
                s -= L[k*astep + i]*b[k*bstep + j];
            b[i*bstep + j] = (_Tp)(s*L[i*astep + i]);
        }
    }
    for( i = 0; i < m; i++ )
            L[i*astep + i]=1/L[i*astep + i];

    return true;
}

bool Cholesky32f(float* A, size_t astep, int m, float* b, size_t bstep, int n)
{
    CV_INSTRUMENT_REGION()

    bool output;
    CALL_HAL_RET(Cholesky32f, cv_hal_Cholesky32f, output, A, astep, m, b, bstep, n)
    return CholImpl(A, astep, m, b, bstep, n);
}

bool Cholesky64f(double* A, size_t astep, int m, double* b, size_t bstep, int n)
{
    CV_INSTRUMENT_REGION()

    bool output;
    CALL_HAL_RET(Cholesky64f, cv_hal_Cholesky64f, output, A, astep, m, b, bstep, n)
    return CholImpl(A, astep, m, b, bstep, n);
}

template<typename _Tp> inline static int
sign(_Tp x)
{
    if (x >= (_Tp)0)
        return 1;
    else
        return -1;
}

template<typename _Tp> static inline int
QRImpl(_Tp* A, size_t astep, int m, int n, int k, _Tp* b, size_t bstep, _Tp* hFactors, _Tp eps)
{
    astep /= sizeof(_Tp);
    bstep /= sizeof(_Tp);

    cv::AutoBuffer<_Tp> buffer;
    size_t buf_size = m ? m + n : hFactors != NULL;
    buffer.allocate(buf_size);
    _Tp* vl = buffer;
    if (hFactors == NULL)
        hFactors = vl + m;

    for (int l = 0; l < n; l++)
    {
        //generate vl
        int vlSize = m - l;
        _Tp vlNorm = (_Tp)0;
        for (int i = 0; i < vlSize; i++)
        {
            vl[i] = A[(l + i)*astep + l];
            vlNorm += vl[i] * vl[i];
        }
        _Tp tmpV = vl[0];
        vl[0] = vl[0] + sign(vl[0])*std::sqrt(vlNorm);
        vlNorm = std::sqrt(vlNorm + vl[0] * vl[0] - tmpV*tmpV);
        for (int i = 0; i < vlSize; i++)
        {
            vl[i] /= vlNorm;
        }
        //multiply A_l*vl
        for (int j = l; j < n; j++)
        {
            _Tp v_lA = (_Tp)0;
            for (int i = l; i < m; i++)
            {
                v_lA += vl[i - l] * A[i*astep + j];
            }

            for (int i = l; i < m; i++)
            {
                A[i*astep + j] -= 2 * vl[i - l] * v_lA;
            }
        }

        //save vl and factors
        hFactors[l] = vl[0] * vl[0];
        for (int i = 1; i < vlSize; i++)
        {
            A[(l + i)*astep + l] = vl[i] / vl[0];
        }
    }

    if (b)
    {
        //generate new rhs
        for (int l = 0; l < n; l++)
        {
            //unpack vl
            vl[0] = (_Tp)1;
            for (int j = 1; j < m - l; j++)
            {
              vl[j] = A[(j + l)*astep + l];
            }

            //h_l*x
            for (int j = 0; j < k; j++)
            {
                _Tp v_lB = (_Tp)0;
                for (int i = l; i < m; i++)
                  v_lB += vl[i - l] * b[i*bstep + j];

                for (int i = l; i < m; i++)
                    b[i*bstep + j] -= 2 * vl[i - l] * v_lB * hFactors[l];
            }
        }
        //do back substitution
        for (int i = n - 1; i >= 0; i--)
        {
            for (int j = n - 1; j > i; j--)
            {
                for (int p = 0; p < k; p++)
                    b[i*bstep + p] -= b[j*bstep + p] * A[i*astep + j];
            }
            if (std::abs(A[i*astep + i]) < eps)
                return 0;
            for (int p = 0; p < k; p++)
                b[i*bstep + p] /= A[i*astep + i];
        }
    }

    return 1;
}

int QR32f(float* A, size_t astep, int m, int n, int k, float* b, size_t bstep, float* hFactors)
{
    CV_INSTRUMENT_REGION()

    int output;
    CALL_HAL_RET(QR32f, cv_hal_QR32f, output, A, astep, m, n, k, b, bstep, hFactors);
    output = QRImpl(A, astep, m, n, k, b, bstep, hFactors, FLT_EPSILON * 10);
    return output;
}

int QR64f(double* A, size_t astep, int m, int n, int k, double* b, size_t bstep, double* hFactors)
{
    CV_INSTRUMENT_REGION()

    int output;
    CALL_HAL_RET(QR64f, cv_hal_QR64f, output, A, astep, m, n, k, b, bstep, hFactors)
    output = QRImpl(A, astep, m, n, k, b, bstep, hFactors, DBL_EPSILON * 100);
    return output;
}

//=============================================================================
// for compatibility with 3.0

int LU(float* A, size_t astep, int m, float* b, size_t bstep, int n)
{
    return LUImpl(A, astep, m, b, bstep, n, FLT_EPSILON*10);
}

int LU(double* A, size_t astep, int m, double* b, size_t bstep, int n)
{
    return LUImpl(A, astep, m, b, bstep, n, DBL_EPSILON*100);
}

bool Cholesky(float* A, size_t astep, int m, float* b, size_t bstep, int n)
{
    return CholImpl(A, astep, m, b, bstep, n);
}

bool Cholesky(double* A, size_t astep, int m, double* b, size_t bstep, int n)
{
    return CholImpl(A, astep, m, b, bstep, n);
}


}}
