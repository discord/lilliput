#include "perf_precomp.hpp"

using namespace std;
using namespace cv;
using namespace perf;
using namespace testing;
using std::tr1::make_tuple;
using std::tr1::get;

CV_ENUM(Mat_Type, CV_8UC1, CV_8UC3, CV_32FC1, CV_32FC3)

typedef TestBaseWithParam< tr1::tuple<Size, int, Mat_Type> > TestBilateralFilter;

PERF_TEST_P( TestBilateralFilter, BilateralFilter,
             Combine(
                Values( szVGA, sz1080p ), // image size
                Values( 3, 5 ), // d
                Mat_Type::all() // image type
             )
)
{
    Size sz;
    int d, type;
    const double sigmaColor = 1., sigmaSpace = 1.;

    sz         = get<0>(GetParam());
    d          = get<1>(GetParam());
    type       = get<2>(GetParam());

    Mat src(sz, type);
    Mat dst(sz, type);

    declare.in(src, WARMUP_RNG).out(dst).time(20);

    TEST_CYCLE() bilateralFilter(src, dst, d, sigmaColor, sigmaSpace, BORDER_DEFAULT);

    SANITY_CHECK(dst, .01, ERROR_RELATIVE);
}
