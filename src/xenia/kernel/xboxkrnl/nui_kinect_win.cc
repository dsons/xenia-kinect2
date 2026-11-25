/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xboxkrnl/nui_kinect.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/string.h"
#include "xenia/kernel/xboxkrnl/nui_kinect2_convert.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
// ksmedia.h needs mmreg.h first.
#include <ksmedia.h>

#pragma comment(lib, "ole32.lib")

DECLARE_int32(nui_kinect2_pan);
DECLARE_double(nui_kinect2_zoom);
DECLARE_string(nui_mic_device);

namespace xe {
namespace kernel {
namespace xboxkrnl {

namespace {

// tools/kinect_runtime/xenia_kinect.h
constexpr int kRuntimeApiVersion = 2;
constexpr int kRuntimeKinect1 = 1;
constexpr int kRuntimeKinect2 = 2;
constexpr int kRuntimeOk = 0;
constexpr int kRuntimeTimeout = 1;
constexpr int kRuntimeNoDevice = -2;
constexpr int kRuntimeOpenFailed = -3;
using LogCallback = void (*)(int level, const char* message);
using ApiVersionFn = int (*)();
using SetLogCallbackFn = void (*)(LogCallback);
using OpenFn = int (*)(int sensor);
using FrameSizeFn = int (*)(int* width, int* height);
using ReadFn = int (*)(uint8_t* rgb, uint16_t* depth, int timeout_ms);
using CloseFn = void (*)();

void RuntimeLog(int level, const char* message) {
  std::string text(message ? message : "");
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.pop_back();
  }
  if (level <= 1) {
    XELOGE("Kinect: {}", text);
  } else {
    XELOGW("Kinect: {}", text);
  }
}

// Xenia leaves with std::quick_exit, without tearing the emulator down. Stop
// the sensor and the microphones first: the sensor's lights then turn off,
// and no capture thread is left running while the process exits.
void StopKinectDevices() {
  KinectCamera::Get()->Stop();
  KinectMicrophones::Get()->Stop();
}

void RegisterExitHandlers() {
  static std::once_flag once;
  std::call_once(once, []() {
    std::at_quick_exit(StopKinectDevices);
    std::atexit(StopKinectDevices);
  });
}

}  // namespace

struct KinectCamera::Impl {
  HMODULE module = nullptr;
  bool load_tried = false;
  OpenFn open = nullptr;
  FrameSizeFn frame_size = nullptr;
  ReadFn read = nullptr;
  CloseFn close = nullptr;

  std::mutex start_mutex;
  bool started = false;
  std::atomic<bool> streaming{false};
  std::thread thread;
  std::atomic<bool> stop{false};

  std::atomic<int> pan{0};
  std::atomic<double> zoom{1.3};

  std::mutex frame_mutex;
  std::vector<uint16_t> depth = std::vector<uint16_t>(kWidth * kHeight);
  std::vector<uint8_t> rgb = std::vector<uint8_t>(kWidth * kHeight * 3);
  uint32_t frame = 0;

  std::mutex status_mutex;
  KinectSensorKind kind = KinectSensorKind::kNone;
  std::string message = "Not started.";
  std::atomic<float> fps{0.0f};

  void SetMessage(std::string text) {
    std::lock_guard<std::mutex> lock(status_mutex);
    message = std::move(text);
  }

  bool Load() {
    if (load_tried) {
      return module != nullptr;
    }
    load_tried = true;
    auto path =
        xe::filesystem::GetExecutableFolder() / "kinect" / "xenia_kinect.dll";
    module = LoadLibraryExW(
        path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!module) {
      XELOGW("Kinect: runtime not found at {} (error {})",
             xe::path_to_utf8(path), GetLastError());
      SetMessage("The kinect folder next to xenia_canary.exe is missing.");
      return false;
    }
    auto api_version = reinterpret_cast<ApiVersionFn>(
        GetProcAddress(module, "xk_api_version"));
    auto set_log = reinterpret_cast<SetLogCallbackFn>(
        GetProcAddress(module, "xk_set_log_callback"));
    open = reinterpret_cast<OpenFn>(GetProcAddress(module, "xk_open"));
    frame_size =
        reinterpret_cast<FrameSizeFn>(GetProcAddress(module, "xk_frame_size"));
    read = reinterpret_cast<ReadFn>(GetProcAddress(module, "xk_read"));
    close = reinterpret_cast<CloseFn>(GetProcAddress(module, "xk_close"));
    if (!api_version || !set_log || !open || !frame_size || !read || !close ||
        api_version() != kRuntimeApiVersion) {
      XELOGE("Kinect: {} is not a compatible runtime", xe::path_to_utf8(path));
      SetMessage("The kinect folder does not match this version of Xenia.");
      FreeLibrary(module);
      module = nullptr;
      return false;
    }
    set_log(RuntimeLog);
    return true;
  }

