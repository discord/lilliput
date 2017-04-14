// C++
#include <iostream>
#include <time.h>
// OpenCV
#include <opencv2//core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>
// PnP Tutorial
#include "Mesh.h"
#include "Model.h"
#include "PnPProblem.h"
#include "RobustMatcher.h"
#include "ModelRegistration.h"
#include "Utils.h"

/**  GLOBAL VARIABLES  **/

using namespace cv;
using namespace std;

string tutorial_path = "../../samples/cpp/tutorial_code/calib3d/real_time_pose_estimation/"; // path to tutorial

string video_read_path = tutorial_path + "Data/box.mp4";       // recorded video
string yml_read_path = tutorial_path + "Data/cookies_ORB.yml"; // 3dpts + descriptors
string ply_read_path = tutorial_path + "Data/box.ply";         // mesh

// Intrinsic camera parameters: UVC WEBCAM
double f = 55;                           // focal length in mm
double sx = 22.3, sy = 14.9;             // sensor size
double width = 640, height = 480;        // image size

double params_WEBCAM[] = { width*f/sx,   // fx
                           height*f/sy,  // fy
                           width/2,      // cx
                           height/2};    // cy

// Some basic colors
Scalar red(0, 0, 255);
Scalar green(0,255,0);
Scalar blue(255,0,0);
Scalar yellow(0,255,255);


// Robust Matcher parameters
int numKeyPoints = 2000;      // number of detected keypoints
float ratioTest = 0.70f;          // ratio test
bool fast_match = true;       // fastRobustMatch() or robustMatch()

// RANSAC parameters
int iterationsCount = 500;      // number of Ransac iterations.
float reprojectionError = 2.0;  // maximum allowed distance to consider it an inlier.
double confidence = 0.95;        // ransac successful confidence.

// Kalman Filter parameters
int minInliersKalman = 30;    // Kalman threshold updating

// PnP parameters
int pnpMethod = SOLVEPNP_ITERATIVE;


/**  Functions headers  **/
void help();
void initKalmanFilter( KalmanFilter &KF, int nStates, int nMeasurements, int nInputs, double dt);
void updateKalmanFilter( KalmanFilter &KF, Mat &measurements,
                         Mat &translation_estimated, Mat &rotation_estimated );
void fillMeasurements( Mat &measurements,
                       const Mat &translation_measured, const Mat &rotation_measured);


