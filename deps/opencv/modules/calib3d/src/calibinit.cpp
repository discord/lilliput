//M*//////////////////////////////////////////////////////////////////////////////////////
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

/************************************************************************************\
    This is improved variant of chessboard corner detection algorithm that
    uses a graph of connected quads. It is based on the code contributed
    by Vladimir Vezhnevets and Philip Gruebele.
    Here is the copyright notice from the original Vladimir's code:
    ===============================================================

    The algorithms developed and implemented by Vezhnevets Vldimir
    aka Dead Moroz (vvp@graphics.cs.msu.ru)
    See http://graphics.cs.msu.su/en/research/calibration/opencv.html
    for detailed information.

    Reliability additions and modifications made by Philip Gruebele.
    <a href="mailto:pgruebele@cox.net">pgruebele@cox.net</a>

    Some further improvements for detection of partially ocluded boards at non-ideal
    lighting conditions have been made by Alex Bovyrin and Kurt Kolonige

\************************************************************************************/

/************************************************************************************\
  This version adds a new and improved variant of chessboard corner detection
  that works better in poor lighting condition. It is based on work from
  Oliver Schreer and Stefano Masneri. This method works faster than the previous
  one and reverts back to the older method in case no chessboard detection is
  possible. Overall performance improves also because now the method avoids
  performing the same computation multiple times when not necessary.

\************************************************************************************/

#include "precomp.hpp"
#include "opencv2/imgproc/imgproc_c.h"
#include "opencv2/calib3d/calib3d_c.h"
#include "circlesgrid.hpp"
#include <stdarg.h>
#include <vector>

using namespace cv;
using namespace std;

//#define ENABLE_TRIM_COL_ROW

//#define DEBUG_CHESSBOARD

#ifdef DEBUG_CHESSBOARD
static int PRINTF( const char* fmt, ... )
{
    va_list args;
    va_start(args, fmt);
    return vprintf(fmt, args);
}
#else
#define PRINTF(...)
#endif

//=====================================================================================
// Implementation for the enhanced calibration object detection
//=====================================================================================

#define MAX_CONTOUR_APPROX  7

struct CvContourEx
{
    CV_CONTOUR_FIELDS()
    int counter;
};

//=====================================================================================

/// Corner info structure
/** This structure stores information about the chessboard corner.*/
struct CvCBCorner
{
    CvPoint2D32f pt; // Coordinates of the corner
    int row;         // Board row index
    int count;       // Number of neighbor corners
    struct CvCBCorner* neighbors[4]; // Neighbor corners

    float meanDist(int *_n) const
    {
        float sum = 0;
        int n = 0;
        for( int i = 0; i < 4; i++ )
        {
            if( neighbors[i] )
            {
                float dx = neighbors[i]->pt.x - pt.x;
                float dy = neighbors[i]->pt.y - pt.y;
                sum += sqrt(dx*dx + dy*dy);
                n++;
            }
        }
        if(_n)
            *_n = n;
        return sum/MAX(n,1);
    }
};

//=====================================================================================
/// Quadrangle contour info structure
/** This structure stores information about the chessboard quadrange.*/
struct CvCBQuad
{
    int count;      // Number of quad neighbors
    int group_idx;  // quad group ID
    int row, col;   // row and column of this quad
    bool ordered;   // true if corners/neighbors are ordered counter-clockwise
    float edge_len; // quad edge len, in pix^2
    // neighbors and corners are synced, i.e., neighbor 0 shares corner 0
    CvCBCorner *corners[4]; // Coordinates of quad corners
    struct CvCBQuad *neighbors[4]; // Pointers of quad neighbors
};

//=====================================================================================

#ifdef DEBUG_CHESSBOARD
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
static void SHOW(const std::string & name, Mat & img)
{
    imshow(name, img);
    while ((uchar)waitKey(0) != 'q') {}
}
static void SHOW_QUADS(const std::string & name, const Mat & img_, CvCBQuad * quads, int quads_count)
{
    Mat img = img_.clone();
    if (img.channels() == 1)
        cvtColor(img, img, COLOR_GRAY2BGR);
    for (int i = 0; i < quads_count; ++i)
    {
        CvCBQuad & quad = quads[i];
        for (int j = 0; j < 4; ++j)
        {
            line(img, quad.corners[j]->pt, quad.corners[(j + 1) % 4]->pt, Scalar(0, 240, 0), 1, LINE_AA);
        }
    }
    imshow(name, img);
    while ((uchar)waitKey(0) != 'q') {}
}
#else
#define SHOW(...)
#define SHOW_QUADS(...)
#endif

//=====================================================================================

static int icvGenerateQuads( CvCBQuad **quads, CvCBCorner **corners,
                             CvMemStorage *storage, const Mat &image_, int flags, int *max_quad_buf_size);

static bool processQuads(CvCBQuad *quads, int quad_count, CvSize pattern_size, int max_quad_buf_size,
                         CvMemStorage * storage, CvCBCorner *corners, CvPoint2D32f *out_corners, int *out_corner_count, int & prev_sqr_size);

/*static int
icvGenerateQuadsEx( CvCBQuad **out_quads, CvCBCorner **out_corners,
    CvMemStorage *storage, CvMat *image, CvMat *thresh_img, int dilation, int flags );*/

static void icvFindQuadNeighbors( CvCBQuad *quads, int quad_count );

static int icvFindConnectedQuads( CvCBQuad *quads, int quad_count,
                                  CvCBQuad **quad_group, int group_idx,
                                  CvMemStorage* storage );

static int icvCheckQuadGroup( CvCBQuad **quad_group, int count,
                              CvCBCorner **out_corners, CvSize pattern_size );

static int icvCleanFoundConnectedQuads( int quad_count,
                CvCBQuad **quads, CvSize pattern_size );

static int icvOrderFoundConnectedQuads( int quad_count, CvCBQuad **quads,
           int *all_count, CvCBQuad **all_quads, CvCBCorner **corners,
           CvSize pattern_size, int max_quad_buf_size, CvMemStorage* storage );

static void icvOrderQuad(CvCBQuad *quad, CvCBCorner *corner, int common);

#ifdef ENABLE_TRIM_COL_ROW
static int icvTrimCol(CvCBQuad **quads, int count, int col, int dir);

static int icvTrimRow(CvCBQuad **quads, int count, int row, int dir);
#endif

static int icvAddOuterQuad(CvCBQuad *quad, CvCBQuad **quads, int quad_count,
                    CvCBQuad **all_quads, int all_count, CvCBCorner **corners, int max_quad_buf_size);

static void icvRemoveQuadFromGroup(CvCBQuad **quads, int count, CvCBQuad *q0);

static int icvCheckBoardMonotony( CvPoint2D32f* corners, CvSize pattern_size );

