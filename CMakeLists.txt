cmake_minimum_required(VERSION 3.18)

# OpenCVConfig.cmake records the CUDA ABI used to build OpenCV and requires an
# exact toolkit match.  The installed OpenCV 4.7 tree was built with CUDA 11.7.
set(CUDAToolkit_ROOT "/usr/local/cuda-11.7" CACHE PATH
    "CUDA toolkit root (must match the CUDA version in OpenCVConfig.cmake)")
set(CUDA_TOOLKIT_ROOT_DIR "${CUDAToolkit_ROOT}" CACHE PATH
    "Legacy FindCUDA root used by the installed OpenCVConfig.cmake")
set(CMAKE_CUDA_COMPILER "${CUDAToolkit_ROOT}/bin/nvcc" CACHE FILEPATH
    "CUDA compiler")

project(gaussian_lic LANGUAGES CXX CUDA)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
# CUDA 11.7 predates native sm_89 code generation.  Keep compute_86 PTX in
# the binary so the NVIDIA driver JITs it for Ada (RTX 4090).
set(CMAKE_CUDA_ARCHITECTURES 86 CACHE STRING "CUDA architectures")
set(ENV{TORCH_CUDA_ARCH_LIST} "8.6+PTX")

set(OpenCV_DIR "/home/lyt/cpp_lib/opencv-4.7.0/build" CACHE PATH
    "Directory containing OpenCVConfig.cmake")
set(Torch_DIR "/home/lyt/cpp_lib/libtorch/share/cmake/Torch" CACHE PATH
    "Directory containing TorchConfig.cmake")
set(TENSORRT_ROOT "/home/lyt/cpp_lib/TensorRT-8.6.1.6" CACHE PATH
    "TensorRT installation root")
set(GLM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/libglm/usr"
    CACHE PATH "GLM installation prefix")
foreach(required_path OpenCV_DIR Torch_DIR TENSORRT_ROOT CUDAToolkit_ROOT GLM_ROOT)
  if(NOT EXISTS "${${required_path}}")
    message(FATAL_ERROR "${required_path} does not exist: ${${required_path}}")
  endif()
endforeach()

find_package(ament_cmake REQUIRED)
find_package(ament_index_cpp REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(pcl_conversions REQUIRED)
find_package(tf2 REQUIRED)
find_package(tf2_eigen REQUIRED)

find_package(OpenCV 4.7 REQUIRED)
find_package(Torch 2.0 REQUIRED)
find_package(CUDAToolkit 11.7 EXACT REQUIRED)
find_package(PCL REQUIRED COMPONENTS common io)
find_package(PkgConfig REQUIRED)
pkg_check_modules(YAML_CPP REQUIRED yaml-cpp>=0.5)
find_package(Python3 COMPONENTS Interpreter Development NumPy REQUIRED)
find_library(FFI_LIBRARY NAMES ffi REQUIRED)

find_path(TENSORRT_INCLUDE_DIR NvInfer.h
  HINTS "${TENSORRT_ROOT}/include" REQUIRED)
find_library(NVINFER_LIBRARY nvinfer
  HINTS "${TENSORRT_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(NVONNXPARSER_LIBRARY nvonnxparser
  HINTS "${TENSORRT_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)
find_library(NVINFER_PLUGIN_LIBRARY nvinfer_plugin
  HINTS "${TENSORRT_ROOT}/lib" REQUIRED NO_DEFAULT_PATH)

separate_arguments(TORCH_CXX_FLAGS_LIST NATIVE_COMMAND "${TORCH_CXX_FLAGS}")

add_executable(gs_mapping
  src/mapping.cpp
  src/gaussian.cpp
  src/tinyply.cpp
  src/depth_completer.cpp
  src/simple-knn/spatial.cu
  src/simple-knn/simple_knn.cu
  src/fused-ssim/ssim.cu
  src/rasterizer/renderer.cpp
  src/rasterizer/rasterizer.cpp
  src/rasterizer/rasterize_points.cu
  src/rasterizer/cuda_rasterizer/adam.cu
  src/rasterizer/cuda_rasterizer/forward.cu
  src/rasterizer/cuda_rasterizer/backward.cu
  src/rasterizer/cuda_rasterizer/rasterizer_impl.cu
)

target_include_directories(gs_mapping PRIVATE
  src
  ${OpenCV_INCLUDE_DIRS}
  ${PCL_INCLUDE_DIRS}
  ${YAML_CPP_INCLUDE_DIRS}
  ${TENSORRT_INCLUDE_DIR}
  ${GLM_ROOT}/include
  ${Python3_INCLUDE_DIRS}
)
target_compile_options(gs_mapping PRIVATE
  $<$<COMPILE_LANGUAGE:CXX>:-O3;-Wall;-Wno-sign-compare;-Wno-unused;-Wno-comment>
  $<$<COMPILE_LANGUAGE:CUDA>:-O3;--expt-relaxed-constexpr;--extended-lambda>
  ${TORCH_CXX_FLAGS_LIST}
)
set_target_properties(gs_mapping PROPERTIES
  CUDA_SEPARABLE_COMPILATION ON
  BUILD_RPATH "${TENSORRT_ROOT}/lib;${Torch_DIR}/../../../lib;${OpenCV_DIR}/lib;${CUDAToolkit_ROOT}/lib64"
  INSTALL_RPATH "${TENSORRT_ROOT}/lib;${Torch_DIR}/../../../lib;${OpenCV_DIR}/lib;${CUDAToolkit_ROOT}/lib64"
)

ament_target_dependencies(gs_mapping
  ament_index_cpp rclcpp sensor_msgs geometry_msgs
  pcl_conversions tf2 tf2_eigen
)
target_link_libraries(gs_mapping
  ${OpenCV_LIBS}
  ${TORCH_LIBRARIES}
  ${PCL_LIBRARIES}
  ${YAML_CPP_LIBRARIES}
  Python3::Python
  ${FFI_LIBRARY}
  CUDA::cudart
  ${NVINFER_LIBRARY}
  ${NVONNXPARSER_LIBRARY}
  ${NVINFER_PLUGIN_LIBRARY}
)

install(TARGETS gs_mapping DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY launch config DESTINATION share/${PROJECT_NAME})
install(FILES
  ckpt/spnet_512_640.engine
  ckpt/spnet_480_640.engine
  ckpt/Large_300.pth
  DESTINATION share/${PROJECT_NAME}/ckpt
)
install(DIRECTORY src/lpips DESTINATION share/${PROJECT_NAME})

ament_environment_hooks(
  "${CMAKE_CURRENT_SOURCE_DIR}/env/gaussian_lic_library_path.sh.in")
ament_package()
