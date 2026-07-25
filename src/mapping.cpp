/*
 * Gaussian-LIC: Real-Time Photo-Realistic SLAM with Gaussian Splatting and LiDAR-Inertial-Camera Fusion
 * Copyright (C) 2025 Xiaolei Lang
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "mapping.h"
#include "gaussian.h"

#include <atomic>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <filesystem>

std::mutex m_buf;
std::condition_variable con;

std::queue<sensor_msgs::msg::PointCloud2::ConstSharedPtr> point_buf;
std::queue<geometry_msgs::msg::PoseStamped::ConstSharedPtr> pose_buf;
std::queue<sensor_msgs::msg::Image::ConstSharedPtr> image_buf;
std::queue<sensor_msgs::msg::Image::ConstSharedPtr> depth_buf;

std::atomic<bool> exit_flag(false);
std::atomic<double> last_point_time(0.0);
std::atomic<bool> gaussians_initialized(false);

/// ROS publishers for rendered images (declared globally)
using ImagePublisher = rclcpp::Publisher<sensor_msgs::msg::Image>;
ImagePublisher::SharedPtr rendered_rgb_pub = nullptr;
ImagePublisher::SharedPtr rendered_depth_pub = nullptr;

void pointCallback(
    const sensor_msgs::msg::PointCloud2::ConstSharedPtr point_msg)
{
    std::lock_guard<std::mutex> lock(m_buf);
    point_buf.push(point_msg);
    last_point_time = rclcpp::Clock(RCL_ROS_TIME).now().seconds();
    con.notify_one();
}

// Forward declaration of render function for current view
void renderAndPublishCurrentView(const std::shared_ptr<Dataset>& dataset,
                                 std::shared_ptr<GaussianModel>& pc,
                                 uint32_t frame_id);

void poseCallback(
    const geometry_msgs::msg::PoseStamped::ConstSharedPtr pose_msg)
{
    std::lock_guard<std::mutex> lock(m_buf);
    pose_buf.push(pose_msg);
    con.notify_one();
}

void imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr image_msg)
{
    std::lock_guard<std::mutex> lock(m_buf);
    image_buf.push(image_msg);
    con.notify_one();
}

void depthCallback(const sensor_msgs::msg::Image::ConstSharedPtr depth_msg)
{
    std::lock_guard<std::mutex> lock(m_buf);
    depth_buf.push(depth_msg);
    con.notify_one();
}

bool getAlignedData(Frame& cur_frame)
{
    if (point_buf.empty() || pose_buf.empty() || image_buf.empty() || depth_buf.empty()) 
    {
        return false;
    }

    double frame_time = rclcpp::Time(point_buf.front()->header.stamp).seconds();

    while (1) 
    {
        if (rclcpp::Time(pose_buf.front()->header.stamp).seconds() < frame_time - 0.01)
        {
            pose_buf.pop();
            if (pose_buf.empty()) 
            {
                return false;
            }
        } 
        else break;
    }
    if (rclcpp::Time(pose_buf.front()->header.stamp).seconds() > frame_time + 0.01)
    {
        point_buf.pop();
        return false;
    }

    while (1) 
    {
        if (rclcpp::Time(image_buf.front()->header.stamp).seconds() < frame_time - 0.01)
        {
            image_buf.pop();
            if (image_buf.empty()) 
            {
                return false;
            }
        } 
        else break;
    }
    if (rclcpp::Time(image_buf.front()->header.stamp).seconds() > frame_time + 0.01)
    {
        point_buf.pop();
        return false;
    }

    while (1) 
    {
        if (rclcpp::Time(depth_buf.front()->header.stamp).seconds() < frame_time - 0.01)
        {
            depth_buf.pop();
            if (depth_buf.empty()) 
            {
                return false;
            }
        } 
        else break;
    }
    if (rclcpp::Time(depth_buf.front()->header.stamp).seconds() > frame_time + 0.01)
    {
        point_buf.pop();
        return false;
    }

    auto cur_point = point_buf.front();
    auto cur_pose = pose_buf.front();
    auto cur_image = image_buf.front();
    auto cur_depth = depth_buf.front();

    cur_frame.point_msg = cur_point;
    cur_frame.pose_msg = cur_pose;
    cur_frame.image_msg = cur_image;
    cur_frame.depth_msg = cur_depth;

    point_buf.pop();
    pose_buf.pop();
    image_buf.pop();
    depth_buf.pop();

    return true;
}

void mapping(const YAML::Node& node, const std::string& result_path, const std::string& lpips_path)
{
    torch::jit::setGraphExecutorOptimize(false);

    Params prm(node);
    std::shared_ptr<GaussianModel> gaussians = std::make_shared<GaussianModel>(prm);
    std::shared_ptr<Dataset> dataset = std::make_shared<Dataset>(prm);

    std::chrono::steady_clock::time_point t_start, t_end;
    double total_mapping_time = 0;
    double total_adding_time = 0;
    double total_extending_time = 0;

    Frame cur_frame;
    while (!exit_flag)
    {
        /// [1] data alignment
        std::unique_lock<std::mutex> lock(m_buf);
        con.wait_for(lock, std::chrono::milliseconds(10), [] {
            return exit_flag ||
                   (!point_buf.empty() && !pose_buf.empty() &&
                    !image_buf.empty() && !depth_buf.empty());
        });
        bool align_flag = getAlignedData(cur_frame);
        lock.unlock();
        if (!align_flag) continue;
        
        /// [2] add every frame
        t_start = std::chrono::steady_clock::now();
        dataset->addFrame(cur_frame);
        torch::cuda::synchronize();
        t_end = std::chrono::steady_clock::now();
        if (dataset->is_keyframe_current_)
        {
            total_adding_time += std::chrono::duration_cast<std::chrono::duration<double>>(t_end - t_start).count();
            std::cout << "\033[1;33m     Cur Frame " << dataset->all_frame_num_ - 1 << ",\033[0m";
        }
        else continue;

        if (!gaussians->is_init_)
        {
            /// [3] initialize map
            gaussians->is_init_ = true;
            gaussians_initialized = true;
            gaussians->initialize(dataset);
            gaussians->trainingSetup();
        }
        else 
        {
            /// [4] extend map
            t_start = std::chrono::steady_clock::now();
            extend(dataset, gaussians);
            torch::cuda::synchronize();
            t_end = std::chrono::steady_clock::now();
            total_extending_time += std::chrono::duration_cast<std::chrono::duration<double>>(t_end - t_start).count();
        }

        /// [5] optimize map
        t_start = std::chrono::steady_clock::now();
        double updated_num = optimize(dataset, gaussians);
        torch::cuda::synchronize();
        t_end = std::chrono::steady_clock::now();
        total_mapping_time += std::chrono::duration_cast<std::chrono::duration<double>>(t_end - t_start).count();
        std::cout << std::fixed << std::setprecision(2) 
                  << "\033[1;36m Update " << updated_num / 10000 
                  << "w GS per Iter \033[0m" << std::endl;

        /// [6] publish current view rendering
        renderAndPublishCurrentView(dataset, gaussians, dataset->all_frame_num_ - 1);
    }

    if (dataset->all_frame_num_ == 0)
    {
        std::cout << "\nGaussian-LIC stopped before receiving a complete frame."
                  << std::endl;
        return;
    }

    /// [7] evaluation
    std::cout << "\n     🎉 Runtime Statistics 🎉\n";
    std::cout << std::fixed << std::setprecision(2) << "\n        [Total Mapping Time] " << total_mapping_time << "s" << std::endl;
    std::cout << std::fixed << std::setprecision(2) << "         1) Forward " << gaussians->t_forward_ << "s" << std::endl;
    std::cout << std::fixed << std::setprecision(2) << "         2) Backward " << gaussians->t_backward_ << "s" << std::endl;
    std::cout << std::fixed << std::setprecision(2) << "         3) Step " << gaussians->t_step_ << "s" << std::endl;
    std::cout << std::fixed << std::setprecision(2) << "         4) CPU2GPU " << gaussians->t_tocuda_ << "s" << std::endl;
    std::cout << std::fixed << std::setprecision(2) << "        [Total Adding Time] " << total_adding_time << "s" << std::endl;
    std::cout << std::fixed << std::setprecision(2) << "        [Total Extending Time] " << total_extending_time << "s" << std::endl;
    torch::NoGradGuard no_grad;
    evaluateVisualQuality(dataset, gaussians, result_path, lpips_path);
    gaussians->saveMap(result_path);

    std::cout << "\n\n😋 Gaussian-LIC Done!\n\n\n";
}

int main(int argc, char** argv)
{
    std::cout << "\n\n😋 Gaussian-LIC Ready!\n\n\n";
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("gaussianlic");
    rclcpp::on_shutdown([] {
        exit_flag = true;
        con.notify_all();
    });
    auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();
    auto sub_point = node->create_subscription<sensor_msgs::msg::PointCloud2>(
        "/points_for_gs", qos, pointCallback);
    auto sub_pose = node->create_subscription<geometry_msgs::msg::PoseStamped>(
        "/pose_for_gs", qos, poseCallback);
    auto image_sub = node->create_subscription<sensor_msgs::msg::Image>(
        "/image_for_gs", qos, imageCallback);
    auto depth_sub = node->create_subscription<sensor_msgs::msg::Image>(
        "/depth_for_gs", qos, depthCallback);

    /// Initialize rendered image publishers
    rendered_rgb_pub =
        node->create_publisher<sensor_msgs::msg::Image>("/render_rgb", 1);
    rendered_depth_pub =
        node->create_publisher<sensor_msgs::msg::Image>("/render_depth", 1);

    const std::string share_dir =
        ament_index_cpp::get_package_share_directory("gaussian_lic");
    std::string config_path = node->declare_parameter<std::string>(
        "config_path", share_dir + "/config/fastlivo2.yaml");
    YAML::Node config_node = YAML::LoadFile(config_path);
    std::string result_path = node->declare_parameter<std::string>(
        "result_path",
        (std::filesystem::current_path() / "result").string());
    std::string lpips_path = node->declare_parameter<std::string>(
        "lpips_path", share_dir + "/lpips");
    std::filesystem::create_directories(result_path);

    std::thread mapping_process(mapping, config_node, result_path, lpips_path);
    std::thread monitor_thread([](){
        while (!exit_flag) 
        {
            double now = rclcpp::Clock(RCL_ROS_TIME).now().seconds();
            if (last_point_time > 0.0 && (now - last_point_time > 5.0))
            {
                std::lock_guard<std::mutex> lock(m_buf);
                if (point_buf.empty()) {
                    exit_flag = true;
                    con.notify_all();
                }
            } 
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
    
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    while (rclcpp::ok() && !exit_flag)
    {
        executor.spin_some();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    executor.remove_node(node);

    mapping_process.join();
    monitor_thread.join();

    sub_point.reset();
    sub_pose.reset();
    image_sub.reset();
    depth_sub.reset();
    rendered_rgb_pub.reset();
    rendered_depth_pub.reset();
    node.reset();
    if (rclcpp::ok())
    {
        rclcpp::shutdown();
    }
    return 0;
}