/***************************************************************************************************/
//COMPUTE INTENSITY HISTOGRAM OF INPUT IMAGE
static int icvGetIntensityHistogram( const Mat & img, std::vector<int>& piHist )
{
    // sum up all pixel in row direction and divide by number of columns
    for ( int j=0; j<img.rows; j++ )
    {
        const uchar * row = img.ptr(j);
        for ( int i=0; i<img.cols; i++ )
        {
            piHist[row[i]]++;
        }
    }
    return 0;
}
/***************************************************************************************************/
//SMOOTH HISTOGRAM USING WINDOW OF SIZE 2*iWidth+1
static int icvSmoothHistogram( const std::vector<int>& piHist, std::vector<int>& piHistSmooth, int iWidth )
{
    int iIdx;
    for ( int i=0; i<256; i++)
    {
        int iSmooth = 0;
        for ( int ii=-iWidth; ii<=iWidth; ii++)
        {
            iIdx = i+ii;
            if (iIdx > 0 && iIdx < 256)
            {
                iSmooth += piHist[iIdx];
            }
        }
        piHistSmooth[i] = iSmooth/(2*iWidth+1);
    }
    return 0;
}
/***************************************************************************************************/
//COMPUTE FAST HISTOGRAM GRADIENT
static int icvGradientOfHistogram( const std::vector<int>& piHist, std::vector<int>& piHistGrad )
{
    piHistGrad[0] = 0;
    for ( int i=1; i<255; i++)
    {
        piHistGrad[i] = piHist[i-1] - piHist[i+1];
        if ( abs(piHistGrad[i]) < 100 )
        {
            if ( piHistGrad[i-1] == 0)
                piHistGrad[i] = -100;
            else
                piHistGrad[i] = piHistGrad[i-1];
        }
    }
    return 0;
}
/***************************************************************************************************/
//PERFORM SMART IMAGE THRESHOLDING BASED ON ANALYSIS OF INTENSTY HISTOGRAM
static bool icvBinarizationHistogramBased( Mat & img )
{
    CV_Assert(img.channels() == 1 && img.depth() == CV_8U);
    int iCols = img.cols;
    int iRows = img.rows;
    int iMaxPix = iCols*iRows;
    int iMaxPix1 = iMaxPix/100;
    const int iNumBins = 256;
    std::vector<int> piHistIntensity(iNumBins, 0);
    std::vector<int> piHistSmooth(iNumBins, 0);
    std::vector<int> piHistGrad(iNumBins, 0);
    std::vector<int> piAccumSum(iNumBins, 0);
    std::vector<int> piMaxPos(20, 0);
    int iThresh = 0;
    int iIdx;
    int iWidth = 1;

    icvGetIntensityHistogram( img, piHistIntensity );

    // get accumulated sum starting from bright
    piAccumSum[iNumBins-1] = piHistIntensity[iNumBins-1];
    for ( int i=iNumBins-2; i>=0; i-- )
    {
        piAccumSum[i] = piHistIntensity[i] + piAccumSum[i+1];
    }

    // first smooth the distribution
    icvSmoothHistogram( piHistIntensity, piHistSmooth, iWidth );

    // compute gradient
    icvGradientOfHistogram( piHistSmooth, piHistGrad );

    // check for zeros
    int iCntMaxima = 0;
    for ( int i=iNumBins-2; (i>2) && (iCntMaxima<20); i--)
    {
        if ( (piHistGrad[i-1] < 0) && (piHistGrad[i] > 0) )
        {
            piMaxPos[iCntMaxima] = i;
            iCntMaxima++;
        }
    }

    iIdx = 0;
    int iSumAroundMax = 0;
    for ( int i=0; i<iCntMaxima; i++ )
    {
        iIdx = piMaxPos[i];
        iSumAroundMax = piHistSmooth[iIdx-1] + piHistSmooth[iIdx] + piHistSmooth[iIdx+1];
        if ( iSumAroundMax < iMaxPix1 && iIdx < 64 )
        {
            for ( int j=i; j<iCntMaxima-1; j++ )
            {
                piMaxPos[j] = piMaxPos[j+1];
            }
            iCntMaxima--;
            i--;
        }
    }
    if ( iCntMaxima == 1)
    {
        iThresh = piMaxPos[0]/2;
    }
    else if ( iCntMaxima == 2)
    {
        iThresh = (piMaxPos[0] + piMaxPos[1])/2;
    }
    else // iCntMaxima >= 3
    {
        // CHECKING THRESHOLD FOR WHITE
        int iIdxAccSum = 0, iAccum = 0;
        for (int i=iNumBins-1; i>0; i--)
        {
            iAccum += piHistIntensity[i];
            // iMaxPix/18 is about 5,5%, minimum required number of pixels required for white part of chessboard
            if ( iAccum > (iMaxPix/18) )
            {
                iIdxAccSum = i;
                break;
            }
        }

        int iIdxBGMax = 0;
        int iBrightMax = piMaxPos[0];
        // printf("iBrightMax = %d\n", iBrightMax);
        for ( int n=0; n<iCntMaxima-1; n++)
        {
            iIdxBGMax = n+1;
            if ( piMaxPos[n] < iIdxAccSum )
            {
                break;
            }
            iBrightMax = piMaxPos[n];
        }

        // CHECKING THRESHOLD FOR BLACK
        int iMaxVal = piHistIntensity[piMaxPos[iIdxBGMax]];

        //IF TOO CLOSE TO 255, jump to next maximum
        if ( piMaxPos[iIdxBGMax] >= 250 && iIdxBGMax < iCntMaxima )
        {
            iIdxBGMax++;
            iMaxVal = piHistIntensity[piMaxPos[iIdxBGMax]];
        }

        for ( int n=iIdxBGMax + 1; n<iCntMaxima; n++)
        {
            if ( piHistIntensity[piMaxPos[n]] >= iMaxVal )
            {
                iMaxVal = piHistIntensity[piMaxPos[n]];
                iIdxBGMax = n;
            }
        }

        //SETTING THRESHOLD FOR BINARIZATION
        int iDist2 = (iBrightMax - piMaxPos[iIdxBGMax])/2;
        iThresh = iBrightMax - iDist2;
        PRINTF("THRESHOLD SELECTED = %d, BRIGHTMAX = %d, DARKMAX = %d\n", iThresh, iBrightMax, piMaxPos[iIdxBGMax]);
    }


    if ( iThresh > 0 )
    {
        for ( int jj=0; jj<iRows; jj++)
        {
            uchar * row = img.ptr(jj);
            for ( int ii=0; ii<iCols; ii++)
            {
                if ( row[ii] < iThresh )
                    row[ii] = 0;
                else
                    row[ii] = 255;
            }
        }
    }

    return true;
}

CV_IMPL
int cvFindChessboardCorners( const void* arr, CvSize pattern_size,
                             CvPoint2D32f* out_corners, int* out_corner_count,
                             int flags )
{
    int found = 0;
    CvCBQuad *quads = 0;
    CvCBCorner *corners = 0;

    cv::Ptr<CvMemStorage> storage;

    try
    {
    int k = 0;
    const int min_dilations = 0;
    const int max_dilations = 7;

    if( out_corner_count )
        *out_corner_count = 0;

    Mat img = cvarrToMat((CvMat*)arr).clone();

    if( img.depth() != CV_8U || (img.channels() != 1 && img.channels() != 3) )
       CV_Error( CV_StsUnsupportedFormat, "Only 8-bit grayscale or color images are supported" );

    if( pattern_size.width <= 2 || pattern_size.height <= 2 )
        CV_Error( CV_StsOutOfRange, "Both width and height of the pattern should have bigger than 2" );

    if( !out_corners )
        CV_Error( CV_StsNullPtr, "Null pointer to corners" );

    if (img.channels() != 1)
    {
        cvtColor(img, img, COLOR_BGR2GRAY);
    }


    Mat thresh_img_new = img.clone();
    icvBinarizationHistogramBased( thresh_img_new ); // process image in-place
    SHOW("New binarization", thresh_img_new);

    if( flags & CV_CALIB_CB_FAST_CHECK)
    {
        //perform new method for checking chessboard using a binary image.
        //image is binarised using a threshold dependent on the image histogram
        if (checkChessboardBinary(thresh_img_new, pattern_size) <= 0) //fall back to the old method
        {
            if (checkChessboard(img, pattern_size) <= 0)
            {
                return found;
            }
        }
    }

    storage.reset(cvCreateMemStorage(0));

    int prev_sqr_size = 0;

    // Try our standard "1" dilation, but if the pattern is not found, iterate the whole procedure with higher dilations.
    // This is necessary because some squares simply do not separate properly with a single dilation.  However,
    // we want to use the minimum number of dilations possible since dilations cause the squares to become smaller,
    // making it difficult to detect smaller squares.
    for( int dilations = min_dilations; dilations <= max_dilations; dilations++ )
    {
        if (found)
            break;      // already found it

        //USE BINARY IMAGE COMPUTED USING icvBinarizationHistogramBased METHOD
        dilate( thresh_img_new, thresh_img_new, Mat(), Point(-1, -1), 1 );

        // So we can find rectangles that go to the edge, we draw a white line around the image edge.
        // Otherwise FindContours will miss those clipped rectangle contours.
        // The border color will be the image mean, because otherwise we risk screwing up filters like cvSmooth()...
        rectangle( thresh_img_new, Point(0,0), Point(thresh_img_new.cols-1, thresh_img_new.rows-1), Scalar(255,255,255), 3, LINE_8);
        int max_quad_buf_size = 0;
        cvFree(&quads);
        cvFree(&corners);
        int quad_count = icvGenerateQuads( &quads, &corners, storage, thresh_img_new, flags, &max_quad_buf_size );
        PRINTF("Quad count: %d/%d\n", quad_count, (pattern_size.width/2+1)*(pattern_size.height/2+1));
        SHOW_QUADS("New quads", thresh_img_new, quads, quad_count);
        if (processQuads(quads, quad_count, pattern_size, max_quad_buf_size, storage, corners, out_corners, out_corner_count, prev_sqr_size))
            found = 1;
    }

    PRINTF("Chessboard detection result 0: %d\n", found);

    // revert to old, slower, method if detection failed
    if (!found)
    {
        if( flags & CV_CALIB_CB_NORMALIZE_IMAGE )
        {
            equalizeHist( img, img );
        }

        Mat thresh_img;
        prev_sqr_size = 0;

        PRINTF("Fallback to old algorithm\n");
        const bool useAdaptive = flags & CV_CALIB_CB_ADAPTIVE_THRESH;
        if (!useAdaptive)
        {
            // empiric threshold level
            // thresholding performed here and not inside the cycle to save processing time
            double mean = cv::mean(img).val[0];
            int thresh_level = MAX(cvRound( mean - 10 ), 10);
            threshold( img, thresh_img, thresh_level, 255, THRESH_BINARY );
        }
        //if flag CV_CALIB_CB_ADAPTIVE_THRESH is not set it doesn't make sense to iterate over k
        int max_k = useAdaptive ? 6 : 1;
        for( k = 0; k < max_k; k++ )
        {
            for( int dilations = min_dilations; dilations <= max_dilations; dilations++ )
            {
                if (found)
                    break;      // already found it

                // convert the input grayscale image to binary (black-n-white)
                if (useAdaptive)
                {
                    int block_size = cvRound(prev_sqr_size == 0
                                             ? MIN(img.cols, img.rows) * (k % 2 == 0 ? 0.2 : 0.1)
                                             : prev_sqr_size * 2);
                    block_size = block_size | 1;
                    // convert to binary
                    adaptiveThreshold( img, thresh_img, 255, ADAPTIVE_THRESH_MEAN_C, THRESH_BINARY, block_size, (k/2)*5 );
                    if (dilations > 0)
                        dilate( thresh_img, thresh_img, Mat(), Point(-1, -1), dilations-1 );

                }
                else
                {
                    dilate( thresh_img, thresh_img, Mat(), Point(-1, -1), 1 );
                }
                SHOW("Old binarization", thresh_img);

                // So we can find rectangles that go to the edge, we draw a white line around the image edge.
                // Otherwise FindContours will miss those clipped rectangle contours.
                // The border color will be the image mean, because otherwise we risk screwing up filters like cvSmooth()...
                rectangle( thresh_img, Point(0,0), Point(thresh_img.cols-1, thresh_img.rows-1), Scalar(255,255,255), 3, LINE_8);
                int max_quad_buf_size = 0;
                cvFree(&quads);
                cvFree(&corners);
                int quad_count = icvGenerateQuads( &quads, &corners, storage, thresh_img, flags, &max_quad_buf_size);
                PRINTF("Quad count: %d/%d\n", quad_count, (pattern_size.width/2+1)*(pattern_size.height/2+1));
                SHOW_QUADS("Old quads", thresh_img, quads, quad_count);
                if (processQuads(quads, quad_count, pattern_size, max_quad_buf_size, storage, corners, out_corners, out_corner_count, prev_sqr_size))
                    found = 1;
            }
        }
    }

    PRINTF("Chessboard detection result 1: %d\n", found);

    if( found )
        found = icvCheckBoardMonotony( out_corners, pattern_size );

    PRINTF("Chessboard detection result 2: %d\n", found);

    // check that none of the found corners is too close to the image boundary
    if( found )
    {
        const int BORDER = 8;
        for( k = 0; k < pattern_size.width*pattern_size.height; k++ )
        {
            if( out_corners[k].x <= BORDER || out_corners[k].x > img.cols - BORDER ||
                out_corners[k].y <= BORDER || out_corners[k].y > img.rows - BORDER )
                break;
        }

        found = k == pattern_size.width*pattern_size.height;
    }

    PRINTF("Chessboard detection result 3: %d\n", found);

    if( found )
    {
        if ( pattern_size.height % 2 == 0 && pattern_size.width % 2 == 0 )
        {
            int last_row = (pattern_size.height-1)*pattern_size.width;
            double dy0 = out_corners[last_row].y - out_corners[0].y;
            if( dy0 < 0 )
            {
                int n = pattern_size.width*pattern_size.height;
                for(int i = 0; i < n/2; i++ )
                {
                    CvPoint2D32f temp;
                    CV_SWAP(out_corners[i], out_corners[n-i-1], temp);
                }
            }
        }
        int wsize = 2;
        CvMat old_img(img);
        cvFindCornerSubPix( &old_img, out_corners, pattern_size.width*pattern_size.height,
                            cvSize(wsize, wsize), cvSize(-1,-1),
                            cvTermCriteria(CV_TERMCRIT_EPS+CV_TERMCRIT_ITER, 15, 0.1));
    }
    }
    catch(...)
    {
        cvFree(&quads);
        cvFree(&corners);
        throw;
    }
    cvFree(&quads);
    cvFree(&corners);
    return found;
}

