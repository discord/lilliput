/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
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
//   * The name of Intel Corporation may not be used to endorse or promote products
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

#include "test_precomp.hpp"

using namespace cv;
using namespace std;

class CV_TemplMatchTest : public cvtest::ArrayTest
{
public:
    CV_TemplMatchTest();

protected:
    int read_params( CvFileStorage* fs );
    void get_test_array_types_and_sizes( int test_case_idx, vector<vector<Size> >& sizes, vector<vector<int> >& types );
    void get_minmax_bounds( int i, int j, int type, Scalar& low, Scalar& high );
    double get_success_error_level( int test_case_idx, int i, int j );
    void run_func();
    void prepare_to_validation( int );

    int max_template_size;
    int method;
    bool test_cpp;
};


CV_TemplMatchTest::CV_TemplMatchTest()
{
    test_array[INPUT].push_back(NULL);
    test_array[INPUT].push_back(NULL);
    test_array[OUTPUT].push_back(NULL);
    test_array[REF_OUTPUT].push_back(NULL);
    element_wise_relative_error = false;
    max_template_size = 100;
    method = 0;
    test_cpp = false;
}


int CV_TemplMatchTest::read_params( CvFileStorage* fs )
{
    int code = cvtest::ArrayTest::read_params( fs );
    if( code < 0 )
        return code;

    max_template_size = cvReadInt( find_param( fs, "max_template_size" ), max_template_size );
    max_template_size = cvtest::clipInt( max_template_size, 1, 100 );

    return code;
}


void CV_TemplMatchTest::get_minmax_bounds( int i, int j, int type, Scalar& low, Scalar& high )
{
    cvtest::ArrayTest::get_minmax_bounds( i, j, type, low, high );
    int depth = CV_MAT_DEPTH(type);
    if( depth == CV_32F )
    {
        low = Scalar::all(-10.);
        high = Scalar::all(10.);
    }
}


void CV_TemplMatchTest::get_test_array_types_and_sizes( int test_case_idx,
                                                vector<vector<Size> >& sizes, vector<vector<int> >& types )
{
    RNG& rng = ts->get_rng();
    int depth = cvtest::randInt(rng) % 2, cn = cvtest::randInt(rng) & 1 ? 3 : 1;
    cvtest::ArrayTest::get_test_array_types_and_sizes( test_case_idx, sizes, types );
    depth = depth == 0 ? CV_8U : CV_32F;

    types[INPUT][0] = types[INPUT][1] = CV_MAKETYPE(depth,cn);
    types[OUTPUT][0] = types[REF_OUTPUT][0] = CV_32FC1;

    sizes[INPUT][1].width = cvtest::randInt(rng)%MIN(sizes[INPUT][1].width,max_template_size) + 1;
    sizes[INPUT][1].height = cvtest::randInt(rng)%MIN(sizes[INPUT][1].height,max_template_size) + 1;
    sizes[OUTPUT][0].width = sizes[INPUT][0].width - sizes[INPUT][1].width + 1;
    sizes[OUTPUT][0].height = sizes[INPUT][0].height - sizes[INPUT][1].height + 1;
    sizes[REF_OUTPUT][0] = sizes[OUTPUT][0];

    method = cvtest::randInt(rng)%6;
    test_cpp = (cvtest::randInt(rng) & 256) == 0;
}


double CV_TemplMatchTest::get_success_error_level( int /*test_case_idx*/, int /*i*/, int /*j*/ )
{
    if( test_mat[INPUT][1].depth() == CV_8U ||
        (method >= CV_TM_CCOEFF && test_mat[INPUT][1].cols*test_mat[INPUT][1].rows <= 2) )
        return 1e-2;
    else
        return 1e-3;
}


