// IMX708 publisher for ROS 2 (C++).
// Captures via GStreamer/Argus, publishes sensor_msgs/Image on /image_raw.

#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include <opencv2/opencv.hpp>
#include <cv_bridge/cv_bridge.h>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

using namespace std::chrono_literals;

namespace imx708_camera_cpp
{

static std::string build_pipeline(int width, int height, int fps, int sensor_id)
{
  std::ostringstream oss;
  oss << "nvarguscamerasrc sensor-id=" << sensor_id << " "
      << "wbmode=5 awblock=true "
      << "exposuretimerange=\"8000000 16000000\" "
      << "gainrange=\"1 4\" ispdigitalgainrange=\"1 1\" "
      << "! video/x-raw(memory:NVMM),width=" << width
      << ",height=" << height
      << ",framerate=" << fps << "/1,format=NV12 "
      << "! nvvidconv ! video/x-raw,format=BGRx "
      << "! videoconvert ! video/x-raw,format=BGR "
      << "! appsink drop=true max-buffers=2 sync=false";
  return oss.str();
}

class IMX708Publisher : public rclcpp::Node
{
public:
  IMX708Publisher()
  : Node("imx708_publisher")
  {
    width_     = this->declare_parameter<int>("width", 1536);
    height_    = this->declare_parameter<int>("height", 864);
    fps_       = this->declare_parameter<int>("fps", 30);
    sensor_id_ = this->declare_parameter<int>("sensor_id", 0);
    frame_id_  = this->declare_parameter<std::string>("frame_id", "imx708_optical_frame");

    const auto pipeline = build_pipeline(width_, height_, fps_, sensor_id_);
    RCLCPP_INFO(this->get_logger(), "Opening pipeline: %s", pipeline.c_str());

    cap_.open(pipeline, cv::CAP_GSTREAMER);
    if (!cap_.isOpened()) {
      RCLCPP_FATAL(this->get_logger(), "Failed to open GStreamer pipeline");
      throw std::runtime_error("VideoCapture::open failed");
    }

    pub_ = this->create_publisher<sensor_msgs::msg::Image>("image_raw", 10);

    const auto period = std::chrono::milliseconds(1000 / fps_);
    timer_ = this->create_wall_timer(period, std::bind(&IMX708Publisher::tick, this));

    RCLCPP_INFO(this->get_logger(),
      "Publishing %dx%d @ %d fps on /image_raw", width_, height_, fps_);
  }

  ~IMX708Publisher() override
  {
    if (cap_.isOpened()) {
      cap_.release();
    }
  }

private:
  void tick()
  {
    cv::Mat frame;
    if (!cap_.read(frame) || frame.empty()) {
      RCLCPP_WARN(this->get_logger(), "Frame grab failed");
      return;
    }

    std_msgs::msg::Header hdr;
    hdr.stamp = this->now();
    hdr.frame_id = frame_id_;

    auto msg = cv_bridge::CvImage(hdr, "bgr8", frame).toImageMsg();
    pub_->publish(*msg);

    if (++frame_count_ % static_cast<uint64_t>(fps_) == 0) {
      RCLCPP_DEBUG(this->get_logger(), "Published %lu frames",
        static_cast<unsigned long>(frame_count_));
    }
  }

  int width_, height_, fps_, sensor_id_;
  std::string frame_id_;
  cv::VideoCapture cap_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  uint64_t frame_count_ {0};
};

}  // namespace imx708_camera_cpp

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<imx708_camera_cpp::IMX708Publisher>());
  rclcpp::shutdown();
  return 0;
}