//
// Checks that each board row and column is pretty much monotonous curve:
// It analyzes each row and each column of the chessboard as following:
//    for each corner c lying between end points in the same row/column it checks that
//    the point projection to the line segment (a,b) is lying between projections
//    of the neighbor corners in the same row/column.
//
// This function has been created as temporary workaround for the bug in current implementation
// of cvFindChessboardCornes that produces absolutely unordered sets of corners.
//

static int
icvCheckBoardMonotony( CvPoint2D32f* corners, CvSize pattern_size )
{
    int i, j, k;

    for( k = 0; k < 2; k++ )
    {
        for( i = 0; i < (k == 0 ? pattern_size.height : pattern_size.width); i++ )
        {
            CvPoint2D32f a = k == 0 ? corners[i*pattern_size.width] : corners[i];
            CvPoint2D32f b = k == 0 ? corners[(i+1)*pattern_size.width-1] :
                corners[(pattern_size.height-1)*pattern_size.width + i];
            float prevt = 0, dx0 = b.x - a.x, dy0 = b.y - a.y;
            if( fabs(dx0) + fabs(dy0) < FLT_EPSILON )
                return 0;
            for( j = 1; j < (k == 0 ? pattern_size.width : pattern_size.height) - 1; j++ )
            {
                CvPoint2D32f c = k == 0 ? corners[i*pattern_size.width + j] :
                    corners[j*pattern_size.width + i];
                float t = ((c.x - a.x)*dx0 + (c.y - a.y)*dy0)/(dx0*dx0 + dy0*dy0);
                if( t < prevt || t > 1 )
                    return 0;
                prevt = t;
            }
        }
    }

    return 1;
}

//
// order a group of connected quads
// order of corners:
//   0 is top left
//   clockwise from there
// note: "top left" is nominal, depends on initial ordering of starting quad
//   but all other quads are ordered consistently
//
// can change the number of quads in the group
// can add quads, so we need to have quad/corner arrays passed in
//

