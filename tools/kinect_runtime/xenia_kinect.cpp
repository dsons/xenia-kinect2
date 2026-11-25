/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#define XENIA_KINECT_BUILD
#include "xenia_kinect.h"

// struct timeval, before libfreenect.h pulls in the older winsock.h.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>

#include <libfreenect/libfreenect.h>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/led_settings.h>
#include <libfreenect2/logger.h>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/libfreenect2.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<xk_log_callback> g_log_callback{nullptr};
// Set when libusb refused to open a sensor: another program holds it.
std::atomic<bool> g_access_denied{false};
// Set while a sensor is being closed: stopping the streams cancels transfers
// in flight, which the libraries report as failures.
std::atomic<bool> g_closing{false};

void Log(int level, const std::string& message) {
  if (message.find("LIBUSB_ERROR_ACCESS") != std::string::npos) {
    g_access_denied = true;
  }
  if (level > 2 || g_closing) {
    return;
  }
  if (auto callback = g_log_callback.load()) {
    callback(level, message.c_str());
  }
}

class Sensor {
 public:
  virtual ~Sensor() = default;
  virtual int Read(uint8_t* rgb, uint16_t* depth, int timeout_ms) = 0;
  int width = 0;
  int height = 0;
};

// Kinect for Xbox 360 / Kinect for Windows v1, through libfreenect.

class Kinect1Sensor final : public Sensor {
 public:
  ~Kinect1Sensor() override { Close(); }

  int Open() {
    width = 640;
    height = 480;
    if (freenect_init(&context_, nullptr) < 0) {
      return XK_ERROR_START_FAILED;
    }
    freenect_set_log_level(context_, FREENECT_LOG_WARNING);
    freenect_set_log_callback(context_, &Kinect1Sensor::OnLog);
    if (freenect_num_devices(context_) <= 0) {
      return XK_ERROR_NO_DEVICE;
    }
    // The motor (also the light) is a separate USB device; a Kinect for
    // Windows v1 or a sensor without it bound still opens as a camera.
    freenect_select_subdevices(
        context_, static_cast<freenect_device_flags>(FREENECT_DEVICE_MOTOR |
                                                     FREENECT_DEVICE_CAMERA));
    if (freenect_open_device(context_, &device_, 0) < 0) {
      freenect_select_subdevices(context_, FREENECT_DEVICE_CAMERA);
      if (freenect_open_device(context_, &device_, 0) < 0) {
        device_ = nullptr;
        return XK_ERROR_OPEN_FAILED;
      }
    }
    freenect_set_user(device_, this);
    const size_t pixels = size_t(width) * height;
    video_back_.resize(pixels * 3);
    depth_back_.resize(pixels);
    rgb_.resize(pixels * 3);
    depth_.resize(pixels);
    if (freenect_set_video_mode(
            device_, freenect_find_video_mode(FREENECT_RESOLUTION_MEDIUM,
                                              FREENECT_VIDEO_RGB)) < 0 ||
        freenect_set_depth_mode(
            device_, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM,
                                              FREENECT_DEPTH_REGISTERED)) < 0) {
      return XK_ERROR_START_FAILED;
    }
    freenect_set_video_buffer(device_, video_back_.data());
    freenect_set_depth_buffer(device_, depth_back_.data());
    freenect_set_video_callback(device_, &Kinect1Sensor::OnVideo);
    freenect_set_depth_callback(device_, &Kinect1Sensor::OnDepth);
    if (freenect_start_video(device_) < 0 ||
        freenect_start_depth(device_) < 0) {
      return XK_ERROR_START_FAILED;
    }
    freenect_set_led(device_, LED_GREEN);
    events_ = std::thread([this]() {
      while (!stop_) {
        timeval timeout = {0, 100000};
        if (freenect_process_events_timeout(context_, &timeout) < 0) {
          break;
        }
      }
    });
    return XK_OK;
  }

  int Read(uint8_t* rgb, uint16_t* depth, int timeout_ms) override {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!frame_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                            [this]() { return depth_frame_ != depth_seen_; })) {
      return XK_TIMEOUT;
    }
    depth_seen_ = depth_frame_;
    std::memcpy(depth, depth_.data(), depth_.size() * sizeof(uint16_t));
    std::memcpy(rgb, rgb_.data(), rgb_.size());
    return XK_OK;
  }

 private:
  static void OnLog(freenect_context*, freenect_loglevel level,
                    const char* message) {
    std::string text(message ? message : "");
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
      text.pop_back();
    }
    Log(level <= FREENECT_LOG_ERROR ? 1 : 2, text);
  }

  static void OnVideo(freenect_device* device, void* data, uint32_t) {
    auto self = static_cast<Kinect1Sensor*>(freenect_get_user(device));
    std::lock_guard<std::mutex> lock(self->mutex_);
    std::memcpy(self->rgb_.data(), data, self->rgb_.size());
  }

  static void OnDepth(freenect_device* device, void* data, uint32_t) {
    auto self = static_cast<Kinect1Sensor*>(freenect_get_user(device));
    {
      std::lock_guard<std::mutex> lock(self->mutex_);
      std::memcpy(self->depth_.data(), data,
                  self->depth_.size() * sizeof(uint16_t));
      ++self->depth_frame_;
    }
    self->frame_cv_.notify_all();
  }

  void Close() {
    stop_ = true;
    if (events_.joinable()) {
      events_.join();
    }
    if (device_) {
      freenect_stop_depth(device_);
      freenect_stop_video(device_);
      freenect_set_led(device_, LED_OFF);
      freenect_close_device(device_);
      device_ = nullptr;
    }
    if (context_) {
      freenect_shutdown(context_);
      context_ = nullptr;
    }
  }

  freenect_context* context_ = nullptr;
  freenect_device* device_ = nullptr;
  std::thread events_;
  std::atomic<bool> stop_{false};
  std::mutex mutex_;
  std::condition_variable frame_cv_;
  std::vector<uint8_t> video_back_;
  std::vector<uint16_t> depth_back_;
  std::vector<uint8_t> rgb_;
  std::vector<uint16_t> depth_;
  uint32_t depth_frame_ = 0;
  uint32_t depth_seen_ = 0;
};

