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

using namespace cv;
using namespace cv::cuda;

#if !defined (HAVE_CUDA) || defined (CUDA_DISABLER)

Ptr<cv::cuda::ORB> cv::cuda::ORB::create(int, float, int, int, int, int, int, int, int, bool) { throw_no_cuda(); return Ptr<cv::cuda::ORB>(); }

#else /* !defined (HAVE_CUDA) */

namespace cv { namespace cuda { namespace device
{
    namespace orb
    {
        int cull_gpu(int* loc, float* response, int size, int n_points, cudaStream_t stream);

        void HarrisResponses_gpu(PtrStepSzb img, const short2* loc, float* response, const int npoints, int blockSize, float harris_k, cudaStream_t stream);

        void loadUMax(const int* u_max, int count);

        void IC_Angle_gpu(PtrStepSzb image, const short2* loc, float* angle, int npoints, int half_k, cudaStream_t stream);

        void computeOrbDescriptor_gpu(PtrStepb img, const short2* loc, const float* angle, const int npoints,
            const int* pattern_x, const int* pattern_y, PtrStepb desc, int dsize, int WTA_K, cudaStream_t stream);

        void mergeLocation_gpu(const short2* loc, float* x, float* y, int npoints, float scale, cudaStream_t stream);
    }
}}}

namespace
{
    const float HARRIS_K = 0.04f;
    const int DESCRIPTOR_SIZE = 32;

