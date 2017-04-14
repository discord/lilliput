#include "perf_precomp.hpp"

using namespace std;
using namespace cv;
using namespace perf;
using std::tr1::make_tuple;
using std::tr1::get;

typedef std::tr1::tuple<Size, MatType, int> Size_MatType_kSize_t;
typedef perf::TestBaseWithParam<Size_MatType_kSize_t> Size_MatType_kSize;

PERF_TEST_P(Size_MatType_kSize, medianBlur,
            testing::Combine(
                testing::Values(szODD, szQVGA, szVGA, sz720p),
                testing::Values(CV_8UC1, CV_8UC4, CV_16UC1, CV_16SC1, CV_32FC1),
                testing::Values(3, 5)
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    int ksize = get<2>(GetParam());

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    if (CV_MAT_DEPTH(type) > CV_16S || CV_MAT_CN(type) > 1)
        declare.time(15);

    TEST_CYCLE() medianBlur(src, dst, ksize);

    SANITY_CHECK(dst);
}

CV_ENUM(BorderType3x3, BORDER_REPLICATE, BORDER_CONSTANT)
CV_ENUM(BorderType, BORDER_REPLICATE, BORDER_CONSTANT, BORDER_REFLECT, BORDER_REFLECT101)

typedef std::tr1::tuple<Size, MatType, BorderType3x3> Size_MatType_BorderType3x3_t;
typedef perf::TestBaseWithParam<Size_MatType_BorderType3x3_t> Size_MatType_BorderType3x3;

typedef std::tr1::tuple<Size, MatType, BorderType> Size_MatType_BorderType_t;
typedef perf::TestBaseWithParam<Size_MatType_BorderType_t> Size_MatType_BorderType;

PERF_TEST_P(Size_MatType_BorderType3x3, gaussianBlur3x3,
            testing::Combine(
                testing::Values(szODD, szQVGA, szVGA, sz720p),
                testing::Values(CV_8UC1, CV_8UC4, CV_16UC1, CV_16SC1, CV_32FC1),
                BorderType3x3::all()
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    BorderType3x3 btype = get<2>(GetParam());

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() GaussianBlur(src, dst, Size(3,3), 0, 0, btype);

    SANITY_CHECK(dst, 1);
}

PERF_TEST_P(Size_MatType_BorderType3x3, blur3x3,
            testing::Combine(
                testing::Values(szODD, szQVGA, szVGA, sz720p),
                testing::Values(CV_8UC1, CV_8UC4, CV_16UC1, CV_16SC1, CV_32FC1),
                BorderType3x3::all()
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    BorderType3x3 btype = get<2>(GetParam());

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() blur(src, dst, Size(3,3), Point(-1,-1), btype);

    SANITY_CHECK(dst, 1);
}

PERF_TEST_P(Size_MatType_BorderType, blur16x16,
            testing::Combine(
                testing::Values(szVGA, sz720p),
                testing::Values(CV_8UC1, CV_8UC4, CV_16UC1, CV_16SC1, CV_32FC1),
                BorderType::all()
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    BorderType btype = get<2>(GetParam());
    double eps = 1e-3;

    eps = CV_MAT_DEPTH(type) <= CV_32S ? 1 : eps;

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() blur(src, dst, Size(16,16), Point(-1,-1), btype);

    SANITY_CHECK(dst, eps);
}

PERF_TEST_P(Size_MatType_BorderType3x3, box3x3,
            testing::Combine(
                testing::Values(szODD, szQVGA, szVGA, sz720p),
                testing::Values(CV_8UC1, CV_16SC1, CV_32SC1, CV_32FC1, CV_32FC3),
                BorderType3x3::all()
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    BorderType3x3 btype = get<2>(GetParam());

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() boxFilter(src, dst, -1, Size(3,3), Point(-1,-1), false, btype);

    SANITY_CHECK(dst, 1e-6, ERROR_RELATIVE);
}

PERF_TEST_P(Size_MatType_BorderType3x3, box3x3_inplace,
            testing::Combine(
                testing::Values(szODD, szQVGA, szVGA, sz720p),
                testing::Values(CV_8UC1, CV_16SC1, CV_32SC1, CV_32FC1, CV_32FC3),
                BorderType3x3::all()
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    BorderType3x3 btype = get<2>(GetParam());

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    while(next())
    {
        src.copyTo(dst);
        startTimer();
        boxFilter(dst, dst, -1, Size(3,3), Point(-1,-1), false, btype);
        stopTimer();
    }

    SANITY_CHECK(dst, 1e-6, ERROR_RELATIVE);
}

PERF_TEST_P(Size_MatType_BorderType, gaussianBlur5x5,
            testing::Combine(
                testing::Values(szODD, szQVGA, szVGA, sz720p),
                testing::Values(CV_8UC1, CV_8UC4, CV_16UC1, CV_16SC1, CV_32FC1),
                BorderType::all()
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    BorderType btype = get<2>(GetParam());

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() GaussianBlur(src, dst, Size(5,5), 0, 0, btype);

    SANITY_CHECK(dst, 1);
}

PERF_TEST_P(Size_MatType_BorderType, blur5x5,
            testing::Combine(
                testing::Values(szVGA, sz720p),
                testing::Values(CV_8UC1, CV_8UC4, CV_16UC1, CV_16SC1, CV_32FC1, CV_32FC3),
                BorderType::all()
                )
            )
{
    Size size = get<0>(GetParam());
    int type = get<1>(GetParam());
    BorderType btype = get<2>(GetParam());

    Mat src(size, type);
    Mat dst(size, type);

    declare.in(src, WARMUP_RNG).out(dst);

    TEST_CYCLE() blur(src, dst, Size(5,5), Point(-1,-1), btype);

    SANITY_CHECK(dst, 1);
}
