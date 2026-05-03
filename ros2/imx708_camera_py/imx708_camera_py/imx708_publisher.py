#!/usr/bin/env python3
"""IMX708 publisher for ROS 2 (Python).

Captures frames from the Arducam IMX708 via GStreamer/Argus and publishes
them as sensor_msgs/Image on /image_raw. Color correction belongs to
downstream nodes; the Argus ISP tuning for IMX708 is imperfect.
"""
import cv2
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Image
from cv_bridge import CvBridge


def build_pipeline(width, height, fps, sensor_id=0):
    return (
        f"nvarguscamerasrc sensor-id={sensor_id} "
        f"wbmode=5 awblock=true "
        f'exposuretimerange="8000000 16000000" '
        f'gainrange="1 4" ispdigitalgainrange="1 1" '
        f"! video/x-raw(memory:NVMM),width={width},height={height},"
        f"framerate={fps}/1,format=NV12 "
        f"! nvvidconv ! video/x-raw,format=BGRx "
        f"! videoconvert ! video/x-raw,format=BGR "
        f"! appsink drop=true max-buffers=2 sync=false"
    )


class IMX708Publisher(Node):
    def __init__(self):
        super().__init__("imx708_publisher")
        self.declare_parameter("width", 1536)
        self.declare_parameter("height", 864)
        self.declare_parameter("fps", 30)
        self.declare_parameter("sensor_id", 0)
        self.declare_parameter("frame_id", "imx708_optical_frame")

        w = self.get_parameter("width").value
        h = self.get_parameter("height").value
        fps = self.get_parameter("fps").value
        sid = self.get_parameter("sensor_id").value
        self.frame_id = self.get_parameter("frame_id").value

        pipeline = build_pipeline(w, h, fps, sid)
        self.get_logger().info(f"Opening pipeline: {pipeline}")

        self.cap = cv2.VideoCapture(pipeline, cv2.CAP_GSTREAMER)
        if not self.cap.isOpened():
            raise RuntimeError("Failed to open IMX708 GStreamer pipeline")

        self.bridge = CvBridge()
        self.pub = self.create_publisher(Image, "image_raw", 10)
        self.timer = self.create_timer(1.0 / float(fps), self.tick)
        self.get_logger().info(f"Publishing {w}x{h} @ {fps} fps on /image_raw")

    def tick(self):
        ok, frame = self.cap.read()
        if not ok or frame is None:
            self.get_logger().warn("Frame grab failed")
            return
        msg = self.bridge.cv2_to_imgmsg(frame, encoding="bgr8")
        msg.header.stamp = self.get_clock().now().to_msg()
        msg.header.frame_id = self.frame_id
        self.pub.publish(msg)

    def destroy_node(self):
        if self.cap.isOpened():
            self.cap.release()
        return super().destroy_node()


def main():
    rclpy.init()
    node = IMX708Publisher()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