    const int bit_pattern_31_[256 * 4] =
    {
        8,-3, 9,5/*mean (0), correlation (0)*/,
        4,2, 7,-12/*mean (1.12461e-05), correlation (0.0437584)*/,
        -11,9, -8,2/*mean (3.37382e-05), correlation (0.0617409)*/,
        7,-12, 12,-13/*mean (5.62303e-05), correlation (0.0636977)*/,
        2,-13, 2,12/*mean (0.000134953), correlation (0.085099)*/,
        1,-7, 1,6/*mean (0.000528565), correlation (0.0857175)*/,
        -2,-10, -2,-4/*mean (0.0188821), correlation (0.0985774)*/,
        -13,-13, -11,-8/*mean (0.0363135), correlation (0.0899616)*/,
        -13,-3, -12,-9/*mean (0.121806), correlation (0.099849)*/,
        10,4, 11,9/*mean (0.122065), correlation (0.093285)*/,
        -13,-8, -8,-9/*mean (0.162787), correlation (0.0942748)*/,
        -11,7, -9,12/*mean (0.21561), correlation (0.0974438)*/,
        7,7, 12,6/*mean (0.160583), correlation (0.130064)*/,
        -4,-5, -3,0/*mean (0.228171), correlation (0.132998)*/,
        -13,2, -12,-3/*mean (0.00997526), correlation (0.145926)*/,
        -9,0, -7,5/*mean (0.198234), correlation (0.143636)*/,
        12,-6, 12,-1/*mean (0.0676226), correlation (0.16689)*/,
        -3,6, -2,12/*mean (0.166847), correlation (0.171682)*/,
        -6,-13, -4,-8/*mean (0.101215), correlation (0.179716)*/,
        11,-13, 12,-8/*mean (0.200641), correlation (0.192279)*/,
        4,7, 5,1/*mean (0.205106), correlation (0.186848)*/,
        5,-3, 10,-3/*mean (0.234908), correlation (0.192319)*/,
        3,-7, 6,12/*mean (0.0709964), correlation (0.210872)*/,
        -8,-7, -6,-2/*mean (0.0939834), correlation (0.212589)*/,
        -2,11, -1,-10/*mean (0.127778), correlation (0.20866)*/,
        -13,12, -8,10/*mean (0.14783), correlation (0.206356)*/,
        -7,3, -5,-3/*mean (0.182141), correlation (0.198942)*/,
        -4,2, -3,7/*mean (0.188237), correlation (0.21384)*/,
        -10,-12, -6,11/*mean (0.14865), correlation (0.23571)*/,
        5,-12, 6,-7/*mean (0.222312), correlation (0.23324)*/,
        5,-6, 7,-1/*mean (0.229082), correlation (0.23389)*/,
        1,0, 4,-5/*mean (0.241577), correlation (0.215286)*/,
        9,11, 11,-13/*mean (0.00338507), correlation (0.251373)*/,
        4,7, 4,12/*mean (0.131005), correlation (0.257622)*/,
        2,-1, 4,4/*mean (0.152755), correlation (0.255205)*/,
        -4,-12, -2,7/*mean (0.182771), correlation (0.244867)*/,
        -8,-5, -7,-10/*mean (0.186898), correlation (0.23901)*/,
        4,11, 9,12/*mean (0.226226), correlation (0.258255)*/,
        0,-8, 1,-13/*mean (0.0897886), correlation (0.274827)*/,
        -13,-2, -8,2/*mean (0.148774), correlation (0.28065)*/,
        -3,-2, -2,3/*mean (0.153048), correlation (0.283063)*/,
        -6,9, -4,-9/*mean (0.169523), correlation (0.278248)*/,
        8,12, 10,7/*mean (0.225337), correlation (0.282851)*/,
        0,9, 1,3/*mean (0.226687), correlation (0.278734)*/,
        7,-5, 11,-10/*mean (0.00693882), correlation (0.305161)*/,
        -13,-6, -11,0/*mean (0.0227283), correlation (0.300181)*/,
        10,7, 12,1/*mean (0.125517), correlation (0.31089)*/,
        -6,-3, -6,12/*mean (0.131748), correlation (0.312779)*/,
        10,-9, 12,-4/*mean (0.144827), correlation (0.292797)*/,
        -13,8, -8,-12/*mean (0.149202), correlation (0.308918)*/,
        -13,0, -8,-4/*mean (0.160909), correlation (0.310013)*/,
        3,3, 7,8/*mean (0.177755), correlation (0.309394)*/,
        5,7, 10,-7/*mean (0.212337), correlation (0.310315)*/,
        -1,7, 1,-12/*mean (0.214429), correlation (0.311933)*/,
        3,-10, 5,6/*mean (0.235807), correlation (0.313104)*/,
        2,-4, 3,-10/*mean (0.00494827), correlation (0.344948)*/,
        -13,0, -13,5/*mean (0.0549145), correlation (0.344675)*/,
        -13,-7, -12,12/*mean (0.103385), correlation (0.342715)*/,
        -13,3, -11,8/*mean (0.134222), correlation (0.322922)*/,
        -7,12, -4,7/*mean (0.153284), correlation (0.337061)*/,
        6,-10, 12,8/*mean (0.154881), correlation (0.329257)*/,
        -9,-1, -7,-6/*mean (0.200967), correlation (0.33312)*/,
        -2,-5, 0,12/*mean (0.201518), correlation (0.340635)*/,
        -12,5, -7,5/*mean (0.207805), correlation (0.335631)*/,
        3,-10, 8,-13/*mean (0.224438), correlation (0.34504)*/,
        -7,-7, -4,5/*mean (0.239361), correlation (0.338053)*/,
        -3,-2, -1,-7/*mean (0.240744), correlation (0.344322)*/,
        2,9, 5,-11/*mean (0.242949), correlation (0.34145)*/,
        -11,-13, -5,-13/*mean (0.244028), correlation (0.336861)*/,
        -1,6, 0,-1/*mean (0.247571), correlation (0.343684)*/,
        5,-3, 5,2/*mean (0.000697256), correlation (0.357265)*/,
        -4,-13, -4,12/*mean (0.00213675), correlation (0.373827)*/,
        -9,-6, -9,6/*mean (0.0126856), correlation (0.373938)*/,
        -12,-10, -8,-4/*mean (0.0152497), correlation (0.364237)*/,
        10,2, 12,-3/*mean (0.0299933), correlation (0.345292)*/,
        7,12, 12,12/*mean (0.0307242), correlation (0.366299)*/,
        -7,-13, -6,5/*mean (0.0534975), correlation (0.368357)*/,
        -4,9, -3,4/*mean (0.099865), correlation (0.372276)*/,
        7,-1, 12,2/*mean (0.117083), correlation (0.364529)*/,
        -7,6, -5,1/*mean (0.126125), correlation (0.369606)*/,
        -13,11, -12,5/*mean (0.130364), correlation (0.358502)*/,
        -3,7, -2,-6/*mean (0.131691), correlation (0.375531)*/,
        7,-8, 12,-7/*mean (0.160166), correlation (0.379508)*/,
        -13,-7, -11,-12/*mean (0.167848), correlation (0.353343)*/,
        1,-3, 12,12/*mean (0.183378), correlation (0.371916)*/,
        2,-6, 3,0/*mean (0.228711), correlation (0.371761)*/,
        -4,3, -2,-13/*mean (0.247211), correlation (0.364063)*/,
        -1,-13, 1,9/*mean (0.249325), correlation (0.378139)*/,
        7,1, 8,-6/*mean (0.000652272), correlation (0.411682)*/,
        1,-1, 3,12/*mean (0.00248538), correlation (0.392988)*/,
        9,1, 12,6/*mean (0.0206815), correlation (0.386106)*/,
        -1,-9, -1,3/*mean (0.0364485), correlation (0.410752)*/,
        -13,-13, -10,5/*mean (0.0376068), correlation (0.398374)*/,
        7,7, 10,12/*mean (0.0424202), correlation (0.405663)*/,
        12,-5, 12,9/*mean (0.0942645), correlation (0.410422)*/,
        6,3, 7,11/*mean (0.1074), correlation (0.413224)*/,
        5,-13, 6,10/*mean (0.109256), correlation (0.408646)*/,
        2,-12, 2,3/*mean (0.131691), correlation (0.416076)*/,
        3,8, 4,-6/*mean (0.165081), correlation (0.417569)*/,
        2,6, 12,-13/*mean (0.171874), correlation (0.408471)*/,
        9,-12, 10,3/*mean (0.175146), correlation (0.41296)*/,
        -8,4, -7,9/*mean (0.183682), correlation (0.402956)*/,
        -11,12, -4,-6/*mean (0.184672), correlation (0.416125)*/,
        1,12, 2,-8/*mean (0.191487), correlation (0.386696)*/,
        6,-9, 7,-4/*mean (0.192668), correlation (0.394771)*/,
        2,3, 3,-2/*mean (0.200157), correlation (0.408303)*/,
        6,3, 11,0/*mean (0.204588), correlation (0.411762)*/,
        3,-3, 8,-8/*mean (0.205904), correlation (0.416294)*/,
        7,8, 9,3/*mean (0.213237), correlation (0.409306)*/,
        -11,-5, -6,-4/*mean (0.243444), correlation (0.395069)*/,
        -10,11, -5,10/*mean (0.247672), correlation (0.413392)*/,
        -5,-8, -3,12/*mean (0.24774), correlation (0.411416)*/,
        -10,5, -9,0/*mean (0.00213675), correlation (0.454003)*/,
        8,-1, 12,-6/*mean (0.0293635), correlation (0.455368)*/,
        4,-6, 6,-11/*mean (0.0404971), correlation (0.457393)*/,
        -10,12, -8,7/*mean (0.0481107), correlation (0.448364)*/,
        4,-2, 6,7/*mean (0.050641), correlation (0.455019)*/,
        -2,0, -2,12/*mean (0.0525978), correlation (0.44338)*/,
        -5,-8, -5,2/*mean (0.0629667), correlation (0.457096)*/,
        7,-6, 10,12/*mean (0.0653846), correlation (0.445623)*/,
        -9,-13, -8,-8/*mean (0.0858749), correlation (0.449789)*/,
        -5,-13, -5,-2/*mean (0.122402), correlation (0.450201)*/,
        8,-8, 9,-13/*mean (0.125416), correlation (0.453224)*/,
        -9,-11, -9,0/*mean (0.130128), correlation (0.458724)*/,
        1,-8, 1,-2/*mean (0.132467), correlation (0.440133)*/,
        7,-4, 9,1/*mean (0.132692), correlation (0.454)*/,
        -2,1, -1,-4/*mean (0.135695), correlation (0.455739)*/,
        11,-6, 12,-11/*mean (0.142904), correlation (0.446114)*/,
        -12,-9, -6,4/*mean (0.146165), correlation (0.451473)*/,
        3,7, 7,12/*mean (0.147627), correlation (0.456643)*/,
        5,5, 10,8/*mean (0.152901), correlation (0.455036)*/,
        0,-4, 2,8/*mean (0.167083), correlation (0.459315)*/,
        -9,12, -5,-13/*mean (0.173234), correlation (0.454706)*/,
        0,7, 2,12/*mean (0.18312), correlation (0.433855)*/,
        -1,2, 1,7/*mean (0.185504), correlation (0.443838)*/,
        5,11, 7,-9/*mean (0.185706), correlation (0.451123)*/,
        3,5, 6,-8/*mean (0.188968), correlation (0.455808)*/,
        -13,-4, -8,9/*mean (0.191667), correlation (0.459128)*/,
        -5,9, -3,-3/*mean (0.193196), correlation (0.458364)*/,
        -4,-7, -3,-12/*mean (0.196536), correlation (0.455782)*/,
        6,5, 8,0/*mean (0.1972), correlation (0.450481)*/,
        -7,6, -6,12/*mean (0.199438), correlation (0.458156)*/,
        -13,6, -5,-2/*mean (0.211224), correlation (0.449548)*/,
        1,-10, 3,10/*mean (0.211718), correlation (0.440606)*/,
        4,1, 8,-4/*mean (0.213034), correlation (0.443177)*/,
        -2,-2, 2,-13/*mean (0.234334), correlation (0.455304)*/,
        2,-12, 12,12/*mean (0.235684), correlation (0.443436)*/,
        -2,-13, 0,-6/*mean (0.237674), correlation (0.452525)*/,
        4,1, 9,3/*mean (0.23962), correlation (0.444824)*/,
        -6,-10, -3,-5/*mean (0.248459), correlation (0.439621)*/,
        -3,-13, -1,1/*mean (0.249505), correlation (0.456666)*/,
        7,5, 12,-11/*mean (0.00119208), correlation (0.495466)*/,
        4,-2, 5,-7/*mean (0.00372245), correlation (0.484214)*/,
        -13,9, -9,-5/*mean (0.00741116), correlation (0.499854)*/,
        7,1, 8,6/*mean (0.0208952), correlation (0.499773)*/,
        7,-8, 7,6/*mean (0.0220085), correlation (0.501609)*/,
        -7,-4, -7,1/*mean (0.0233806), correlation (0.496568)*/,
        -8,11, -7,-8/*mean (0.0236505), correlation (0.489719)*/,
        -13,6, -12,-8/*mean (0.0268781), correlation (0.503487)*/,
        2,4, 3,9/*mean (0.0323324), correlation (0.501938)*/,
        10,-5, 12,3/*mean (0.0399235), correlation (0.494029)*/,
        -6,-5, -6,7/*mean (0.0420153), correlation (0.486579)*/,
        8,-3, 9,-8/*mean (0.0548021), correlation (0.484237)*/,
        2,-12, 2,8/*mean (0.0616622), correlation (0.496642)*/,
        -11,-2, -10,3/*mean (0.0627755), correlation (0.498563)*/,
        -12,-13, -7,-9/*mean (0.0829622), correlation (0.495491)*/,
        -11,0, -10,-5/*mean (0.0843342), correlation (0.487146)*/,
        5,-3, 11,8/*mean (0.0929937), correlation (0.502315)*/,
        -2,-13, -1,12/*mean (0.113327), correlation (0.48941)*/,
        -1,-8, 0,9/*mean (0.132119), correlation (0.467268)*/,
        -13,-11, -12,-5/*mean (0.136269), correlation (0.498771)*/,
        -10,-2, -10,11/*mean (0.142173), correlation (0.498714)*/,
        -3,9, -2,-13/*mean (0.144141), correlation (0.491973)*/,
        2,-3, 3,2/*mean (0.14892), correlation (0.500782)*/,
        -9,-13, -4,0/*mean (0.150371), correlation (0.498211)*/,
        -4,6, -3,-10/*mean (0.152159), correlation (0.495547)*/,
        -4,12, -2,-7/*mean (0.156152), correlation (0.496925)*/,
        -6,-11, -4,9/*mean (0.15749), correlation (0.499222)*/,
        6,-3, 6,11/*mean (0.159211), correlation (0.503821)*/,
        -13,11, -5,5/*mean (0.162427), correlation (0.501907)*/,
        11,11, 12,6/*mean (0.16652), correlation (0.497632)*/,
        7,-5, 12,-2/*mean (0.169141), correlation (0.484474)*/,
        -1,12, 0,7/*mean (0.169456), correlation (0.495339)*/,
        -4,-8, -3,-2/*mean (0.171457), correlation (0.487251)*/,
        -7,1, -6,7/*mean (0.175), correlation (0.500024)*/,
        -13,-12, -8,-13/*mean (0.175866), correlation (0.497523)*/,
        -7,-2, -6,-8/*mean (0.178273), correlation (0.501854)*/,
        -8,5, -6,-9/*mean (0.181107), correlation (0.494888)*/,
        -5,-1, -4,5/*mean (0.190227), correlation (0.482557)*/,
        -13,7, -8,10/*mean (0.196739), correlation (0.496503)*/,
        1,5, 5,-13/*mean (0.19973), correlation (0.499759)*/,
        1,0, 10,-13/*mean (0.204465), correlation (0.49873)*/,
        9,12, 10,-1/*mean (0.209334), correlation (0.49063)*/,
        5,-8, 10,-9/*mean (0.211134), correlation (0.503011)*/,
        -1,11, 1,-13/*mean (0.212), correlation (0.499414)*/,
        -9,-3, -6,2/*mean (0.212168), correlation (0.480739)*/,
        -1,-10, 1,12/*mean (0.212731), correlation (0.502523)*/,
        -13,1, -8,-10/*mean (0.21327), correlation (0.489786)*/,
        8,-11, 10,-6/*mean (0.214159), correlation (0.488246)*/,
        2,-13, 3,-6/*mean (0.216993), correlation (0.50287)*/,
        7,-13, 12,-9/*mean (0.223639), correlation (0.470502)*/,
        -10,-10, -5,-7/*mean (0.224089), correlation (0.500852)*/,
        -10,-8, -8,-13/*mean (0.228666), correlation (0.502629)*/,
        4,-6, 8,5/*mean (0.22906), correlation (0.498305)*/,
        3,12, 8,-13/*mean (0.233378), correlation (0.503825)*/,
        -4,2, -3,-3/*mean (0.234323), correlation (0.476692)*/,
        5,-13, 10,-12/*mean (0.236392), correlation (0.475462)*/,
        4,-13, 5,-1/*mean (0.236842), correlation (0.504132)*/,
        -9,9, -4,3/*mean (0.236977), correlation (0.497739)*/,
        0,3, 3,-9/*mean (0.24314), correlation (0.499398)*/,
        -12,1, -6,1/*mean (0.243297), correlation (0.489447)*/,
        3,2, 4,-8/*mean (0.00155196), correlation (0.553496)*/,
        -10,-10, -10,9/*mean (0.00239541), correlation (0.54297)*/,
        8,-13, 12,12/*mean (0.0034413), correlation (0.544361)*/,
        -8,-12, -6,-5/*mean (0.003565), correlation (0.551225)*/,
        2,2, 3,7/*mean (0.00835583), correlation (0.55285)*/,
        10,6, 11,-8/*mean (0.00885065), correlation (0.540913)*/,
        6,8, 8,-12/*mean (0.0101552), correlation (0.551085)*/,
        -7,10, -6,5/*mean (0.0102227), correlation (0.533635)*/,
        -3,-9, -3,9/*mean (0.0110211), correlation (0.543121)*/,
        -1,-13, -1,5/*mean (0.0113473), correlation (0.550173)*/,
        -3,-7, -3,4/*mean (0.0140913), correlation (0.554774)*/,
        -8,-2, -8,3/*mean (0.017049), correlation (0.55461)*/,
        4,2, 12,12/*mean (0.01778), correlation (0.546921)*/,
        2,-5, 3,11/*mean (0.0224022), correlation (0.549667)*/,
        6,-9, 11,-13/*mean (0.029161), correlation (0.546295)*/,
        3,-1, 7,12/*mean (0.0303081), correlation (0.548599)*/,
        11,-1, 12,4/*mean (0.0355151), correlation (0.523943)*/,
        -3,0, -3,6/*mean (0.0417904), correlation (0.543395)*/,
        4,-11, 4,12/*mean (0.0487292), correlation (0.542818)*/,
        2,-4, 2,1/*mean (0.0575124), correlation (0.554888)*/,
        -10,-6, -8,1/*mean (0.0594242), correlation (0.544026)*/,
        -13,7, -11,1/*mean (0.0597391), correlation (0.550524)*/,
        -13,12, -11,-13/*mean (0.0608974), correlation (0.55383)*/,
        6,0, 11,-13/*mean (0.065126), correlation (0.552006)*/,
        0,-1, 1,4/*mean (0.074224), correlation (0.546372)*/,
        -13,3, -9,-2/*mean (0.0808592), correlation (0.554875)*/,
        -9,8, -6,-3/*mean (0.0883378), correlation (0.551178)*/,
        -13,-6, -8,-2/*mean (0.0901035), correlation (0.548446)*/,
        5,-9, 8,10/*mean (0.0949843), correlation (0.554694)*/,
        2,7, 3,-9/*mean (0.0994152), correlation (0.550979)*/,
        -1,-6, -1,-1/*mean (0.10045), correlation (0.552714)*/,
        9,5, 11,-2/*mean (0.100686), correlation (0.552594)*/,
        11,-3, 12,-8/*mean (0.101091), correlation (0.532394)*/,
        3,0, 3,5/*mean (0.101147), correlation (0.525576)*/,
        -1,4, 0,10/*mean (0.105263), correlation (0.531498)*/,
        3,-6, 4,5/*mean (0.110785), correlation (0.540491)*/,
        -13,0, -10,5/*mean (0.112798), correlation (0.536582)*/,
        5,8, 12,11/*mean (0.114181), correlation (0.555793)*/,
        8,9, 9,-6/*mean (0.117431), correlation (0.553763)*/,
        7,-4, 8,-12/*mean (0.118522), correlation (0.553452)*/,
        -10,4, -10,9/*mean (0.12094), correlation (0.554785)*/,
        7,3, 12,4/*mean (0.122582), correlation (0.555825)*/,
        9,-7, 10,-2/*mean (0.124978), correlation (0.549846)*/,
        7,0, 12,-2/*mean (0.127002), correlation (0.537452)*/,
        -1,-6, 0,-11/*mean (0.127148), correlation (0.547401)*/
    };

