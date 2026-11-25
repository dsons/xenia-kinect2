/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// PsCamDeviceRequest: the kernel side of the Kinect (NUI) camera.
//
// Kinect titles link the NUI runtime statically and talk to the camera through
// one kernel export, PsCamDeviceRequest(request). The request block was
// reverse-engineered from Kinect Adventures (4D5308ED, NUI 2.0.11775):
//
//   +0x00 be32 code
//   +0x10 be32 completion context      (async requests only)
//   +0x14 be32 completion routine      void (*)(request, status, bytes)
//   ... code specific fields ...
//
// A request either completes synchronously (the result is the return value
// and any output is written into the block) or returns STATUS_PENDING and
// later calls the completion routine.
//
// Codes seen in KA:
//   0x00 get state: +0x04 = state. 4 = present and idle (the runtime then
//        runs its start-up sequence), 2 = streaming, 0/1 = absent.
//   0x02 stop streaming.
//   0x05 read frame (async): +0x18 stream (2 colour, 4 depth), +0x1C buffer,
//        +0x20 buffer size, +0x28 pointer to frame info (+0 frame number,
//        +8 be64 capture time in ms: games time gestures by its delta).
//        Depth is 640x480 be16 millimetres (or 11-bit packed, 0x67200
//        bytes, which code 0x1E unpacks). Colour is 640x480 YUV 4:2:2.
//   0x0B legacy calibration (12 doubles, a scale double at +0x68, a 2048
//        entry shift->mm table at +0x70, an offset at +0x1074).
//   0x0D firmware info: +0x04 byte major; >= 5 selects code 0x0E.
//   0x0E calibration in the firmware's own register format.
//   0x0F colour exposure (async), 0x18 frame value->mm table (identity when
//   it fails), 0x19 colour settings, 0x1B start streams (+0x1C bit 0 depth,
//   bit 1 colour), 0x1E unpack an 11-bit depth frame, 0x21, 0x24 status.
//   0x23 colour camera settings (async, 0x5C bytes at +0x1C): the
//        completion reads the applied settings id from +0x18, and every
//        colour frame names the id it was taken with in the byte at +0x2C.
//        The runtime resubmits it from its completion, so it is completed
//        from the worker, never inside the request.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/nui_kinect.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DEFINE_string(nui_camera, "auto",
              "Kinect sensor: auto (a Kinect for Xbox 360 if one is connected, "
              "else a Kinect for Xbox One, else none), kinect1, kinect2, "
              "synthetic (a test figure, no sensor needed), none.",
              "NUI");
DEFINE_int32(nui_kinect2_pan, 0,
             "Kinect for Xbox One: slide the view sideways, in sensor pixels "
             "(-40 to 40, positive is the sensor's right).",
             "NUI");
DEFINE_double(nui_kinect2_zoom, 1.3,
              "Kinect for Xbox One: virtual step back (1.0 to 1.6). Games see "
              "the player this many times farther away than they stand, which "
              "helps in smaller rooms.",
              "NUI");
DEFINE_string(nui_mic_device, "Xbox NUI Sensor",
              "Microphone used for the Kinect microphone array (the first "
              "capture device whose name contains this text).",
              "NUI");
DEFINE_bool(nui_camera_mirror, false,
            "Mirror camera frames horizontally before the title sees them.",
            "NUI");
DEFINE_bool(nui_camera_log, false, "Log every PsCamDeviceRequest.", "NUI");

namespace xe {
namespace kernel {
namespace xboxkrnl {

namespace {

constexpr X_STATUS kStatusCancelled = 0xC0000120;
constexpr X_STATUS kStatusDeviceNotConnected = 0xC000009D;

constexpr uint32_t kWidth = 640;
constexpr uint32_t kHeight = 480;
constexpr uint32_t kPixels = kWidth * kHeight;

constexpr uint32_t kStreamColor = 2;
constexpr uint32_t kStreamDepth = 4;

constexpr uint32_t kStateIdle = 4;
constexpr uint32_t kStateStreaming = 2;
constexpr uint32_t kStateStopping = 5;

struct PendingRead {
  uint32_t request;
};

class NuiCamera {
 public:
  static NuiCamera* Get() {
    static NuiCamera camera;
    return &camera;
  }

  X_STATUS Request(uint32_t request_ptr);

 private:
  enum class Source { kNone, kSynthetic, kKinect };

  void EnsureStarted();
  void WorkerMain();
  void DeliverPending(uint32_t stream, std::deque<PendingRead>& queue);
  void CompleteRead(uint32_t request_ptr, X_STATUS status, uint32_t bytes,
                    cpu::ThreadState* thread_state);
  void CancelReads();
  bool UpdateFrames();
  void SynthesizeFrames();
  void FillLegacyCalibration(uint8_t* req);

