# Pinned OpenCV, built as static libraries for the host and for the aarch64
# cross toolchain. There is no ARM64 sysroot of Ubuntu packages in this
# project, so the same source build is used everywhere.
#
# Modules are the ones calibration and a later stereo depth map need:
#   core, imgproc, imgcodecs, flann, features2d, calib3d, video, photo, ximgproc
# features2d and flann are required by calib3d. video is required by ximgproc
# (disparity WLS) and is also the optical-flow / tracking set. photo is
# denoise and inpaint. ximgproc is the disparity smoother and guided filter.

include(FetchContent)
include(ExternalProject)

set(DOGGY_OPENCV_PREFIX "${CMAKE_BINARY_DIR}/opencv-install")

FetchContent_Declare(
    opencv_src
    URL https://github.com/opencv/opencv/archive/refs/tags/4.10.0.tar.gz
    URL_HASH SHA256=b2171af5be6b26f7a06b1229948bbb2bdaa74fcf5cd097e0af6378fce50a6eb9
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    opencv_contrib_src
    URL https://github.com/opencv/opencv_contrib/archive/refs/tags/4.10.0.tar.gz
    URL_HASH SHA256=65597f8fb8dc2b876c1b45b928bbcc5f772ddbaf97539bf1b737623d0604cba1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(opencv_src)
if(NOT opencv_src_POPULATED)
    FetchContent_Populate(opencv_src)
endif()
FetchContent_GetProperties(opencv_contrib_src)
if(NOT opencv_contrib_src_POPULATED)
    FetchContent_Populate(opencv_contrib_src)
endif()

# ==============================================================================

# ================================================================================

function(doggy_opencv_root extracted out_var)
    if(EXISTS "${extracted}/CMakeLists.txt"
            OR EXISTS "${extracted}/modules/core/CMakeLists.txt"
            OR EXISTS "${extracted}/modules/ximgproc/CMakeLists.txt")
        set(${out_var} "${extracted}" PARENT_SCOPE)
        return()
    endif()
    file(GLOB _children LIST_DIRECTORIES true "${extracted}/*")
    foreach(_child IN LISTS _children)
        if(EXISTS "${_child}/CMakeLists.txt"
                OR EXISTS "${_child}/modules/core/CMakeLists.txt"
                OR EXISTS "${_child}/modules/ximgproc/CMakeLists.txt")
            set(${out_var} "${_child}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "OpenCV sources not found under ${extracted}")
endfunction()

doggy_opencv_root("${opencv_src_SOURCE_DIR}" DOGGY_OPENCV_SOURCE)
doggy_opencv_root("${opencv_contrib_src_SOURCE_DIR}" DOGGY_OPENCV_CONTRIB)

set(DOGGY_OPENCV_CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${DOGGY_OPENCV_PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_LIST=core,imgproc,imgcodecs,flann,features2d,calib3d,video,photo,ximgproc
    -DOPENCV_EXTRA_MODULES_PATH=${DOGGY_OPENCV_CONTRIB}/modules
    -DBUILD_TESTS=OFF
    -DBUILD_PERF_TESTS=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_opencv_apps=OFF
    -DBUILD_opencv_java=OFF
    -DBUILD_opencv_python3=OFF
    -DBUILD_opencv_python2=OFF
    -DBUILD_JAVA=OFF
    -DWITH_GTK=OFF
    -DWITH_QT=OFF
    -DWITH_FFMPEG=OFF
    -DWITH_GSTREAMER=OFF
    -DWITH_V4L=OFF
    -DWITH_IPP=OFF
    -DWITH_ITT=OFF
    -DBUILD_ITT=OFF
    -DWITH_EIGEN=OFF
    -DWITH_LAPACK=OFF
    -DWITH_OPENCL=OFF
    -DWITH_CUDA=OFF
    -DWITH_PROTOBUF=OFF
    -DBUILD_PROTOBUF=OFF
    -DWITH_PNG=OFF
    -DWITH_TIFF=OFF
    -DWITH_WEBP=OFF
    -DWITH_OPENJPEG=OFF
    -DWITH_JASPER=OFF
    -DWITH_OPENEXR=OFF
    -DWITH_JPEG=ON
    -DBUILD_JPEG=ON
    -DWITH_ZLIB=OFF
    -DBUILD_ZLIB=OFF
    -DWITH_CAROTENE=OFF
    -DWITH_1394=OFF
    -DWITH_OPENMP=OFF
    -DOPENCV_GENERATE_PKGCONFIG=OFF
    -DENABLE_PRECOMPILED_HEADERS=OFF
    -DBUILD_opencv_world=OFF
)
if(CMAKE_TOOLCHAIN_FILE)
    get_filename_component(_doggy_opencv_toolchain "${CMAKE_TOOLCHAIN_FILE}" ABSOLUTE)
    list(APPEND DOGGY_OPENCV_CMAKE_ARGS
        -DCMAKE_TOOLCHAIN_FILE=${_doggy_opencv_toolchain}
        -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
        -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
    )
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    list(APPEND DOGGY_OPENCV_CMAKE_ARGS
        -DCPU_BASELINE=NEON
        -DENABLE_NEON=ON
    )
endif()

set(DOGGY_OPENCV_THIRDPARTY "${DOGGY_OPENCV_PREFIX}/lib/opencv4/3rdparty")

ExternalProject_Add(opencv_build
    SOURCE_DIR "${DOGGY_OPENCV_SOURCE}"
    BINARY_DIR "${CMAKE_BINARY_DIR}/opencv-build"
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS ${DOGGY_OPENCV_CMAKE_ARGS}
    BUILD_COMMAND "${CMAKE_COMMAND}" --build "<BINARY_DIR>" --parallel
    BUILD_BYPRODUCTS
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_core.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_imgproc.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_imgcodecs.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_flann.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_features2d.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_calib3d.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_video.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_photo.a"
        "${DOGGY_OPENCV_PREFIX}/lib/libopencv_ximgproc.a"
        "${DOGGY_OPENCV_PREFIX}/include/opencv4/opencv2/core.hpp"
        "${DOGGY_OPENCV_THIRDPARTY}/liblibjpeg-turbo.a"
)

add_library(doggy_opencv INTERFACE)
add_dependencies(doggy_opencv opencv_build)
target_include_directories(doggy_opencv SYSTEM INTERFACE
    "${DOGGY_OPENCV_PREFIX}/include/opencv4")
target_link_libraries(doggy_opencv INTERFACE
    -Wl,--start-group
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_ximgproc.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_photo.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_video.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_calib3d.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_features2d.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_flann.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_imgcodecs.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_imgproc.a"
    "${DOGGY_OPENCV_PREFIX}/lib/libopencv_core.a"
    "${DOGGY_OPENCV_THIRDPARTY}/liblibjpeg-turbo.a"
    -Wl,--end-group
    pthread
    dl
    m
)