    class ORB_Impl : public cv::cuda::ORB
    {
    public:
        ORB_Impl(int nfeatures,
                 float scaleFactor,
                 int nlevels,
                 int edgeThreshold,
                 int firstLevel,
                 int WTA_K,
                 int scoreType,
                 int patchSize,
                 int fastThreshold,
                 bool blurForDescriptor);

        virtual void detectAndCompute(InputArray _image, InputArray _mask, std::vector<KeyPoint>& keypoints, OutputArray _descriptors, bool useProvidedKeypoints);
        virtual void detectAndComputeAsync(InputArray _image, InputArray _mask, OutputArray _keypoints, OutputArray _descriptors, bool useProvidedKeypoints, Stream& stream);

        virtual void convert(InputArray _gpu_keypoints, std::vector<KeyPoint>& keypoints);

        virtual int descriptorSize() const { return kBytes; }
        virtual int descriptorType() const { return CV_8U; }
        virtual int defaultNorm() const { return NORM_HAMMING; }

        virtual void setMaxFeatures(int maxFeatures) { nFeatures_ = maxFeatures; }
        virtual int getMaxFeatures() const { return nFeatures_; }

        virtual void setScaleFactor(double scaleFactor) { scaleFactor_ = scaleFactor; }
        virtual double getScaleFactor() const { return scaleFactor_; }

