#include "ros/ros.h"
#include "tf/transform_listener.h"
#include "sensor_msgs/PointCloud.h"
#include "tf/message_filter.h"
#include "message_filters/subscriber.h"
#include "laser_geometry/laser_geometry.h"
#include <pcl_ros/point_cloud.h>
#include <pcl_ros/transforms.h>
#include <pcl/io/pcd_io.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/PCLPointCloud2.h>
#include <pcl/filters/passthrough.h>
#include <pcl/point_types.h>
#include <pcl/common/common.h>
#include <sensor_msgs/CameraInfo.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud_conversion.h>
#include <sensor_msgs/image_encodings.h>
#include <image_transport/image_transport.h>
#include <image_transport/subscriber_filter.h>
#include <memory>
#include <chrono>
#include <sstream>
#include <mutex>
#include <image_geometry/pinhole_camera_model.h>
#include <sensor_msgs/image_encodings.h>
#include <std_msgs/String.h>
#include "../include/DepthTraits.h"
#include "../include/utils.h"
#include <atomic>
#include <thread>
#include <visualization_msgs/Marker.h>
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>
#include <cv_bridge/cv_bridge.h>
#include <opencv2/core/core.hpp>
#include <atomic>

// Services
#include <laser_assembler/AssembleScans2.h>
#include <vision/CropShelf.h>

#include <simtrack_nodes/UpdateValidationPointCloud.h>
#include <pr2_msgs/SetPeriodicCmd.h>
#include <pr2_msgs/SetLaserTrajCmd.h>

using namespace std;

namespace amazon_challenge {

class CropTool {

 public:
  CropTool() : m_nh() {

    m_depth_it.reset(new image_transport::ImageTransport(m_nh));
    m_sub_depth.subscribe(
        *m_depth_it,
        "/head_mount_kinect/depth_registered/sw_registered/image_rect_raw", 1,
        image_transport::TransportHints("compressedDepth"));
    m_rgb_it.reset(new image_transport::ImageTransport(m_nh));
    m_sub_rgb.subscribe(*m_rgb_it, "/head_mount_kinect/rgb/image_rect_color", 1,
                        image_transport::TransportHints("compressed"));
    m_sub_rgb_info.subscribe(m_nh, "/head_mount_kinect/rgb/camera_info", 1);

    m_sync_rgbd.reset(new SynchronizerRGBD(SyncPolicyRGBD(5), m_sub_depth,
                                           m_sub_rgb, m_sub_rgb_info));
    m_sync_rgbd->registerCallback(
        boost::bind(&CropTool::rgbdCallback, this, _1, _2, _3));

    m_cloud_publisher =
        m_nh.advertise<sensor_msgs::PointCloud2>("crop_label_cloud", 1);
    m_rgbd_publisher =
        m_nh.advertise<sensor_msgs::PointCloud2>("crop_rgbd_cloud", 1);
    m_shelf_A_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_A", 1);
    m_shelf_B_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_B", 1);
    m_shelf_C_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_C", 1);
    m_shelf_D_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_D", 1);
    m_shelf_E_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_E", 1);
    m_shelf_F_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_F", 1);
    m_shelf_G_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_G", 1);
    m_shelf_H_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_H", 1);
    m_shelf_I_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_I", 1);
    m_shelf_J_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_J", 1);
    m_shelf_K_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_K", 1);
    m_shelf_L_pub =
            m_nh.advertise<sensor_msgs::PointCloud2>("crop_bin_L", 1);

    m_service_server = m_nh.advertiseService(
        "crop_cloud", &CropTool::serviceCallback, this);

    m_front_crop = 0.03;
    m_back_crop = 0.04;
    m_top_crop = 0.04;
    m_bottom_crop = 0.01;
    m_left_crop = 0.03;
    m_right_crop = 0.03;


  };

  bool serviceCallback(vision::CropShelf::Request& req,
                       vision::CropShelf::Response& res)
  {
    m_crop_requested = true;

    res.result = true;
    return true;
  }