  Memory* memory() { return kernel_state()->memory(); }

  std::mutex mutex_;
  // Guards one-time startup only; never held while guest code runs.
  std::mutex start_mutex_;
  std::deque<PendingRead> depth_reads_;
  std::deque<PendingRead> color_reads_;
  std::deque<PendingRead> settings_reads_;
  uint32_t settings_id_ = 0;
  uint32_t heartbeat_ = 0;
  std::atomic<uint32_t> state_{kStateIdle};
  std::atomic<bool> streaming_{false};
  bool started_ = false;
  Source source_ = Source::kNone;
  object_ref<XHostThread> worker_;

  // Latest frames, host order.
  std::vector<uint16_t> depth_mm_ = std::vector<uint16_t>(kPixels);
  std::vector<uint8_t> rgb_ = std::vector<uint8_t>(kPixels * 3);
  uint32_t depth_frame_ = 0;
  uint32_t color_frame_ = 0;
  uint32_t synthetic_tick_ = 0;
};

void DumpRequest(const char* what, uint32_t request_ptr, const uint8_t* req) {
  if (!cvars::nui_camera_log) {
    return;
  }
  std::string hex;
  for (int i = 0; i < 0x30; i += 4) {
    hex += fmt::format("{:08X} ", xe::load_and_swap<uint32_t>(req + i));
  }
  XELOGI("PsCam {} {:08X}: {}", what, request_ptr, hex);
}

void NuiCamera::EnsureStarted() {
  // Two guest threads open the camera at once (the NUI runtime's own thread
  // and the title's), so this must be serialised: without the lock both can
  // see started_ false, and the second worker overwrites worker_ while the
  // first one is still running. The orphaned worker then completes guest
  // reads on the other worker's ThreadState: two host threads on one guest
  // stack.
  std::lock_guard<std::mutex> start_lock(start_mutex_);
  if (started_) {
    return;
  }
  started_ = true;
  const std::string& mode = cvars::nui_camera;
  if (mode == "none") {
    source_ = Source::kNone;
  } else if (mode == "synthetic") {
    source_ = Source::kSynthetic;
  } else if (KinectCamera::Get()->Start(mode)) {
    source_ = Source::kKinect;
  } else {
    source_ = Source::kNone;
  }
  XELOGI("PsCam: camera source = {}",
         source_ == Source::kKinect
             ? KinectSensorName(KinectCamera::Get()->GetStatus().kind)
         : source_ == Source::kSynthetic ? "synthetic"
                                         : "none");
  if (source_ == Source::kNone) {
    return;
  }
  worker_ = object_ref<XHostThread>(new XHostThread(
      kernel_state(), 128 * 1024, 0,
      [this]() {
        WorkerMain();
        return 0;
      },
      kernel_state()->GetSystemProcess()));
  worker_->set_can_debugger_suspend(true);
  worker_->set_name("NUI Camera");
  worker_->Create();
}

X_STATUS NuiCamera::Request(uint32_t request_ptr) {
  EnsureStarted();
  auto req = memory()->TranslateVirtual<uint8_t*>(request_ptr);
  uint32_t code = xe::load_and_swap<uint32_t>(req);
  DumpRequest("request", request_ptr, req);
  if (source_ == Source::kNone) {
    return kStatusDeviceNotConnected;
  }
  switch (code) {
    case 0x00:
      xe::store_and_swap<uint32_t>(req + 4, state_.load());
      return X_STATUS_SUCCESS;
    case 0x02:
      streaming_ = false;
      CancelReads();
      state_ = kStateIdle;
      return X_STATUS_SUCCESS;
    case 0x05: {
      uint32_t stream = xe::load_and_swap<uint32_t>(req + 0x18);
      std::lock_guard<std::mutex> lock(mutex_);
      if (stream == kStreamDepth) {
        depth_reads_.push_back({request_ptr});
      } else if (stream == kStreamColor) {
        color_reads_.push_back({request_ptr});
      } else {
        XELOGW("PsCam: read from unknown stream {}", stream);
        return X_STATUS_INVALID_PARAMETER;
      }
      return X_STATUS_PENDING;
    }
    case 0x0B:
      FillLegacyCalibration(req);
      return X_STATUS_SUCCESS;
    case 0x0D:
      // Fail so the runtime takes the legacy (0x0B) calibration path, whose
      // fields are plain doubles and a shift->mm table.
      return X_STATUS_NOT_SUPPORTED;
    case 0x0F: {
      // Colour exposure. Complete it at once.
      uint32_t routine = xe::load_and_swap<uint32_t>(req + 0x14);
      if (routine) {
        uint64_t args[] = {request_ptr, X_STATUS_SUCCESS, 0};
        kernel_state()->processor()->Execute(
            XThread::GetCurrentThread()->thread_state(), routine, args,
            xe::countof(args));
      }
      return X_STATUS_SUCCESS;
    }
    case 0x18:
      // No raw->mm table: the runtime then uses an identity table, so depth
      // frames carry millimetres directly.
      return X_STATUS_NOT_SUPPORTED;
    case 0x19:
      std::memset(req + 4, 0, 0x38);
      return X_STATUS_SUCCESS;
    case 0x1B:
      streaming_ = true;
      state_ = kStateStreaming;
      return X_STATUS_SUCCESS;
    case 0x23: {
      std::lock_guard<std::mutex> lock(mutex_);
      settings_reads_.push_back({request_ptr});
      return X_STATUS_PENDING;
    }
    case 0x21:
    case 0x24:
      return X_STATUS_SUCCESS;
    default:
      XELOGW("PsCam: unhandled request code {:02X}", code);
      return X_STATUS_NOT_SUPPORTED;
  }
}

void NuiCamera::FillLegacyCalibration(uint8_t* req) {
  // Twelve registration polynomial coefficients (depth, then colour) and the
  // registration scale are zero: host depth frames are already registered
  // onto the colour picture, so depth pixel (x, y) is colour pixel (x, y).
  std::memset(req + 0x08, 0, 0x68);
  // Shift -> mm, the usual Kinect v1 fit; 0 where the shift is invalid.
  for (uint32_t i = 0; i < 0x800; ++i) {
    double denom = i * -0.0030711016 + 3.3309495161;
    double mm = denom > 0 ? 1000.0 / denom : 0;
    uint16_t v = (mm > 0 && mm < 0x2000) ? static_cast<uint16_t>(mm) : 0;
    xe::store_and_swap<uint16_t>(req + 0x70 + i * 2, v);
  }
  xe::store_and_swap<uint16_t>(req + 0x1074, 0);
}

void NuiCamera::CancelReads() {
  std::deque<PendingRead> reads;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    reads.insert(reads.end(), depth_reads_.begin(), depth_reads_.end());
    reads.insert(reads.end(), color_reads_.begin(), color_reads_.end());
    depth_reads_.clear();
    color_reads_.clear();
  }
  // While the cancelled completions run the device reads as stopping, so the
  // runtime's error path does not restart the streams it is stopping.
  state_ = kStateStopping;
  auto thread_state = XThread::GetCurrentThread()->thread_state();
  for (auto& read : reads) {
    CompleteRead(read.request, kStatusCancelled, 0, thread_state);
  }
}

void NuiCamera::CompleteRead(uint32_t request_ptr, X_STATUS status,
                             uint32_t bytes, cpu::ThreadState* thread_state) {
  auto req = memory()->TranslateVirtual<uint8_t*>(request_ptr);
  uint32_t routine = xe::load_and_swap<uint32_t>(req + 0x14);
  if (!routine) {
    return;
  }
  uint64_t args[] = {request_ptr, status, bytes};
  kernel_state()->processor()->Execute(thread_state, routine, args,
                                       xe::countof(args));
}

void NuiCamera::DeliverPending(uint32_t stream,
                               std::deque<PendingRead>& queue) {
  PendingRead read;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue.empty()) {
      return;
    }
    read = queue.front();
    queue.pop_front();
  }
  auto req = memory()->TranslateVirtual<uint8_t*>(read.request);
  uint32_t buffer = xe::load_and_swap<uint32_t>(req + 0x1C);
  uint32_t size = xe::load_and_swap<uint32_t>(req + 0x20);
  uint32_t info = xe::load_and_swap<uint32_t>(req + 0x28);
  auto out = memory()->TranslateVirtual<uint8_t*>(buffer);
  const bool mirror = cvars::nui_camera_mirror;
  uint32_t bytes = 0;
  uint32_t frame = 0;
  if (stream == kStreamDepth && size >= kPixels * 2) {
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t x = 0; x < kWidth; ++x) {
        uint32_t sx = mirror ? kWidth - 1 - x : x;
        uint16_t mm = depth_mm_[y * kWidth + sx];
        if (mm >= 0x2000) {
          mm = 0;
        }
        xe::store_and_swap<uint16_t>(out + (y * kWidth + x) * 2, mm);
      }
    }
    bytes = kPixels * 2;
    frame = depth_frame_;
  } else if (stream == kStreamColor && size >= kPixels * 2) {
    // YUV 4:2:2, UYVY byte order.
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t x = 0; x < kWidth; x += 2) {
        uint8_t yuv[2][3];
        for (int k = 0; k < 2; ++k) {
          uint32_t sx = mirror ? kWidth - 1 - (x + k) : x + k;
          const uint8_t* p = &rgb_[(y * kWidth + sx) * 3];
          float r = p[0], g = p[1], b = p[2];
          yuv[k][0] = static_cast<uint8_t>(std::clamp(
              0.257f * r + 0.504f * g + 0.098f * b + 16, 0.f, 255.f));
          yuv[k][1] = static_cast<uint8_t>(std::clamp(
              -0.148f * r - 0.291f * g + 0.439f * b + 128, 0.f, 255.f));
          yuv[k][2] = static_cast<uint8_t>(std::clamp(
              0.439f * r - 0.368f * g - 0.071f * b + 128, 0.f, 255.f));
        }
        uint8_t* o = out + (y * kWidth + x) * 2;
        o[0] = static_cast<uint8_t>((yuv[0][1] + yuv[1][1]) / 2);
        o[1] = yuv[0][0];
        o[2] = static_cast<uint8_t>((yuv[0][2] + yuv[1][2]) / 2);
        o[3] = yuv[1][0];
      }
    }
    bytes = kPixels * 2;
    frame = color_frame_;
  } else if (stream == kStreamColor && size >= kPixels) {
    // Raw Bayer, one byte a pixel, the Kinect 1 sensor's GRBG pattern
    // (Kinect Sports Season Two asks for this instead of UYVY).
    for (uint32_t y = 0; y < kHeight; ++y) {
      for (uint32_t x = 0; x < kWidth; ++x) {
        uint32_t sx = mirror ? kWidth - 1 - x : x;
        const uint8_t* p = &rgb_[(y * kWidth + sx) * 3];
        uint32_t c = (y & 1) ? ((x & 1) ? 1 : 2) : ((x & 1) ? 0 : 1);
        out[y * kWidth + x] = p[c];
      }
    }
    bytes = kPixels;
    frame = color_frame_;
  } else {
    XELOGW("PsCam: stream {} read of {:X} bytes is not a format we make",
           stream, size);
  }
  if (stream == kStreamColor) {
    req[0x2C] = static_cast<uint8_t>(settings_id_);
  }
  if (info) {
    auto info_ptr = memory()->TranslateVirtual<uint8_t*>(info);
    xe::store_and_swap<uint32_t>(info_ptr, frame);
    // +8: the capture time in milliseconds. The runtime copies it into the
    // image and skeleton frames' liTimeStamp, and games time gestures by
    // its delta (Kinect Sports Season Two skips every gesture update while
    // the delta is 0).
    xe::store_and_swap<uint64_t>(
        info_ptr + 8,
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count()));
  }
  // This thread's own ThreadState, not worker_'s: guest code must never run
  // on a ThreadState another host thread is using.
  CompleteRead(read.request,
               bytes ? X_STATUS_SUCCESS : X_STATUS_INVALID_PARAMETER, bytes,
               XThread::GetCurrentThread()->thread_state());
}