        virtual void setNLevels(int nlevels) { nLevels_ = nlevels; }
        virtual int getNLevels() const { return nLevels_; }

        virtual void setEdgeThreshold(int edgeThreshold) { edgeThreshold_ = edgeThreshold; }
        virtual int getEdgeThreshold() const { return edgeThreshold_; }

        virtual void setFirstLevel(int firstLevel) { firstLevel_ = firstLevel; }
        virtual int getFirstLevel() const { return firstLevel_; }

        virtual void setWTA_K(int wta_k) { WTA_K_ = wta_k; }
        virtual int getWTA_K() const { return WTA_K_; }

        virtual void setScoreType(int scoreType) { scoreType_ = scoreType; }
        virtual int getScoreType() const { return scoreType_; }

        virtual void setPatchSize(int patchSize) { patchSize_ = patchSize; }
        virtual int getPatchSize() const { return patchSize_; }

        virtual void setFastThreshold(int fastThreshold) { fastThreshold_ = fastThreshold; }
        virtual int getFastThreshold() const { return fastThreshold_; }

        virtual void setBlurForDescriptor(bool blurForDescriptor) { blurForDescriptor_ = blurForDescriptor; }
        virtual bool getBlurForDescriptor() const { return blurForDescriptor_; }

    private:
        int nFeatures_;
        float scaleFactor_;
        int nLevels_;
        int edgeThreshold_;
        int firstLevel_;
        int WTA_K_;
        int scoreType_;
        int patchSize_;
        int fastThreshold_;
        bool blurForDescriptor_;

