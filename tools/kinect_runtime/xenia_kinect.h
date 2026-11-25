/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// xenia_kinect.dll: a small C API over libfreenect (Kinect for Xbox 360 and
// Kinect for Windows v1) and libfreenect2 (Kinect for Xbox One and Kinect for
// Windows v2). Xenia loads it at runtime from the kinect folder next to the
// executable, so the emulator itself has no build-time dependency on these
// libraries, libusb or libjpeg-turbo.

#ifndef XENIA_KINECT_H_
#define XENIA_KINECT_H_

#include <stdint.h>

#ifdef XENIA_KINECT_BUILD
#define XK_API extern "C" __declspec(dllexport)
#else
#define XK_API extern "C" __declspec(dllimport)
#endif

#define XK_API_VERSION 2

#define XK_SENSOR_KINECT1 1  // 640x480 frames, the Xbox 360 Kinect's own
#define XK_SENSOR_KINECT2 2  // 960x540 frames (half of 1920x1080)

// Pinhole of the Kinect 2's 960x540 frames (factory colour camera
// intrinsics, halved).
#define XK2_FOCAL_LENGTH 540.685f
#define XK2_CENTER_X 479.75f
#define XK2_CENTER_Y 269.75f

#define XK_OK 0
#define XK_TIMEOUT 1
#define XK_ERROR_NOT_OPEN -1
#define XK_ERROR_NO_DEVICE -2
#define XK_ERROR_OPEN_FAILED -3  // usually: another program has the sensor
#define XK_ERROR_START_FAILED -4
#define XK_ERROR_INVALID -5

typedef void (*xk_log_callback)(int level, const char* message);

// XK_API_VERSION of the DLL.
XK_API int xk_api_version(void);

// Receives the libraries' errors (level 1) and warnings (level 2), and while
// xk_open runs also their information (level 3: devices found, firmware). May
// be called on the libraries' threads.
XK_API void xk_set_log_callback(xk_log_callback callback);

// Opens the first sensor of the given kind (XK_SENSOR_*) and starts depth and
// colour streaming. Only one sensor is open at a time; opening closes the
// previous one. The sensor's light is on while it streams.
XK_API int xk_open(int sensor);

// Frame size of the open sensor. Returns XK_ERROR_NOT_OPEN if none is open.
XK_API int xk_frame_size(int* width, int* height);

// Waits up to timeout_ms for the next depth frame. rgb: width*height*3 bytes,
// RGB. depth: width*height values, millimetres, registered onto the colour
// picture (0 = no reading). Colour is the newest colour frame at that time.
// Frames are not mirrored.
XK_API int xk_read(uint8_t* rgb, uint16_t* depth, int timeout_ms);

// Stops streaming, turns the light off and releases the sensor.
XK_API void xk_close(void);

#endif  // XENIA_KINECT_H_