  void rgbdCallback(const sensor_msgs::ImageConstPtr& depth_msg,
                    const sensor_msgs::ImageConstPtr& rgb_msg,
                    const sensor_msgs::CameraInfoConstPtr& rgb_info_msg) {

    //ROS_INFO("Received full optional kinect data :)");

    image_geometry::PinholeCameraModel model;
    model.fromCameraInfo(rgb_info_msg);
    sensor_msgs::PointCloud2::Ptr cloud_msg(new sensor_msgs::PointCloud2);

    if (depth_msg->encoding == sensor_msgs::image_encodings::TYPE_16UC1) {
      depthToCloud<uint16_t>(depth_msg, model, *cloud_msg, 0.0);
    } else if (depth_msg->encoding ==
               sensor_msgs::image_encodings::TYPE_32FC1) {
      depthToCloud<float>(depth_msg, model, *cloud_msg, 0.0);
    } else {
      ROS_WARN("Warning: unsupported depth image format!");
      m_mutex.unlock();
      // ROS_INFO("Mutex unlocked by cloud message");
      return;
    }

    cloud_msg->header.frame_id = rgb_msg->header.frame_id;
    m_label_cloud.header.frame_id = rgb_msg->header.frame_id;

    pcl::PointCloud<pcl::PointXYZ> cloud;
    pcl::fromROSMsg(*cloud_msg, cloud);
    pcl::copyPointCloud(cloud, m_rgbd_cloud);

    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr =
          cv_bridge::toCvShare(rgb_msg, sensor_msgs::image_encodings::BGR8);
    }
    catch (cv_bridge::Exception& e) {
      ROS_ERROR("cv_bridge exception: %s", e.what());
      return;
    }

    auto w = m_rgbd_cloud.width;
    auto h = m_rgbd_cloud.height;

    const cv::Mat& img = cv_ptr->image;

    for (auto row = 0; row < h; ++row) {
      for (auto col = 0; col < w; ++col) {
        auto id = col + w * row;
        cv::Vec3b rgb = img.at<cv::Vec3b>(row, col);
        pcl::PointXYZRGB& p = m_rgbd_cloud.points[id];
        p.r = rgb.val[2];
        p.g = rgb.val[1];
        p.b = rgb.val[0];
      }
    }

    if(!m_crop_requested)
        return;

    ROS_INFO("Cropping...");

    debugCropping();
    m_crop_requested = false;

    ROS_INFO("Cropped");

  }

  void cropCloudBin(string bin_name, const pcl::PointCloud<pcl::PointXYZRGB>::Ptr& in,
                    pcl::PointCloud<pcl::PointXYZRGB>::Ptr& out) {
    tf::StampedTransform transform;
    if (!getTimedTransform(m_tf_listener, "base_footprint", bin_name, 2.0f,
                           transform)) {
      return;
    }
    Eigen::Vector3f size = getBinSize(bin_name);

    auto origin = transform.getOrigin();

    float min_x = origin.x() + m_front_crop;
    float max_x = origin.x() + size.x() - m_back_crop;
    float min_y = origin.y() + m_right_crop;
    float max_y = origin.y() + size.y() - m_left_crop;
    float min_z = origin.z() + m_bottom_crop;
    float max_z = origin.z() + size.z() - m_top_crop;

    cropCloud(in, min_x, max_x, min_y, max_y, min_z, max_z, out);
  }


  void debugBin(string bin_name, pcl::PointCloud<pcl::PointXYZRGB>& out_rgb,
                pcl::PointCloud<pcl::PointXYZRGB>& out_label)
  {
      auto cloud_ptr = m_rgbd_cloud.makeShared();
      auto out_ptr = out_rgb.makeShared();
      cropCloudBin(bin_name, cloud_ptr, out_ptr);
      Eigen::Vector3i color = getBinColor(bin_name);
      out_label = out_rgb;
      for(pcl::PointXYZRGB& p : out_label.points)
      {
          p.r = color.x();
          p.g = color.y();
          p.b = color.z();
      }

  }

  void debugCropping()
  {
    m_label_cloud.points.clear();
    pcl::PointCloud<pcl::PointXYZRGB> out;

    m_bin_A_cloud.points.clear();
    debugBin("shelf_bin_A", m_bin_A_cloud, out);
    m_label_cloud += out;

    m_bin_B_cloud.points.clear();
    debugBin("shelf_bin_B", m_bin_B_cloud, out);
    m_label_cloud += out;

    m_bin_C_cloud.points.clear();
    debugBin("shelf_bin_C", m_bin_C_cloud, out);
    m_label_cloud += out;

    m_bin_D_cloud.points.clear();
    debugBin("shelf_bin_D", m_bin_D_cloud, out);
    m_label_cloud += out;

    m_bin_E_cloud.points.clear();
    debugBin("shelf_bin_E", m_bin_E_cloud, out);
    m_label_cloud += out;

    m_bin_F_cloud.points.clear();
    debugBin("shelf_bin_F", m_bin_F_cloud, out);
    m_label_cloud += out;

    m_bin_G_cloud.points.clear();
    debugBin("shelf_bin_G", m_bin_G_cloud, out);
    m_label_cloud += out;

    m_bin_H_cloud.points.clear();
    debugBin("shelf_bin_H", m_bin_H_cloud, out);
    m_label_cloud += out;

    m_bin_I_cloud.points.clear();
    debugBin("shelf_bin_I", m_bin_I_cloud, out);
    m_label_cloud += out;

    m_bin_J_cloud.points.clear();
    debugBin("shelf_bin_J", m_bin_J_cloud, out);
    m_label_cloud += out;

    m_bin_K_cloud.points.clear();
    debugBin("shelf_bin_K", m_bin_K_cloud, out);
    m_label_cloud += out;

    m_bin_L_cloud.points.clear();
    debugBin("shelf_bin_L", m_bin_L_cloud, out);
    m_label_cloud += out;
  }