  // Opens the sensor kinds in order; returns the one that opened.
  KinectSensorKind Open(const std::vector<KinectSensorKind>& kinds) {
    bool in_use = false;
    for (auto k : kinds) {
      const int result = open(
          k == KinectSensorKind::kKinect1 ? kRuntimeKinect1 : kRuntimeKinect2);
      if (result == kRuntimeOk) {
        return k;
      }
      XELOGI("Kinect: {}: {}", KinectSensorName(k),
             result == kRuntimeNoDevice     ? "not found"
             : result == kRuntimeOpenFailed ? "in use or not accessible"
                                            : "did not start");
      in_use |= result == kRuntimeOpenFailed;
    }
    SetMessage(
        in_use ? "The Kinect is in use by another program. Close it and "
                 "press Retry."
               : "No Kinect found. Check that it is plugged in and powered, "
                 "and that the driver is installed (Install driver).");
    return KinectSensorKind::kNone;
  }

  void Run() {
    int width = 0, height = 0;
    frame_size(&width, &height);
    const bool convert = kind == KinectSensorKind::kKinect2;
    Kinect2ToKinect1 converter;
    int configured_pan = pan;
    double configured_zoom = zoom;
    converter.Configure(configured_pan, configured_zoom);
    std::vector<uint8_t> rgb_in(size_t(width) * height * 3);
    std::vector<uint16_t> depth_in(size_t(width) * height);
    std::vector<uint8_t> rgb_out(kWidth * kHeight * 3);
    std::vector<uint16_t> depth_out(kWidth * kHeight);
    bool reported_loss = false;
    uint32_t counted = 0;
    auto count_start = std::chrono::steady_clock::now();
    while (!stop) {
      int result = read(rgb_in.data(), depth_in.data(), 250);
      auto now = std::chrono::steady_clock::now();
      const double elapsed =
          std::chrono::duration<double>(now - count_start).count();
      if (elapsed >= 1.0) {
        fps = float(counted / elapsed);
        counted = 0;
        count_start = now;
      }
      if (result == kRuntimeTimeout) {
        continue;
      }
      if (result != kRuntimeOk) {
        // Unplugged or failed: try to open it again every two seconds.
        if (!reported_loss) {
          reported_loss = true;
          XELOGW("Kinect: lost the sensor ({}), reconnecting", result);
          SetMessage("The Kinect stopped responding. Reconnecting...");
        }
        fps = 0.0f;
        close();
        for (int i = 0; i < 20 && !stop; ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!stop &&
            open(convert ? kRuntimeKinect2 : kRuntimeKinect1) == kRuntimeOk) {
          XELOGI("Kinect: sensor reconnected");
          SetMessage("");
          reported_loss = false;
        }
        continue;
      }
      ++counted;
      if (convert) {
        if (pan != configured_pan || zoom != configured_zoom) {
          configured_pan = pan;
          configured_zoom = zoom;
          converter.Configure(configured_pan, configured_zoom);
        }
        converter.ConvertDepth(depth_in.data(), depth_out.data());
        converter.ConvertColor(rgb_in.data(), rgb_out.data());
      } else {
        std::memcpy(depth_out.data(), depth_in.data(),
                    depth_out.size() * sizeof(uint16_t));
        std::memcpy(rgb_out.data(), rgb_in.data(), rgb_out.size());
      }
      std::lock_guard<std::mutex> lock(frame_mutex);
      depth.swap(depth_out);
      rgb.swap(rgb_out);
      ++frame;
    }
    fps = 0.0f;
  }
};

KinectCamera* KinectCamera::Get() {
  static KinectCamera camera;
  return &camera;
}

KinectCamera::~KinectCamera() {
  Stop();
  delete impl_;
}

bool KinectCamera::Start(const std::string& mode) {
  if (!impl_) {
    impl_ = new Impl();
    impl_->pan = cvars::nui_kinect2_pan;
    impl_->zoom = cvars::nui_kinect2_zoom;
  }
  std::lock_guard<std::mutex> lock(impl_->start_mutex);
  if (impl_->started) {
    return impl_->streaming;
  }
  impl_->started = true;
  if (!impl_->Load()) {
    return false;
  }
  std::vector<KinectSensorKind> kinds;
  if (mode == "kinect1") {
    kinds = {KinectSensorKind::kKinect1};
  } else if (mode == "kinect2") {
    kinds = {KinectSensorKind::kKinect2};
  } else {
    kinds = {KinectSensorKind::kKinect1, KinectSensorKind::kKinect2};
  }
  KinectSensorKind kind = impl_->Open(kinds);
  if (kind == KinectSensorKind::kNone) {
    return false;
  }
  {
    std::lock_guard<std::mutex> status_lock(impl_->status_mutex);
    impl_->kind = kind;
    impl_->message.clear();
  }
  if (kind == KinectSensorKind::kKinect2) {
    XELOGI("Kinect: {} streaming (pan {}, zoom {:.2f})", KinectSensorName(kind),
           int(impl_->pan), double(impl_->zoom));
  } else {
    XELOGI("Kinect: {} streaming", KinectSensorName(kind));
  }
  impl_->stop = false;
  impl_->streaming = true;
  impl_->thread = std::thread([this]() { impl_->Run(); });
  RegisterExitHandlers();
  return true;
}

bool KinectCamera::Restart(const std::string& mode) {
  Stop();
  // Also retries a sensor that failed to open.
  if (impl_) {
    std::lock_guard<std::mutex> lock(impl_->start_mutex);
    impl_->started = false;
  }
  return Start(mode);
}

bool KinectCamera::Read(uint16_t* depth_mm, uint8_t* rgb,
                        uint32_t* frame_number) {
  if (!impl_ || !impl_->streaming) {
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->frame_mutex);
  if (impl_->frame == *frame_number) {
    return false;
  }
  std::memcpy(depth_mm, impl_->depth.data(), kWidth * kHeight * 2);
  std::memcpy(rgb, impl_->rgb.data(), kWidth * kHeight * 3);
  *frame_number = impl_->frame;
  return true;
}

void KinectCamera::Stop() {
  if (!impl_ || !impl_->streaming.exchange(false)) {
    return;
  }
  impl_->stop = true;
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_->close();
  {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->kind = KinectSensorKind::kNone;
    impl_->message = "Stopped.";
  }
  {
    // The next Start() opens the sensor again.
    std::lock_guard<std::mutex> lock(impl_->start_mutex);
    impl_->started = false;
  }
  XELOGI("Kinect: sensor released");
  xe::FlushLog();
}

void KinectCamera::SetView(int pan, double zoom) {
  if (!impl_) {
    return;
  }
  impl_->pan = pan;
  impl_->zoom = zoom;
}

KinectCamera::Status KinectCamera::GetStatus() {
  Status status;
  if (!impl_) {
    status.message = "Not started.";
    return status;
  }
  std::lock_guard<std::mutex> lock(impl_->status_mutex);
  status.kind = impl_->kind;
  status.streaming = impl_->streaming;
  status.frames_per_second = impl_->fps;
  status.message = impl_->message;
  return status;
}

namespace {

// PKEY_Device_FriendlyName.
const PROPERTYKEY kDeviceFriendlyName = {
    {0xA45C254E,
     0xDF1C,
     0x4EFD,
     {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}},
    14};

template <typename T>
void SafeRelease(T*& p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

std::wstring Lower(std::wstring text) {
  std::transform(text.begin(), text.end(), text.begin(), ::towlower);
  return text;
}

// Calls fn(device, friendly name) for each active capture device until it
// returns true.
template <typename Fn>
void ForEachCaptureDevice(IMMDeviceEnumerator* enumerator, Fn fn) {
  IMMDeviceCollection* devices = nullptr;
  if (FAILED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE,
                                            &devices))) {
    return;
  }
  UINT count = 0;
  devices->GetCount(&count);
  bool done = false;
  for (UINT i = 0; i < count && !done; ++i) {
    IMMDevice* device = nullptr;
    IPropertyStore* props = nullptr;
    if (SUCCEEDED(devices->Item(i, &device)) &&
        SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
      PROPVARIANT name;
      PropVariantInit(&name);
      if (SUCCEEDED(props->GetValue(kDeviceFriendlyName, &name)) &&
          name.vt == VT_LPWSTR) {
        done = fn(device, std::wstring(name.pwszVal));
      }
      PropVariantClear(&name);
    }
    SafeRelease(props);
    SafeRelease(device);
  }
  SafeRelease(devices);
}

std::string ToUtf8(const std::wstring& text) {
  return xe::to_utf8(std::u16string(text.begin(), text.end()));
}

}  // namespace

struct KinectMicrophones::Impl {
  static constexpr uint32_t kCapacity = 16384;  // frames, a power of two

