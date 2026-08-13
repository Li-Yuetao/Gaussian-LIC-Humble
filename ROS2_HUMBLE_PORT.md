# Gaussian-LIC ROS 2 Humble Migration

This checkout has been migrated from ROS 1/catkin to ROS 2 Humble and tested
with Coco-LIC on an NVIDIA RTX 4090.

## Main changes

- Build system: `catkin` → `ament_cmake`
- Node API and callbacks: ROS 1 → `rclcpp`
- TF conversions: `tf2_eigen`
- Launch files: XML → Python launch
- Package resources are resolved with `ament_index_cpp`
- ROS image conversion no longer depends on `cv_bridge`, avoiding a conflict
  between the ROS OpenCV build and the custom OpenCV 4.7 build
- Subscriber queues are bounded and use reliable QoS

Input topics:

| Topic | ROS 2 type |
|---|---|
| `/points_for_gs` | `sensor_msgs/msg/PointCloud2` |
| `/pose_for_gs` | `geometry_msgs/msg/PoseStamped` |
| `/image_for_gs` | `sensor_msgs/msg/Image` |
| `/depth_for_gs` | `sensor_msgs/msg/Image` |

Rendered output topics:

| Topic | Encoding |
|---|---|
| `/render_rgb` | `rgb8` |
| `/render_depth` | `32FC1` |

`/input_rgb_processed` publishes the exact resized RGB keyframe tensor used
as Gaussian mapping supervision. It shares the resolution, camera timestamp,
and frame ID of `/render_rgb`, making the two topics directly comparable in
RViz2.

## GPU dependencies

The CMake cache defaults reuse the libraries already installed on this
machine:

```text
OpenCV_DIR=/home/lyt/cpp_lib/opencv-4.7.0/build
Torch_DIR=/home/lyt/cpp_lib/libtorch/share/cmake/Torch
TENSORRT_ROOT=/home/lyt/cpp_lib/TensorRT-8.6.1.6
CUDAToolkit_ROOT=/usr/local/cuda-11.7
```

CUDA 11.7 is intentional: the installed OpenCV and LibTorch builds require
that version. The NVIDIA 580 driver is backward compatible with it.

The existing assets are reused:

- `ckpt/spnet_512_640.engine`
- `ckpt/spnet_480_640.engine`
- `ckpt/Large_300.pth`
- `src/lpips`

## Build

```bash
cd /home/lyt/workspace/lic_ws
source /opt/ros/humble/setup.bash

colcon build --packages-select gaussian_lic --symlink-install --cmake-args \
  -DOpenCV_DIR=/home/lyt/cpp_lib/opencv-4.7.0/build \
  -DTorch_DIR=/home/lyt/cpp_lib/libtorch/share/cmake/Torch \
  -DTENSORRT_ROOT=/home/lyt/cpp_lib/TensorRT-8.6.1.6 \
  -DCUDAToolkit_ROOT=/usr/local/cuda-11.7

source install/setup.bash
```

## Run with Coco-LIC

FAST-LIVO2 profile:

```bash
ros2 launch gaussian_lic cocolic_gaussian.launch.py \
  bag_path:=/absolute/path/to/rosbag2 \
  rviz:=true
```

SimLab custom scanner profile:

```bash
ros2 launch gaussian_lic simlab.launch.py \
  bag_path:=/absolute/path/to/rosbag2 \
  rviz:=true \
  result_path:=/home/lyt/workspace/lic_ws/result_simlab
```

Use `bag_durr:=10.0` for a short test. The default `bag_durr:=-1.0`
processes the complete bag.

## SimLab camera handling

The source camera is calibrated at 1920×1200. Gaussian-LIC resizes the RGB
image and sparse depth to 640×480 to reuse `spnet_480_640.engine`.
`config/simlab.yaml` contains the correspondingly scaled intrinsics:

```text
fx=481.0666666667
fy=577.2
cx=323.4666666667
cy=236.72
```

RGB is resized with area interpolation; sparse depth uses nearest-neighbor
interpolation to preserve valid depth samples.

## Runtime notes

- The joint launch starts Gaussian-LIC first, then Coco-LIC after two seconds.
- RViz2 loads the migrated `coco.rviz` configuration.
- A TensorRT warning about using an engine built on another GPU model may
  appear. The supplied engine has been verified to load and run on this RTX
  4090.
- Results are written under `result_path`, including `point_cloud.ply`,
  ground-truth images, RGB renders, and depth renders.