// Kinect for Xbox One / Kinect for Windows v2, through libfreenect2.

constexpr int kColorWidth = 1920;
constexpr int kColorHeight = 1080;

// Colour exposure. Automatic exposure lengthens frames to 66 ms in a dim
// room, which halves the frame rate; instead the shutter is held at 30 ms and
// the analog gain is steered towards a target brightness.
constexpr float kShutterMs = 30.0f;
constexpr float kMinShutterMs = 2.0f;
constexpr float kMinGain = 1.0f;
constexpr float kMaxGain = 4.0f;
constexpr float kTargetLow = 90.0f;
constexpr float kTargetHigh = 150.0f;
constexpr int kExposureInterval = 15;

class Freenect2Logger : public libfreenect2::Logger {
 public:
  Freenect2Logger() { level_ = Warning; }
  void log(Level level, const std::string& message) override {
    // With Microsoft's WinUSB driver the IR interface cannot be switched off;
    // the patched libfreenect2 stops the sensor anyway. Not worth an error.
    if (message.find("failed to set ir interface state") != std::string::npos) {
      return;
    }
    Log(static_cast<int>(level), message);
  }
};

void SetLeds(libfreenect2::Freenect2Device* device, uint16_t level) {
  for (uint16_t id = 0; id < 2; ++id) {
    libfreenect2::LedSettings led = {};
    led.LedId = id;
    led.Mode = 0;  // constant
    led.StartLevel = level;
    led.StopLevel = level;
    led.IntervalInMs = 0;
    try {
      device->setLedStatus(led);
    } catch (...) {
      // Older firmware may not accept LED commands.
    }
  }
}

class Kinect2Sensor final : public Sensor {
 public:
  ~Kinect2Sensor() override {
    if (device_) {
      device_->stop();
      SetLeds(device_, 0);
      device_->close();
      delete device_;
    }
  }