  std::mutex start_mutex;
  bool started = false;
  std::atomic<bool> running{false};
  std::atomic<bool> stop{false};
  std::thread thread;

  std::vector<int32_t> ring = std::vector<int32_t>(kCapacity * kChannels);
  std::atomic<uint64_t> written{0};
  uint64_t read_pos = UINT64_MAX;

  std::mutex name_mutex;
  std::string name;
  std::atomic<float> peak{0.0f};

  void Write(const float* in, uint32_t frames, uint32_t channels) {
    uint64_t pos = written.load(std::memory_order_relaxed);
    // The Kinect 2 exposes two live microphones; when the device reports
    // more channels, the extra ones are silent and get the first two.
    bool upper_silent = true;
    if (channels >= 4) {
      for (uint32_t f = 0; f < frames && upper_silent; ++f) {
        upper_silent = std::abs(in[f * channels + 2]) < 1e-4f &&
                       std::abs(in[f * channels + 3]) < 1e-4f;
      }
    }
    float block_peak = 0.0f;
    for (uint32_t f = 0; f < frames; ++f, ++pos) {
      const float* s = in + size_t(f) * channels;
      float c[kChannels];
      if (channels >= 4 && !upper_silent) {
        c[0] = s[0], c[1] = s[1], c[2] = s[2], c[3] = s[3];
      } else if (channels >= 2) {
        c[0] = s[0], c[1] = s[1], c[2] = s[0], c[3] = s[1];
      } else {
        c[0] = c[1] = c[2] = c[3] = s[0];
      }
      int32_t* out = &ring[(pos & (kCapacity - 1)) * kChannels];
      for (uint32_t i = 0; i < kChannels; ++i) {
        const float v = std::clamp(c[i], -1.0f, 1.0f);
        block_peak = std::max(block_peak, std::abs(v));
        out[i] = static_cast<int32_t>(std::lrint(v * 2147483520.0f));
      }
    }
    written.store(pos, std::memory_order_release);
    // A meter that falls back over about half a second.
    peak = std::max(block_peak, peak * 0.9f);
  }

