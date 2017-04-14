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

#if !defined CUDA_DISABLER

#include "opencv2/core/cuda/common.hpp"
#include "opencv2/core/cuda/transform.hpp"
#include "opencv2/core/cuda/functional.hpp"
#include "opencv2/core/cuda/reduce.hpp"

namespace cv { namespace cuda { namespace device
{
    #define SOLVE_PNP_RANSAC_MAX_NUM_ITERS 200

    namespace transform_points
    {
        __constant__ float3 crot0;
        __constant__ float3 crot1;
        __constant__ float3 crot2;
        __constant__ float3 ctransl;

        struct TransformOp : unary_function<float3, float3>
        {
            __device__ __forceinline__ float3 operator()(const float3& p) const
            {
                return make_float3(
                        crot0.x * p.x + crot0.y * p.y + crot0.z * p.z + ctransl.x,
                        crot1.x * p.x + crot1.y * p.y + crot1.z * p.z + ctransl.y,
                        crot2.x * p.x + crot2.y * p.y + crot2.z * p.z + ctransl.z);
            }
            __host__ __device__ __forceinline__ TransformOp() {}
            __host__ __device__ __forceinline__ TransformOp(const TransformOp&) {}
        };

        void call(const PtrStepSz<float3> src, const float* rot,
                  const float* transl, PtrStepSz<float3> dst,
                  cudaStream_t stream)
        {
            cudaSafeCall(cudaMemcpyToSymbol(crot0, rot, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(crot1, rot + 3, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(crot2, rot + 6, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(ctransl, transl, sizeof(float) * 3));
            cv::cuda::device::transform(src, dst, TransformOp(), WithOutMask(), stream);
        }
    } // namespace transform_points

    namespace project_points
    {
        __constant__ float3 crot0;
        __constant__ float3 crot1;
        __constant__ float3 crot2;
        __constant__ float3 ctransl;
        __constant__ float3 cproj0;
        __constant__ float3 cproj1;

        struct ProjectOp : unary_function<float3, float3>
        {
            __device__ __forceinline__ float2 operator()(const float3& p) const
            {
                // Rotate and translate in 3D
                float3 t = make_float3(
                        crot0.x * p.x + crot0.y * p.y + crot0.z * p.z + ctransl.x,
                        crot1.x * p.x + crot1.y * p.y + crot1.z * p.z + ctransl.y,
                        crot2.x * p.x + crot2.y * p.y + crot2.z * p.z + ctransl.z);
                // Project on 2D plane
                return make_float2(
                        (cproj0.x * t.x + cproj0.y * t.y) / t.z + cproj0.z,
                        (cproj1.x * t.x + cproj1.y * t.y) / t.z + cproj1.z);
            }
            __host__ __device__ __forceinline__ ProjectOp() {}
            __host__ __device__ __forceinline__ ProjectOp(const ProjectOp&) {}
        };

        void call(const PtrStepSz<float3> src, const float* rot,
                  const float* transl, const float* proj, PtrStepSz<float2> dst,
                  cudaStream_t stream)
        {
            cudaSafeCall(cudaMemcpyToSymbol(crot0, rot, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(crot1, rot + 3, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(crot2, rot + 6, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(ctransl, transl, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(cproj0, proj, sizeof(float) * 3));
            cudaSafeCall(cudaMemcpyToSymbol(cproj1, proj + 3, sizeof(float) * 3));
            cv::cuda::device::transform(src, dst, ProjectOp(), WithOutMask(), stream);
        }
    } // namespace project_points

    namespace solve_pnp_ransac
    {
        __constant__ float3 crot_matrices[SOLVE_PNP_RANSAC_MAX_NUM_ITERS * 3];
        __constant__ float3 ctransl_vectors[SOLVE_PNP_RANSAC_MAX_NUM_ITERS];

        int maxNumIters()
        {
            return SOLVE_PNP_RANSAC_MAX_NUM_ITERS;
        }

        __device__ __forceinline__ float sqr(float x)
        {
            return x * x;
        }

        template <int BLOCK_SIZE>
        __global__ void computeHypothesisScoresKernel(
                const int num_points, const float3* object, const float2* image,
                const float dist_threshold, int* g_num_inliers)
        {
            const float3* const &rot_mat = crot_matrices + blockIdx.x * 3;
            const float3 &transl_vec = ctransl_vectors[blockIdx.x];
            int num_inliers = 0;

            for (int i = threadIdx.x; i < num_points; i += blockDim.x)
            {
                float3 p = object[i];
                p = make_float3(
                        rot_mat[0].x * p.x + rot_mat[0].y * p.y + rot_mat[0].z * p.z + transl_vec.x,
                        rot_mat[1].x * p.x + rot_mat[1].y * p.y + rot_mat[1].z * p.z + transl_vec.y,
                        rot_mat[2].x * p.x + rot_mat[2].y * p.y + rot_mat[2].z * p.z + transl_vec.z);
                p.x /= p.z;
                p.y /= p.z;
                float2 image_p = image[i];
                if (sqr(p.x - image_p.x) + sqr(p.y - image_p.y) < dist_threshold)
                    ++num_inliers;
            }

            __shared__ int s_num_inliers[BLOCK_SIZE];
            reduce<BLOCK_SIZE>(s_num_inliers, num_inliers, threadIdx.x, plus<int>());

            if (threadIdx.x == 0)
                g_num_inliers[blockIdx.x] = num_inliers;
        }

        void computeHypothesisScores(
                const int num_hypotheses, const int num_points, const float* rot_matrices,
                const float3* transl_vectors, const float3* object, const float2* image,
                const float dist_threshold, int* hypothesis_scores)
        {
            cudaSafeCall(cudaMemcpyToSymbol(crot_matrices, rot_matrices, num_hypotheses * 3 * sizeof(float3)));
            cudaSafeCall(cudaMemcpyToSymbol(ctransl_vectors, transl_vectors, num_hypotheses * sizeof(float3)));

            dim3 threads(256);
            dim3 grid(num_hypotheses);

            computeHypothesisScoresKernel<256><<<grid, threads>>>(
                    num_points, object, image, dist_threshold, hypothesis_scores);
            cudaSafeCall( cudaGetLastError() );

            cudaSafeCall( cudaDeviceSynchronize() );
        }
    } // namespace solvepnp_ransac
}}} // namespace cv { namespace cuda { namespace cudev


#endif /* CUDA_DISABLER */