  int Open() {
    width = 960;
    height = 540;
    if (context_.enumerateDevices() == 0) {
      return g_access_denied ? XK_ERROR_OPEN_FAILED : XK_ERROR_NO_DEVICE;
    }
    device_ = context_.openDevice(context_.getDefaultDeviceSerialNumber(),
                                  new libfreenect2::CpuPacketPipeline());
    if (!device_) {
      return XK_ERROR_OPEN_FAILED;
    }
    depth_listener_ = std::make_unique<libfreenect2::SyncMultiFrameListener>(
        libfreenect2::Frame::Depth);
    color_listener_ = std::make_unique<libfreenect2::SyncMultiFrameListener>(
        libfreenect2::Frame::Color);
    device_->setIrAndDepthFrameListener(depth_listener_.get());
    device_->setColorFrameListener(color_listener_.get());
    color_.format = libfreenect2::Frame::BGRX;
    // LED levels only take effect before start() or after stop().
    SetLeds(device_, 1000);
    if (!device_->start()) {
      device_->close();
      delete device_;
      device_ = nullptr;
      return XK_ERROR_START_FAILED;
    }
    device_->setColorManualExposure(shutter_ms_, gain_);
    registration_ = std::make_unique<libfreenect2::Registration>(
        device_->getIrCameraParams(), device_->getColorCameraParams());
    return XK_OK;
  }

  int Read(uint8_t* rgb, uint16_t* depth, int timeout_ms) override {
    libfreenect2::FrameMap frames;
    if (!depth_listener_->waitForNewFrame(frames, timeout_ms)) {
      return XK_TIMEOUT;
    }
    if (!UpdateColor(timeout_ms)) {
      depth_listener_->release(frames);
      return XK_TIMEOUT;
    }
    registration_->apply(&color_, frames[libfreenect2::Frame::Depth],
                         &undistorted_, &registered_, true, &big_depth_);
    depth_listener_->release(frames);
    Downsample(rgb, depth);
    if (++frames_ % kExposureInterval == 0) {
      SteerExposure(rgb);
    }
    return XK_OK;
  }

 private:
  // Takes the newest colour frame if one has arrived. Waits only for the
  // first.
  bool UpdateColor(int timeout_ms) {
    if (have_color_ && !color_listener_->hasNewFrame()) {
      return true;
    }
    libfreenect2::FrameMap frames;
    if (!color_listener_->waitForNewFrame(frames,
                                          have_color_ ? 0 : timeout_ms)) {
      return have_color_;
    }
    std::memcpy(color_.data, frames[libfreenect2::Frame::Color]->data,
                size_t(kColorWidth) * kColorHeight * 4);
    color_listener_->release(frames);
    have_color_ = true;
    return true;
  }

  // libfreenect2's images are mirrored; the output is not. Depth takes every
  // other registered pixel, colour the mean of each 2x2 block.
  void Downsample(uint8_t* rgb, uint16_t* depth) const {
    const uint8_t* bgrx = color_.data;
    const float* big = reinterpret_cast<const float*>(big_depth_.data) +
                       kColorWidth;  // skip the top padding row
#pragma omp parallel for num_threads(2) schedule(static)
    for (int y = 0; y < height; ++y) {
      const uint8_t* row0 = bgrx + size_t(2 * y) * kColorWidth * 4;
      const uint8_t* row1 = row0 + size_t(kColorWidth) * 4;
      const float* depth_row = big + size_t(2 * y) * kColorWidth;
      uint8_t* rgb_out = rgb + size_t(y) * width * 3;
      uint16_t* depth_out = depth + size_t(y) * width;
      for (int x = 0; x < width; ++x) {
        const int sx = kColorWidth - 2 - 2 * x;
        const uint8_t* a = row0 + sx * 4;
        const uint8_t* b = row1 + sx * 4;
        for (int c = 0; c < 3; ++c) {
          // BGRX in, RGB out.
          rgb_out[x * 3 + c] =
              uint8_t((a[2 - c] + a[6 - c] + b[2 - c] + b[6 - c] + 2) / 4);
        }
        const float mm = depth_row[kColorWidth - 1 - 2 * x];
        depth_out[x] = (mm > 0.0f && mm < 65535.0f) ? uint16_t(mm + 0.5f) : 0;
      }
    }
  }

