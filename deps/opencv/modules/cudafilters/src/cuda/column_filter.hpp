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

#include "opencv2/core/cuda/common.hpp"
#include "opencv2/core/cuda/saturate_cast.hpp"
#include "opencv2/core/cuda/vec_math.hpp"
#include "opencv2/core/cuda/border_interpolate.hpp"

using namespace cv::cuda;
using namespace cv::cuda::device;

namespace column_filter
{
    #define MAX_KERNEL_SIZE 32

    __constant__ float c_kernel[MAX_KERNEL_SIZE];

    template <int KSIZE, typename T, typename D, typename B>
    __global__ void linearColumnFilter(const PtrStepSz<T> src, PtrStep<D> dst, const int anchor, const B brd)
    {
        #if defined(__CUDA_ARCH__) && (__CUDA_ARCH__ >= 200)
            const int BLOCK_DIM_X = 16;
            const int BLOCK_DIM_Y = 16;
            const int PATCH_PER_BLOCK = 4;
            const int HALO_SIZE = KSIZE <= 16 ? 1 : 2;
        #else
            const int BLOCK_DIM_X = 16;
            const int BLOCK_DIM_Y = 8;
            const int PATCH_PER_BLOCK = 2;
            const int HALO_SIZE = 2;
        #endif

        typedef typename TypeVec<float, VecTraits<T>::cn>::vec_type sum_t;

        __shared__ sum_t smem[(PATCH_PER_BLOCK + 2 * HALO_SIZE) * BLOCK_DIM_Y][BLOCK_DIM_X];

        const int x = blockIdx.x * BLOCK_DIM_X + threadIdx.x;

        if (x >= src.cols)
            return;

        const T* src_col = src.ptr() + x;

        const int yStart = blockIdx.y * (BLOCK_DIM_Y * PATCH_PER_BLOCK) + threadIdx.y;

        if (blockIdx.y > 0)
        {
            //Upper halo
            #pragma unroll
            for (int j = 0; j < HALO_SIZE; ++j)
                smem[threadIdx.y + j * BLOCK_DIM_Y][threadIdx.x] = saturate_cast<sum_t>(src(yStart - (HALO_SIZE - j) * BLOCK_DIM_Y, x));
        }
        else
        {
            //Upper halo
            #pragma unroll
            for (int j = 0; j < HALO_SIZE; ++j)
                smem[threadIdx.y + j * BLOCK_DIM_Y][threadIdx.x] = saturate_cast<sum_t>(brd.at_low(yStart - (HALO_SIZE - j) * BLOCK_DIM_Y, src_col, src.step));
        }

        if (blockIdx.y + 2 < gridDim.y)
        {
            //Main data
            #pragma unroll
            for (int j = 0; j < PATCH_PER_BLOCK; ++j)
                smem[threadIdx.y + HALO_SIZE * BLOCK_DIM_Y + j * BLOCK_DIM_Y][threadIdx.x] = saturate_cast<sum_t>(src(yStart + j * BLOCK_DIM_Y, x));

            //Lower halo
            #pragma unroll
            for (int j = 0; j < HALO_SIZE; ++j)
                smem[threadIdx.y + (PATCH_PER_BLOCK + HALO_SIZE) * BLOCK_DIM_Y + j * BLOCK_DIM_Y][threadIdx.x] = saturate_cast<sum_t>(src(yStart + (PATCH_PER_BLOCK + j) * BLOCK_DIM_Y, x));
        }
        else
        {
            //Main data
            #pragma unroll
            for (int j = 0; j < PATCH_PER_BLOCK; ++j)
                smem[threadIdx.y + HALO_SIZE * BLOCK_DIM_Y + j * BLOCK_DIM_Y][threadIdx.x] = saturate_cast<sum_t>(brd.at_high(yStart + j * BLOCK_DIM_Y, src_col, src.step));

            //Lower halo
            #pragma unroll
            for (int j = 0; j < HALO_SIZE; ++j)
                smem[threadIdx.y + (PATCH_PER_BLOCK + HALO_SIZE) * BLOCK_DIM_Y + j * BLOCK_DIM_Y][threadIdx.x] = saturate_cast<sum_t>(brd.at_high(yStart + (PATCH_PER_BLOCK + j) * BLOCK_DIM_Y, src_col, src.step));
        }

        __syncthreads();

        #pragma unroll
        for (int j = 0; j < PATCH_PER_BLOCK; ++j)
        {
            const int y = yStart + j * BLOCK_DIM_Y;

            if (y < src.rows)
            {
                sum_t sum = VecTraits<sum_t>::all(0);

                #pragma unroll
                for (int k = 0; k < KSIZE; ++k)
                    sum = sum + smem[threadIdx.y + HALO_SIZE * BLOCK_DIM_Y + j * BLOCK_DIM_Y - anchor + k][threadIdx.x] * c_kernel[k];

                dst(y, x) = saturate_cast<D>(sum);
            }
        }
    }