void NuiCamera::SynthesizeFrames() {
  // A person in a T-pose 2.2 m away in front of a wall 3.5 m away and a
  // floor, swaying a little so the tracker sees motion.
  float t = synthetic_tick_++ / 30.0f;
  float sway = std::sin(t * 1.5f) * 25.0f;
  const float fx = 525.0f;
  for (uint32_t y = 0; y < kHeight; ++y) {
    for (uint32_t x = 0; x < kWidth; ++x) {
      float mm = 3500.0f;
      // Floor 0.9 m below the camera: rows below the horizon hit it first.
      float dy = static_cast<float>(y) - 240.0f;
      if (dy > 1.0f) {
        mm = std::min(mm, 900.0f * fx / dy);
      }
      // Body in image coordinates, centred, 2.2 m away (1 m ~ 239 px).
      float px = static_cast<float>(x) - 320.0f - sway;
      float py = static_cast<float>(y);
      bool body = false;
      // Head.
      if ((px * px) / (24.0f * 24.0f) +
              ((py - 95.0f) * (py - 95.0f)) / (30.0f * 30.0f) <=
          1.0f) {
        body = true;
      }
      // Torso.
      if (std::abs(px) < 45.0f && py > 125.0f && py < 290.0f) {
        body = true;
      }
      // Arms out to the sides.
      if (std::abs(px) < 190.0f && py > 135.0f && py < 160.0f) {
        body = true;
      }
      // Legs.
      if (py >= 290.0f && py < 445.0f &&
          (std::abs(px - 22.0f) < 18.0f || std::abs(px + 22.0f) < 18.0f)) {
        body = true;
      }
      if (body) {
        mm = 2200.0f + std::abs(px) * 0.3f;
      }
      depth_mm_[y * kWidth + x] = static_cast<uint16_t>(mm);
      uint8_t* p = &rgb_[(y * kWidth + x) * 3];
      uint8_t shade =
          body ? 200 : static_cast<uint8_t>(60 + (y * 80) / kHeight);
      p[0] = shade;
      p[1] = body ? 150 : shade;
      p[2] = body ? 120 : shade;
    }
  }
  ++depth_frame_;
  ++color_frame_;
}