static int
icvOrderFoundConnectedQuads( int quad_count, CvCBQuad **quads,
        int *all_count, CvCBQuad **all_quads, CvCBCorner **corners,
        CvSize pattern_size, int max_quad_buf_size, CvMemStorage* storage )
{
    cv::Ptr<CvMemStorage> temp_storage(cvCreateChildMemStorage( storage ));
    CvSeq* stack = cvCreateSeq( 0, sizeof(*stack), sizeof(void*), temp_storage );

    // first find an interior quad
    CvCBQuad *start = NULL;
    for (int i=0; i<quad_count; i++)
    {
        if (quads[i]->count == 4)
        {
            start = quads[i];
            break;
        }
    }

    if (start == NULL)
        return 0;   // no 4-connected quad

    // start with first one, assign rows/cols
    int row_min = 0, col_min = 0, row_max=0, col_max = 0;

    std::map<int, int> col_hist;
    std::map<int, int> row_hist;

    cvSeqPush(stack, &start);
    start->row = 0;
    start->col = 0;
    start->ordered = true;

    // Recursively order the quads so that all position numbers (e.g.,
    // 0,1,2,3) are in the at the same relative corner (e.g., lower right).

    while( stack->total )
    {
        CvCBQuad* q;
        cvSeqPop( stack, &q );
        int col = q->col;
        int row = q->row;
        col_hist[col]++;
        row_hist[row]++;

        // check min/max
        if (row > row_max) row_max = row;
        if (row < row_min) row_min = row;
        if (col > col_max) col_max = col;
        if (col < col_min) col_min = col;

        for(int i = 0; i < 4; i++ )
        {
            CvCBQuad *neighbor = q->neighbors[i];
            switch(i)   // adjust col, row for this quad
            {           // start at top left, go clockwise
            case 0:
                row--; col--; break;
            case 1:
                col += 2; break;
            case 2:
                row += 2;   break;
            case 3:
                col -= 2; break;
            }

            // just do inside quads
            if (neighbor && neighbor->ordered == false && neighbor->count == 4)
            {
                PRINTF("col: %d  row: %d\n", col, row);
                icvOrderQuad(neighbor, q->corners[i], (i+2)%4); // set in order
                neighbor->ordered = true;
                neighbor->row = row;
                neighbor->col = col;
                cvSeqPush( stack, &neighbor );
            }
        }
    }

    for (int i=col_min; i<=col_max; i++)
        PRINTF("HIST[%d] = %d\n", i, col_hist[i]);

    // analyze inner quad structure
    int w = pattern_size.width - 1;
    int h = pattern_size.height - 1;
    int drow = row_max - row_min + 1;
    int dcol = col_max - col_min + 1;

    // normalize pattern and found quad indices
    if ((w > h && dcol < drow) ||
        (w < h && drow < dcol))
    {
        h = pattern_size.width - 1;
        w = pattern_size.height - 1;
    }

    PRINTF("Size: %dx%d  Pattern: %dx%d\n", dcol, drow, w, h);

    // check if there are enough inner quads
    if (dcol < w || drow < h)   // found enough inner quads?
    {
        PRINTF("Too few inner quad rows/cols\n");
        return 0;   // no, return
    }
#ifdef ENABLE_TRIM_COL_ROW
    // too many columns, not very common
    if (dcol == w+1)    // too many, trim
    {
        PRINTF("Trimming cols\n");
        if (col_hist[col_max] > col_hist[col_min])
        {
            PRINTF("Trimming left col\n");
            quad_count = icvTrimCol(quads,quad_count,col_min,-1);
        }
        else
        {
            PRINTF("Trimming right col\n");
            quad_count = icvTrimCol(quads,quad_count,col_max,+1);
        }
    }

    // too many rows, not very common
    if (drow == h+1)    // too many, trim
    {
        PRINTF("Trimming rows\n");
        if (row_hist[row_max] > row_hist[row_min])
        {
            PRINTF("Trimming top row\n");
            quad_count = icvTrimRow(quads,quad_count,row_min,-1);
        }
        else
        {
            PRINTF("Trimming bottom row\n");
            quad_count = icvTrimRow(quads,quad_count,row_max,+1);
        }
    }
#endif

    // check edges of inner quads
    // if there is an outer quad missing, fill it in
    // first order all inner quads
    int found = 0;
    for (int i=0; i<quad_count; i++)
    {
        if (quads[i]->count == 4)
        {   // ok, look at neighbors
            int col = quads[i]->col;
            int row = quads[i]->row;
            for (int j=0; j<4; j++)
            {
                switch(j)   // adjust col, row for this quad
                {       // start at top left, go clockwise
                case 0:
                    row--; col--; break;
                case 1:
                    col += 2; break;
                case 2:
                    row += 2;   break;
                case 3:
                    col -= 2; break;
                }
                CvCBQuad *neighbor = quads[i]->neighbors[j];
                if (neighbor && !neighbor->ordered && // is it an inner quad?
                    col <= col_max && col >= col_min &&
                    row <= row_max && row >= row_min)
                {
                    // if so, set in order
                    PRINTF("Adding inner: col: %d  row: %d\n", col, row);
                    found++;
                    icvOrderQuad(neighbor, quads[i]->corners[j], (j+2)%4);
                    neighbor->ordered = true;
                    neighbor->row = row;
                    neighbor->col = col;
                }
            }
        }
    }

    // if we have found inner quads, add corresponding outer quads,
    //   which are missing
    if (found > 0)
    {
        PRINTF("Found %d inner quads not connected to outer quads, repairing\n", found);
        for (int i=0; i<quad_count && *all_count < max_quad_buf_size; i++)
        {
            if (quads[i]->count < 4 && quads[i]->ordered)
            {
                int added = icvAddOuterQuad(quads[i],quads,quad_count,all_quads,*all_count,corners, max_quad_buf_size);
                *all_count += added;
                quad_count += added;
            }
        }

        if (*all_count >= max_quad_buf_size)
            return 0;
    }


    // final trimming of outer quads
    if (dcol == w && drow == h) // found correct inner quads
    {
        PRINTF("Inner bounds ok, check outer quads\n");
        int rcount = quad_count;
        for (int i=quad_count-1; i>=0; i--) // eliminate any quad not connected to
            // an ordered quad
        {
            if (quads[i]->ordered == false)
            {
                bool outer = false;
                for (int j=0; j<4; j++) // any neighbors that are ordered?
                {
                    if (quads[i]->neighbors[j] && quads[i]->neighbors[j]->ordered)
                        outer = true;
                }
                if (!outer) // not an outer quad, eliminate
                {
                    PRINTF("Removing quad %d\n", i);
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]);
                    rcount--;
                }
            }

        }
        return rcount;
    }

    return 0;
}


// add an outer quad
// looks for the neighbor of <quad> that isn't present,
//   tries to add it in.
// <quad> is ordered

static int
icvAddOuterQuad( CvCBQuad *quad, CvCBQuad **quads, int quad_count,
        CvCBQuad **all_quads, int all_count, CvCBCorner **corners, int max_quad_buf_size )

{
    int added = 0;
    for (int i=0; i<4 && all_count < max_quad_buf_size; i++) // find no-neighbor corners
    {
        if (!quad->neighbors[i])    // ok, create and add neighbor
        {
            int j = (i+2)%4;
            PRINTF("Adding quad as neighbor 2\n");
            CvCBQuad *q = &(*all_quads)[all_count];
            memset( q, 0, sizeof(*q) );
            added++;
            quads[quad_count] = q;
            quad_count++;

            // set neighbor and group id
            quad->neighbors[i] = q;
            quad->count += 1;
            q->neighbors[j] = quad;
            q->group_idx = quad->group_idx;
            q->count = 1;   // number of neighbors
            q->ordered = false;
            q->edge_len = quad->edge_len;

            // make corners of new quad
            // same as neighbor quad, but offset
            CvPoint2D32f pt = quad->corners[i]->pt;
            CvCBCorner* corner;
            float dx = pt.x - quad->corners[j]->pt.x;
            float dy = pt.y - quad->corners[j]->pt.y;
            for (int k=0; k<4; k++)
            {
                corner = &(*corners)[all_count*4+k];
                pt = quad->corners[k]->pt;
                memset( corner, 0, sizeof(*corner) );
                corner->pt = pt;
                q->corners[k] = corner;
                corner->pt.x += dx;
                corner->pt.y += dy;
            }
            // have to set exact corner
            q->corners[j] = quad->corners[i];

            // now find other neighbor and add it, if possible
            if (quad->neighbors[(i+3)%4] &&
                quad->neighbors[(i+3)%4]->ordered &&
                quad->neighbors[(i+3)%4]->neighbors[i] &&
                quad->neighbors[(i+3)%4]->neighbors[i]->ordered )
            {
                CvCBQuad *qn = quad->neighbors[(i+3)%4]->neighbors[i];
                q->count = 2;
                q->neighbors[(j+1)%4] = qn;
                qn->neighbors[(i+1)%4] = q;
                qn->count += 1;
                // have to set exact corner
                q->corners[(j+1)%4] = qn->corners[(i+1)%4];
            }

            all_count++;
        }
    }
    return added;
}


// trimming routines
#ifdef ENABLE_TRIM_COL_ROW
static int
icvTrimCol(CvCBQuad **quads, int count, int col, int dir)
{
    int rcount = count;
    // find the right quad(s)
    for (int i=0; i<count; i++)
    {
#ifdef DEBUG_CHESSBOARD
        if (quads[i]->ordered)
            PRINTF("index: %d  cur: %d\n", col, quads[i]->col);
#endif
        if (quads[i]->ordered && quads[i]->col == col)
        {
            if (dir == 1)
            {
                if (quads[i]->neighbors[1])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[1]);
                    rcount--;
                }
                if (quads[i]->neighbors[2])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[2]);
                    rcount--;
                }
            }
            else
            {
                if (quads[i]->neighbors[0])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[0]);
                    rcount--;
                }
                if (quads[i]->neighbors[3])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[3]);
                    rcount--;
                }
            }

        }
    }
    return rcount;
}

static int
icvTrimRow(CvCBQuad **quads, int count, int row, int dir)
{
    int i, rcount = count;
    // find the right quad(s)
    for (i=0; i<count; i++)
    {
#ifdef DEBUG_CHESSBOARD
        if (quads[i]->ordered)
            PRINTF("index: %d  cur: %d\n", row, quads[i]->row);
#endif
        if (quads[i]->ordered && quads[i]->row == row)
        {
            if (dir == 1)   // remove from bottom
            {
                if (quads[i]->neighbors[2])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[2]);
                    rcount--;
                }
                if (quads[i]->neighbors[3])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[3]);
                    rcount--;
                }
            }
            else    // remove from top
            {
                if (quads[i]->neighbors[0])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[0]);
                    rcount--;
                }
                if (quads[i]->neighbors[1])
                {
                    icvRemoveQuadFromGroup(quads,rcount,quads[i]->neighbors[1]);
                    rcount--;
                }
            }

        }
    }
    return rcount;
}
#endif

//
// remove quad from quad group
//

static void
icvRemoveQuadFromGroup(CvCBQuad **quads, int count, CvCBQuad *q0)
{
    int i, j;
    // remove any references to this quad as a neighbor
    for(i = 0; i < count; i++ )
    {
        CvCBQuad *q = quads[i];
        for(j = 0; j < 4; j++ )
        {
            if( q->neighbors[j] == q0 )
            {
                q->neighbors[j] = 0;
                q->count--;
                for(int k = 0; k < 4; k++ )
                    if( q0->neighbors[k] == q )
                    {
                        q0->neighbors[k] = 0;
                        q0->count--;
                        break;
                    }
                break;
            }
        }
    }

    // remove the quad
    for(i = 0; i < count; i++ )
    {
        CvCBQuad *q = quads[i];
        if (q == q0)
        {
            quads[i] = quads[count-1];
            break;
        }
    }
}

