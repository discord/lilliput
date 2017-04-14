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
// Copyright (C) 2010-2012, Multicoreware, Inc., all rights reserved.
// Copyright (C) 2010-2012, Advanced Micro Devices, Inc., all rights reserved.
// Third party copyrights are property of their respective owners.
//
// @Authors
//    Sen Liu, swjtuls1987@126.com
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
// This software is provided by the copyright holders and contributors as is and
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


#define tx  (int)get_local_id(0)
#define ty  get_local_id(1)
#define bx  get_group_id(0)
#define bdx (int)get_local_size(0)

#define BORDER_SIZE 5
#define MAX_KSIZE_HALF 100

#ifndef polyN
#define polyN 5
#endif

#if USE_DOUBLE
#ifdef cl_amd_fp64
#pragma OPENCL EXTENSION cl_amd_fp64:enable
#elif defined (cl_khr_fp64)
#pragma OPENCL EXTENSION cl_khr_fp64:enable
#endif
#define TYPE double
#define VECTYPE double4
#else
#define TYPE float
#define VECTYPE float4
#endif

__kernel void polynomialExpansion(__global __const float * src, int srcStep,
                                  __global float * dst, int dstStep,
                                  const int rows, const  int cols,
                                  __global __const float * c_g,
                                  __global __const float * c_xg,
                                  __global __const float * c_xxg,
                                  __local float * smem,
                                  const VECTYPE ig)
{
    const int y = get_global_id(1);
    const int x = bx * (bdx - 2*polyN) + tx - polyN;

    int xWarped;
    __local float *row = smem + tx;

    if (y < rows && y >= 0)
    {
        xWarped = min(max(x, 0), cols - 1);

        row[0] = src[mad24(y, srcStep, xWarped)] * c_g[0];
        row[bdx] = 0.f;
        row[2*bdx] = 0.f;

#pragma unroll
        for (int k = 1; k <= polyN; ++k)
        {
            float t0 = src[mad24(max(y - k, 0), srcStep, xWarped)];
            float t1 = src[mad24(min(y + k, rows - 1), srcStep, xWarped)];

            row[0] += c_g[k] * (t0 + t1);
            row[bdx] += c_xg[k] * (t1 - t0);
            row[2*bdx] += c_xxg[k] * (t0 + t1);
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    if (y < rows && y >= 0 && tx >= polyN && tx + polyN < bdx && x < cols)
    {
        TYPE b1 = c_g[0] * row[0];
        TYPE b3 = c_g[0] * row[bdx];
        TYPE b5 = c_g[0] * row[2*bdx];
        TYPE b2 = 0, b4 = 0, b6 = 0;

#pragma unroll
        for (int k = 1; k <= polyN; ++k)
        {
            b1 += (row[k] + row[-k]) * c_g[k];
            b4 += (row[k] + row[-k]) * c_xxg[k];
            b2 += (row[k] - row[-k]) * c_xg[k];
            b3 += (row[k + bdx] + row[-k + bdx]) * c_g[k];
            b6 += (row[k + bdx] - row[-k + bdx]) * c_xg[k];
            b5 += (row[k + 2*bdx] + row[-k + 2*bdx]) * c_g[k];
        }

        dst[mad24(y, dstStep, xWarped)] = (float)(b3*ig.s0);
        dst[mad24(rows + y, dstStep, xWarped)] = (float)(b2*ig.s0);
        dst[mad24(2*rows + y, dstStep, xWarped)] = (float)(b1*ig.s1 + b5*ig.s2);
        dst[mad24(3*rows + y, dstStep, xWarped)] = (float)(b1*ig.s1 + b4*ig.s2);
        dst[mad24(4*rows + y, dstStep, xWarped)] = (float)(b6*ig.s3);
    }
}

inline int idx_row_low(const int y, const int last_row)
{
    return abs(y) % (last_row + 1);
}

inline int idx_row_high(const int y, const int last_row)
{
    return abs(last_row - abs(last_row - y)) % (last_row + 1);
}

inline int idx_col_low(const int x, const int last_col)
{
    return abs(x) % (last_col + 1);
}

inline int idx_col_high(const int x, const int last_col)
{
    return abs(last_col - abs(last_col - x)) % (last_col + 1);
}

inline int idx_col(const int x, const int last_col)
{
    return idx_col_low(idx_col_high(x, last_col), last_col);
}

__kernel void gaussianBlur(__global const float * src, int srcStep,
                           __global float * dst, int dstStep, const int rows, const  int cols,
                           __global const float * c_gKer, const int ksizeHalf,
                           __local float * smem)
{
    const int y = get_global_id(1);
    const int x = get_global_id(0);

    __local float *row = smem + ty * (bdx + 2*ksizeHalf);

    if (y < rows)
    {
        // Vertical pass
        for (int i = tx; i < bdx + 2*ksizeHalf; i += bdx)
        {
            int xExt = (int)(bx * bdx) + i - ksizeHalf;
            xExt = idx_col(xExt, cols - 1);
            row[i] = src[mad24(y, srcStep, xExt)] * c_gKer[0];
            for (int j = 1; j <= ksizeHalf; ++j)
                row[i] += (src[mad24(idx_row_low(y - j, rows - 1), srcStep, xExt)]
                           + src[mad24(idx_row_high(y + j, rows - 1), srcStep, xExt)]) * c_gKer[j];
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    if (y < rows && y >= 0 && x < cols && x >= 0)
    {
        // Horizontal pass
        row += tx + ksizeHalf;
        float res = row[0] * c_gKer[0];
        for (int i = 1; i <= ksizeHalf; ++i)
            res += (row[-i] + row[i]) * c_gKer[i];

        dst[mad24(y, dstStep, x)] = res;
    }
}

__kernel void gaussianBlur5(__global const float * src, int srcStep,
                            __global float * dst, int dstStep,
                            const int rows, const  int cols,
                            __global const float * c_gKer, const int ksizeHalf,
                            __local float * smem)
{
    const int y = get_global_id(1);
    const int x = get_global_id(0);

    const int smw = bdx + 2*ksizeHalf; // shared memory "cols"
    __local volatile float *row = smem + 5 * ty * smw;

    if (y < rows)
    {
        // Vertical pass
        for (int i = tx; i < bdx + 2*ksizeHalf; i += bdx)
        {
            int xExt = (int)(bx * bdx) + i - ksizeHalf;
            xExt = idx_col(xExt, cols - 1);

#pragma unroll
            for (int k = 0; k < 5; ++k)
                row[k*smw + i] = src[mad24(k*rows + y, srcStep, xExt)] * c_gKer[0];

            for (int j = 1; j <= ksizeHalf; ++j)
#pragma unroll
                for (int k = 0; k < 5; ++k)
                    row[k*smw + i] +=
                        (src[mad24(k*rows + idx_row_low(y - j, rows - 1), srcStep, xExt)] +
                         src[mad24(k*rows + idx_row_high(y + j, rows - 1), srcStep, xExt)]) * c_gKer[j];
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    if (y < rows && y >= 0 && x < cols && x >= 0)
    {
        // Horizontal pass

        row += tx + ksizeHalf;
        float res[5];

#pragma unroll
        for (int k = 0; k < 5; ++k)
            res[k] = row[k*smw] * c_gKer[0];

        for (int i = 1; i <= ksizeHalf; ++i)
#pragma unroll
            for (int k = 0; k < 5; ++k)
                res[k] += (row[k*smw - i] + row[k*smw + i]) * c_gKer[i];

#pragma unroll
        for (int k = 0; k < 5; ++k)
            dst[mad24(k*rows + y, dstStep, x)] = res[k];
    }
}
__constant float c_border[BORDER_SIZE + 1] = { 0.14f, 0.14f, 0.4472f, 0.4472f, 0.4472f, 1.f };

__kernel void updateMatrices(__global const float * flowx, int xStep,
                             __global const float * flowy, int yStep,
                             const int rows, const int cols,
                             __global const float * R0, int R0Step,
                             __global const float * R1, int R1Step,
                             __global float * M, int mStep)
{
    const int y = get_global_id(1);
    const int x = get_global_id(0);

    if (y < rows && y >= 0 && x < cols && x >= 0)
    {
        float dx = flowx[mad24(y, xStep, x)];
        float dy = flowy[mad24(y, yStep, x)];
        float fx = x + dx;
        float fy = y + dy;

        int x1 = convert_int(floor(fx));
        int y1 = convert_int(floor(fy));
        fx -= x1;
        fy -= y1;

        float r2, r3, r4, r5, r6;

        if (x1 >= 0 && y1 >= 0 && x1 < cols - 1 && y1 < rows - 1)
        {
            float a00 = (1.f - fx) * (1.f - fy);
            float a01 = fx * (1.f - fy);
            float a10 = (1.f - fx) * fy;
            float a11 = fx * fy;

            r2 = a00 * R1[mad24(y1, R1Step, x1)] +
                 a01 * R1[mad24(y1, R1Step, x1 + 1)] +
                 a10 * R1[mad24(y1 + 1, R1Step, x1)] +
                 a11 * R1[mad24(y1 + 1, R1Step, x1 + 1)];

            r3 = a00 * R1[mad24(rows + y1, R1Step, x1)] +
                 a01 * R1[mad24(rows + y1, R1Step, x1 + 1)] +
                 a10 * R1[mad24(rows + y1 + 1, R1Step, x1)] +
                 a11 * R1[mad24(rows + y1 + 1, R1Step, x1 + 1)];

            r4 = a00 * R1[mad24(2*rows + y1, R1Step, x1)] +
                 a01 * R1[mad24(2*rows + y1, R1Step, x1 + 1)] +
                 a10 * R1[mad24(2*rows + y1 + 1, R1Step, x1)] +
                 a11 * R1[mad24(2*rows + y1 + 1, R1Step, x1 + 1)];

            r5 = a00 * R1[mad24(3*rows + y1, R1Step, x1)] +
                 a01 * R1[mad24(3*rows + y1, R1Step, x1 + 1)] +
                 a10 * R1[mad24(3*rows + y1 + 1, R1Step, x1)] +
                 a11 * R1[mad24(3*rows + y1 + 1, R1Step, x1 + 1)];

            r6 = a00 * R1[mad24(4*rows + y1, R1Step, x1)] +
                 a01 * R1[mad24(4*rows + y1, R1Step, x1 + 1)] +
                 a10 * R1[mad24(4*rows + y1 + 1, R1Step, x1)] +
                 a11 * R1[mad24(4*rows + y1 + 1, R1Step, x1 + 1)];

            r4 = (R0[mad24(2*rows + y, R0Step, x)] + r4) * 0.5f;
            r5 = (R0[mad24(3*rows + y, R0Step, x)] + r5) * 0.5f;
            r6 = (R0[mad24(4*rows + y, R0Step, x)] + r6) * 0.25f;
        }
        else
        {
            r2 = r3 = 0.f;
            r4 = R0[mad24(2*rows + y, R0Step, x)];
            r5 = R0[mad24(3*rows + y, R0Step, x)];
            r6 = R0[mad24(4*rows + y, R0Step, x)] * 0.5f;
        }

        r2 = (R0[mad24(y, R0Step, x)] - r2) * 0.5f;
        r3 = (R0[mad24(rows + y, R0Step, x)] - r3) * 0.5f;

        r2 += r4*dy + r6*dx;
        r3 += r6*dy + r5*dx;

        float scale =
            c_border[min(x, BORDER_SIZE)] *
            c_border[min(y, BORDER_SIZE)] *
            c_border[min(cols - x - 1, BORDER_SIZE)] *
            c_border[min(rows - y - 1, BORDER_SIZE)];

        r2 *= scale;
        r3 *= scale;
        r4 *= scale;
        r5 *= scale;
        r6 *= scale;

        M[mad24(y, mStep, x)] = r4*r4 + r6*r6;
        M[mad24(rows + y, mStep, x)] = (r4 + r5)*r6;
        M[mad24(2*rows + y, mStep, x)] = r5*r5 + r6*r6;
        M[mad24(3*rows + y, mStep, x)] = r4*r2 + r6*r3;
        M[mad24(4*rows + y, mStep, x)] = r6*r2 + r5*r3;
    }
}

__kernel void boxFilter5(__global const float * src, int srcStep,
                         __global float * dst, int dstStep,
                         const int rows, const  int cols,
                         const int ksizeHalf,
                         __local float * smem)
{
    const int y = get_global_id(1);
    const int x = get_global_id(0);

    const float boxAreaInv = 1.f / ((1 + 2*ksizeHalf) * (1 + 2*ksizeHalf));
    const int smw = bdx + 2*ksizeHalf; // shared memory "width"
    __local float *row = smem + 5 * ty * smw;

    if (y < rows)
    {
        // Vertical pass
        for (int i = tx; i < bdx + 2*ksizeHalf; i += bdx)
        {
            int xExt = (int)(bx * bdx) + i - ksizeHalf;
            xExt = min(max(xExt, 0), cols - 1);

#pragma unroll
            for (int k = 0; k < 5; ++k)
                row[k*smw + i] = src[mad24(k*rows + y, srcStep, xExt)];

            for (int j = 1; j <= ksizeHalf; ++j)
#pragma unroll
                for (int k = 0; k < 5; ++k)
                    row[k*smw + i] +=
                        src[mad24(k*rows + max(y - j, 0), srcStep, xExt)] +
                        src[mad24(k*rows + min(y + j, rows - 1), srcStep, xExt)];
        }
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    if (y < rows && y >= 0 && x < cols && x >= 0)
    {
        // Horizontal pass

        row += tx + ksizeHalf;
        float res[5];

#pragma unroll
        for (int k = 0; k < 5; ++k)
            res[k] = row[k*smw];

        for (int i = 1; i <= ksizeHalf; ++i)
#pragma unroll
            for (int k = 0; k < 5; ++k)
                res[k] += row[k*smw - i] + row[k*smw + i];

#pragma unroll
        for (int k = 0; k < 5; ++k)
            dst[mad24(k*rows + y, dstStep, x)] = res[k] * boxAreaInv;
    }
}

__kernel void updateFlow(__global const float * M, int mStep,
                         __global float * flowx, int xStep,
                         __global float * flowy, int yStep,
                         const int rows, const int cols)
{
    const int y = get_global_id(1);
    const int x = get_global_id(0);

    if (y < rows && y >= 0 && x < cols && x >= 0)
    {
        float g11 = M[mad24(y, mStep, x)];
        float g12 = M[mad24(rows + y, mStep, x)];
        float g22 = M[mad24(2*rows + y, mStep, x)];
        float h1 =  M[mad24(3*rows + y, mStep, x)];
        float h2 =  M[mad24(4*rows + y, mStep, x)];

        float detInv = 1.f / (g11*g22 - g12*g12 + 1e-3f);

        flowx[mad24(y, xStep, x)] = (g11*h2 - g12*h1) * detInv;
        flowy[mad24(y, yStep, x)] = (g22*h1 - g12*h2) * detInv;
    }
}