void CV_TemplMatchTest::run_func()
{
    if(!test_cpp)
        cvMatchTemplate( test_array[INPUT][0], test_array[INPUT][1], test_array[OUTPUT][0], method );
    else
    {
        cv::Mat _out = cv::cvarrToMat(test_array[OUTPUT][0]);
        cv::matchTemplate(cv::cvarrToMat(test_array[INPUT][0]), cv::cvarrToMat(test_array[INPUT][1]), _out, method);
    }
}


static void cvTsMatchTemplate( const CvMat* img, const CvMat* templ, CvMat* result, int method )
{
    int i, j, k, l;
    int depth = CV_MAT_DEPTH(img->type), cn = CV_MAT_CN(img->type);
    int width_n = templ->cols*cn, height = templ->rows;
    int a_step = img->step / CV_ELEM_SIZE(img->type & CV_MAT_DEPTH_MASK);
    int b_step = templ->step / CV_ELEM_SIZE(templ->type & CV_MAT_DEPTH_MASK);
    CvScalar b_mean, b_sdv;
    double b_denom = 1., b_sum2 = 0;
    int area = templ->rows*templ->cols;

    cvAvgSdv(templ, &b_mean, &b_sdv);

    for( i = 0; i < cn; i++ )
        b_sum2 += (b_sdv.val[i]*b_sdv.val[i] + b_mean.val[i]*b_mean.val[i])*area;

    if( b_sdv.val[0]*b_sdv.val[0] + b_sdv.val[1]*b_sdv.val[1] +
        b_sdv.val[2]*b_sdv.val[2] + b_sdv.val[3]*b_sdv.val[3] < DBL_EPSILON &&
        method == CV_TM_CCOEFF_NORMED )
    {
        cvSet( result, cvScalarAll(1.) );
        return;
    }

    if( method & 1 )
    {
        b_denom = 0;
        if( method != CV_TM_CCOEFF_NORMED )
        {
            b_denom = b_sum2;
        }
        else
        {
            for( i = 0; i < cn; i++ )
                b_denom += b_sdv.val[i]*b_sdv.val[i]*area;
        }
        b_denom = sqrt(b_denom);
        if( b_denom == 0 )
            b_denom = 1.;
    }

    assert( CV_TM_SQDIFF <= method && method <= CV_TM_CCOEFF_NORMED );

    for( i = 0; i < result->rows; i++ )
    {
        for( j = 0; j < result->cols; j++ )
        {
            CvScalar a_sum(0), a_sum2(0);
            CvScalar ccorr(0);
            double value = 0.;

            if( depth == CV_8U )
            {
                const uchar* a = img->data.ptr + i*img->step + j*cn;
                const uchar* b = templ->data.ptr;

                if( cn == 1 || method < CV_TM_CCOEFF )
                {
                    for( k = 0; k < height; k++, a += a_step, b += b_step )
                        for( l = 0; l < width_n; l++ )
                        {
                            ccorr.val[0] += a[l]*b[l];
                            a_sum.val[0] += a[l];
                            a_sum2.val[0] += a[l]*a[l];
                        }
                }
                else
                {
                    for( k = 0; k < height; k++, a += a_step, b += b_step )
                        for( l = 0; l < width_n; l += 3 )
                        {
                            ccorr.val[0] += a[l]*b[l];
                            ccorr.val[1] += a[l+1]*b[l+1];
                            ccorr.val[2] += a[l+2]*b[l+2];
                            a_sum.val[0] += a[l];
                            a_sum.val[1] += a[l+1];
                            a_sum.val[2] += a[l+2];
                            a_sum2.val[0] += a[l]*a[l];
                            a_sum2.val[1] += a[l+1]*a[l+1];
                            a_sum2.val[2] += a[l+2]*a[l+2];
                        }
                }
            }
            else
            {
                const float* a = (const float*)(img->data.ptr + i*img->step) + j*cn;
                const float* b = (const float*)templ->data.ptr;

                if( cn == 1 || method < CV_TM_CCOEFF )
                {
                    for( k = 0; k < height; k++, a += a_step, b += b_step )
                        for( l = 0; l < width_n; l++ )
                        {
                            ccorr.val[0] += a[l]*b[l];
                            a_sum.val[0] += a[l];
                            a_sum2.val[0] += a[l]*a[l];
                        }
                }
                else
                {
                    for( k = 0; k < height; k++, a += a_step, b += b_step )
                        for( l = 0; l < width_n; l += 3 )
                        {
                            ccorr.val[0] += a[l]*b[l];
                            ccorr.val[1] += a[l+1]*b[l+1];
                            ccorr.val[2] += a[l+2]*b[l+2];
                            a_sum.val[0] += a[l];
                            a_sum.val[1] += a[l+1];
                            a_sum.val[2] += a[l+2];
                            a_sum2.val[0] += a[l]*a[l];
                            a_sum2.val[1] += a[l+1]*a[l+1];
                            a_sum2.val[2] += a[l+2]*a[l+2];
                        }
                }
            }

            switch( method )
            {
            case CV_TM_CCORR:
            case CV_TM_CCORR_NORMED:
                value = ccorr.val[0];
                break;
            case CV_TM_SQDIFF:
            case CV_TM_SQDIFF_NORMED:
                value = (a_sum2.val[0] + b_sum2 - 2*ccorr.val[0]);
                break;
            default:
                value = (ccorr.val[0] - a_sum.val[0]*b_mean.val[0]+
                         ccorr.val[1] - a_sum.val[1]*b_mean.val[1]+
                         ccorr.val[2] - a_sum.val[2]*b_mean.val[2]);
            }

            if( method & 1 )
            {
                double denom;

                // calc denominator
                if( method != CV_TM_CCOEFF_NORMED )
                {
                    denom = a_sum2.val[0] + a_sum2.val[1] + a_sum2.val[2];
                }
                else
                {
                    denom = a_sum2.val[0] - (a_sum.val[0]*a_sum.val[0])/area;
                    denom += a_sum2.val[1] - (a_sum.val[1]*a_sum.val[1])/area;
                    denom += a_sum2.val[2] - (a_sum.val[2]*a_sum.val[2])/area;
                }
                denom = sqrt(MAX(denom,0))*b_denom;
                if( fabs(value) < denom )
                    value /= denom;
                else if( fabs(value) < denom*1.125 )
                    value = value > 0 ? 1 : -1;
                else
                    value = method != CV_TM_SQDIFF_NORMED ? 0 : 1;
            }

            ((float*)(result->data.ptr + result->step*i))[j] = (float)value;
        }
    }
}


