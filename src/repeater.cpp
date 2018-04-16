#include <stdio.h>
#include <string>
#include <opencv2/opencv.hpp>
#include <ros/ros.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/Image.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/distortion_models.h>
#include <image_transport/image_transport.h>
#include <boost/optional.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <camera_info_manager/camera_info_manager.h>

#define WIDTH_ID 3
#define HEIGHT_ID 4
#define FPS_ID 5

namespace arti {

/**
 * @brief       the camera ros warpper class
 */
class ZedCameraROS {
public:


	/**
	 * @brief      { function_description }
	 *
	 * @param[in]  resolution  The resolution
	 * @param[in]  frame_rate  The frame rate
	 */
    ZedCameraROS();

	/**
	 * @brief      Gets the camera information From Zed config.
	 *
	 * @param[in]  config_file         The configuration file
	 * @param[in]  resolution          The resolution
	 * @param[in]  left_cam_info_msg   The left camera information message
	 * @param[in]  right_cam_info_msg  The right camera information message
	 */
    void getZedCameraInfo(std::string config_file, int resolution, sensor_msgs::CameraInfo& left_info, sensor_msgs::CameraInfo& right_info);

	/**
	 * @brief      { publish camera info }
	 *
	 * @param[in]  pub_cam_info  The pub camera information
	 * @param[in]  cam_info_msg  The camera information message
	 * @param[in]  now           The now
	 */
	void publishCamInfo(const ros::Publisher& pub_cam_info, sensor_msgs::CameraInfo& cam_info_msg, ros::Time now) {
		cam_info_msg.header.stamp = now;
		pub_cam_info.publish(cam_info_msg);
	}

	/**
	 * @brief      { publish image }
	 *
	 * @param[in]  img           The image
	 * @param      img_pub       The image pub
	 * @param[in]  img_frame_id  The image frame identifier
	 * @param[in]  t             { parameter_description }
	 */
    void publishImage(cv::Mat img, image_transport::Publisher &img_pub, std::string img_frame_id, ros::Time t);

private:
	int device_id_, resolution_;
	double frame_rate_;
	bool show_image_, load_zed_config_;
	double width_, height_;
	std::string left_frame_id_, right_frame_id_;
	std::string config_file_location_;
    cv::Mat left_, right_;
    ros::Subscriber left_image_sub_, right_image_sub_;
    image_transport::Publisher left_image_pub, right_image_pub;
    ros::Publisher left_cam_info_pub, right_cam_info_pub;
    sensor_msgs::CameraInfo left_info, right_info;