bool NuiCamera::UpdateFrames() {
  if (source_ == Source::kKinect) {
    if (!KinectCamera::Get()->Read(depth_mm_.data(), rgb_.data(),
                                   &depth_frame_)) {
      return false;
    }
    color_frame_ = depth_frame_;
    return true;
  }
  SynthesizeFrames();
  return true;
}

void NuiCamera::WorkerMain() {
  using clock = std::chrono::steady_clock;
  auto next = clock::now();
  while (true) {
    next += std::chrono::microseconds(33333);
    auto now = clock::now();
    if (next > now) {
      xe::threading::Sleep(
          std::chrono::duration_cast<std::chrono::milliseconds>(next - now));
    } else {
      next = now;
    }
    if (cvars::nui_camera_log && ++heartbeat_ % 150 == 0) {
      std::lock_guard<std::mutex> lock(mutex_);
      XELOGI("PsCam: worker alive, streaming {}, reads {} depth {} colour",
             streaming_.load(), depth_reads_.size(), color_reads_.size());
    }
    std::deque<PendingRead> settings;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      settings.swap(settings_reads_);
    }
    for (auto& read : settings) {
      auto req = memory()->TranslateVirtual<uint8_t*>(read.request);
      // Ids stay in 1..255: the runtime keeps them as a byte and starts
      // from id - 1.
      settings_id_ = settings_id_ % 255 + 1;
      xe::store_and_swap<uint32_t>(req + 0x18, settings_id_);
      if (cvars::nui_camera_log) {
        XELOGI("PsCam: colour settings {:08X} applied as id {}", read.request,
               settings_id_);
      }
      CompleteRead(read.request, X_STATUS_SUCCESS, 0,
                   XThread::GetCurrentThread()->thread_state());
      if (cvars::nui_camera_log) {
        XELOGI("PsCam: colour settings completion returned");
      }
    }
    if (!streaming_) {
      continue;
    }
    bool have_depth, have_color;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      have_depth = !depth_reads_.empty();
      have_color = !color_reads_.empty();
    }
    if (!have_depth && !have_color) {
      continue;
    }
    if (!UpdateFrames()) {
      continue;
    }
    DeliverPending(kStreamDepth, depth_reads_);
    DeliverPending(kStreamColor, color_reads_);
  }
}

}  // namespace