//
// put quad into correct order, where <corner> has value <common>
//

static void
icvOrderQuad(CvCBQuad *quad, CvCBCorner *corner, int common)
{
    // find the corner
    int tc;
    for (tc=0; tc<4; tc++)
        if (quad->corners[tc]->pt.x == corner->pt.x &&
            quad->corners[tc]->pt.y == corner->pt.y)
            break;

    // set corner order
    // shift
    while (tc != common)
    {
        // shift by one
        CvCBCorner *tempc;
        CvCBQuad *tempq;
        tempc = quad->corners[3];
        tempq = quad->neighbors[3];
        for (int i=3; i>0; i--)
        {
            quad->corners[i] = quad->corners[i-1];
            quad->neighbors[i] = quad->neighbors[i-1];
        }
        quad->corners[0] = tempc;
        quad->neighbors[0] = tempq;
        tc++;
        tc = tc%4;
    }
}


// if we found too many connect quads, remove those which probably do not belong.
static int
icvCleanFoundConnectedQuads( int quad_count, CvCBQuad **quad_group, CvSize pattern_size )
{
    CvPoint2D32f center;
    int i, j, k;
    // number of quads this pattern should contain
    int count = ((pattern_size.width + 1)*(pattern_size.height + 1) + 1)/2;

    // if we have more quadrangles than we should,
    // try to eliminate duplicates or ones which don't belong to the pattern rectangle...
    if( quad_count <= count )
        return quad_count;

    // create an array of quadrangle centers
    cv::AutoBuffer<CvPoint2D32f> centers( quad_count );
    cv::Ptr<CvMemStorage> temp_storage(cvCreateMemStorage(0));

    for( i = 0; i < quad_count; i++ )
    {
        CvPoint2D32f ci;
        CvCBQuad* q = quad_group[i];

        for( j = 0; j < 4; j++ )
        {
            CvPoint2D32f pt = q->corners[j]->pt;
            ci.x += pt.x;
            ci.y += pt.y;
        }

        ci.x *= 0.25f;
        ci.y *= 0.25f;

        centers[i] = ci;
        center.x += ci.x;
        center.y += ci.y;
    }
    center.x /= quad_count;
    center.y /= quad_count;

    // If we still have more quadrangles than we should,
    // we try to eliminate bad ones based on minimizing the bounding box.
    // We iteratively remove the point which reduces the size of
    // the bounding box of the blobs the most
    // (since we want the rectangle to be as small as possible)
    // remove the quadrange that causes the biggest reduction
    // in pattern size until we have the correct number
    for( ; quad_count > count; quad_count-- )
    {
        double min_box_area = DBL_MAX;
        int skip, min_box_area_index = -1;

        // For each point, calculate box area without that point
        for( skip = 0; skip < quad_count; skip++ )
        {
            // get bounding rectangle
            CvPoint2D32f temp = centers[skip]; // temporarily make index 'skip' the same as
            centers[skip] = center;            // pattern center (so it is not counted for convex hull)
            CvMat pointMat = cvMat(1, quad_count, CV_32FC2, centers);
            CvSeq *hull = cvConvexHull2( &pointMat, temp_storage, CV_CLOCKWISE, 1 );
            centers[skip] = temp;
            double hull_area = fabs(cvContourArea(hull, CV_WHOLE_SEQ));

            // remember smallest box area
            if( hull_area < min_box_area )
            {
                min_box_area = hull_area;
                min_box_area_index = skip;
            }
            cvClearMemStorage( temp_storage );
        }

        CvCBQuad *q0 = quad_group[min_box_area_index];

        // remove any references to this quad as a neighbor
        for( i = 0; i < quad_count; i++ )
        {
            CvCBQuad *q = quad_group[i];
            for( j = 0; j < 4; j++ )
            {
                if( q->neighbors[j] == q0 )
                {
                    q->neighbors[j] = 0;
                    q->count--;
                    for( k = 0; k < 4; k++ )
                        if( q0->neighbors[k] == q )
                        {
                            q0->neighbors[k] = 0;
                            q0->count--;
                            break;
                        }
                    break;
                }
            }
        }

        // remove the quad
        quad_count--;
        quad_group[min_box_area_index] = quad_group[quad_count];
        centers[min_box_area_index] = centers[quad_count];
    }

    return quad_count;
}

//=====================================================================================

static int
icvFindConnectedQuads( CvCBQuad *quad, int quad_count, CvCBQuad **out_group,
                       int group_idx, CvMemStorage* storage )
{
    cv::Ptr<CvMemStorage> temp_storage(cvCreateChildMemStorage( storage ));
    CvSeq* stack = cvCreateSeq( 0, sizeof(*stack), sizeof(void*), temp_storage );
    int i, count = 0;

    // Scan the array for a first unlabeled quad
    for( i = 0; i < quad_count; i++ )
    {
        if( quad[i].count > 0 && quad[i].group_idx < 0)
            break;
    }

    // Recursively find a group of connected quads starting from the seed quad[i]
    if( i < quad_count )
    {
        CvCBQuad* q = &quad[i];
        cvSeqPush( stack, &q );
        out_group[count++] = q;
        q->group_idx = group_idx;
        q->ordered = false;

        while( stack->total )
        {
            cvSeqPop( stack, &q );
            for( i = 0; i < 4; i++ )
            {
                CvCBQuad *neighbor = q->neighbors[i];
                if( neighbor && neighbor->count > 0 && neighbor->group_idx < 0 )
                {
                    cvSeqPush( stack, &neighbor );
                    out_group[count++] = neighbor;
                    neighbor->group_idx = group_idx;
                    neighbor->ordered = false;
                }
            }
        }
    }

    return count;
}


//=====================================================================================

