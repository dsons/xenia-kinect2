cmake_minimum_required(VERSION 3.16)
project(xenia_kinect CXX)

set(FREENECT_ROOT "" CACHE PATH "libfreenect install prefix")
set(FREENECT2_ROOT "" CACHE PATH "libfreenect2 install prefix")
set(LIBUSB_INCLUDE_DIR "" CACHE PATH "Folder holding libusb.h")
set(LIBUSB_LIBRARY "" CACHE FILEPATH "libusb-1.0.lib")
foreach(var FREENECT_ROOT FREENECT2_ROOT LIBUSB_INCLUDE_DIR LIBUSB_LIBRARY)
  if(NOT ${var})
    message(FATAL_ERROR "Set ${var}")
  endif()
endforeach()

find_package(OpenMP REQUIRED)

add_library(xenia_kinect SHARED xenia_kinect.cpp)
set_target_properties(xenia_kinect PROPERTIES CXX_STANDARD 17)
target_include_directories(xenia_kinect PRIVATE
  ${FREENECT_ROOT}/include
  ${FREENECT2_ROOT}/include
  ${LIBUSB_INCLUDE_DIR}
)
target_link_libraries(xenia_kinect PRIVATE
  ${FREENECT_ROOT}/lib/freenect.lib
  ${FREENECT2_ROOT}/lib/freenect2.lib
  ${LIBUSB_LIBRARY}
  OpenMP::OpenMP_CXX
  ws2_32
)
install(TARGETS xenia_kinect RUNTIME DESTINATION bin)