  void SteerExposure(const uint8_t* rgb) {
    uint64_t sum = 0;
    uint32_t count = 0;
    const size_t pixels = size_t(width) * height;
    for (size_t i = 0; i < pixels; i += 61) {
      sum += rgb[i * 3] + rgb[i * 3 + 1] + rgb[i * 3 + 2];
      count += 3;
    }
    const float brightness = count ? float(sum) / count : 0.0f;
    float gain = gain_;
    float shutter = shutter_ms_;
    if (brightness < kTargetLow) {
      if (shutter < kShutterMs) {
        shutter = std::min(kShutterMs, shutter * 1.25f);
      } else {
        gain = std::min(kMaxGain, gain * 1.2f);
      }
    } else if (brightness > kTargetHigh) {
      if (gain > kMinGain) {
        gain = std::max(kMinGain, gain / 1.2f);
      } else {
        shutter = std::max(kMinShutterMs, shutter / 1.25f);
      }
    }
    if (gain != gain_ || shutter != shutter_ms_) {
      gain_ = gain;
      shutter_ms_ = shutter;
      device_->setColorManualExposure(shutter, gain);
    }
  }

  libfreenect2::Freenect2 context_;
  libfreenect2::Freenect2Device* device_ = nullptr;
  std::unique_ptr<libfreenect2::SyncMultiFrameListener> depth_listener_;
  std::unique_ptr<libfreenect2::SyncMultiFrameListener> color_listener_;
  std::unique_ptr<libfreenect2::Registration> registration_;
  libfreenect2::Frame color_{kColorWidth, kColorHeight, 4};
  libfreenect2::Frame undistorted_{512, 424, 4};
  libfreenect2::Frame registered_{512, 424, 4};
  // Depth registered onto the colour picture, with a padding row above and
  // below (libfreenect2's "bigdepth" layout).
  libfreenect2::Frame big_depth_{kColorWidth, kColorHeight + 2, 4};
  bool have_color_ = false;
  int frames_ = 0;
  float shutter_ms_ = kShutterMs;
  float gain_ = kMaxGain;
};

std::mutex g_mutex;
std::unique_ptr<Sensor> g_sensor;
bool g_logger_installed = false;

void CloseSensor() {
  g_closing = true;
  g_sensor.reset();
  g_closing = false;
}

void InstallLogger() {
  if (!g_logger_installed) {
    g_logger_installed = true;
    libfreenect2::setGlobalLogger(new Freenect2Logger());
  }
}

}  // namespace

XK_API int xk_api_version(void) { return XK_API_VERSION; }

XK_API void xk_set_log_callback(xk_log_callback callback) {
  std::lock_guard<std::mutex> lock(g_mutex);
  InstallLogger();
  g_log_callback = callback;
}

XK_API int xk_open(int sensor) {
  std::lock_guard<std::mutex> lock(g_mutex);
  CloseSensor();
  InstallLogger();
  g_access_denied = false;
  int result = XK_ERROR_INVALID;
  if (sensor == XK_SENSOR_KINECT1) {
    auto s = std::make_unique<Kinect1Sensor>();
    result = s->Open();
    if (result == XK_OK) {
      g_sensor = std::move(s);
    }
  } else if (sensor == XK_SENSOR_KINECT2) {
    auto s = std::make_unique<Kinect2Sensor>();
    result = s->Open();
    if (result == XK_OK) {
      g_sensor = std::move(s);
    }
  }
  if (result == XK_ERROR_NO_DEVICE && g_access_denied) {
    result = XK_ERROR_OPEN_FAILED;
  }
  return result;
}

XK_API int xk_frame_size(int* width, int* height) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_sensor) {
    return XK_ERROR_NOT_OPEN;
  }
  *width = g_sensor->width;
  *height = g_sensor->height;
  return XK_OK;
}

XK_API int xk_read(uint8_t* rgb, uint16_t* depth, int timeout_ms) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_sensor) {
    return XK_ERROR_NOT_OPEN;
  }
  return g_sensor->Read(rgb, depth, timeout_ms);
}

XK_API void xk_close(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  CloseSensor();
}