void CV_TemplMatchTest::prepare_to_validation( int /*test_case_idx*/ )
{
    CvMat _input = test_mat[INPUT][0], _templ = test_mat[INPUT][1];
    CvMat _output = test_mat[REF_OUTPUT][0];
    cvTsMatchTemplate( &_input, &_templ, &_output, method );

    //if( ts->get_current_test_info()->test_case_idx == 0 )
    /*{
        CvFileStorage* fs = cvOpenFileStorage( "_match_template.yml", 0, CV_STORAGE_WRITE );
        cvWrite( fs, "image", &test_mat[INPUT][0] );
        cvWrite( fs, "template", &test_mat[INPUT][1] );
        cvWrite( fs, "ref", &test_mat[REF_OUTPUT][0] );
        cvWrite( fs, "opencv", &test_mat[OUTPUT][0] );
        cvWriteInt( fs, "method", method );
        cvReleaseFileStorage( &fs );
    }*/

    if( method >= CV_TM_CCOEFF )
    {
        // avoid numerical stability problems in singular cases (when the results are near to 0)
        const double delta = 10.;
        test_mat[REF_OUTPUT][0] += Scalar::all(delta);
        test_mat[OUTPUT][0] += Scalar::all(delta);
    }
}

TEST(Imgproc_MatchTemplate, accuracy) { CV_TemplMatchTest test; test.safe_run(); }