    template <int KSIZE, typename T, typename D, template<typename> class B>
    void caller(PtrStepSz<T> src, PtrStepSz<D> dst, int anchor, int cc, cudaStream_t stream)
    {
        int BLOCK_DIM_X;
        int BLOCK_DIM_Y;
        int PATCH_PER_BLOCK;

        if (cc >= 20)
        {
            BLOCK_DIM_X = 16;
            BLOCK_DIM_Y = 16;
            PATCH_PER_BLOCK = 4;
        }
        else
        {
            BLOCK_DIM_X = 16;
            BLOCK_DIM_Y = 8;
            PATCH_PER_BLOCK = 2;
        }

        const dim3 block(BLOCK_DIM_X, BLOCK_DIM_Y);
        const dim3 grid(divUp(src.cols, BLOCK_DIM_X), divUp(src.rows, BLOCK_DIM_Y * PATCH_PER_BLOCK));

        B<T> brd(src.rows);

        linearColumnFilter<KSIZE, T, D><<<grid, block, 0, stream>>>(src, dst, anchor, brd);

        cudaSafeCall( cudaGetLastError() );

        if (stream == 0)
            cudaSafeCall( cudaDeviceSynchronize() );
    }
}

namespace filter
{
    template <typename T, typename D>
    void linearColumn(PtrStepSzb src, PtrStepSzb dst, const float* kernel, int ksize, int anchor, int brd_type, int cc, cudaStream_t stream)
    {
        typedef void (*caller_t)(PtrStepSz<T> src, PtrStepSz<D> dst, int anchor, int cc, cudaStream_t stream);

        static const caller_t callers[5][33] =
        {
            {
                0,
                column_filter::caller< 1, T, D, BrdColConstant>,
                column_filter::caller< 2, T, D, BrdColConstant>,
                column_filter::caller< 3, T, D, BrdColConstant>,
                column_filter::caller< 4, T, D, BrdColConstant>,
                column_filter::caller< 5, T, D, BrdColConstant>,
                column_filter::caller< 6, T, D, BrdColConstant>,
                column_filter::caller< 7, T, D, BrdColConstant>,
                column_filter::caller< 8, T, D, BrdColConstant>,
                column_filter::caller< 9, T, D, BrdColConstant>,
                column_filter::caller<10, T, D, BrdColConstant>,
                column_filter::caller<11, T, D, BrdColConstant>,
                column_filter::caller<12, T, D, BrdColConstant>,
                column_filter::caller<13, T, D, BrdColConstant>,
                column_filter::caller<14, T, D, BrdColConstant>,
                column_filter::caller<15, T, D, BrdColConstant>,
                column_filter::caller<16, T, D, BrdColConstant>,
                column_filter::caller<17, T, D, BrdColConstant>,
                column_filter::caller<18, T, D, BrdColConstant>,
                column_filter::caller<19, T, D, BrdColConstant>,
                column_filter::caller<20, T, D, BrdColConstant>,
                column_filter::caller<21, T, D, BrdColConstant>,
                column_filter::caller<22, T, D, BrdColConstant>,
                column_filter::caller<23, T, D, BrdColConstant>,
                column_filter::caller<24, T, D, BrdColConstant>,
                column_filter::caller<25, T, D, BrdColConstant>,
                column_filter::caller<26, T, D, BrdColConstant>,
                column_filter::caller<27, T, D, BrdColConstant>,
                column_filter::caller<28, T, D, BrdColConstant>,
                column_filter::caller<29, T, D, BrdColConstant>,
                column_filter::caller<30, T, D, BrdColConstant>,
                column_filter::caller<31, T, D, BrdColConstant>,
                column_filter::caller<32, T, D, BrdColConstant>
            },
            {
                0,
                column_filter::caller< 1, T, D, BrdColReplicate>,
                column_filter::caller< 2, T, D, BrdColReplicate>,
                column_filter::caller< 3, T, D, BrdColReplicate>,
                column_filter::caller< 4, T, D, BrdColReplicate>,
                column_filter::caller< 5, T, D, BrdColReplicate>,
                column_filter::caller< 6, T, D, BrdColReplicate>,
                column_filter::caller< 7, T, D, BrdColReplicate>,
                column_filter::caller< 8, T, D, BrdColReplicate>,
                column_filter::caller< 9, T, D, BrdColReplicate>,
                column_filter::caller<10, T, D, BrdColReplicate>,
                column_filter::caller<11, T, D, BrdColReplicate>,
                column_filter::caller<12, T, D, BrdColReplicate>,
                column_filter::caller<13, T, D, BrdColReplicate>,
                column_filter::caller<14, T, D, BrdColReplicate>,
                column_filter::caller<15, T, D, BrdColReplicate>,
                column_filter::caller<16, T, D, BrdColReplicate>,
                column_filter::caller<17, T, D, BrdColReplicate>,
                column_filter::caller<18, T, D, BrdColReplicate>,
                column_filter::caller<19, T, D, BrdColReplicate>,
                column_filter::caller<20, T, D, BrdColReplicate>,
                column_filter::caller<21, T, D, BrdColReplicate>,
                column_filter::caller<22, T, D, BrdColReplicate>,
                column_filter::caller<23, T, D, BrdColReplicate>,
                column_filter::caller<24, T, D, BrdColReplicate>,
                column_filter::caller<25, T, D, BrdColReplicate>,
                column_filter::caller<26, T, D, BrdColReplicate>,
                column_filter::caller<27, T, D, BrdColReplicate>,
                column_filter::caller<28, T, D, BrdColReplicate>,
                column_filter::caller<29, T, D, BrdColReplicate>,
                column_filter::caller<30, T, D, BrdColReplicate>,
                column_filter::caller<31, T, D, BrdColReplicate>,
                column_filter::caller<32, T, D, BrdColReplicate>
            },
            {
                0,
                column_filter::caller< 1, T, D, BrdColReflect>,
                column_filter::caller< 2, T, D, BrdColReflect>,
                column_filter::caller< 3, T, D, BrdColReflect>,
                column_filter::caller< 4, T, D, BrdColReflect>,
                column_filter::caller< 5, T, D, BrdColReflect>,
                column_filter::caller< 6, T, D, BrdColReflect>,
                column_filter::caller< 7, T, D, BrdColReflect>,
                column_filter::caller< 8, T, D, BrdColReflect>,
                column_filter::caller< 9, T, D, BrdColReflect>,
                column_filter::caller<10, T, D, BrdColReflect>,
                column_filter::caller<11, T, D, BrdColReflect>,
                column_filter::caller<12, T, D, BrdColReflect>,
                column_filter::caller<13, T, D, BrdColReflect>,
                column_filter::caller<14, T, D, BrdColReflect>,
                column_filter::caller<15, T, D, BrdColReflect>,
                column_filter::caller<16, T, D, BrdColReflect>,
                column_filter::caller<17, T, D, BrdColReflect>,
                column_filter::caller<18, T, D, BrdColReflect>,
                column_filter::caller<19, T, D, BrdColReflect>,
                column_filter::caller<20, T, D, BrdColReflect>,
                column_filter::caller<21, T, D, BrdColReflect>,
                column_filter::caller<22, T, D, BrdColReflect>,
                column_filter::caller<23, T, D, BrdColReflect>,
                column_filter::caller<24, T, D, BrdColReflect>,
                column_filter::caller<25, T, D, BrdColReflect>,
                column_filter::caller<26, T, D, BrdColReflect>,
                column_filter::caller<27, T, D, BrdColReflect>,
                column_filter::caller<28, T, D, BrdColReflect>,
                column_filter::caller<29, T, D, BrdColReflect>,
                column_filter::caller<30, T, D, BrdColReflect>,
                column_filter::caller<31, T, D, BrdColReflect>,
                column_filter::caller<32, T, D, BrdColReflect>
            },
            {
                0,
                column_filter::caller< 1, T, D, BrdColWrap>,
                column_filter::caller< 2, T, D, BrdColWrap>,
                column_filter::caller< 3, T, D, BrdColWrap>,
                column_filter::caller< 4, T, D, BrdColWrap>,
                column_filter::caller< 5, T, D, BrdColWrap>,
                column_filter::caller< 6, T, D, BrdColWrap>,
                column_filter::caller< 7, T, D, BrdColWrap>,
                column_filter::caller< 8, T, D, BrdColWrap>,
                column_filter::caller< 9, T, D, BrdColWrap>,
                column_filter::caller<10, T, D, BrdColWrap>,
                column_filter::caller<11, T, D, BrdColWrap>,
                column_filter::caller<12, T, D, BrdColWrap>,
                column_filter::caller<13, T, D, BrdColWrap>,
                column_filter::caller<14, T, D, BrdColWrap>,
                column_filter::caller<15, T, D, BrdColWrap>,
                column_filter::caller<16, T, D, BrdColWrap>,
                column_filter::caller<17, T, D, BrdColWrap>,
                column_filter::caller<18, T, D, BrdColWrap>,
                column_filter::caller<19, T, D, BrdColWrap>,
                column_filter::caller<20, T, D, BrdColWrap>,
                column_filter::caller<21, T, D, BrdColWrap>,
                column_filter::caller<22, T, D, BrdColWrap>,
                column_filter::caller<23, T, D, BrdColWrap>,
                column_filter::caller<24, T, D, BrdColWrap>,
                column_filter::caller<25, T, D, BrdColWrap>,
                column_filter::caller<26, T, D, BrdColWrap>,
                column_filter::caller<27, T, D, BrdColWrap>,
                column_filter::caller<28, T, D, BrdColWrap>,
                column_filter::caller<29, T, D, BrdColWrap>,
                column_filter::caller<30, T, D, BrdColWrap>,
                column_filter::caller<31, T, D, BrdColWrap>,
                column_filter::caller<32, T, D, BrdColWrap>
            },
            {
                0,
                column_filter::caller< 1, T, D, BrdColReflect101>,
                column_filter::caller< 2, T, D, BrdColReflect101>,
                column_filter::caller< 3, T, D, BrdColReflect101>,
                column_filter::caller< 4, T, D, BrdColReflect101>,
                column_filter::caller< 5, T, D, BrdColReflect101>,
                column_filter::caller< 6, T, D, BrdColReflect101>,
                column_filter::caller< 7, T, D, BrdColReflect101>,
                column_filter::caller< 8, T, D, BrdColReflect101>,
                column_filter::caller< 9, T, D, BrdColReflect101>,
                column_filter::caller<10, T, D, BrdColReflect101>,
                column_filter::caller<11, T, D, BrdColReflect101>,
                column_filter::caller<12, T, D, BrdColReflect101>,
                column_filter::caller<13, T, D, BrdColReflect101>,
                column_filter::caller<14, T, D, BrdColReflect101>,
                column_filter::caller<15, T, D, BrdColReflect101>,
                column_filter::caller<16, T, D, BrdColReflect101>,
                column_filter::caller<17, T, D, BrdColReflect101>,
                column_filter::caller<18, T, D, BrdColReflect101>,
                column_filter::caller<19, T, D, BrdColReflect101>,
                column_filter::caller<20, T, D, BrdColReflect101>,
                column_filter::caller<21, T, D, BrdColReflect101>,
                column_filter::caller<22, T, D, BrdColReflect101>,
                column_filter::caller<23, T, D, BrdColReflect101>,
                column_filter::caller<24, T, D, BrdColReflect101>,
                column_filter::caller<25, T, D, BrdColReflect101>,
                column_filter::caller<26, T, D, BrdColReflect101>,
                column_filter::caller<27, T, D, BrdColReflect101>,
                column_filter::caller<28, T, D, BrdColReflect101>,
                column_filter::caller<29, T, D, BrdColReflect101>,
                column_filter::caller<30, T, D, BrdColReflect101>,
                column_filter::caller<31, T, D, BrdColReflect101>,
                column_filter::caller<32, T, D, BrdColReflect101>
            }
        };

        if (stream == 0)
            cudaSafeCall( cudaMemcpyToSymbol(column_filter::c_kernel, kernel, ksize * sizeof(float), 0, cudaMemcpyDeviceToDevice) );
        else
            cudaSafeCall( cudaMemcpyToSymbolAsync(column_filter::c_kernel, kernel, ksize * sizeof(float), 0, cudaMemcpyDeviceToDevice, stream) );

        callers[brd_type][ksize]((PtrStepSz<T>)src, (PtrStepSz<D>)dst, anchor, cc, stream);
    }
}