static int
icvCheckQuadGroup( CvCBQuad **quad_group, int quad_count,
                   CvCBCorner **out_corners, CvSize pattern_size )
{
    const int ROW1 = 1000000;
    const int ROW2 = 2000000;
    const int ROW_ = 3000000;
    int result = 0;
    int i, out_corner_count = 0, corner_count = 0;
    std::vector<CvCBCorner*> corners(quad_count*4);

    int j, k, kk;
    int width = 0, height = 0;
    int hist[5] = {0,0,0,0,0};
    CvCBCorner* first = 0, *first2 = 0, *right, *cur, *below, *c;

    // build dual graph, which vertices are internal quad corners
    // and two vertices are connected iff they lie on the same quad edge
    for( i = 0; i < quad_count; i++ )
    {
        CvCBQuad* q = quad_group[i];
        /*CvScalar color = q->count == 0 ? cvScalar(0,255,255) :
                         q->count == 1 ? cvScalar(0,0,255) :
                         q->count == 2 ? cvScalar(0,255,0) :
                         q->count == 3 ? cvScalar(255,255,0) :
                                         cvScalar(255,0,0);*/

        for( j = 0; j < 4; j++ )
        {
            //cvLine( debug_img, cvPointFrom32f(q->corners[j]->pt), cvPointFrom32f(q->corners[(j+1)&3]->pt), color, 1, CV_AA, 0 );
            if( q->neighbors[j] )
            {
                CvCBCorner *a = q->corners[j], *b = q->corners[(j+1)&3];
                // mark internal corners that belong to:
                //   - a quad with a single neighbor - with ROW1,
                //   - a quad with two neighbors     - with ROW2
                // make the rest of internal corners with ROW_
                int row_flag = q->count == 1 ? ROW1 : q->count == 2 ? ROW2 : ROW_;

                if( a->row == 0 )
                {
                    corners[corner_count++] = a;
                    a->row = row_flag;
                }
                else if( a->row > row_flag )
                    a->row = row_flag;

                if( q->neighbors[(j+1)&3] )
                {
                    if( a->count >= 4 || b->count >= 4 )
                        goto finalize;
                    for( k = 0; k < 4; k++ )
                    {
                        if( a->neighbors[k] == b )
                            goto finalize;
                        if( b->neighbors[k] == a )
                            goto finalize;
                    }
                    a->neighbors[a->count++] = b;
                    b->neighbors[b->count++] = a;
                }
            }
        }
    }

    if( corner_count != pattern_size.width*pattern_size.height )
        goto finalize;

    for( i = 0; i < corner_count; i++ )
    {
        int n = corners[i]->count;
        assert( 0 <= n && n <= 4 );
        hist[n]++;
        if( !first && n == 2 )
        {
            if( corners[i]->row == ROW1 )
                first = corners[i];
            else if( !first2 && corners[i]->row == ROW2 )
                first2 = corners[i];
        }
    }

    // start with a corner that belongs to a quad with a signle neighbor.
    // if we do not have such, start with a corner of a quad with two neighbors.
    if( !first )
        first = first2;

    if( !first || hist[0] != 0 || hist[1] != 0 || hist[2] != 4 ||
        hist[3] != (pattern_size.width + pattern_size.height)*2 - 8 )
        goto finalize;

    cur = first;
    right = below = 0;
    out_corners[out_corner_count++] = cur;

    for( k = 0; k < 4; k++ )
    {
        c = cur->neighbors[k];
        if( c )
        {
            if( !right )
                right = c;
            else if( !below )
                below = c;
        }
    }

    if( !right || (right->count != 2 && right->count != 3) ||
        !below || (below->count != 2 && below->count != 3) )
        goto finalize;

    cur->row = 0;
    //cvCircle( debug_img, cvPointFrom32f(cur->pt), 3, cvScalar(0,255,0), -1, 8, 0 );

    first = below; // remember the first corner in the next row
    // find and store the first row (or column)
    for(j=1;;j++)
    {
        right->row = 0;
        out_corners[out_corner_count++] = right;
        //cvCircle( debug_img, cvPointFrom32f(right->pt), 3, cvScalar(0,255-j*10,0), -1, 8, 0 );
        if( right->count == 2 )
            break;
        if( right->count != 3 || out_corner_count >= MAX(pattern_size.width,pattern_size.height) )
            goto finalize;
        cur = right;
        for( k = 0; k < 4; k++ )
        {
            c = cur->neighbors[k];
            if( c && c->row > 0 )
            {
                for( kk = 0; kk < 4; kk++ )
                {
                    if( c->neighbors[kk] == below )
                        break;
                }
                if( kk < 4 )
                    below = c;
                else
                    right = c;
            }
        }
    }

    width = out_corner_count;
    if( width == pattern_size.width )
        height = pattern_size.height;
    else if( width == pattern_size.height )
        height = pattern_size.width;
    else
        goto finalize;

    // find and store all the other rows
    for( i = 1; ; i++ )
    {
        if( !first )
            break;
        cur = first;
        first = 0;
        for( j = 0;; j++ )
        {
            cur->row = i;
            out_corners[out_corner_count++] = cur;
            //cvCircle( debug_img, cvPointFrom32f(cur->pt), 3, cvScalar(0,0,255-j*10), -1, 8, 0 );
            if( cur->count == 2 + (i < height-1) && j > 0 )
                break;

            right = 0;

            // find a neighbor that has not been processed yet
            // and that has a neighbor from the previous row
            for( k = 0; k < 4; k++ )
            {
                c = cur->neighbors[k];
                if( c && c->row > i )
                {
                    for( kk = 0; kk < 4; kk++ )
                    {
                        if( c->neighbors[kk] && c->neighbors[kk]->row == i-1 )
                            break;
                    }
                    if( kk < 4 )
                    {
                        right = c;
                        if( j > 0 )
                            break;
                    }
                    else if( j == 0 )
                        first = c;
                }
            }
            if( !right )
                goto finalize;
            cur = right;
        }

        if( j != width - 1 )
            goto finalize;
    }

    if( out_corner_count != corner_count )
        goto finalize;

    // check if we need to transpose the board
    if( width != pattern_size.width )
    {
        CV_SWAP( width, height, k );

        memcpy( &corners[0], out_corners, corner_count*sizeof(corners[0]) );
        for( i = 0; i < height; i++ )
            for( j = 0; j < width; j++ )
                out_corners[i*width + j] = corners[j*height + i];
    }

    // check if we need to revert the order in each row
    {
        CvPoint2D32f p0 = out_corners[0]->pt, p1 = out_corners[pattern_size.width-1]->pt,
                     p2 = out_corners[pattern_size.width]->pt;
        if( (p1.x - p0.x)*(p2.y - p1.y) - (p1.y - p0.y)*(p2.x - p1.x) < 0 )
        {
            if( width % 2 == 0 )
            {
                for( i = 0; i < height; i++ )
                    for( j = 0; j < width/2; j++ )
                        CV_SWAP( out_corners[i*width+j], out_corners[i*width+width-j-1], c );
            }
            else
            {
                for( j = 0; j < width; j++ )
                    for( i = 0; i < height/2; i++ )
                        CV_SWAP( out_corners[i*width+j], out_corners[(height - i - 1)*width+j], c );
            }
        }
    }

    result = corner_count;

finalize:

    if( result <= 0 )
    {
        corner_count = MIN( corner_count, pattern_size.width*pattern_size.height );
        for( i = 0; i < corner_count; i++ )
            out_corners[i] = corners[i];
        result = -corner_count;

        if (result == -pattern_size.width*pattern_size.height)
            result = -result;
    }

    return result;
}




//=====================================================================================

static void icvFindQuadNeighbors( CvCBQuad *quads, int quad_count )
{
    const float thresh_scale = 1.f;
    int idx, i, k, j;
    float dx, dy, dist;

    // find quad neighbors
    for( idx = 0; idx < quad_count; idx++ )
    {
        CvCBQuad* cur_quad = &quads[idx];

        // choose the points of the current quadrangle that are close to
        // some points of the other quadrangles
        // (it can happen for split corners (due to dilation) of the
        // checker board). Search only in other quadrangles!

        // for each corner of this quadrangle
        for( i = 0; i < 4; i++ )
        {
            CvPoint2D32f pt;
            float min_dist = FLT_MAX;
            int closest_corner_idx = -1;
            CvCBQuad *closest_quad = 0;
            CvCBCorner *closest_corner = 0;

            if( cur_quad->neighbors[i] )
                continue;

            pt = cur_quad->corners[i]->pt;

            // find the closest corner in all other quadrangles
            for( k = 0; k < quad_count; k++ )
            {
                if( k == idx )
                    continue;

                for( j = 0; j < 4; j++ )
                {
                    if( quads[k].neighbors[j] )
                        continue;

                    dx = pt.x - quads[k].corners[j]->pt.x;
                    dy = pt.y - quads[k].corners[j]->pt.y;
                    dist = dx * dx + dy * dy;

                    if( dist < min_dist &&
                        dist <= cur_quad->edge_len*thresh_scale &&
                        dist <= quads[k].edge_len*thresh_scale )
                    {
                        // check edge lengths, make sure they're compatible
                        // edges that are different by more than 1:4 are rejected
                        float ediff = cur_quad->edge_len - quads[k].edge_len;
                        if (ediff > 32*cur_quad->edge_len ||
                            ediff > 32*quads[k].edge_len)
                        {
                            PRINTF("Incompatible edge lengths\n");
                            continue;
                        }
                        closest_corner_idx = j;
                        closest_quad = &quads[k];
                        min_dist = dist;
                    }
                }
            }

            // we found a matching corner point?
            if( closest_corner_idx >= 0 && min_dist < FLT_MAX )
            {
                // If another point from our current quad is closer to the found corner
                // than the current one, then we don't count this one after all.
                // This is necessary to support small squares where otherwise the wrong
                // corner will get matched to closest_quad;
                closest_corner = closest_quad->corners[closest_corner_idx];

                for( j = 0; j < 4; j++ )
                {
                    if( cur_quad->neighbors[j] == closest_quad )
                        break;

                    dx = closest_corner->pt.x - cur_quad->corners[j]->pt.x;
                    dy = closest_corner->pt.y - cur_quad->corners[j]->pt.y;

                    if( dx * dx + dy * dy < min_dist )
                        break;
                }

                if( j < 4 || cur_quad->count >= 4 || closest_quad->count >= 4 )
                    continue;

                // Check that each corner is a neighbor of different quads
                for( j = 0; j < closest_quad->count; j++ )
                {
                    if( closest_quad->neighbors[j] == cur_quad )
                        break;
                }
                if( j < closest_quad->count )
                    continue;

                // check whether the closest corner to closest_corner
                // is different from cur_quad->corners[i]->pt
                for( k = 0; k < quad_count; k++ )
                {
                    CvCBQuad* q = &quads[k];
                    if( k == idx || q == closest_quad )
                        continue;

                    for( j = 0; j < 4; j++ )
                        if( !q->neighbors[j] )
                        {
                            dx = closest_corner->pt.x - q->corners[j]->pt.x;
                            dy = closest_corner->pt.y - q->corners[j]->pt.y;
                            dist = dx*dx + dy*dy;
                            if( dist < min_dist )
                                break;
                        }
                    if( j < 4 )
                        break;
                }

                if( k < quad_count )
                    continue;

                closest_corner->pt.x = (pt.x + closest_corner->pt.x) * 0.5f;
                closest_corner->pt.y = (pt.y + closest_corner->pt.y) * 0.5f;

                // We've found one more corner - remember it
                cur_quad->count++;
                cur_quad->neighbors[i] = closest_quad;
                cur_quad->corners[i] = closest_corner;

                closest_quad->count++;
                closest_quad->neighbors[closest_corner_idx] = cur_quad;
            }
        }
    }
}