/**  Main program  **/
int main(int argc, char *argv[])
{

  help();

  const String keys =
      "{help h        |      | print this message                   }"
      "{video v       |      | path to recorded video               }"
      "{model         |      | path to yml model                    }"
      "{mesh          |      | path to ply mesh                     }"
      "{keypoints k   |2000  | number of keypoints to detect        }"
      "{ratio r       |0.7   | threshold for ratio test             }"
      "{iterations it |500   | RANSAC maximum iterations count      }"
      "{error e       |2.0   | RANSAC reprojection errror           }"
      "{confidence c  |0.95  | RANSAC confidence                    }"
      "{inliers in    |30    | minimum inliers for Kalman update    }"
      "{method  pnp   |0     | PnP method: (0) ITERATIVE - (1) EPNP - (2) P3P - (3) DLS}"
      "{fast f        |true  | use of robust fast match             }"
      ;
  CommandLineParser parser(argc, argv, keys);

  if (parser.has("help"))
  {
      parser.printMessage();
      return 0;
  }
  else
  {
    video_read_path = parser.get<string>("video").size() > 0 ? parser.get<string>("video") : video_read_path;
    yml_read_path = parser.get<string>("model").size() > 0 ? parser.get<string>("model") : yml_read_path;
    ply_read_path = parser.get<string>("mesh").size() > 0 ? parser.get<string>("mesh") : ply_read_path;
    numKeyPoints = !parser.has("keypoints") ? parser.get<int>("keypoints") : numKeyPoints;
    ratioTest = !parser.has("ratio") ? parser.get<float>("ratio") : ratioTest;
    fast_match = !parser.has("fast") ? parser.get<bool>("fast") : fast_match;
    iterationsCount = !parser.has("iterations") ? parser.get<int>("iterations") : iterationsCount;
    reprojectionError = !parser.has("error") ? parser.get<float>("error") : reprojectionError;
    confidence = !parser.has("confidence") ? parser.get<float>("confidence") : confidence;
    minInliersKalman = !parser.has("inliers") ? parser.get<int>("inliers") : minInliersKalman;
    pnpMethod = !parser.has("method") ? parser.get<int>("method") : pnpMethod;
  }

  PnPProblem pnp_detection(params_WEBCAM);
  PnPProblem pnp_detection_est(params_WEBCAM);

  Model model;               // instantiate Model object
  model.load(yml_read_path); // load a 3D textured object model

  Mesh mesh;                 // instantiate Mesh object
  mesh.load(ply_read_path);  // load an object mesh

  RobustMatcher rmatcher;                                                     // instantiate RobustMatcher

  Ptr<FeatureDetector> orb = ORB::create();

  rmatcher.setFeatureDetector(orb);                                      // set feature detector
  rmatcher.setDescriptorExtractor(orb);                                 // set descriptor extractor

  Ptr<flann::IndexParams> indexParams = makePtr<flann::LshIndexParams>(6, 12, 1); // instantiate LSH index parameters
  Ptr<flann::SearchParams> searchParams = makePtr<flann::SearchParams>(50);       // instantiate flann search parameters

  // instantiate FlannBased matcher
  Ptr<DescriptorMatcher> matcher = makePtr<FlannBasedMatcher>(indexParams, searchParams);
  rmatcher.setDescriptorMatcher(matcher);                                                         // set matcher
  rmatcher.setRatio(ratioTest); // set ratio test parameter

  KalmanFilter KF;         // instantiate Kalman Filter
  int nStates = 18;            // the number of states
  int nMeasurements = 6;       // the number of measured states
  int nInputs = 0;             // the number of control actions
  double dt = 0.125;           // time between measurements (1/FPS)

  initKalmanFilter(KF, nStates, nMeasurements, nInputs, dt);    // init function
  Mat measurements(nMeasurements, 1, CV_64F); measurements.setTo(Scalar(0));
  bool good_measurement = false;


  // Get the MODEL INFO
  vector<Point3f> list_points3d_model = model.get_points3d();  // list with model 3D coordinates
  Mat descriptors_model = model.get_descriptors();                  // list with descriptors of each 3D coordinate


  // Create & Open Window
  namedWindow("REAL TIME DEMO", WINDOW_KEEPRATIO);


  VideoCapture cap;                           // instantiate VideoCapture
  cap.open(video_read_path);                      // open a recorded video

  if(!cap.isOpened())   // check if we succeeded
  {
    cout << "Could not open the camera device" << endl;
    return -1;
  }

  // start and end times
  time_t start, end;

  // fps calculated using number of frames / seconds
  // floating point seconds elapsed since start
  double fps, sec;

  // frame counter
  int counter = 0;

  // start the clock
  time(&start);

  Mat frame, frame_vis;

  while(cap.read(frame) && (char)waitKey(30) != 27) // capture frame until ESC is pressed
  {

    frame_vis = frame.clone();    // refresh visualisation frame


    // -- Step 1: Robust matching between model descriptors and scene descriptors

    vector<DMatch> good_matches;       // to obtain the 3D points of the model
    vector<KeyPoint> keypoints_scene;  // to obtain the 2D points of the scene


    if(fast_match)
    {
      rmatcher.fastRobustMatch(frame, good_matches, keypoints_scene, descriptors_model);
    }
    else
    {
      rmatcher.robustMatch(frame, good_matches, keypoints_scene, descriptors_model);
    }


    // -- Step 2: Find out the 2D/3D correspondences

    vector<Point3f> list_points3d_model_match; // container for the model 3D coordinates found in the scene
    vector<Point2f> list_points2d_scene_match; // container for the model 2D coordinates found in the scene

    for(unsigned int match_index = 0; match_index < good_matches.size(); ++match_index)
    {
      Point3f point3d_model = list_points3d_model[ good_matches[match_index].trainIdx ];  // 3D point from model
      Point2f point2d_scene = keypoints_scene[ good_matches[match_index].queryIdx ].pt; // 2D point from the scene
      list_points3d_model_match.push_back(point3d_model);         // add 3D point
      list_points2d_scene_match.push_back(point2d_scene);         // add 2D point
    }

    // Draw outliers
    draw2DPoints(frame_vis, list_points2d_scene_match, red);


    Mat inliers_idx;
    vector<Point2f> list_points2d_inliers;

    if(good_matches.size() > 0) // None matches, then RANSAC crashes
    {

      // -- Step 3: Estimate the pose using RANSAC approach
      pnp_detection.estimatePoseRANSAC( list_points3d_model_match, list_points2d_scene_match,
                                        pnpMethod, inliers_idx,
                                        iterationsCount, reprojectionError, confidence );

      // -- Step 4: Catch the inliers keypoints to draw
      for(int inliers_index = 0; inliers_index < inliers_idx.rows; ++inliers_index)
      {
        int n = inliers_idx.at<int>(inliers_index);         // i-inlier
        Point2f point2d = list_points2d_scene_match[n]; // i-inlier point 2D
        list_points2d_inliers.push_back(point2d);           // add i-inlier to list
      }

      // Draw inliers points 2D
      draw2DPoints(frame_vis, list_points2d_inliers, blue);


      // -- Step 5: Kalman Filter

      good_measurement = false;

      // GOOD MEASUREMENT
      if( inliers_idx.rows >= minInliersKalman )
      {

        // Get the measured translation
        Mat translation_measured(3, 1, CV_64F);
        translation_measured = pnp_detection.get_t_matrix();

        // Get the measured rotation
        Mat rotation_measured(3, 3, CV_64F);
        rotation_measured = pnp_detection.get_R_matrix();

        // fill the measurements vector
        fillMeasurements(measurements, translation_measured, rotation_measured);

        good_measurement = true;

      }

      // Instantiate estimated translation and rotation
      Mat translation_estimated(3, 1, CV_64F);
      Mat rotation_estimated(3, 3, CV_64F);

      // update the Kalman filter with good measurements
      updateKalmanFilter( KF, measurements,
                          translation_estimated, rotation_estimated);


      // -- Step 6: Set estimated projection matrix
      pnp_detection_est.set_P_matrix(rotation_estimated, translation_estimated);

    }

    // -- Step X: Draw pose

    if(good_measurement)
    {
      drawObjectMesh(frame_vis, &mesh, &pnp_detection, green);  // draw current pose
    }
    else
    {
      drawObjectMesh(frame_vis, &mesh, &pnp_detection_est, yellow); // draw estimated pose
    }

    float l = 5;
    vector<Point2f> pose_points2d;
    pose_points2d.push_back(pnp_detection_est.backproject3DPoint(Point3f(0,0,0)));  // axis center
    pose_points2d.push_back(pnp_detection_est.backproject3DPoint(Point3f(l,0,0)));  // axis x
    pose_points2d.push_back(pnp_detection_est.backproject3DPoint(Point3f(0,l,0)));  // axis y
    pose_points2d.push_back(pnp_detection_est.backproject3DPoint(Point3f(0,0,l)));  // axis z
    draw3DCoordinateAxes(frame_vis, pose_points2d);           // draw axes

    // FRAME RATE

    // see how much time has elapsed
    time(&end);

    // calculate current FPS
    ++counter;
    sec = difftime (end, start);

    fps = counter / sec;

    drawFPS(frame_vis, fps, yellow); // frame ratio
    double detection_ratio = ((double)inliers_idx.rows/(double)good_matches.size())*100;
    drawConfidence(frame_vis, detection_ratio, yellow);


    // -- Step X: Draw some debugging text

    // Draw some debug text
    int inliers_int = inliers_idx.rows;
    int outliers_int = (int)good_matches.size() - inliers_int;
    string inliers_str = IntToString(inliers_int);
    string outliers_str = IntToString(outliers_int);
    string n = IntToString((int)good_matches.size());
    string text = "Found " + inliers_str + " of " + n + " matches";
    string text2 = "Inliers: " + inliers_str + " - Outliers: " + outliers_str;

    drawText(frame_vis, text, green);
    drawText2(frame_vis, text2, red);

    imshow("REAL TIME DEMO", frame_vis);
  }

  // Close and Destroy Window
  destroyWindow("REAL TIME DEMO");

  cout << "GOODBYE ..." << endl;

}

