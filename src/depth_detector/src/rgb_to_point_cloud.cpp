#include "../include/depth_detector/rgb_to_point_cloud.h"
#include <string>
#include <limits.h>
#include <unistd.h>
#include <depth_detector/rgb_to_point_cloud.h>


using namespace depth_detector;

RGBToPointCloud::RGBToPointCloud(const ros::NodeHandlePtr &nh, const std::string &rgb_topic,
                                 const std::string &camera_info_topic, const std::string &depth_topic,
                                 const std::string &pc_topic, const std::string &data_set_dir, int y_data_set_size,
                                 int x_data_set_size, int depth_data_set_size, const std::string &color_lower,
                                 const std::string &color_upper, const std::string &pc_frame) {
    RGBToPointCloud::initialize(nh, rgb_topic, camera_info_topic, depth_topic, pc_topic, data_set_dir, y_data_set_size,
                                x_data_set_size, depth_data_set_size, color_lower, color_upper, pc_frame);
}

void RGBToPointCloud::initialize(const ros::NodeHandlePtr &nh, const std::string &rgb_topic,
                                 const std::string &camera_info_topic, const std::string &depth_topic,
                                 const std::string &pc_topic, const std::string &data_set_dir, int y_data_set_size,
                                 int x_data_set_size, int depth_data_set_size, const std::string &color_lower,
                                 const std::string &color_upper, const std::string &pc_frame) {
    if (nh == nullptr) {
        RGBToPointCloud::nh = ros::NodeHandlePtr(new ros::NodeHandle);
    } else {
        RGBToPointCloud::nh = nh;
    }
    RGBToPointCloud::data_set_dir = data_set_dir;
    RGBToPointCloud::x_data_set_size = x_data_set_size;
    RGBToPointCloud::y_data_set_size = y_data_set_size;
    RGBToPointCloud::depth_data_set_size = depth_data_set_size;
    RGBToPointCloud::read_data_set();
    RGBToPointCloud::lower = new cv::Scalar;
    RGBToPointCloud::upper = new cv::Scalar;
    depth_detector::read_color(color_lower, *RGBToPointCloud::lower);
    depth_detector::read_color(color_upper, *RGBToPointCloud::upper);
    RGBToPointCloud::pc_frame = pc_frame;
    RGBToPointCloud::point_obj = sensor_msgs::PointCloud2Ptr(new sensor_msgs::PointCloud2);
    RGBToPointCloud::create_camera_model(camera_info_topic);
    RGBToPointCloud::pc_publisher = new ros::Publisher(
            RGBToPointCloud::nh->advertise<sensor_msgs::PointCloud2>(pc_topic, 1000));
    RGBToPointCloud::depth_publisher = new ros::Publisher(
            RGBToPointCloud::nh->advertise<sensor_msgs::Image>(depth_topic, 1000));
    RGBToPointCloud::rgb_sub = new ros::Subscriber(
            RGBToPointCloud::nh->subscribe(rgb_topic, 1000, &RGBToPointCloud::get_image, this));
}

void tmp(double distance, const cv::Rect &rect, const ros::NodeHandlePtr &nh) {
    sensor_msgs::ImageConstPtr msg = ros::topic::waitForMessage<sensor_msgs::Image>("/camera/depth/image_raw");
    cv_bridge::CvImageConstPtr img_ptr = cv_bridge::toCvCopy(msg, "32FC1");
    cv::Mat frame = img_ptr->image;
    std::ofstream out_file;
    out_file.open("/home/ashkan/fira_challenge/src/depth_detector/data/depth_data.aura", std::fstream::app);
    float depth = frame.at<float>(rect.x + (rect.width / 2), rect.y + (rect.height / 2));
    out_file << depth << " | " << distance << std::endl;
    out_file.close();
}