  // Runs on the capture thread. Returns false if capture could not start.
  bool Capture() {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;
    WAVEFORMATEX* mix = nullptr;
    HANDLE event = nullptr;
    bool ok = false;
    do {
      if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(&enumerator)))) {
        break;
      }
      const std::u16string want16 = xe::to_utf16(cvars::nui_mic_device);
      const std::wstring want =
          Lower(std::wstring(want16.begin(), want16.end()));
      std::wstring found_name;
      ForEachCaptureDevice(
          enumerator, [&](IMMDevice* d, const std::wstring& n) {
            if (!want.empty() && Lower(n).find(want) != std::wstring::npos) {
              device = d;
              device->AddRef();
              found_name = n;
              return true;
            }
            return false;
          });
      if (!device) {
        XELOGW("Kinect microphones: no capture device named \"{}\"",
               cvars::nui_mic_device);
        break;
      }
      {
        std::lock_guard<std::mutex> lock(name_mutex);
        name = ToUtf8(found_name);
      }
      XELOGI("Kinect microphones: {}", ToUtf8(found_name));
      if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(&client))) ||
          FAILED(client->GetMixFormat(&mix))) {
        break;
      }
      const uint32_t channels = std::max<uint32_t>(1, mix->nChannels);
      WAVEFORMATEXTENSIBLE format = {};
      format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
      format.Format.nChannels = WORD(channels);
      format.Format.nSamplesPerSec = kSampleRate;
      format.Format.wBitsPerSample = 32;
      format.Format.nBlockAlign = WORD(channels * 4);
      format.Format.nAvgBytesPerSec = kSampleRate * channels * 4;
      format.Format.cbSize = sizeof(format) - sizeof(WAVEFORMATEX);
      format.Samples.wValidBitsPerSample = 32;
      format.dwChannelMask =
          mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE
              ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(mix)->dwChannelMask
              : 0;
      format.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
      event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
      if (!event ||
          FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    1000000, 0, &format.Format, nullptr)) ||
          FAILED(client->SetEventHandle(event)) ||
          FAILED(client->GetService(__uuidof(IAudioCaptureClient),
                                    reinterpret_cast<void**>(&capture))) ||
          FAILED(client->Start())) {
        XELOGW("Kinect microphones: could not start capture");
        break;
      }
      ok = true;
      running = true;
      std::vector<float> silence;
      while (!stop) {
        WaitForSingleObject(event, 200);
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet) {
          BYTE* data = nullptr;
          UINT32 frames = 0;
          DWORD flags = 0;
          if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr,
                                        nullptr))) {
            break;
          }
          if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            silence.assign(size_t(frames) * channels, 0.0f);
            Write(silence.data(), frames, channels);
          } else {
            Write(reinterpret_cast<const float*>(data), frames, channels);
          }
          capture->ReleaseBuffer(frames);
        }
      }
      client->Stop();
    } while (false);
    running = false;
    peak = 0.0f;
    {
      std::lock_guard<std::mutex> lock(name_mutex);
      name.clear();
    }
    SafeRelease(capture);
    SafeRelease(client);
    if (mix) {
      CoTaskMemFree(mix);
    }
    SafeRelease(device);
    SafeRelease(enumerator);
    if (event) {
      CloseHandle(event);
    }
    return ok;
  }
};