/**********************************************************************************************************/
void help()
{
cout
<< "--------------------------------------------------------------------------"   << endl
<< "This program shows how to detect an object given its 3D textured model. You can choose to "
<< "use a recorded video or the webcam."                                          << endl
<< "Usage:"                                                                       << endl
<< "./cpp-tutorial-pnp_detection -help"                                           << endl
<< "Keys:"                                                                        << endl
<< "'esc' - to quit."                                                             << endl
<< "--------------------------------------------------------------------------"   << endl
<< endl;
}

/**********************************************************************************************************/
void initKalmanFilter(KalmanFilter &KF, int nStates, int nMeasurements, int nInputs, double dt)
{

  KF.init(nStates, nMeasurements, nInputs, CV_64F);                 // init Kalman Filter

  setIdentity(KF.processNoiseCov, Scalar::all(1e-5));       // set process noise
  setIdentity(KF.measurementNoiseCov, Scalar::all(1e-2));   // set measurement noise
  setIdentity(KF.errorCovPost, Scalar::all(1));             // error covariance


                     /** DYNAMIC MODEL **/

  //  [1 0 0 dt  0  0 dt2   0   0 0 0 0  0  0  0   0   0   0]
  //  [0 1 0  0 dt  0   0 dt2   0 0 0 0  0  0  0   0   0   0]
  //  [0 0 1  0  0 dt   0   0 dt2 0 0 0  0  0  0   0   0   0]
  //  [0 0 0  1  0  0  dt   0   0 0 0 0  0  0  0   0   0   0]
  //  [0 0 0  0  1  0   0  dt   0 0 0 0  0  0  0   0   0   0]
  //  [0 0 0  0  0  1   0   0  dt 0 0 0  0  0  0   0   0   0]
  //  [0 0 0  0  0  0   1   0   0 0 0 0  0  0  0   0   0   0]
  //  [0 0 0  0  0  0   0   1   0 0 0 0  0  0  0   0   0   0]
  //  [0 0 0  0  0  0   0   0   1 0 0 0  0  0  0   0   0   0]
  //  [0 0 0  0  0  0   0   0   0 1 0 0 dt  0  0 dt2   0   0]
  //  [0 0 0  0  0  0   0   0   0 0 1 0  0 dt  0   0 dt2   0]
  //  [0 0 0  0  0  0   0   0   0 0 0 1  0  0 dt   0   0 dt2]
  //  [0 0 0  0  0  0   0   0   0 0 0 0  1  0  0  dt   0   0]
  //  [0 0 0  0  0  0   0   0   0 0 0 0  0  1  0   0  dt   0]
  //  [0 0 0  0  0  0   0   0   0 0 0 0  0  0  1   0   0  dt]
  //  [0 0 0  0  0  0   0   0   0 0 0 0  0  0  0   1   0   0]
  //  [0 0 0  0  0  0   0   0   0 0 0 0  0  0  0   0   1   0]
  //  [0 0 0  0  0  0   0   0   0 0 0 0  0  0  0   0   0   1]

  // position
  KF.transitionMatrix.at<double>(0,3) = dt;
  KF.transitionMatrix.at<double>(1,4) = dt;
  KF.transitionMatrix.at<double>(2,5) = dt;
  KF.transitionMatrix.at<double>(3,6) = dt;
  KF.transitionMatrix.at<double>(4,7) = dt;
  KF.transitionMatrix.at<double>(5,8) = dt;
  KF.transitionMatrix.at<double>(0,6) = 0.5*pow(dt,2);
  KF.transitionMatrix.at<double>(1,7) = 0.5*pow(dt,2);
  KF.transitionMatrix.at<double>(2,8) = 0.5*pow(dt,2);

  // orientation
  KF.transitionMatrix.at<double>(9,12) = dt;
  KF.transitionMatrix.at<double>(10,13) = dt;
  KF.transitionMatrix.at<double>(11,14) = dt;
  KF.transitionMatrix.at<double>(12,15) = dt;
  KF.transitionMatrix.at<double>(13,16) = dt;
  KF.transitionMatrix.at<double>(14,17) = dt;
  KF.transitionMatrix.at<double>(9,15) = 0.5*pow(dt,2);
  KF.transitionMatrix.at<double>(10,16) = 0.5*pow(dt,2);
  KF.transitionMatrix.at<double>(11,17) = 0.5*pow(dt,2);


           /** MEASUREMENT MODEL **/

  //  [1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0]
  //  [0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0]
  //  [0 0 1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0]
  //  [0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 0]
  //  [0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0]
  //  [0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0]

  KF.measurementMatrix.at<double>(0,0) = 1;  // x
  KF.measurementMatrix.at<double>(1,1) = 1;  // y
  KF.measurementMatrix.at<double>(2,2) = 1;  // z
  KF.measurementMatrix.at<double>(3,9) = 1;  // roll
  KF.measurementMatrix.at<double>(4,10) = 1; // pitch
  KF.measurementMatrix.at<double>(5,11) = 1; // yaw

}