    void leftImageCallback(const sensor_msgs::Image &msg);
    void rightImageCallback(const sensor_msgs::Image &msg);
    void process()
    {
        if (left_.empty() || right_.empty())
            return;
        ros::Time now = ros::Time::now();
        cv::Mat left_image = left_, right_image = right_;
        left_ = cv::Mat();
        right_ = cv::Mat();

        ROS_INFO_ONCE("Repeater: publishing");
        if (show_image_) {
            cv::imshow("left", left_image);
            cv::imshow("right", right_image);
        }

        if (left_image_pub.getNumSubscribers() > 0) {
            publishImage(left_image, left_image_pub, "left_frame", now);
        }
        if (right_image_pub.getNumSubscribers() > 0) {
            publishImage(right_image, right_image_pub, "right_frame", now);
        }
        if (left_cam_info_pub.getNumSubscribers() > 0) {
            publishCamInfo(left_cam_info_pub, left_info, now);
        }
        if (right_cam_info_pub.getNumSubscribers() > 0) {
            publishCamInfo(right_cam_info_pub, right_info, now);
        }
    }
};

ZedCameraROS::ZedCameraROS() {
    ros::NodeHandle nh;
    ros::NodeHandle private_nh("~");
    // get ros param
    private_nh.param("resolution", resolution_, 1);
    private_nh.param("frame_rate", frame_rate_, 30.0);
    private_nh.param("config_file_location", config_file_location_, std::string("/home/igor/ws/src/zed_cpu_ros/config/SN12880.conf"));
    private_nh.param("left_frame_id", left_frame_id_, std::string("left_camera"));
    private_nh.param("right_frame_id", right_frame_id_, std::string("right_camera"));
    private_nh.param("show_image", show_image_, false);
    private_nh.param("load_zed_config", load_zed_config_, true);
    private_nh.param("device_id", device_id_, 0);

    ROS_INFO("Try to initialize the camera");
    //StereoCamera zed(device_id_, resolution_, frame_rate_);
    ROS_INFO("Initialized the camera");

    // setup publisher stuff
    image_transport::ImageTransport it(nh);
    left_image_pub = it.advertise("left/image_raw", 1);
    right_image_pub = it.advertise("right/image_raw", 1);

    left_image_sub_ = nh.subscribe("left/image_throttle", 10, &ZedCameraROS::leftImageCallback, this);
    right_image_sub_ = nh.subscribe("right/image_throttle", 10, &ZedCameraROS::rightImageCallback, this);

    left_cam_info_pub = nh.advertise<sensor_msgs::CameraInfo>("left/camera_info", 1);
    right_cam_info_pub = nh.advertise<sensor_msgs::CameraInfo>("right/camera_info", 1);

    switch (resolution_) {
    case 0: width_ = 2208; height_ = 1242; break;
    case 1: width_ = 1720; height_ = 1080; break;
    case 2: width_ = 1280; height_ = 720; break;
    case 3: width_ = 672; height_ = 376; break;
    }

    ROS_INFO("Try load camera calibration files");
    if (load_zed_config_) {
        ROS_INFO("Loading from zed calibration files");
        // get camera info from zed
        try {
            getZedCameraInfo(config_file_location_, resolution_, left_info, right_info);
        }
        catch (std::runtime_error& e) {
            ROS_INFO("Can't load camera info");
            ROS_ERROR("%s", e.what());
            throw e;
        }
    } else {
        ROS_INFO("Loading from ROS calibration files");
        // get config from the left, right.yaml in config
        camera_info_manager::CameraInfoManager info_manager(nh);
        info_manager.setCameraName("zed/left");
        info_manager.loadCameraInfo( "package://zed_cpu_ros/config/left.yaml");
        left_info = info_manager.getCameraInfo();

        info_manager.setCameraName("zed/right");
        info_manager.loadCameraInfo( "package://zed_cpu_ros/config/right.yaml");
        right_info = info_manager.getCameraInfo();

        left_info.header.frame_id = left_frame_id_;
        right_info.header.frame_id = right_frame_id_;
    }

    // std::cout << left_info << std::endl;
    // std::cout << right_info << std::endl;

    ROS_INFO("Got camera calibration files");
    ros::spin();
}

void ZedCameraROS::getZedCameraInfo(std::string config_file, int resolution, sensor_msgs::CameraInfo &left_info, sensor_msgs::CameraInfo &right_info) {
    boost::property_tree::ptree pt;
    boost::property_tree::ini_parser::read_ini(config_file, pt);
    std::string left_str = "LEFT_CAM_";
    std::string right_str = "RIGHT_CAM_";
    std::string reso_str = "";

    switch (resolution) {
    case 0: reso_str = "2K"; break;
    case 1: reso_str = "FHD"; break;
    case 2: reso_str = "HD"; break;
    case 3: reso_str = "VGA"; break;
    }
    // left value
    double l_cx = pt.get<double>(left_str + reso_str + ".cx");
    double l_cy = pt.get<double>(left_str + reso_str + ".cy");
    double l_fx = pt.get<double>(left_str + reso_str + ".fx");
    double l_fy = pt.get<double>(left_str + reso_str + ".fy");
    double l_k1 = pt.get<double>(left_str + reso_str + ".k1");
    double l_k2 = pt.get<double>(left_str + reso_str + ".k2");
    // right value
    double r_cx = pt.get<double>(right_str + reso_str + ".cx");
    double r_cy = pt.get<double>(right_str + reso_str + ".cy");
    double r_fx = pt.get<double>(right_str + reso_str + ".fx");
    double r_fy = pt.get<double>(right_str + reso_str + ".fy");
    double r_k1 = pt.get<double>(right_str + reso_str + ".k1");
    double r_k2 = pt.get<double>(right_str + reso_str + ".k2");

    // get baseline and convert mm to m
    boost::optional<double> baselineCheck;
    double baseline = 0.0;
    // some config files have "Baseline" instead of "BaseLine", check accordingly...
    if (baselineCheck = pt.get_optional<double>("STEREO.BaseLine")) {
        baseline = pt.get<double>("STEREO.BaseLine") * 0.001;
    }
    else if (baselineCheck = pt.get_optional<double>("STEREO.Baseline")) {
        baseline = pt.get<double>("STEREO.Baseline") * 0.001;
    }
    else
        throw std::runtime_error("baseline parameter not found");

    // get Rx and Rz
    double rx = pt.get<double>("STEREO.RX_"+reso_str);
    double rz = pt.get<double>("STEREO.RZ_"+reso_str);
    double ry = pt.get<double>("STEREO.CV_"+reso_str);

    // assume zeros, maybe not right
    double p1 = 0, p2 = 0, k3 = 0;

    left_info.distortion_model = sensor_msgs::distortion_models::PLUMB_BOB;
    right_info.distortion_model = sensor_msgs::distortion_models::PLUMB_BOB;

    // distortion parameters
    // For "plumb_bob", the 5 parameters are: (k1, k2, t1, t2, k3).
    left_info.D.resize(5);
    left_info.D[0] = l_k1;
    left_info.D[1] = l_k2;
    left_info.D[2] = k3;
    left_info.D[3] = p1;
    left_info.D[4] = p2;

    right_info.D.resize(5);
    right_info.D[0] = r_k1;
    right_info.D[1] = r_k2;
    right_info.D[2] = k3;
    right_info.D[3] = p1;
    right_info.D[4] = p2;

    // Intrinsic camera matrix
    // 	[fx  0 cx]
    // K =  [ 0 fy cy]
    //	[ 0  0  1]
    left_info.K.fill(0.0);
    left_info.K[0] = l_fx;
    left_info.K[2] = l_cx;
    left_info.K[4] = l_fy;
    left_info.K[5] = l_cy;
    left_info.K[8] = 1.0;

    right_info.K.fill(0.0);
    right_info.K[0] = r_fx;
    right_info.K[2] = r_cx;
    right_info.K[4] = r_fy;
    right_info.K[5] = r_cy;
    right_info.K[8] = 1.0;

    // rectification matrix
    // Rl = R_rect, R_r = R * R_rect
    // since R is identity, Rl = Rr;
    left_info.R.fill(0.0);
    right_info.R.fill(0.0);

    cv::Mat rvec = (cv::Mat_<double>(3, 1) << 5 * rx, ry, rz);
    cv::Mat rmat(3, 3, CV_64F);
    cv::Rodrigues(rvec, rmat);
    int id = 0;
    cv::MatIterator_<double> it, end;
    //std::copy(rmat.begin<double>(), rmat.end<double>(), right_info.R.begin());
    for (it = rmat.begin<double>(); it != rmat.end<double>(); ++it, id++){
        //left_info.R[id] = *it;
        right_info.R[id] = *it;
    }
    left_info.R[0] = 1.0;
    left_info.R[4] = 1.0;
    left_info.R[8] = 1.0;
    /*right_info.R[0] = 1.0;
        right_info.R[4] = 1.0;
        right_info.R[8] = 1.0;*/


    // Projection/camera matrix
    //     [fx'  0  cx' Tx]
    // P = [ 0  fy' cy' Ty]
    //     [ 0   0   1   0]
    left_info.P.fill(0.0);
    left_info.P[0] = l_fx;
    left_info.P[2] = l_cx;
    left_info.P[5] = l_fy;
    left_info.P[6] = l_cy;
    left_info.P[10] = 1.0;

    right_info.P.fill(0.0);
    right_info.P[0] = r_fx;
    right_info.P[2] = r_cx;
    right_info.P[3] = (-1 * l_fx * baseline);
    right_info.P[5] = r_fy;
    right_info.P[6] = r_cy;
    right_info.P[10] = 1.0;

    left_info.width = right_info.width = width_;
    left_info.height = right_info.height = height_;

    left_info.header.frame_id = left_frame_id_;
    right_info.header.frame_id = right_frame_id_;
}

void ZedCameraROS::publishImage(cv::Mat img, image_transport::Publisher &img_pub, std::string img_frame_id, ros::Time t) {

    cv_bridge::CvImage cv_image;
    cv_image.image = img;
    cv_image.encoding = sensor_msgs::image_encodings::BGR8;
    cv_image.header.frame_id = img_frame_id;
    cv_image.header.stamp = t;
    img_pub.publish(cv_image.toImageMsg());
}

void ZedCameraROS::leftImageCallback(const sensor_msgs::Image &msg)
{
    ROS_INFO_ONCE("Repeater: Left image received");
    left_ = cv_bridge::toCvCopy(msg, "bgr8")->image;
    process();
}

void ZedCameraROS::rightImageCallback(const sensor_msgs::Image &msg)
{
    ROS_INFO_ONCE("Repeater: Right image received");
    right_ = cv_bridge::toCvCopy(msg, "bgr8")->image;
    process();
}

}


int main(int argc, char **argv) {
    try {
        ros::init(argc, argv, "repeater");
        arti::ZedCameraROS zed_ros;
        return EXIT_SUCCESS;
    }
    catch(std::runtime_error& e) {
        ros::shutdown();
        return EXIT_FAILURE;
    }
}