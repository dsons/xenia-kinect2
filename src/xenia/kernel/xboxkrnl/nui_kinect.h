/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_NUI_KINECT_H_
#define XENIA_KERNEL_XBOXKRNL_NUI_KINECT_H_

#include <cstdint>
#include <string>
#include <vector>

#include "xenia/base/platform.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

enum class KinectSensorKind { kNone, kKinect1, kKinect2 };

inline const char* KinectSensorName(KinectSensorKind kind) {
  switch (kind) {
    case KinectSensorKind::kKinect1:
      return "Kinect for Xbox 360";
    case KinectSensorKind::kKinect2:
      return "Kinect for Xbox One";
    default:
      return "No Kinect";
  }
}

// A Kinect sensor on the host standing in for the Xbox 360 Kinect: a Kinect
// for Xbox 360 / Kinect for Windows v1 (frames used as they are), or a Kinect
// for Xbox One / Kinect for Windows v2 (frames converted to Kinect 1 frames).
// Frames come from the kinect runtime next to the executable
// (tools/kinect_runtime).
class KinectCamera {
 public:
  static constexpr uint32_t kWidth = 640;
  static constexpr uint32_t kHeight = 480;

  struct Status {
    KinectSensorKind kind = KinectSensorKind::kNone;
    bool streaming = false;
    float frames_per_second = 0.0f;
    std::string message;
  };

  static KinectCamera* Get();

  // Opens a sensor and starts capturing, if that has not been tried yet.
  // mode: "auto" (a Kinect 1, else a Kinect 2), "kinect1" or "kinect2".
  // Returns whether a sensor is streaming.
  bool Start(const std::string& mode);

  // Closes the sensor, if any, and tries mode again.
  bool Restart(const std::string& mode);

  // Copies the newest frame (depth in millimetres, RGB24). Returns false if
  // there is no frame newer than *frame_number, which is updated.
  bool Read(uint16_t* depth_mm, uint8_t* rgb, uint32_t* frame_number);

  void Stop();

  // Kinect 2 view: sideways shift and virtual step back (see
  // nui_kinect2_convert.h). Takes effect on the next frame.
  void SetView(int pan, double zoom);

  Status GetStatus();

 private:
  KinectCamera() = default;
  ~KinectCamera();
  struct Impl;
  Impl* impl_ = nullptr;
};

// The microphone array: by default the Kinect 2's microphones, or any capture
// device, captured with WASAPI and resampled to the Xbox 360 Kinect's 16 kHz,
// four channels of 32-bit samples.
class KinectMicrophones {
 public:
  static constexpr uint32_t kChannels = 4;
  static constexpr uint32_t kSampleRate = 16000;

  static KinectMicrophones* Get();

  // Names of the active capture devices.
  static std::vector<std::string> ListDevices();

  // Starts capture from the first capture device whose name contains the
  // nui_mic_device cvar, if not started yet. Returns whether it captures.
  bool Start();

  // Stops and starts again (after nui_mic_device changes).
  bool Restart();

  // Fills count frames if that many are buffered; otherwise returns false.
  bool Read(int32_t (*out)[kChannels], uint32_t count);

  void Stop();

  // Name of the device being captured, or empty.
  std::string device_name();

  // Recent peak level, 0 to 1.
  float level();

 private:
  KinectMicrophones() = default;
  ~KinectMicrophones();
  struct Impl;
  Impl* impl_ = nullptr;
};

#if !XE_PLATFORM_WIN32
inline KinectCamera* KinectCamera::Get() {
  static KinectCamera camera;
  return &camera;
}
inline bool KinectCamera::Start(const std::string&) { return false; }
inline bool KinectCamera::Restart(const std::string&) { return false; }
inline bool KinectCamera::Read(uint16_t*, uint8_t*, uint32_t*) { return false; }
inline void KinectCamera::Stop() {}
inline void KinectCamera::SetView(int, double) {}
inline KinectCamera::Status KinectCamera::GetStatus() {
  Status status;
  status.message = "Kinect sensors are supported on Windows only.";
  return status;
}
inline KinectCamera::~KinectCamera() = default;
inline KinectMicrophones* KinectMicrophones::Get() {
  static KinectMicrophones microphones;
  return &microphones;
}
inline std::vector<std::string> KinectMicrophones::ListDevices() { return {}; }
inline bool KinectMicrophones::Start() { return false; }
inline bool KinectMicrophones::Restart() { return false; }
inline bool KinectMicrophones::Read(int32_t (*)[kChannels], uint32_t) {
  return false;
}
inline void KinectMicrophones::Stop() {}
inline std::string KinectMicrophones::device_name() { return {}; }
inline float KinectMicrophones::level() { return 0.0f; }
inline KinectMicrophones::~KinectMicrophones() = default;
#endif

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_NUI_KINECT_H_
