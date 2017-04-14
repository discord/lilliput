/**
 * @file Threshold.cpp
 * @brief Sample code that shows how to use the diverse threshold options offered by OpenCV
 * @author OpenCV team
 */

#include "opencv2/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"

using namespace cv;

/// Global variables

int threshold_value = 0;
int threshold_type = 3;
int const max_value = 255;
int const max_type = 4;
int const max_BINARY_value = 255;

Mat src, src_gray, dst;
const char* window_name = "Threshold Demo";

const char* trackbar_type = "Type: \n 0: Binary \n 1: Binary Inverted \n 2: Truncate \n 3: To Zero \n 4: To Zero Inverted";
const char* trackbar_value = "Value";

/// Function headers
void Threshold_Demo( int, void* );

/**
 * @function main
 */
int main( int, char** argv )
{
  //! [load]
  src = imread( argv[1], IMREAD_COLOR ); // Load an image

  if( src.empty() )
    { return -1; }

  cvtColor( src, src_gray, COLOR_BGR2GRAY ); // Convert the image to Gray
  //! [load]

  //! [window]
  namedWindow( window_name, WINDOW_AUTOSIZE ); // Create a window to display results
  //! [window]

  //! [trackbar]
  createTrackbar( trackbar_type,
                  window_name, &threshold_type,
                  max_type, Threshold_Demo ); // Create Trackbar to choose type of Threshold

  createTrackbar( trackbar_value,
                  window_name, &threshold_value,
                  max_value, Threshold_Demo ); // Create Trackbar to choose Threshold value
  //! [trackbar]

  Threshold_Demo( 0, 0 ); // Call the function to initialize

  /// Wait until user finishes program
  for(;;)
    {
      char c = (char)waitKey( 20 );
      if( c == 27 )
    { break; }
    }

}

//![Threshold_Demo]
/**
 * @function Threshold_Demo
 */
void Threshold_Demo( int, void* )
{
  /* 0: Binary
     1: Binary Inverted
     2: Threshold Truncated
     3: Threshold to Zero
     4: Threshold to Zero Inverted
   */

  threshold( src_gray, dst, threshold_value, max_BINARY_value,threshold_type );

  imshow( window_name, dst );
}
//![Threshold_Demo]