    private:
        void buildScalePyramids(InputArray _image, InputArray _mask, Stream& stream);
        void computeKeyPointsPyramid(Stream& stream);
        void computeDescriptors(OutputArray _descriptors, Stream& stream);
        void mergeKeyPoints(OutputArray _keypoints, Stream& stream);

    private:
        Ptr<cv::cuda::FastFeatureDetector> fastDetector_;

        //! The number of desired features per scale
        std::vector<size_t> n_features_per_level_;

        //! Points to compute BRIEF descriptors from
        GpuMat pattern_;

        std::vector<GpuMat> imagePyr_;
        std::vector<GpuMat> maskPyr_;

        GpuMat buf_;

        std::vector<GpuMat> keyPointsPyr_;
        std::vector<int> keyPointsCount_;

        Ptr<cuda::Filter> blurFilter_;

        GpuMat d_keypoints_;
    };

    static void initializeOrbPattern(const Point* pattern0, Mat& pattern, int ntuples, int tupleSize, int poolSize)
    {
        RNG rng(0x12345678);

        pattern.create(2, ntuples * tupleSize, CV_32SC1);
        pattern.setTo(Scalar::all(0));

        int* pattern_x_ptr = pattern.ptr<int>(0);
        int* pattern_y_ptr = pattern.ptr<int>(1);

        for (int i = 0; i < ntuples; i++)
        {
            for (int k = 0; k < tupleSize; k++)
            {
                for(;;)
                {
                    int idx = rng.uniform(0, poolSize);
                    Point pt = pattern0[idx];

                    int k1;
                    for (k1 = 0; k1 < k; k1++)
                        if (pattern_x_ptr[tupleSize * i + k1] == pt.x && pattern_y_ptr[tupleSize * i + k1] == pt.y)
                            break;

                    if (k1 == k)
                    {
                        pattern_x_ptr[tupleSize * i + k] = pt.x;
                        pattern_y_ptr[tupleSize * i + k] = pt.y;
                        break;
                    }
                }
            }
        }
    }

    static void makeRandomPattern(int patchSize, Point* pattern, int npoints)
    {
        // we always start with a fixed seed,
        // to make patterns the same on each run
        RNG rng(0x34985739);

        for (int i = 0; i < npoints; i++)
        {
            pattern[i].x = rng.uniform(-patchSize / 2, patchSize / 2 + 1);
            pattern[i].y = rng.uniform(-patchSize / 2, patchSize / 2 + 1);
        }
    }

