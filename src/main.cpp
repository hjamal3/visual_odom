#include "opencv2/video/tracking.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/features2d/features2d.hpp"
#include "opencv2/calib3d/calib3d.hpp"

#include <iostream>
#include <ctype.h>
#include <algorithm>
#include <iterator>
#include <vector>
#include <ctime>
#include <sstream>
#include <fstream>
#include <string>

#include "feature.h"
#include "utils.h"
#include "evaluate_odometry.h"
#include "visualOdometry.h"
#include "Frame.h"

using namespace std;

int main(int argc, char **argv)
{

    #if USE_CUDA
        printf("CUDA Enabled\n");
    #endif
    // -----------------------------------------
    // Load images and calibration parameters
    // -----------------------------------------
    bool display_ground_truth = false;
    std::vector<Matrix> pose_matrix_gt;
    if(argc == 4)
    {   display_ground_truth = true;
        cerr << "Display ground truth trajectory" << endl;
        // load ground truth pose
        string filename_pose = string(argv[3]);
        pose_matrix_gt = loadPoses(filename_pose);

    }
    if(argc < 3)
    {
        cerr << "Usage: ./run path_to_sequence path_to_calibration [optional]path_to_ground_truth_pose" << endl;
        return 1;
    }

    // Sequence
    string filepath = string(argv[1]);
    cout << "Filepath: " << filepath << endl;

    // Camera calibration
    string strSettingPath = string(argv[2]);
    cout << "Calibration Filepath: " << strSettingPath << endl;

    cv::FileStorage fSettings(strSettingPath, cv::FileStorage::READ);

    float fx = fSettings["Camera.fx"];
    float fy = fSettings["Camera.fy"];
    float cx = fSettings["Camera.cx"];
    float cy = fSettings["Camera.cy"];
    float bf = fSettings["Camera.bf"];

    cv::Mat projMatrl = (cv::Mat_<float>(3, 4) << fx, 0., cx, 0., 0., fy, cy, 0., 0,  0., 1., 0.);
    cv::Mat projMatrr = (cv::Mat_<float>(3, 4) << fx, 0., cx, bf, 0., fy, cy, 0., 0,  0., 1., 0.);
    cout << "K_left: " << endl << projMatrl << endl;
    cout << "K_right: " << endl << projMatrr << endl;

    // -----------------------------------------
    // Initialize variables
    // -----------------------------------------
    cv::Mat rotation = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat translation = cv::Mat::zeros(3, 1, CV_64F);
    cv::Mat frame_pose = cv::Mat::eye(4, 4, CV_64F);

    std::cout << "frame_pose " << frame_pose << std::endl;
    cv::Mat trajectory = cv::Mat::zeros(600, 1200, CV_8UC3);
    FeatureSet currentVOFeatures;

    int init_frame_id = 0;

    // ------------------------
    // Load first images
    // ------------------------
    cv::Mat imageRight_t0,  imageLeft_t0;
    cv::Mat imageLeft_t0_color;
    loadImageLeft(imageLeft_t0_color,  imageLeft_t0, init_frame_id, filepath);
    cv::Mat imageRight_t0_color;  
    loadImageRight(imageRight_t0_color, imageRight_t0, init_frame_id, filepath);
    clock_t t_a, t_b;

    // -----------------------------------------
    // Run visual odometry
    // -----------------------------------------

    for (int frame_id = init_frame_id+1; frame_id < 9000; frame_id++)
    {

        std::cout << std::endl << "frame id " << frame_id << std::endl;
        // ------------
        // Load images
        // ------------
        cv::Mat imageRight_t1,  imageLeft_t1;

        cv::Mat imageLeft_t1_color;
        loadImageLeft(imageLeft_t1_color,  imageLeft_t1, frame_id, filepath);        
        cv::Mat imageRight_t1_color;  
        loadImageRight(imageRight_t1_color, imageRight_t1, frame_id, filepath);   

        t_a = clock();
        std::vector<cv::Point2f> pointsLeft_t0, pointsRight_t0, pointsLeft_t1, pointsRight_t1;  
        matchingFeatures( imageLeft_t0, imageRight_t0,
                          imageLeft_t1, imageRight_t1, 
                          currentVOFeatures,
                          pointsLeft_t0, 
                          pointsRight_t0, 
                          pointsLeft_t1, 
                          pointsRight_t1);  

        // set new images as old images
        imageLeft_t0 = imageLeft_t1;
        imageRight_t0 = imageRight_t1;

        // ---------------------
        // Triangulate 3D Points
        // ---------------------
        cv::Mat points3D_t0, points4D_t0;
        cv::triangulatePoints( projMatrl,  projMatrr,  pointsLeft_t0,  pointsRight_t0,  points4D_t0);
        cv::convertPointsFromHomogeneous(points4D_t0.t(), points3D_t0);

        // ---------------------
        // Tracking transfomation
        // ---------------------
        clock_t tic_gpu = clock();
        trackingFrame2Frame(projMatrl, projMatrr, pointsLeft_t0, pointsLeft_t1, points3D_t0, rotation, translation, false);
        clock_t toc_gpu = clock();
        std::cerr << "tracking frame 2 frame: " << float(toc_gpu - tic_gpu)/CLOCKS_PER_SEC*1000 << "ms" << std::endl;
        displayTracking(imageLeft_t1, pointsLeft_t0, pointsLeft_t1);

        // ------------------------------------------------
        // Integrating and display
        // ------------------------------------------------

        cv::Vec3f rotation_euler = rotationMatrixToEulerAngles(rotation);
        cv::Mat rigid_body_transformation;

        if(abs(rotation_euler[1])<0.1 && abs(rotation_euler[0])<0.1 && abs(rotation_euler[2])<0.1)
        {
            integrateOdometryStereo(frame_id, rigid_body_transformation, frame_pose, rotation, translation);

        } else {

            std::cout << "Too large rotation"  << std::endl;
        }
        t_b = clock();
        float frame_time = 1000*(double)(t_b-t_a)/CLOCKS_PER_SEC;
        float fps = 1000/frame_time;
        cout << "[Info] frame times (ms): " << frame_time << endl;
        cout << "[Info] FPS: " << fps << endl;

        cv::Mat xyz = frame_pose.col(3).clone();
        display(frame_id, trajectory, xyz, pose_matrix_gt, fps, display_ground_truth);

    }

    return 0;
}

