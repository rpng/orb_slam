/**
* This file is part of ORB-SLAM.
*
* Copyright (C) 2014 Raúl Mur-Artal <raulmur at unizar dot es> (University of Zaragoza)
* For more information see <http://webdiis.unizar.es/~raulmur/orbslam/>
*
* ORB-SLAM is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* ORB-SLAM is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with ORB-SLAM. If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <fstream>
#include <ros/ros.h>
#include <ros/package.h>
#include <boost/thread.hpp>

#include <opencv2/core/core.hpp>

#include "Tracking.h"
#include "FramePublisher.h"
#include "Map.h"
#include "MapPublisher.h"
#include "LocalMapping.h"
#include "LoopClosing.h"
#include "KeyFrameDatabase.h"
#include "ORBVocabulary.h"


#include "Converter.h"


using namespace std;


int main(int argc, char **argv)
{
    ros::init(argc, argv, "ORB_SLAM");
    ros::start();

    cout << endl << "ORB-SLAM Copyright (C) 2014 Raul Mur-Artal" << endl <<
            "This program comes with ABSOLUTELY NO WARRANTY;" << endl  <<
            "This is free software, and you are welcome to redistribute it" << endl <<
            "under certain conditions. See LICENSE.txt." << endl;

    if(argc != 3)
    {
        cerr << endl << "Usage: rosrun ORB_SLAM ORB_SLAM path_to_vocabulary path_to_settings (absolute or relative to package directory)" << endl;
        ros::shutdown();
        return 1;
    }

    // Load Settings and Check
    string strSettingsFile = ros::package::getPath("orb_slam")+"/"+argv[2];

    cv::FileStorage fsSettings(strSettingsFile.c_str(), cv::FileStorage::READ);
    if(!fsSettings.isOpened())
    {
        ROS_ERROR("Wrong path to settings. Path must be absolute or relative to ORB_SLAM package directory.");
        ros::shutdown();
        return 1;
    }

    //Create Frame Publisher for image_view
    ORB_SLAM::FramePublisher FramePub;

    //Load ORB Vocabulary
    string strVocFile = ros::package::getPath("orb_slam")+"/"+argv[1];
    cout << endl << "Loading ORB Vocabulary. This could take a while." << endl;
    cv::FileStorage fsVoc(strVocFile.c_str(), cv::FileStorage::READ);
    if(!fsVoc.isOpened())
    {
        cerr << endl << "Wrong path to vocabulary. Path must be absolute or relative to ORB_SLAM package directory." << endl;
        ros::shutdown();
        return 1;
    }
    ORB_SLAM::ORBVocabulary Vocabulary;
    Vocabulary.load(fsVoc);

    cout << "Vocabulary loaded!" << endl << endl;

    //Create KeyFrame Database
    ORB_SLAM::KeyFrameDatabase Database(Vocabulary);

    //Create the map database
    ORB_SLAM::MapDatabase WorldDB;

    FramePub.SetMapDB(&WorldDB);

    //Create Map Publisher for Rviz
    ORB_SLAM::MapPublisher MapPub(&WorldDB);

    //Initialize the Tracking Thread and launch
    ORB_SLAM::Tracking Tracker(&Vocabulary, &FramePub, &MapPub, &WorldDB, strSettingsFile);
    boost::thread trackingThread(&ORB_SLAM::Tracking::Run,&Tracker);

    Tracker.SetKeyFrameDatabase(&Database);

    //Initialize the Local Mapping Thread and launch
    ORB_SLAM::LocalMapping LocalMapper(&WorldDB);
    boost::thread localMappingThread(&ORB_SLAM::LocalMapping::Run,&LocalMapper);

    //Initialize the Loop Closing Thread and launch
    ORB_SLAM::LoopClosing LoopCloser(&WorldDB, &Database, &Vocabulary);
    boost::thread loopClosingThread(&ORB_SLAM::LoopClosing::Run, &LoopCloser);

    //Set pointers between threads
    Tracker.SetLocalMapper(&LocalMapper);
    Tracker.SetLoopClosing(&LoopCloser);

    LocalMapper.SetTracker(&Tracker);
    LocalMapper.SetLoopCloser(&LoopCloser);

    LoopCloser.SetTracker(&Tracker);
    LoopCloser.SetLocalMapper(&LocalMapper);

    //This "main" thread will show the current processed frame and publish the map
    float fps = fsSettings["Camera.fps"];
    if(fps==0)
        fps=30;

    ros::Rate r(fps);

    while (ros::ok())
    {
        FramePub.Refresh();
        MapPub.Refresh();
        Tracker.CheckResetByPublishers();
        r.sleep();
    }

    // Save keyframe poses at the end of the execution
    ofstream f;

    vector<ORB_SLAM::KeyFrame*> vpKFs = WorldDB.getLatestMap()->GetAllKeyFrames();
    sort(vpKFs.begin(),vpKFs.end(),ORB_SLAM::KeyFrame::lId);

    cout << endl << "Saving Keyframe Trajectory to KeyFrameTrajectory.txt" << endl;
    string strFile = ros::package::getPath("orb_slam")+"/"+"KeyFrameTrajectory.txt";
    f.open(strFile.c_str());
    f << fixed;

    for(size_t i=0; i<vpKFs.size(); i++)
    {
        ORB_SLAM::KeyFrame* pKF = vpKFs[i];

        if(pKF->isBad())
            continue;

        cv::Mat R = pKF->GetRotation().t();
        vector<float> q = ORB_SLAM::Converter::toQuaternion(R);
        cv::Mat t = pKF->GetCameraCenter();
        // Timestamp: t
        // Position: x, y, z
        // Quaternions: q0, q1, q2, q3
        f << setprecision(6) << pKF->mTimeStamp << setprecision(7) 
            << " " << t.at<float>(0) << " " << t.at<float>(1) << " " << t.at<float>(2)
            << " " << q[0] << " " << q[1] << " " << q[2] << " " << q[3] << endl;

    }
    f.close();

    ros::shutdown();

	return 0;
}
