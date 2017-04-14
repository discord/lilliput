/**
 * @function EqualizeHist_Demo.cpp
 * @brief Demo code for equalizeHist function
 * @author OpenCV team
 */

#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include <iostream>

using namespace cv;
using namespace std;

/**
 * @function main
 */
int main( int, char** argv )
{
  Mat src, dst;

  const char* source_window = "Source image";
  const char* equalized_window = "Equalized Image";

  /// Load image
  src = imread( argv[1], IMREAD_COLOR );

  if( src.empty() )
    { cout<<"Usage: ./EqualizeHist_Demo <path_to_image>"<<endl;
      return -1;
    }

  /// Convert to grayscale
  cvtColor( src, src, COLOR_BGR2GRAY );

  /// Apply Histogram Equalization
  equalizeHist( src, dst );

  /// Display results
  namedWindow( source_window, WINDOW_AUTOSIZE );
  namedWindow( equalized_window, WINDOW_AUTOSIZE );

  imshow( source_window, src );
  imshow( equalized_window, dst );

  /// Wait until user exits the program
  waitKey(0);

  return 0;

}