/**********************************************************************************************************/
void updateKalmanFilter( KalmanFilter &KF, Mat &measurement,
                         Mat &translation_estimated, Mat &rotation_estimated )
{

  // First predict, to update the internal statePre variable
  Mat prediction = KF.predict();

  // The "correct" phase that is going to use the predicted value and our measurement
  Mat estimated = KF.correct(measurement);

  // Estimated translation
  translation_estimated.at<double>(0) = estimated.at<double>(0);
  translation_estimated.at<double>(1) = estimated.at<double>(1);
  translation_estimated.at<double>(2) = estimated.at<double>(2);

  // Estimated euler angles
  Mat eulers_estimated(3, 1, CV_64F);
  eulers_estimated.at<double>(0) = estimated.at<double>(9);
  eulers_estimated.at<double>(1) = estimated.at<double>(10);
  eulers_estimated.at<double>(2) = estimated.at<double>(11);

  // Convert estimated quaternion to rotation matrix
  rotation_estimated = euler2rot(eulers_estimated);

}

/**********************************************************************************************************/
void fillMeasurements( Mat &measurements,
                       const Mat &translation_measured, const Mat &rotation_measured)
{
  // Convert rotation matrix to euler angles
  Mat measured_eulers(3, 1, CV_64F);
  measured_eulers = rot2euler(rotation_measured);

  // Set measurement to predict
  measurements.at<double>(0) = translation_measured.at<double>(0); // x
  measurements.at<double>(1) = translation_measured.at<double>(1); // y
  measurements.at<double>(2) = translation_measured.at<double>(2); // z
  measurements.at<double>(3) = measured_eulers.at<double>(0);      // roll
  measurements.at<double>(4) = measured_eulers.at<double>(1);      // pitch
  measurements.at<double>(5) = measured_eulers.at<double>(2);      // yaw
}