// Whether the emulated Kinect has a camera behind it (XamNuiGetDeviceStatus).
bool NuiCameraAvailable() {
  const std::string& mode = cvars::nui_camera;
  if (mode == "none") {
    return false;
  }
  if (mode == "synthetic") {
    return true;
  }
  return KinectCamera::Get()->Start(mode);
}

dword_result_t PsCamDeviceRequest_entry(dword_t request_ptr) {
  return NuiCamera::Get()->Request(request_ptr);
}
DECLARE_XBOXKRNL_EXPORT1(PsCamDeviceRequest, kNone, kImplemented);

// The microphone array and the tilt motor ("Detroit"). Kinect Adventures does
// not call these directly; XAM's NUI functions do.
dword_result_t McaDeviceRequest_entry(dword_t request_ptr) {
  XELOGD("McaDeviceRequest({:08X})", static_cast<uint32_t>(request_ptr));
  return X_STATUS_NOT_SUPPORTED;
}
DECLARE_XBOXKRNL_EXPORT1(McaDeviceRequest, kNone, kStub);

dword_result_t DetroitDeviceRequest_entry(dword_t request_ptr) {
  XELOGD("DetroitDeviceRequest({:08X})", static_cast<uint32_t>(request_ptr));
  return X_STATUS_NOT_SUPPORTED;
}
DECLARE_XBOXKRNL_EXPORT1(DetroitDeviceRequest, kNone, kStub);

dword_result_t EtxProducerUnregister_entry(dword_t producer) {
  return X_STATUS_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(EtxProducerUnregister, kNone, kStub);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(NuiCamera);