    ORB_Impl::ORB_Impl(int nFeatures,
                       float scaleFactor,
                       int nLevels,
                       int edgeThreshold,
                       int firstLevel,
                       int WTA_K,
                       int scoreType,
                       int patchSize,
                       int fastThreshold,
                       bool blurForDescriptor) :
        nFeatures_(nFeatures),
        scaleFactor_(scaleFactor),
        nLevels_(nLevels),
        edgeThreshold_(edgeThreshold),
        firstLevel_(firstLevel),
        WTA_K_(WTA_K),
        scoreType_(scoreType),
        patchSize_(patchSize),
        fastThreshold_(fastThreshold),
        blurForDescriptor_(blurForDescriptor)
    {
        CV_Assert( patchSize_ >= 2 );
        CV_Assert( WTA_K_ == 2 || WTA_K_ == 3 || WTA_K_ == 4 );

        fastDetector_ = cuda::FastFeatureDetector::create(fastThreshold_);

        // fill the extractors and descriptors for the corresponding scales
        float factor = 1.0f / scaleFactor_;
        float n_desired_features_per_scale = nFeatures_ * (1.0f - factor) / (1.0f - std::pow(factor, nLevels_));

        n_features_per_level_.resize(nLevels_);
        size_t sum_n_features = 0;
        for (int level = 0; level < nLevels_ - 1; ++level)
        {
            n_features_per_level_[level] = cvRound(n_desired_features_per_scale);
            sum_n_features += n_features_per_level_[level];
            n_desired_features_per_scale *= factor;
        }
        n_features_per_level_[nLevels_ - 1] = nFeatures - sum_n_features;

        // pre-compute the end of a row in a circular patch
        int half_patch_size = patchSize_ / 2;
        std::vector<int> u_max(half_patch_size + 2);
        for (int v = 0; v <= half_patch_size * std::sqrt(2.f) / 2 + 1; ++v)
        {
            u_max[v] = cvRound(std::sqrt(static_cast<float>(half_patch_size * half_patch_size - v * v)));
        }

        // Make sure we are symmetric
        for (int v = half_patch_size, v_0 = 0; v >= half_patch_size * std::sqrt(2.f) / 2; --v)
        {
            while (u_max[v_0] == u_max[v_0 + 1])
                ++v_0;
            u_max[v] = v_0;
            ++v_0;
        }
        CV_Assert( u_max.size() < 32 );
        cv::cuda::device::orb::loadUMax(&u_max[0], static_cast<int>(u_max.size()));

        // Calc pattern
        const int npoints = 512;
        Point pattern_buf[npoints];
        const Point* pattern0 = (const Point*)bit_pattern_31_;
        if (patchSize_ != 31)
        {
            pattern0 = pattern_buf;
            makeRandomPattern(patchSize_, pattern_buf, npoints);
        }

        Mat h_pattern;
        if (WTA_K_ == 2)
        {
            h_pattern.create(2, npoints, CV_32SC1);

            int* pattern_x_ptr = h_pattern.ptr<int>(0);
            int* pattern_y_ptr = h_pattern.ptr<int>(1);

            for (int i = 0; i < npoints; ++i)
            {
                pattern_x_ptr[i] = pattern0[i].x;
                pattern_y_ptr[i] = pattern0[i].y;
            }
        }
        else
        {
            int ntuples = descriptorSize() * 4;
            initializeOrbPattern(pattern0, h_pattern, ntuples, WTA_K_, npoints);
        }

        pattern_.upload(h_pattern);

        blurFilter_ = cuda::createGaussianFilter(CV_8UC1, -1, Size(7, 7), 2, 2, BORDER_REFLECT_101);
    }

    static float getScale(float scaleFactor, int firstLevel, int level)
    {
        return pow(scaleFactor, level - firstLevel);
    }

    void ORB_Impl::detectAndCompute(InputArray _image, InputArray _mask, std::vector<KeyPoint>& keypoints, OutputArray _descriptors, bool useProvidedKeypoints)
    {
        using namespace cv::cuda::device::orb;
        if (useProvidedKeypoints)
        {
            d_keypoints_.release();
            keyPointsPyr_.clear();

            int j, level, nkeypoints = (int)keypoints.size();
            nLevels_ = 0;
            for( j = 0; j < nkeypoints; j++ )
            {
                level = keypoints[j].octave;
                CV_Assert(level >= 0);
                nLevels_ = std::max(nLevels_, level);
            }
            nLevels_ ++;
            std::vector<std::vector<KeyPoint> > oKeypoints(nLevels_);
            for( j = 0; j < nkeypoints; j++ )
            {
                level = keypoints[j].octave;
                oKeypoints[level].push_back(keypoints[j]);
            }
            if (!keypoints.empty())
            {
                keyPointsPyr_.resize(nLevels_);
                keyPointsCount_.resize(nLevels_);
                int t;
                for(t = 0; t < nLevels_; t++) {
                    const std::vector<KeyPoint>& ks = oKeypoints[t];
                    if (!ks.empty()){

                        Mat h_keypoints(ROWS_COUNT, static_cast<int>(ks.size()), CV_32FC1);

                        float sf = getScale(scaleFactor_, firstLevel_, t);
                        float locScale = t != firstLevel_ ? sf : 1.0f;
                        float scale = 1.f/locScale;

                        short2* x_loc_row = h_keypoints.ptr<short2>(0);
                        float* x_kp_hessian = h_keypoints.ptr<float>(1);
                        float* x_kp_dir = h_keypoints.ptr<float>(2);

                        for (size_t i = 0, size = ks.size(); i < size; ++i)
                        {
                            const KeyPoint& kp = ks[i];
                            x_kp_hessian[i] = kp.response;
                            x_loc_row[i].x = cvRound(kp.pt.x * scale);
                            x_loc_row[i].y = cvRound(kp.pt.y * scale);
                            x_kp_dir[i] = kp.angle;

                        }

                        keyPointsPyr_[t].upload(h_keypoints.rowRange(0,3));
                        keyPointsCount_[t] = h_keypoints.cols;
                    }
                }
            }
        }

        detectAndComputeAsync(_image, _mask, d_keypoints_, _descriptors, useProvidedKeypoints, Stream::Null());

        if (!useProvidedKeypoints) {
            convert(d_keypoints_, keypoints);
        }
    }