KinectMicrophones* KinectMicrophones::Get() {
  static KinectMicrophones microphones;
  return &microphones;
}

KinectMicrophones::~KinectMicrophones() {
  Stop();
  delete impl_;
}

std::vector<std::string> KinectMicrophones::ListDevices() {
  std::vector<std::string> names;
  HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  IMMDeviceEnumerator* enumerator = nullptr;
  if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                 CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                 reinterpret_cast<void**>(&enumerator)))) {
    ForEachCaptureDevice(enumerator, [&](IMMDevice*, const std::wstring& name) {
      names.push_back(ToUtf8(name));
      return false;
    });
    SafeRelease(enumerator);
  }
  if (SUCCEEDED(hr)) {
    CoUninitialize();
  }
  return names;
}

void KinectMicrophones::Stop() {
  if (!impl_) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->start_mutex);
  impl_->stop = true;
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_->started = false;
  impl_->read_pos = UINT64_MAX;
}

bool KinectMicrophones::Start() {
  if (!impl_) {
    impl_ = new Impl();
  }
  std::lock_guard<std::mutex> lock(impl_->start_mutex);
  if (!impl_->started) {
    impl_->started = true;
    impl_->stop = false;
    impl_->thread = std::thread([this]() {
      HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      impl_->Capture();
      if (SUCCEEDED(hr)) {
        CoUninitialize();
      }
    });
    RegisterExitHandlers();
  }
  return impl_->running;
}

bool KinectMicrophones::Restart() {
  Stop();
  return Start();
}

bool KinectMicrophones::Read(int32_t (*out)[kChannels], uint32_t count) {
  if (!impl_ || !impl_->running) {
    return false;
  }
  const uint64_t written = impl_->written.load(std::memory_order_acquire);
  uint64_t& pos = impl_->read_pos;
  // Start a little behind the writer, and resync when too far off.
  if (pos == UINT64_MAX || pos > written ||
      written - pos > Impl::kCapacity / 2) {
    pos = written > count * 2 ? written - count * 2 : 0;
  }
  if (written - pos < count) {
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const int32_t* frame =
        &impl_->ring[((pos + i) & (Impl::kCapacity - 1)) * kChannels];
    for (uint32_t c = 0; c < kChannels; ++c) {
      out[i][c] = frame[c];
    }
  }
  pos += count;
  return true;
}

std::string KinectMicrophones::device_name() {
  if (!impl_) {
    return {};
  }
  std::lock_guard<std::mutex> lock(impl_->name_mutex);
  return impl_->name;
}

float KinectMicrophones::level() { return impl_ ? impl_->peak.load() : 0.0f; }

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe
