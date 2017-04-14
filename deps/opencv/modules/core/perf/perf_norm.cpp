#include "perf_precomp.hpp"

using namespace std;
using namespace cv;
using namespace perf;
using std::tr1::make_tuple;
using std::tr1::get;


CV_FLAGS(NormType, NORM_INF, NORM_L1, NORM_L2, NORM_TYPE_MASK, NORM_RELATIVE, NORM_MINMAX)
typedef std::tr1::tuple<Size, MatType, NormType> Size_MatType_NormType_t;
typedef perf::TestBaseWithParam<Size_MatType_NormType_t> Size_MatType_NormType;

PERF_TEST_P(Size_MatType_NormType, norm,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                testing::Values((int)NORM_INF, (int)NORM_L1, (int)NORM_L2)
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int normType = get<2>(GetParam());

    Mat src(sz, matType);
    double n;

    declare.in(src, WARMUP_RNG);

    TEST_CYCLE() n = norm(src, normType);

    SANITY_CHECK(n, 1e-6, ERROR_RELATIVE);
}

PERF_TEST_P(Size_MatType_NormType, norm_mask,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                testing::Values((int)NORM_INF, (int)NORM_L1, (int)NORM_L2)
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int normType = get<2>(GetParam());

    Mat src(sz, matType);
    Mat mask = Mat::ones(sz, CV_8U);
    double n;

    declare.in(src, WARMUP_RNG).in(mask);

    TEST_CYCLE() n = norm(src, normType, mask);

    SANITY_CHECK(n, 1e-6, ERROR_RELATIVE);
}

PERF_TEST_P(Size_MatType_NormType, norm2,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                testing::Values((int)NORM_INF, (int)NORM_L1, (int)NORM_L2, (int)(NORM_RELATIVE+NORM_INF), (int)(NORM_RELATIVE+NORM_L1), (int)(NORM_RELATIVE+NORM_L2))
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int normType = get<2>(GetParam());

    Mat src1(sz, matType);
    Mat src2(sz, matType);
    double n;

    declare.in(src1, src2, WARMUP_RNG);

    TEST_CYCLE() n = norm(src1, src2, normType);

    SANITY_CHECK(n, 1e-5, ERROR_RELATIVE);
}

PERF_TEST_P(Size_MatType_NormType, norm2_mask,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                testing::Values((int)NORM_INF, (int)NORM_L1, (int)NORM_L2, (int)(NORM_RELATIVE|NORM_INF), (int)(NORM_RELATIVE|NORM_L1), (int)(NORM_RELATIVE|NORM_L2))
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int normType = get<2>(GetParam());

    Mat src1(sz, matType);
    Mat src2(sz, matType);
    Mat mask = Mat::ones(sz, CV_8U);
    double n;

    declare.in(src1, src2, WARMUP_RNG).in(mask);

    TEST_CYCLE() n = norm(src1, src2, normType, mask);

    SANITY_CHECK(n, 1e-5, ERROR_RELATIVE);
}

PERF_TEST_P(Size_MatType_NormType, normalize,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                testing::Values((int)NORM_INF, (int)NORM_L1, (int)NORM_L2)
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int normType = get<2>(GetParam());

    Mat src(sz, matType);
    Mat dst(sz, matType);

    double alpha = 100.;
    if(normType==NORM_L1) alpha = (double)src.total() * src.channels();
    if(normType==NORM_L2) alpha = (double)src.total()/10;

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() normalize(src, dst, alpha, 0., normType);

    SANITY_CHECK(dst, 1e-6);
}

PERF_TEST_P(Size_MatType_NormType, normalize_mask,
            testing::Combine(
                testing::Values(::perf::szVGA, ::perf::sz1080p),
                testing::Values(TYPICAL_MAT_TYPES),
                testing::Values((int)NORM_INF, (int)NORM_L1, (int)NORM_L2)
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int normType = get<2>(GetParam());

    Mat src(sz, matType);
    Mat dst(sz, matType);
    Mat mask = Mat::ones(sz, CV_8U);

    double alpha = 100.;
    if(normType==NORM_L1) alpha = (double)src.total() * src.channels();
    if(normType==NORM_L2) alpha = (double)src.total()/10;

    declare.in(src, WARMUP_RNG).in(mask).out(dst);
    declare.time(100);

    TEST_CYCLE() normalize(src, dst, alpha, 0., normType, -1, mask);

    SANITY_CHECK(dst, 1e-6);
}

PERF_TEST_P(Size_MatType_NormType, normalize_32f,
            testing::Combine(
                testing::Values(TYPICAL_MAT_SIZES),
                testing::Values(TYPICAL_MAT_TYPES),
                testing::Values((int)NORM_INF, (int)NORM_L1, (int)NORM_L2)
                )
            )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());
    int normType = get<2>(GetParam());

    Mat src(sz, matType);
    Mat dst(sz, CV_32F);

    double alpha = 100.;
    if(normType==NORM_L1) alpha = (double)src.total() * src.channels();
    if(normType==NORM_L2) alpha = (double)src.total()/10;

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() normalize(src, dst, alpha, 0., normType, CV_32F);

    SANITY_CHECK(dst, 1e-6, ERROR_RELATIVE);
}

PERF_TEST_P( Size_MatType, normalize_minmax, TYPICAL_MATS )
{
    Size sz = get<0>(GetParam());
    int matType = get<1>(GetParam());

    Mat src(sz, matType);
    Mat dst(sz, matType);

    declare.in(src, WARMUP_RNG).out(dst);
    declare.time(30);

    TEST_CYCLE() normalize(src, dst, 20., 100., NORM_MINMAX);

    SANITY_CHECK(dst, 1e-6, ERROR_RELATIVE);
}