    void ORB_Impl::detectAndComputeAsync(InputArray _image, InputArray _mask, OutputArray _keypoints, OutputArray _descriptors, bool useProvidedKeypoints, Stream& stream)
    {
        buildScalePyramids(_image, _mask, stream);
        if (!useProvidedKeypoints)
        {
           computeKeyPointsPyramid(stream);
        }
        if (_descriptors.needed())
        {
            computeDescriptors(_descriptors, stream);
        }
        if (!useProvidedKeypoints)
        {
            mergeKeyPoints(_keypoints, stream);
        }
    }

    void ORB_Impl::buildScalePyramids(InputArray _image, InputArray _mask, Stream& stream)
    {
        const GpuMat image = _image.getGpuMat();
        const GpuMat mask = _mask.getGpuMat();

        CV_Assert( image.type() == CV_8UC1 );
        CV_Assert( mask.empty() || (mask.type() == CV_8UC1 && mask.size() == image.size()) );

        imagePyr_.resize(nLevels_);
        maskPyr_.resize(nLevels_);

        for (int level = 0; level < nLevels_; ++level)
        {
            float scale = 1.0f / getScale(scaleFactor_, firstLevel_, level);

            Size sz(cvRound(image.cols * scale), cvRound(image.rows * scale));

            ensureSizeIsEnough(sz, image.type(), imagePyr_[level]);
            ensureSizeIsEnough(sz, CV_8UC1, maskPyr_[level]);
            maskPyr_[level].setTo(Scalar::all(255));

            // Compute the resized image
            if (level != firstLevel_)
            {
                if (level < firstLevel_)
                {
                    cuda::resize(image, imagePyr_[level], sz, 0, 0, INTER_LINEAR, stream);

                    if (!mask.empty())
                        cuda::resize(mask, maskPyr_[level], sz, 0, 0, INTER_LINEAR, stream);
                }
                else
                {
                    cuda::resize(imagePyr_[level - 1], imagePyr_[level], sz, 0, 0, INTER_LINEAR, stream);

                    if (!mask.empty())
                    {
                        cuda::resize(maskPyr_[level - 1], maskPyr_[level], sz, 0, 0, INTER_LINEAR, stream);
                        cuda::threshold(maskPyr_[level], maskPyr_[level], 254, 0, THRESH_TOZERO, stream);
                    }
                }
            }
            else
            {
                image.copyTo(imagePyr_[level], stream);

                if (!mask.empty())
                    mask.copyTo(maskPyr_[level], stream);
            }

            // Filter keypoints by image border
            ensureSizeIsEnough(sz, CV_8UC1, buf_);
            buf_.setTo(Scalar::all(0), stream);
            Rect inner(edgeThreshold_, edgeThreshold_, sz.width - 2 * edgeThreshold_, sz.height - 2 * edgeThreshold_);
            buf_(inner).setTo(Scalar::all(255), stream);

            cuda::bitwise_and(maskPyr_[level], buf_, maskPyr_[level], cv::noArray(), stream);
        }
    }

    // takes keypoints and culls them by the response
    static void cull(GpuMat& keypoints, int& count, int n_points, Stream& stream)
    {
        using namespace cv::cuda::device::orb;

        //this is only necessary if the keypoints size is greater than the number of desired points.
        if (count > n_points)
        {
            if (n_points == 0)
            {
                keypoints.release();
                return;
            }

            count = cull_gpu(keypoints.ptr<int>(cuda::FastFeatureDetector::LOCATION_ROW), keypoints.ptr<float>(cuda::FastFeatureDetector::RESPONSE_ROW), count, n_points, StreamAccessor::getStream(stream));
        }
    }

    void ORB_Impl::computeKeyPointsPyramid(Stream& stream)
    {
        using namespace cv::cuda::device::orb;

        int half_patch_size = patchSize_ / 2;

        keyPointsPyr_.resize(nLevels_);
        keyPointsCount_.resize(nLevels_);

        fastDetector_->setThreshold(fastThreshold_);

        for (int level = 0; level < nLevels_; ++level)
        {
            fastDetector_->setMaxNumPoints(0.05 * imagePyr_[level].size().area());

            GpuMat fastKpRange;
            fastDetector_->detectAsync(imagePyr_[level], fastKpRange, maskPyr_[level], stream);

            keyPointsCount_[level] = fastKpRange.cols;

            if (keyPointsCount_[level] == 0)
                continue;

            ensureSizeIsEnough(3, keyPointsCount_[level], fastKpRange.type(), keyPointsPyr_[level]);
            fastKpRange.copyTo(keyPointsPyr_[level].rowRange(0, 2), stream);

            const int n_features = static_cast<int>(n_features_per_level_[level]);

            if (scoreType_ == ORB::HARRIS_SCORE)
            {
                // Keep more points than necessary as FAST does not give amazing corners
                cull(keyPointsPyr_[level], keyPointsCount_[level], 2 * n_features, stream);

                // Compute the Harris cornerness (better scoring than FAST)
                HarrisResponses_gpu(imagePyr_[level], keyPointsPyr_[level].ptr<short2>(0), keyPointsPyr_[level].ptr<float>(1), keyPointsCount_[level], 7, HARRIS_K, StreamAccessor::getStream(stream));
            }

            //cull to the final desired level, using the new Harris scores or the original FAST scores.
            cull(keyPointsPyr_[level], keyPointsCount_[level], n_features, stream);

            // Compute orientation
            IC_Angle_gpu(imagePyr_[level], keyPointsPyr_[level].ptr<short2>(0), keyPointsPyr_[level].ptr<float>(2), keyPointsCount_[level], half_patch_size, StreamAccessor::getStream(stream));
        }
    }