//=====================================================================================

// returns corners in clockwise order
// corners don't necessarily start at same position on quad (e.g.,
//   top left corner)

static int
icvGenerateQuads( CvCBQuad **out_quads, CvCBCorner **out_corners,
                  CvMemStorage *storage, const cv::Mat & image_, int flags, int *max_quad_buf_size )
{
    CvMat image_old(image_), *image = &image_old;
    int quad_count = 0;
    cv::Ptr<CvMemStorage> temp_storage;

    if( out_quads )
        *out_quads = 0;

    if( out_corners )
        *out_corners = 0;

    CvSeq *src_contour = 0;
    CvSeq *root;
    CvContourEx* board = 0;
    CvContourScanner scanner;
    int i, idx, min_size;

    CV_Assert( out_corners && out_quads );

    // empiric bound for minimal allowed perimeter for squares
    min_size = 25; //cvRound( image->cols * image->rows * .03 * 0.01 * 0.92 );

    // create temporary storage for contours and the sequence of pointers to found quadrangles
    temp_storage.reset(cvCreateChildMemStorage( storage ));
    root = cvCreateSeq( 0, sizeof(CvSeq), sizeof(CvSeq*), temp_storage );

    // initialize contour retrieving routine
    scanner = cvStartFindContours( image, temp_storage, sizeof(CvContourEx),
                                   CV_RETR_CCOMP, CV_CHAIN_APPROX_SIMPLE );

    // get all the contours one by one
    while( (src_contour = cvFindNextContour( scanner )) != 0 )
    {
        CvSeq *dst_contour = 0;
        CvRect rect = ((CvContour*)src_contour)->rect;

        // reject contours with too small perimeter
        if( CV_IS_SEQ_HOLE(src_contour) && rect.width*rect.height >= min_size )
        {
            const int min_approx_level = 1, max_approx_level = MAX_CONTOUR_APPROX;
            int approx_level;
            for( approx_level = min_approx_level; approx_level <= max_approx_level; approx_level++ )
            {
                dst_contour = cvApproxPoly( src_contour, sizeof(CvContour), temp_storage,
                                            CV_POLY_APPROX_DP, (float)approx_level );
                if( dst_contour->total == 4 )
                    break;

                // we call this again on its own output, because sometimes
                // cvApproxPoly() does not simplify as much as it should.
                dst_contour = cvApproxPoly( dst_contour, sizeof(CvContour), temp_storage,
                                            CV_POLY_APPROX_DP, (float)approx_level );

                if( dst_contour->total == 4 )
                    break;
            }

            // reject non-quadrangles
            if( dst_contour->total == 4 && cvCheckContourConvexity(dst_contour) )
            {
                CvPoint pt[4];
                double d1, d2, p = cvContourPerimeter(dst_contour);
                double area = fabs(cvContourArea(dst_contour, CV_WHOLE_SEQ));
                double dx, dy;

                for( i = 0; i < 4; i++ )
                    pt[i] = *(CvPoint*)cvGetSeqElem(dst_contour, i);

                dx = pt[0].x - pt[2].x;
                dy = pt[0].y - pt[2].y;
                d1 = sqrt(dx*dx + dy*dy);

                dx = pt[1].x - pt[3].x;
                dy = pt[1].y - pt[3].y;
                d2 = sqrt(dx*dx + dy*dy);

                // philipg.  Only accept those quadrangles which are more square
                // than rectangular and which are big enough
                double d3, d4;
                dx = pt[0].x - pt[1].x;
                dy = pt[0].y - pt[1].y;
                d3 = sqrt(dx*dx + dy*dy);
                dx = pt[1].x - pt[2].x;
                dy = pt[1].y - pt[2].y;
                d4 = sqrt(dx*dx + dy*dy);
                if( !(flags & CV_CALIB_CB_FILTER_QUADS) ||
                    (d3*4 > d4 && d4*4 > d3 && d3*d4 < area*1.5 && area > min_size &&
                    d1 >= 0.15 * p && d2 >= 0.15 * p) )
                {
                    CvContourEx* parent = (CvContourEx*)(src_contour->v_prev);
                    parent->counter++;
                    if( !board || board->counter < parent->counter )
                        board = parent;
                    dst_contour->v_prev = (CvSeq*)parent;
                    //for( i = 0; i < 4; i++ ) cvLine( debug_img, pt[i], pt[(i+1)&3], cvScalar(200,255,255), 1, CV_AA, 0 );
                    cvSeqPush( root, &dst_contour );
                }
            }
        }
    }

    // finish contour retrieving
    cvEndFindContours( &scanner );

    // allocate quad & corner buffers
    *max_quad_buf_size = MAX(1, (root->total+root->total / 2)) * 2;
    *out_quads = (CvCBQuad*)cvAlloc(*max_quad_buf_size * sizeof((*out_quads)[0]));
    *out_corners = (CvCBCorner*)cvAlloc(*max_quad_buf_size * 4 * sizeof((*out_corners)[0]));

    // Create array of quads structures
    for( idx = 0; idx < root->total; idx++ )
    {
        CvCBQuad* q = &(*out_quads)[quad_count];
        src_contour = *(CvSeq**)cvGetSeqElem( root, idx );
        if( (flags & CV_CALIB_CB_FILTER_QUADS) && src_contour->v_prev != (CvSeq*)board )
            continue;

        // reset group ID
        memset( q, 0, sizeof(*q) );
        q->group_idx = -1;
        assert( src_contour->total == 4 );
        for( i = 0; i < 4; i++ )
        {
            CvPoint2D32f pt = cvPointTo32f(*(CvPoint*)cvGetSeqElem(src_contour, i));
            CvCBCorner* corner = &(*out_corners)[quad_count*4 + i];

            memset( corner, 0, sizeof(*corner) );
            corner->pt = pt;
            q->corners[i] = corner;
        }
        q->edge_len = FLT_MAX;
        for( i = 0; i < 4; i++ )
        {
            float dx = q->corners[i]->pt.x - q->corners[(i+1)&3]->pt.x;
            float dy = q->corners[i]->pt.y - q->corners[(i+1)&3]->pt.y;
            float d = dx*dx + dy*dy;
            if( q->edge_len > d )
                q->edge_len = d;
        }
        quad_count++;
    }

    return quad_count;
}

static bool processQuads(CvCBQuad *quads, int quad_count, CvSize pattern_size, int max_quad_buf_size,
                         CvMemStorage * storage, CvCBCorner *corners, CvPoint2D32f *out_corners, int *out_corner_count, int & prev_sqr_size)
{
    if( quad_count <= 0 )
        return false;

    bool found = false;

    // Find quad's neighbors
    icvFindQuadNeighbors( quads, quad_count );

    // allocate extra for adding in icvOrderFoundQuads
    CvCBQuad **quad_group = 0;
    CvCBCorner **corner_group = 0;

    quad_group = (CvCBQuad**)cvAlloc( sizeof(quad_group[0]) * max_quad_buf_size);
    corner_group = (CvCBCorner**)cvAlloc( sizeof(corner_group[0]) * max_quad_buf_size * 4 );

    for( int group_idx = 0; ; group_idx++ )
    {
        int count = icvFindConnectedQuads( quads, quad_count, quad_group, group_idx, storage );

        if( count == 0 )
            break;

        // order the quad corners globally
        // maybe delete or add some
        PRINTF("Starting ordering of inner quads (%d)\n", count);
        count = icvOrderFoundConnectedQuads(count, quad_group, &quad_count, &quads, &corners,
                                            pattern_size, max_quad_buf_size, storage );
        PRINTF("Finished ordering of inner quads (%d)\n", count);

        if (count == 0)
            continue;       // haven't found inner quads

        // If count is more than it should be, this will remove those quads
        // which cause maximum deviation from a nice square pattern.
        count = icvCleanFoundConnectedQuads( count, quad_group, pattern_size );
        PRINTF("Connected group: %d, count: %d\n", group_idx, count);

        count = icvCheckQuadGroup( quad_group, count, corner_group, pattern_size );
        PRINTF("Connected group: %d, count: %d\n", group_idx, count);

        int n = count > 0 ? pattern_size.width * pattern_size.height : -count;
        n = MIN( n, pattern_size.width * pattern_size.height );
        float sum_dist = 0;
        int total = 0;

        for(int i = 0; i < n; i++ )
        {
            int ni = 0;
            float avgi = corner_group[i]->meanDist(&ni);
            sum_dist += avgi*ni;
            total += ni;
        }
        prev_sqr_size = cvRound(sum_dist/MAX(total, 1));

        if( count > 0 || (out_corner_count && -count > *out_corner_count) )
        {
            // copy corners to output array
            for(int i = 0; i < n; i++ )
                out_corners[i] = corner_group[i]->pt;

            if( out_corner_count )
                *out_corner_count = n;

            if( count == pattern_size.width*pattern_size.height
                    && icvCheckBoardMonotony( out_corners, pattern_size ))
            {
                found = true;
                break;
            }
        }
    }

    cvFree(&quad_group);
    cvFree(&corner_group);

    return found;
}