  void publishCloud() {
    sensor_msgs::PointCloud2 rgb,cloud, a,b,c,d,e,f,g,h,i,j,k,l;
    pcl::toROSMsg(m_label_cloud, cloud);
    m_cloud_publisher.publish(cloud);
    pcl::toROSMsg(m_rgbd_cloud, rgb);
    m_rgbd_publisher.publish(rgb);
    pcl::toROSMsg(m_bin_A_cloud, a);
    pcl::toROSMsg(m_bin_B_cloud, b);
    pcl::toROSMsg(m_bin_C_cloud, c);
    pcl::toROSMsg(m_bin_D_cloud, d);
    pcl::toROSMsg(m_bin_E_cloud, e);
    pcl::toROSMsg(m_bin_F_cloud, f);
    pcl::toROSMsg(m_bin_G_cloud, g);
    pcl::toROSMsg(m_bin_H_cloud, h);
    pcl::toROSMsg(m_bin_I_cloud, i);
    pcl::toROSMsg(m_bin_K_cloud, k);
    pcl::toROSMsg(m_bin_J_cloud, j);
    pcl::toROSMsg(m_bin_L_cloud, l);
    m_shelf_A_pub.publish(a);
    m_shelf_B_pub.publish(b);
    m_shelf_C_pub.publish(c);
    m_shelf_D_pub.publish(d);
    m_shelf_E_pub.publish(e);
    m_shelf_F_pub.publish(f);
    m_shelf_G_pub.publish(g);
    m_shelf_H_pub.publish(h);
    m_shelf_I_pub.publish(i);
    m_shelf_J_pub.publish(j);
    m_shelf_K_pub.publish(k);
    m_shelf_L_pub.publish(l);
  }


  float m_left_crop, m_right_crop, m_bottom_crop, m_top_crop, m_front_crop,
  m_back_crop;

private:

  ros::NodeHandle m_nh;
  boost::shared_ptr<image_transport::ImageTransport> m_rgb_it, m_depth_it;
  image_transport::SubscriberFilter m_sub_depth, m_sub_rgb;
  message_filters::Subscriber<sensor_msgs::CameraInfo> m_sub_rgb_info;
  typedef message_filters::sync_policies::ApproximateTime<
      sensor_msgs::Image, sensor_msgs::Image, sensor_msgs::CameraInfo>
      SyncPolicyRGBD;
  typedef message_filters::Synchronizer<SyncPolicyRGBD> SynchronizerRGBD;
  boost::shared_ptr<SynchronizerRGBD> m_sync_rgbd;

  std::mutex m_mutex;
  sensor_msgs::CameraInfo m_camera_info_msg;

  pcl::PointCloud<pcl::PointXYZRGB> m_bin_A_cloud, m_bin_B_cloud, m_bin_C_cloud,
  m_bin_D_cloud, m_bin_E_cloud,m_bin_F_cloud, m_bin_G_cloud, m_bin_H_cloud,
  m_bin_I_cloud, m_bin_J_cloud, m_bin_K_cloud, m_bin_L_cloud, m_rgbd_cloud, m_label_cloud;

  ros::ServiceServer m_service_server;

  ros::Publisher m_cloud_publisher, m_shelf_A_pub, m_shelf_B_pub,
  m_shelf_C_pub, m_shelf_D_pub, m_shelf_E_pub, m_shelf_F_pub, m_shelf_G_pub,
  m_shelf_H_pub, m_shelf_I_pub, m_shelf_J_pub, m_shelf_K_pub, m_shelf_L_pub,
  m_rgbd_publisher;

  tf::TransformListener m_tf_listener;

  atomic_bool m_crop_requested;
};

}  // end namespace

int main(int argc, char** argv) {

  ros::init(argc, argv, "segmentation_test_client");

  amazon_challenge::CropTool ct;

  ros::Rate r(100);
  while (ros::ok()) {

    ct.publishCloud();
    ros::spinOnce();
    r.sleep();
  }

  return 0;
}