    void ORB_Impl::computeDescriptors(OutputArray _descriptors, Stream& stream)
    {
        using namespace cv::cuda::device::orb;

        int nAllkeypoints = 0;

        for (int level = 0; level < nLevels_; ++level)
            nAllkeypoints += keyPointsCount_[level];

        if (nAllkeypoints == 0)
        {
            _descriptors.release();
            return;
        }

        ensureSizeIsEnough(nAllkeypoints, descriptorSize(), CV_8UC1, _descriptors);
        GpuMat descriptors = _descriptors.getGpuMat();

        int offset = 0;

        for (int level = 0; level < nLevels_; ++level)
        {
            if (keyPointsCount_[level] == 0)
                continue;

            GpuMat descRange = descriptors.rowRange(offset, offset + keyPointsCount_[level]);

            if (blurForDescriptor_)
            {
                // preprocess the resized image
                ensureSizeIsEnough(imagePyr_[level].size(), imagePyr_[level].type(), buf_);
                blurFilter_->apply(imagePyr_[level], buf_, stream);
            }

            computeOrbDescriptor_gpu(blurForDescriptor_ ? buf_ : imagePyr_[level], keyPointsPyr_[level].ptr<short2>(0), keyPointsPyr_[level].ptr<float>(2),
                keyPointsCount_[level], pattern_.ptr<int>(0), pattern_.ptr<int>(1), descRange, descriptorSize(), WTA_K_, StreamAccessor::getStream(stream));

            offset += keyPointsCount_[level];
        }
    }

    void ORB_Impl::mergeKeyPoints(OutputArray _keypoints, Stream& stream)
    {
        using namespace cv::cuda::device::orb;

        int nAllkeypoints = 0;

        for (int level = 0; level < nLevels_; ++level)
            nAllkeypoints += keyPointsCount_[level];

        if (nAllkeypoints == 0)
        {
            _keypoints.release();
            return;
        }

        ensureSizeIsEnough(ROWS_COUNT, nAllkeypoints, CV_32FC1, _keypoints);
        GpuMat& keypoints = _keypoints.getGpuMatRef();

        int offset = 0;

        for (int level = 0; level < nLevels_; ++level)
        {
            if (keyPointsCount_[level] == 0)
                continue;

            float sf = getScale(scaleFactor_, firstLevel_, level);

            GpuMat keyPointsRange = keypoints.colRange(offset, offset + keyPointsCount_[level]);

            float locScale = level != firstLevel_ ? sf : 1.0f;

            mergeLocation_gpu(keyPointsPyr_[level].ptr<short2>(0), keyPointsRange.ptr<float>(0), keyPointsRange.ptr<float>(1), keyPointsCount_[level], locScale, StreamAccessor::getStream(stream));

            GpuMat range = keyPointsRange.rowRange(2, 4);
            keyPointsPyr_[level](Range(1, 3), Range(0, keyPointsCount_[level])).copyTo(range, stream);

            keyPointsRange.row(4).setTo(Scalar::all(level), stream);
            keyPointsRange.row(5).setTo(Scalar::all(patchSize_ * sf), stream);

            offset += keyPointsCount_[level];
        }
    }

    void ORB_Impl::convert(InputArray _gpu_keypoints, std::vector<KeyPoint>& keypoints)
    {
        if (_gpu_keypoints.empty())
        {
            keypoints.clear();
            return;
        }

        Mat h_keypoints;
        if (_gpu_keypoints.kind() == _InputArray::CUDA_GPU_MAT)
        {
            _gpu_keypoints.getGpuMat().download(h_keypoints);
        }
        else
        {
            h_keypoints = _gpu_keypoints.getMat();
        }

        CV_Assert( h_keypoints.rows == ROWS_COUNT );
        CV_Assert( h_keypoints.type() == CV_32FC1 );

        const int npoints = h_keypoints.cols;

        keypoints.resize(npoints);

        const float* x_ptr = h_keypoints.ptr<float>(X_ROW);
        const float* y_ptr = h_keypoints.ptr<float>(Y_ROW);
        const float* response_ptr = h_keypoints.ptr<float>(RESPONSE_ROW);
        const float* angle_ptr = h_keypoints.ptr<float>(ANGLE_ROW);
        const float* octave_ptr = h_keypoints.ptr<float>(OCTAVE_ROW);
        const float* size_ptr = h_keypoints.ptr<float>(SIZE_ROW);

        for (int i = 0; i < npoints; ++i)
        {
            KeyPoint kp;

            kp.pt.x = x_ptr[i];
            kp.pt.y = y_ptr[i];
            kp.response = response_ptr[i];
            kp.angle = angle_ptr[i];
            kp.octave = static_cast<int>(octave_ptr[i]);
            kp.size = size_ptr[i];

            keypoints[i] = kp;
        }
    }
}

Ptr<cv::cuda::ORB> cv::cuda::ORB::create(int nfeatures,
                                         float scaleFactor,
                                         int nlevels,
                                         int edgeThreshold,
                                         int firstLevel,
                                         int WTA_K,
                                         int scoreType,
                                         int patchSize,
                                         int fastThreshold,
                                         bool blurForDescriptor)
{
    return makePtr<ORB_Impl>(nfeatures, scaleFactor, nlevels, edgeThreshold, firstLevel, WTA_K, scoreType, patchSize, fastThreshold, blurForDescriptor);
}

#endif /* !defined (HAVE_CUDA) */