//==================================================================================================

CV_IMPL void
cvDrawChessboardCorners( CvArr* _image, CvSize pattern_size,
                         CvPoint2D32f* corners, int count, int found )
{
    const int shift = 0;
    const int radius = 4;
    const int r = radius*(1 << shift);
    int i;
    CvMat stub, *image;
    double scale = 1;
    int type, cn, line_type;

    image = cvGetMat( _image, &stub );

    type = CV_MAT_TYPE(image->type);
    cn = CV_MAT_CN(type);
    if( cn != 1 && cn != 3 && cn != 4 )
        CV_Error( CV_StsUnsupportedFormat, "Number of channels must be 1, 3 or 4" );

    switch( CV_MAT_DEPTH(image->type) )
    {
    case CV_8U:
        scale = 1;
        break;
    case CV_16U:
        scale = 256;
        break;
    case CV_32F:
        scale = 1./255;
        break;
    default:
        CV_Error( CV_StsUnsupportedFormat,
            "Only 8-bit, 16-bit or floating-point 32-bit images are supported" );
    }

    line_type = type == CV_8UC1 || type == CV_8UC3 ? CV_AA : 8;

    if( !found )
    {
        CvScalar color(0,0,255,0);
        if( cn == 1 )
            color = cvScalarAll(200);
        color.val[0] *= scale;
        color.val[1] *= scale;
        color.val[2] *= scale;
        color.val[3] *= scale;

        for( i = 0; i < count; i++ )
        {
            CvPoint pt;
            pt.x = cvRound(corners[i].x*(1 << shift));
            pt.y = cvRound(corners[i].y*(1 << shift));
            cvLine( image, cvPoint( pt.x - r, pt.y - r ),
                    cvPoint( pt.x + r, pt.y + r ), color, 1, line_type, shift );
            cvLine( image, cvPoint( pt.x - r, pt.y + r),
                    cvPoint( pt.x + r, pt.y - r), color, 1, line_type, shift );
            cvCircle( image, pt, r+(1<<shift), color, 1, line_type, shift );
        }
    }
    else
    {
        int x, y;
        CvPoint prev_pt;
        const int line_max = 7;
        static const CvScalar line_colors[line_max] =
        {
            CvScalar(0,0,255),
            CvScalar(0,128,255),
            CvScalar(0,200,200),
            CvScalar(0,255,0),
            CvScalar(200,200,0),
            CvScalar(255,0,0),
            CvScalar(255,0,255)
        };

        for( y = 0, i = 0; y < pattern_size.height; y++ )
        {
            CvScalar color = line_colors[y % line_max];
            if( cn == 1 )
                color = cvScalarAll(200);
            color.val[0] *= scale;
            color.val[1] *= scale;
            color.val[2] *= scale;
            color.val[3] *= scale;

            for( x = 0; x < pattern_size.width; x++, i++ )
            {
                CvPoint pt;
                pt.x = cvRound(corners[i].x*(1 << shift));
                pt.y = cvRound(corners[i].y*(1 << shift));

                if( i != 0 )
                    cvLine( image, prev_pt, pt, color, 1, line_type, shift );

                cvLine( image, cvPoint(pt.x - r, pt.y - r),
                        cvPoint(pt.x + r, pt.y + r), color, 1, line_type, shift );
                cvLine( image, cvPoint(pt.x - r, pt.y + r),
                        cvPoint(pt.x + r, pt.y - r), color, 1, line_type, shift );
                cvCircle( image, pt, r+(1<<shift), color, 1, line_type, shift );
                prev_pt = pt;
            }
        }
    }
}

bool cv::findChessboardCorners( InputArray _image, Size patternSize,
                            OutputArray corners, int flags )
{
    CV_INSTRUMENT_REGION()

    int count = patternSize.area()*2;
    std::vector<Point2f> tmpcorners(count+1);
    Mat image = _image.getMat(); CvMat c_image = image;
    bool ok = cvFindChessboardCorners(&c_image, patternSize,
        (CvPoint2D32f*)&tmpcorners[0], &count, flags ) > 0;
    if( count > 0 )
    {
        tmpcorners.resize(count);
        Mat(tmpcorners).copyTo(corners);
    }
    else
        corners.release();
    return ok;
}

namespace
{
int quiet_error(int /*status*/, const char* /*func_name*/,
                                       const char* /*err_msg*/, const char* /*file_name*/,
                                       int /*line*/, void* /*userdata*/ )
{
  return 0;
}
}

void cv::drawChessboardCorners( InputOutputArray _image, Size patternSize,
                            InputArray _corners,
                            bool patternWasFound )
{
    CV_INSTRUMENT_REGION()

    Mat corners = _corners.getMat();
    if( corners.empty() )
        return;
    Mat image = _image.getMat(); CvMat c_image = image;
    int nelems = corners.checkVector(2, CV_32F, true);
    CV_Assert(nelems >= 0);
    cvDrawChessboardCorners( &c_image, patternSize, corners.ptr<CvPoint2D32f>(),
                             nelems, patternWasFound );
}

bool cv::findCirclesGrid( InputArray _image, Size patternSize,
                          OutputArray _centers, int flags, const Ptr<FeatureDetector> &blobDetector )
{
    CV_INSTRUMENT_REGION()

    bool isAsymmetricGrid = (flags & CALIB_CB_ASYMMETRIC_GRID) ? true : false;
    bool isSymmetricGrid  = (flags & CALIB_CB_SYMMETRIC_GRID ) ? true : false;
    CV_Assert(isAsymmetricGrid ^ isSymmetricGrid);

    Mat image = _image.getMat();
    std::vector<Point2f> centers;

    std::vector<KeyPoint> keypoints;
    blobDetector->detect(image, keypoints);
    std::vector<Point2f> points;
    for (size_t i = 0; i < keypoints.size(); i++)
    {
      points.push_back (keypoints[i].pt);
    }

    if(flags & CALIB_CB_CLUSTERING)
    {
      CirclesGridClusterFinder circlesGridClusterFinder(isAsymmetricGrid);
      circlesGridClusterFinder.findGrid(points, patternSize, centers);
      Mat(centers).copyTo(_centers);
      return !centers.empty();
    }

    CirclesGridFinderParameters parameters;
    parameters.vertexPenalty = -0.6f;
    parameters.vertexGain = 1;
    parameters.existingVertexGain = 10000;
    parameters.edgeGain = 1;
    parameters.edgePenalty = -0.6f;

    if(flags & CALIB_CB_ASYMMETRIC_GRID)
      parameters.gridType = CirclesGridFinderParameters::ASYMMETRIC_GRID;
    if(flags & CALIB_CB_SYMMETRIC_GRID)
      parameters.gridType = CirclesGridFinderParameters::SYMMETRIC_GRID;

    const int attempts = 2;
    const size_t minHomographyPoints = 4;
    Mat H;
    for (int i = 0; i < attempts; i++)
    {
      centers.clear();
      CirclesGridFinder boxFinder(patternSize, points, parameters);
      bool isFound = false;
#define BE_QUIET 1
#if BE_QUIET
      void* oldCbkData;
      ErrorCallback oldCbk = redirectError(quiet_error, 0, &oldCbkData);
#endif
      try
      {
        isFound = boxFinder.findHoles();
      }
      catch (const cv::Exception &)
      {

      }
#if BE_QUIET
      redirectError(oldCbk, oldCbkData);
#endif
      if (isFound)
      {
        switch(parameters.gridType)
        {
          case CirclesGridFinderParameters::SYMMETRIC_GRID:
            boxFinder.getHoles(centers);
            break;
          case CirclesGridFinderParameters::ASYMMETRIC_GRID:
        boxFinder.getAsymmetricHoles(centers);
        break;
          default:
            CV_Error(CV_StsBadArg, "Unkown pattern type");
        }

        if (i != 0)
        {
          Mat orgPointsMat;
          transform(centers, orgPointsMat, H.inv());
          convertPointsFromHomogeneous(orgPointsMat, centers);
        }
        Mat(centers).copyTo(_centers);
        return true;
      }

      boxFinder.getHoles(centers);
      if (i != attempts - 1)
      {
        if (centers.size() < minHomographyPoints)
          break;
        H = CirclesGridFinder::rectifyGrid(boxFinder.getDetectedGridSize(), centers, points, points);
      }
    }
    Mat(centers).copyTo(_centers);
    return false;
}

/* End of file. */