void RGBToPointCloud::get_image(const sensor_msgs::ImageConstPtr &img_msg) {
    cv_bridge::CvImageConstPtr img_ptr = cv_bridge::toCvCopy(img_msg, "bgr8");
    cv::UMat frame_hsv, frame_blur, frame_py_down, frame_py_up, frame_thresh, frame_filter, frame_edge;
    cv::Mat frame = img_ptr->image;
    cv::pyrDown(frame, frame_py_down);
    cv::pyrUp(frame_py_down, frame_py_up);
    cv::GaussianBlur(frame_py_up, frame_blur, cv::Size(5, 5), -1);
    cv::cvtColor(frame_blur, frame_hsv, cv::COLOR_BGR2HSV);
    cv::inRange(frame_hsv, *lower, *upper, frame_thresh);
    cv::bitwise_and(frame_blur, frame_blur, frame_filter, frame_thresh);
    cv::Canny(frame_filter, frame_edge, 100, 200);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(frame_edge, contours, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
    cv::UMat depth_frame(frame.rows, frame.cols, CV_32FC1, cv::Scalar(dNaN));
//    /* for TC */
//    double min = -1;
//    /* */
    for (int i = 0; i < contours.size(); i++) {
        std::vector<cv::Point> approx;
        double epsilon = 0.02 * cv::arcLength(contours[i], true);
        cv::approxPolyDP(contours[i], approx, epsilon, true);
        if (approx.size() == 4 && fabs(cv::contourArea(contours[i])) > 100) {
            double max_cos = 0;
            for (int j = 2; j < 5; j++) {
                double cos = fabs(RGBToPointCloud::angle(approx[j % 4], approx[j - 2], approx[j - 1]));
                max_cos = MAX(max_cos, cos);
            }
            cv::Rect rect = cv::boundingRect(approx);
            RGBDataSet rgb_src(0, 0, rect.width, rect.height), rgb_dst;
            if (depth_detector::closest_data(rgb_src, rgb_dst, RGBToPointCloud::y_data_set,
                                             RGBToPointCloud::y_data_set_size)) {
                depth_detector::DepthDataSet depth_src(fabs(rgb_dst.getObstacle_x() - rgb_dst.getRobot_pose_x()),
                                                       0);
                depth_detector::create_depth_image(depth_src, rect, depth_frame);
//                /* for TC */
//                if (min == -1) {
//                    min = depth_src.getDistance();
//                } else {
//                    if (min > depth_src.getDistance()) {
//                        min = depth_src.getDistance();
//                    }
//                }
//                /* */
            }

        }
    }
//    if (min != -1) {
//        ROS_WARN("Depth = %lf", min);
//    }
    cv_bridge::CvImageConstPtr image1(new cv_bridge::CvImage(img_msg->header, sensor_msgs::image_encodings::TYPE_32FC1,
                                                             depth_frame.getMat(cv::ACCESS_FAST)));
    sensor_msgs::ImagePtr msg = image1->toImageMsg();
    msg->header.frame_id = RGBToPointCloud::pc_frame;
    RGBToPointCloud::depth_publisher->publish(*msg);
    depth_detector::create_pc(msg, RGBToPointCloud::point_obj, RGBToPointCloud::camera_model);
    RGBToPointCloud::publish_pc(RGBToPointCloud::point_obj);
}

void
depth_detector::create_depth_image(const depth_detector::DepthDataSet &src, const cv::Rect &rect, cv::UMat &frame) {
    cv::Point top_left(rect.x, rect.y), bottom_left(rect.x + rect.width, rect.y + rect.height);
    cv::rectangle(frame, top_left, bottom_left, cv::Scalar(src.getDistance()), -1);
}

void depth_detector::create_pc(const sensor_msgs::ImageConstPtr &msg, sensor_msgs::PointCloud2Ptr &point_obj,
                               const image_geometry::PinholeCameraModel *camera_model, float range_max) {
    sensor_msgs::PointCloud2Modifier modifier(*point_obj);
    modifier.setPointCloud2Fields(3, "x", 1, sensor_msgs::PointField::FLOAT32,
                                  "y", 1, sensor_msgs::PointField::FLOAT32,
                                  "z", 1, sensor_msgs::PointField::FLOAT32);
    modifier.setPointCloud2FieldsByString(1, "xyz");
    modifier.resize(static_cast<size_t>(msg->width) * static_cast<size_t>(msg->height));
    point_obj->height = msg->height;
    point_obj->width = msg->width;
    point_obj->header.frame_id = msg->header.frame_id;
    point_obj->header.seq = msg->header.seq;
    point_obj->header.stamp = ros::Time::now();
    auto center_x = static_cast<float>(camera_model->cx());
    auto center_y = static_cast<float>(camera_model->cy());
    double unit_scaling = depth_image_proc::DepthTraits<float>::toMeters(float(1));
    auto constant_x = static_cast<float>(unit_scaling / camera_model->fx());
    auto constant_y = static_cast<float>(unit_scaling / camera_model->fy());
    float bad_point = std::numeric_limits<float>::quiet_NaN();
    sensor_msgs::PointCloud2Iterator<float> iter_x(*point_obj, "x");
    sensor_msgs::PointCloud2Iterator<float> iter_y(*point_obj, "y");
    sensor_msgs::PointCloud2Iterator<float> iter_z(*point_obj, "z");
    const auto *depth_row = reinterpret_cast<const float *>(&msg->data[0]);
    int row_step = msg->step / sizeof(float);
    for (int v = 0; v < (int) point_obj->height; ++v, depth_row += row_step) {
        for (int u = 0; u < (int) point_obj->width; ++u, ++iter_x, ++iter_y, ++iter_z) {
            float depth = depth_row[u];
            if (!depth_image_proc::DepthTraits<float>::valid(depth)) {
                if (range_max != 0.0) {
                    depth = depth_image_proc::DepthTraits<float>::fromMeters(range_max);
                } else {
                    *iter_x = *iter_y = *iter_z = bad_point;
                    continue;
                }
            }
            *iter_x = (u - center_x) * depth * constant_x;
            *iter_y = (v - center_y) * depth * constant_y;
            *iter_z = depth_image_proc::DepthTraits<float>::toMeters(depth);
        }
    }
}

void RGBToPointCloud::publish_pc(const sensor_msgs::PointCloud2ConstPtr &point_cloud2) {
    RGBToPointCloud::pc_publisher->publish(*point_cloud2);
}

int depth_detector::closest_data(const depth_detector::RGBDataSet &src, depth_detector::RGBDataSet &dst,
                                 depth_detector::RGBDataSet *data_set, int data_set_size) {
    depth_detector::RGBDataSet *end = data_set + data_set_size;
    depth_detector::RGBDataSet *current = data_set;
    depth_detector::RGBDataSet *best = nullptr;
    int min_difference = INT_MAX;
    int res = 0;
    while (current != end) {
        int current_difference = abs(current->getImage_height() - src.getImage_height());
        if (current_difference <= min_difference) {
            best = current;
            min_difference = current_difference;
            res = 1;
        }
        current++;
    }
    if (best == nullptr) {
        best = data_set;
        res = 0;
    }
    if (abs(best->getImage_height() - src.getImage_height()) > 2) {
        res = 0;
    }
    dst = *best;
    return res;
}

int depth_detector::closest_data(const depth_detector::DepthDataSet &src, depth_detector::DepthDataSet &dst,
                                 depth_detector::DepthDataSet *depth_data_set, int data_set_size) {
    depth_detector::DepthDataSet *end = depth_data_set + data_set_size;
    depth_detector::DepthDataSet *current = depth_data_set;
    depth_detector::DepthDataSet *best = nullptr;
    double min_difference = INT_MAX;
    int res = 0;
    while (current != end) {
        double current_difference = fabs(current->getDistance() - src.getDistance());
        if (current_difference <= min_difference) {
            best = current;
            min_difference = current_difference;
            res = 1;
        }
        current++;
    }
    if (best == nullptr || min_difference > 0.2) {
        best = depth_data_set;
        res = 0;
    }
    dst = *best;
    return res;
}

void RGBToPointCloud::spin() {
    ros::spin();
}

void RGBToPointCloud::read_data_set() {
    // Read y data set
    RGBToPointCloud::read_y_data_set();
    // Read x data set
    RGBToPointCloud::read_x_data_set();
    // Read depth data set
    RGBToPointCloud::read_depth_data();
}

void RGBToPointCloud::create_camera_model(const std::string &camera_info_topic) {
    RGBToPointCloud::camera_model = new image_geometry::PinholeCameraModel(image_geometry::PinholeCameraModel());
    if (!camera_info_topic.empty()) {
        RGBToPointCloud::camera_info = ros::topic::waitForMessage<sensor_msgs::CameraInfo>(camera_info_topic,
                                                                                           *RGBToPointCloud::nh);
        RGBToPointCloud::camera_model->fromCameraInfo(RGBToPointCloud::camera_info);
    } else {
        sensor_msgs::CameraInfo cam_info;
        RGBToPointCloud::read_camera_info(cam_info);
        RGBToPointCloud::camera_model->fromCameraInfo(cam_info);
        RGBToPointCloud::camera_info = sensor_msgs::CameraInfoConstPtr(&cam_info);
    }
}

void RGBToPointCloud::read_camera_info(sensor_msgs::CameraInfo &cam_info) {
    std::ifstream ifs;

    std::string path = get_exec_path() + "/" + RGBToPointCloud::data_set_dir + "/camera_info.aura";

    ifs.open(path, std::ios::in | std::ios::binary);
    if (ifs.fail()) {
        ROS_ERROR("Error while reading \"camera info\" in %s/camera_info.aura : %s", path.data(), strerror(errno));
        exit(0);
    }
    ifs.seekg(0, std::ios::end);
    std::streampos end = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::streampos begin = ifs.tellg();
    auto file_size = static_cast<uint32_t>(end - begin);
    boost::shared_array<uint8_t> ibuffer(new uint8_t[file_size]);
    ifs.read((char *) ibuffer.get(), file_size);
    ros::serialization::IStream istream(ibuffer.get(), file_size);
    ros::serialization::deserialize(istream, cam_info);
    ifs.close();
}

std::string RGBToPointCloud::get_exec_path() {
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    std::string path = std::string(result, (count > 0) ? count : 0);
    return path.substr(0, path.find_last_of('/'));
}

void RGBToPointCloud::read_y_data_set() {
    std::ifstream y_data_set_file;

    std::string path = get_exec_path() + "/" + RGBToPointCloud::data_set_dir + "/data_set_y.aura";

    y_data_set_file.open(path, std::ifstream::in);
    if (y_data_set_file.fail()) {
        ROS_ERROR("Error while reading \"y\" in %s/data_set_y.aura : %s", path.data(),
                  strerror(errno));
        exit(0);
    }
    RGBToPointCloud::y_data_set = (RGBDataSet *) malloc(RGBToPointCloud::y_data_set_size * sizeof(RGBDataSet));
    std::vector<RGBDataSet> data_set_vector;
    for (int i = 0; i < RGBToPointCloud::y_data_set_size; i++) {
        std::string line;
        std::getline(y_data_set_file, line);
        if (line[0] == '#') continue;
        data_set_vector.emplace_back(line);
    }
    std::copy(data_set_vector.begin(), data_set_vector.end(), RGBToPointCloud::y_data_set);
}

void RGBToPointCloud::read_x_data_set() {
    std::ifstream x_data_set_file;

    std::string path = get_exec_path() + "/" + RGBToPointCloud::data_set_dir + "/data_set_x.aura";

    x_data_set_file.open(path, std::ifstream::in);
    if (x_data_set_file.fail()) {
        ROS_ERROR("Error while reading \"x\" in %s/data_set_x.aura : %s", path.data(),
                  strerror(errno));
        exit(0);
    }
    RGBToPointCloud::x_data_set = (RGBDataSet *) malloc(RGBToPointCloud::x_data_set_size * sizeof(RGBDataSet));
    std::vector<RGBDataSet> data_set_vector;
    for (int i = 0; i < RGBToPointCloud::x_data_set_size; i++) {
        std::string line;
        std::getline(x_data_set_file, line);
        if (line[0] == '#') continue;
        data_set_vector.emplace_back(line);
    }
    std::copy(data_set_vector.begin(), data_set_vector.end(), RGBToPointCloud::x_data_set);
}

void RGBToPointCloud::read_depth_data() {
    std::ifstream depth_data_set_file;

    std::string path = get_exec_path() + "/" + RGBToPointCloud::data_set_dir + "/depth_data.aura";

    depth_data_set_file.open(path, std::ifstream::in);
    if (depth_data_set_file.fail()) {
        ROS_ERROR("Error while reading \"depth\" in %s/depth_data.aura %s", path.data(),
                  strerror(errno));
        exit(0);
    }
    RGBToPointCloud::depth_data_set = (DepthDataSet *) malloc(
            RGBToPointCloud::depth_data_set_size * sizeof(DepthDataSet));
    std::vector<DepthDataSet> depth_vector;
    for (int i = 0; i < RGBToPointCloud::depth_data_set_size; i++) {
        std::string line;
        std::getline(depth_data_set_file, line);
        if (line[0] == '#') continue;
        depth_vector.emplace_back(line);
    }
    std::copy(depth_vector.begin(), depth_vector.end(), RGBToPointCloud::depth_data_set);
}

RGBToPointCloud::RGBToPointCloud() = default;


void depth_detector::read_color(const std::string &color, cv::Scalar &scalar) {
    int *data = (int *) malloc(4 * sizeof(int));
    memset(data, 0, 4 * sizeof(int));
    auto *ss = new std::stringstream;
    int index = 0;
    for (const auto &c : color) {
        if (c == ',') {
            *(data + index) = std::stoi(ss->str());
            index++;
            ss = new std::stringstream;
            continue;
        }
        *(ss) << c;
    }
    *(data + index) = std::stoi(ss->str());
    for (int i = 0; i < 4; i++) {
        scalar[i] = *(data + i);
    }
}

void save_camera_info() {
    auto _msg = ros::topic::waitForMessage<sensor_msgs::CameraInfo>("/camera/rgb/camera_info");
    std::ofstream ofs("/home/ashkan/fira_challenge/src/depth_detector/data/camera_info.aura",
                      std::ios::out | std::ios::binary);
    uint32_t serial_size = ros::serialization::serializationLength(*_msg);
    boost::shared_array<uint8_t> obuffer(new uint8_t[serial_size]);
    ros::serialization::OStream ostream(obuffer.get(), serial_size);
    ros::serialization::serialize(ostream, *_msg);
    ofs.write((char *) obuffer.get(), serial_size);
    ofs.close();
}

int main(int argc, char **argv) {
    ros::init(argc, argv, "rgb_to_point_cloud");
    cv::ocl::setUseOpenCL(true);
    ros::NodeHandlePtr nh(new ros::NodeHandle);
    std::string rgb_topic = "/camera/rgb/image_raw";
    std::string camera_info_topic = "";
    std::string depth_image_topic = "depth_image";
    std::string point_cloud_topic = "point_cloud";
    std::string data_set_dir = "/home/ashkan/fira_challenge/src/depth_detector/data";
    std::string color_lower = "170,75,50";
    std::string color_upper = "180,255,255";
    std::string pc_frame = "camera_depth_optical_frame";
    int y_data_size = 410;
    int x_data_size = 229;
    int depth_data_size = 319;
    if (nh->hasParam(ros::this_node::getName() + "/rgb_topic")) {
        nh->getParam(ros::this_node::getName() + "/rgb_topic", rgb_topic);
    }
    if (nh->hasParam(ros::this_node::getName() + "/camera_info_topic")) {
        nh->getParam(ros::this_node::getName() + "/camera_info_topic", camera_info_topic);
    }
    if (nh->hasParam(ros::this_node::getName() + "/depth_image_topic")) {
        nh->getParam(ros::this_node::getName() + "/depth_image_topic", depth_image_topic);
    }
    if (nh->hasParam(ros::this_node::getName() + "/point_cloud_topic")) {
        nh->getParam(ros::this_node::getName() + "/point_cloud_topic", point_cloud_topic);
    }
    if (nh->hasParam(ros::this_node::getName() + "/data_set_dir")) {
        nh->getParam(ros::this_node::getName() + "/data_set_dir", data_set_dir);
    }
    if (nh->hasParam(ros::this_node::getName() + "/y_data_size")) {
        nh->getParam(ros::this_node::getName() + "/y_data_size", y_data_size);
    }
    if (nh->hasParam(ros::this_node::getName() + "/x_data_size")) {
        nh->getParam(ros::this_node::getName() + "/x_data_size", x_data_size);
    }
    if (nh->hasParam(ros::this_node::getName() + "/depth_data_size")) {
        nh->getParam(ros::this_node::getName() + "/depth_data_size", depth_data_size);
    }
    if (nh->hasParam(ros::this_node::getName() + "/color_lower")) {
        nh->getParam(ros::this_node::getName() + "/color_lower", color_lower);
    }
    if (nh->hasParam(ros::this_node::getName() + "/color_upper")) {
        nh->getParam(ros::this_node::getName() + "/color_upper", color_upper);
    }
    if (nh->hasParam(ros::this_node::getName() + "/pc_frame")) {
        nh->getParam(ros::this_node::getName() + "/pc_frame", pc_frame);
    }
    depth_detector::RGBToPointCloud rgb_to_pc(nh, rgb_topic, camera_info_topic, depth_image_topic, point_cloud_topic,
                                              data_set_dir, y_data_size, x_data_size, depth_data_size, color_lower,
                                              color_upper, pc_frame);
    rgb_to_pc.spin();
}