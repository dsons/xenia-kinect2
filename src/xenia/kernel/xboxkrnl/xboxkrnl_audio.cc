/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include "xenia/apu/audio_system.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/nui_kinect.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xthread.h"
#include "xenia/xbox.h"

DECLARE_uint32(audio_flag);

DEFINE_bool(nui_mic, true,
            "Emulate the Kinect microphone array. Audio comes from the "
            "nui_mic_device capture device when present, else silence.",
            "NUI");
DEFINE_bool(nui_mic_log, false, "Log the Kinect microphone array calls.",
            "NUI");

namespace xe {
namespace kernel {
namespace xboxkrnl {

dword_result_t XAudioGetSpeakerConfig_entry(lpdword_t config_ptr) {
  kernel_state()->xconfig()->ReadSetting(
      XCONFIG_USER_CATEGORY,
      XCONFIG_USER_CATEGORY_ENTRIES::XCONFIG_USER_AUDIO_FLAGS, config_ptr);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioGetSpeakerConfig, kAudio, kImplemented);

dword_result_t XAudioGetVoiceCategoryVolumeChangeMask_entry(
    lpunknown_t driver_ptr, lpdword_t out_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  xe::threading::MaybeYield();

  // Checking these bits to see if any voice volume changed.
  // I think.
  *out_ptr = 0;
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT2(XAudioGetVoiceCategoryVolumeChangeMask, kAudio, kStub,
                         kHighFrequency);

dword_result_t XAudioGetVoiceCategoryVolume_entry(dword_t unk,
                                                  lpfloat_t out_ptr) {
  // Expects a floating point single. Volume %?
  *out_ptr = 1.0f;

  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT2(XAudioGetVoiceCategoryVolume, kAudio, kStub,
                         kHighFrequency);

dword_result_t XAudioEnableDucker_entry(dword_t unk) { return X_ERROR_SUCCESS; }
DECLARE_XBOXKRNL_EXPORT1(XAudioEnableDucker, kAudio, kStub);

dword_result_t XAudioRegisterRenderDriverClient_entry(lpdword_t callback_ptr,
                                                      lpdword_t driver_ptr) {
  if (!callback_ptr) {
    return X_E_INVALIDARG;
  }

  uint32_t callback = callback_ptr[0];

  if (!callback) {
    return X_E_INVALIDARG;
  }
  uint32_t callback_arg = callback_ptr[1];

  auto audio_system = kernel_state()->emulator()->audio_system();

  size_t index;
  auto result = audio_system->RegisterClient(callback, callback_arg, &index);
  if (XFAILED(result)) {
    return result;
  }

  assert_true(!(index & ~0x0000FFFF));
  *driver_ptr = 0x41550000 | (static_cast<uint32_t>(index) & 0x0000FFFF);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioRegisterRenderDriverClient, kAudio,
                         kImplemented);

dword_result_t XAudioUnregisterRenderDriverClient_entry(
    lpunknown_t driver_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  audio_system->UnregisterClient(driver_ptr.guest_address() & 0x0000FFFF);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioUnregisterRenderDriverClient, kAudio,
                         kImplemented);

dword_result_t XAudioSubmitRenderDriverFrame_entry(lpunknown_t driver_ptr,
                                                   lpunknown_t samples_ptr) {
  assert_true((driver_ptr.guest_address() & 0xFFFF0000) == 0x41550000);

  auto audio_system = kernel_state()->emulator()->audio_system();
  auto samples =
      kernel_state()->memory()->TranslateVirtual<float*>(samples_ptr);
  audio_system->SubmitFrame(driver_ptr.guest_address() & 0x0000FFFF, samples);

  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT2(XAudioSubmitRenderDriverFrame, kAudio, kImplemented,
                         kHighFrequency);

// The Kinect microphone array, reverse-engineered from Kinect Sports Season
// Two (4D5309D6, NUIAUD 2.0.20764).
//
// The title registers a "MEC" (echo cancellation) render driver client. Its
// callback runs once per render driver frame (256 samples at 48 kHz, 5.33 ms;
// XAudioGetRenderDriverTic counts them) and fetches the speaker mix with
// XAudioCaptureRenderDriverFrame (256 x 6 floats). Capture is double
// buffered: XamVoiceGetMicArrayAudioEx(packet, event) queues one 16 ms packet
// (three render frames) and the KEVENT is set when it is full; the callback
// polls the event. The packet:
//   +0x00 be32 size (0x68)
//   +0x04 be32 echo-cancelled buffer: 256 x be16, 16 kHz mono
//   +0x08 be32 x4 raw microphone buffers: 256 x be32 each
//   +0x18 be32 x5 the five buffer sizes (0x200, 0x400 x4)
//   +0x40 be16 render driver tic the packet was captured at
//   +0x44 float beam angle (radians), +0x48 float beam confidence
//   +0x64 be32 0x20, set by the title before every submit
// XamVoiceGetMicArrayUnderrunStatus(&flags, &count): flags bits 0-1 set make
// the title reset its capture.
//
// Audio comes from KinectMicrophones: the Kinect 2's microphones or another
// capture device, four channels at 16 kHz.
namespace {
constexpr uint32_t kMicSamples = 256;
constexpr uint32_t kMecDriver = 0x41560000;

class MicArray {
 public:
  void EnsureStarted();
  uint32_t Submit(uint32_t packet, uint32_t event);
  uint32_t RegisterMec(uint32_t callback, uint32_t arg);
  void UnregisterMec(uint32_t driver);
  uint32_t tic() const { return tic_.load(); }
  uint32_t underruns() const { return underruns_.load(); }

 private:
  struct Pending {
    uint32_t packet;
    uint32_t event;
  };
  struct Mec {
    uint32_t callback;
    uint32_t arg;
  };
  void WorkerMain();
  void Fill(uint32_t packet, uint16_t tic);

  std::mutex mutex_;
  bool started_ = false;
  std::deque<Pending> pending_;
  std::vector<Mec> mec_;
  std::atomic<uint32_t> tic_{0};
  std::atomic<uint32_t> underruns_{0};
  uint32_t filled_ = 0;
  object_ref<XHostThread> worker_;
};

MicArray mic_array_;

void MicArray::EnsureStarted() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (started_) {
    return;
  }
  started_ = true;
  KinectMicrophones::Get()->Start();
  worker_ = object_ref<XHostThread>(new XHostThread(
      kernel_state(), 128 * 1024, 0,
      [this]() {
        WorkerMain();
        return 0;
      },
      kernel_state()->GetSystemProcess()));
  worker_->set_can_debugger_suspend(true);
  worker_->set_name("NUI Microphones");
  worker_->Create();
}

uint32_t MicArray::Submit(uint32_t packet, uint32_t event) {
  EnsureStarted();
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.push_back({packet, event});
  return X_ERROR_SUCCESS;
}

uint32_t MicArray::RegisterMec(uint32_t callback, uint32_t arg) {
  EnsureStarted();
  std::lock_guard<std::mutex> lock(mutex_);
  mec_.push_back({callback, arg});
  XELOGI("NUI mic: MEC client {:08X}({:08X}) registered", callback, arg);
  return kMecDriver | static_cast<uint32_t>(mec_.size() - 1);
}

void MicArray::UnregisterMec(uint32_t driver) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint32_t index = driver & 0xFFFF;
  if (index < mec_.size()) {
    mec_[index].callback = 0;
  }
}

void MicArray::Fill(uint32_t packet, uint16_t tic) {
  auto memory = kernel_state()->memory();
  auto p = memory->TranslateVirtual<uint8_t*>(packet);
  int32_t samples[kMicSamples][4];
  if (!KinectMicrophones::Get()->Read(samples, kMicSamples)) {
    std::memset(samples, 0, sizeof(samples));
    underruns_++;
  }
  uint32_t mono_ptr = xe::load_and_swap<uint32_t>(p + 0x04);
  uint32_t mono_size = xe::load_and_swap<uint32_t>(p + 0x18);
  if (mono_ptr && mono_size >= kMicSamples * 2) {
    auto mono = memory->TranslateVirtual<uint8_t*>(mono_ptr);
    for (uint32_t i = 0; i < kMicSamples; ++i) {
      // No real echo canceller: the average of the four microphones.
      int64_t sum = 0;
      for (uint32_t c = 0; c < 4; ++c) {
        sum += samples[i][c];
      }
      xe::store_and_swap<int16_t>(mono + i * 2,
                                  static_cast<int16_t>((sum / 4) >> 16));
    }
  }
  for (uint32_t c = 0; c < 4; ++c) {
    uint32_t ptr = xe::load_and_swap<uint32_t>(p + 0x08 + c * 4);
    uint32_t size = xe::load_and_swap<uint32_t>(p + 0x1C + c * 4);
    if (!ptr || size < kMicSamples * 4) {
      continue;
    }
    auto raw = memory->TranslateVirtual<uint8_t*>(ptr);
    for (uint32_t i = 0; i < kMicSamples; ++i) {
      xe::store_and_swap<int32_t>(raw + i * 4, samples[i][c]);
    }
  }
  xe::store_and_swap<uint16_t>(p + 0x40, tic);
  xe::store_and_swap<float>(p + 0x44, 0.0f);
  xe::store_and_swap<float>(p + 0x48, 0.0f);
}

void MicArray::WorkerMain() {
  using clock = std::chrono::steady_clock;
  auto start = clock::now();
  uint64_t frame = 0;
  while (true) {
    ++frame;
    // 256 samples at 48 kHz per render driver frame.
    auto next = start + std::chrono::microseconds(frame * 16000 / 3);
    auto now = clock::now();
    if (next > now) {
      xe::threading::Sleep(
          std::chrono::duration_cast<std::chrono::milliseconds>(next - now));
    }
    uint32_t tic = tic_.fetch_add(1) + 1;
    // The title arms its sync on (XAudioGetRenderDriverTic() & 0xFFFF) % 3 == 0
    // and needs the packet stamped with exactly that tic, also after the wrap.
    if ((tic & 0xFFFF) % 3 == 0) {
      Pending pending = {0, 0};
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pending_.empty()) {
          pending = pending_.front();
          pending_.pop_front();
        }
      }
      if (pending.packet) {
        Fill(pending.packet, static_cast<uint16_t>(tic));
        if (pending.event) {
          xeKeSetEvent(kernel_state()->memory()->TranslateVirtual<X_KEVENT*>(
                           pending.event),
                       1, 0);
        }
        if (cvars::nui_mic_log && ++filled_ % 300 == 1) {
          XELOGI("NUI mic: packet {} filled, {} underruns", filled_,
                 underruns_.load());
        }
      }
    }
    std::vector<Mec> mec;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      mec = mec_;
    }
    for (auto& client : mec) {
      if (client.callback) {
        uint64_t args[] = {client.arg};
        kernel_state()->processor()->Execute(worker_->thread_state(),
                                             client.callback, args, 1);
      }
    }
  }
}
}  // namespace

uint32_t xeMicArraySubmit(uint32_t packet, uint32_t event) {
  static uint32_t submits = 0;
  if (cvars::nui_mic_log && submits++ % 300 == 0) {
    XELOGI("NUI mic: XamVoiceGetMicArrayAudioEx({:08X}, {:08X}) #{}", packet,
           event, submits);
  }
  return mic_array_.Submit(packet, event);
}

uint32_t xeMicArrayUnderruns() { return mic_array_.underruns(); }

dword_result_t XAudioGetRenderDriverTic_entry() {
  return mic_array_.tic() & 0xFFFF;
}
DECLARE_XBOXKRNL_EXPORT2(XAudioGetRenderDriverTic, kAudio, kImplemented,
                         kHighFrequency);

dword_result_t XAudioRegisterRenderDriverMECClient_entry(lpdword_t callback_ptr,
                                                         lpdword_t driver_ptr) {
  if (!callback_ptr || !callback_ptr[0] || !cvars::nui_mic) {
    return X_E_INVALIDARG;
  }
  *driver_ptr = mic_array_.RegisterMec(callback_ptr[0], callback_ptr[1]);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioRegisterRenderDriverMECClient, kAudio,
                         kImplemented);

dword_result_t XAudioUnregisterRenderDriverMECClient_entry(dword_t driver) {
  mic_array_.UnregisterMec(driver);
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioUnregisterRenderDriverMECClient, kAudio,
                         kImplemented);

dword_result_t XAudioCaptureRenderDriverFrame_entry(dword_t driver,
                                                    lpvoid_t samples_ptr) {
  // The speaker mix the echo canceller subtracts: 256 samples x 6 channels.
  // Not captured yet, so the title cancels nothing.
  if (samples_ptr) {
    std::memset(samples_ptr, 0, kMicSamples * 6 * sizeof(float));
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioCaptureRenderDriverFrame, kAudio, kStub);

dword_result_t XAudioGetDuckerLevel_entry(lpfloat_t level) {
  if (level) {
    *level = 0.0f;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioGetDuckerLevel, kAudio, kStub);

dword_result_t XAudioGetDuckerThreshold_entry(lpfloat_t value) {
  if (value) {
    *value = 0.0f;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioGetDuckerThreshold, kAudio, kStub);

dword_result_t XAudioGetDuckerAttackTime_entry(lpdword_t value) {
  if (value) {
    *value = 0;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioGetDuckerAttackTime, kAudio, kStub);

dword_result_t XAudioGetDuckerReleaseTime_entry(lpdword_t value) {
  if (value) {
    *value = 0;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioGetDuckerReleaseTime, kAudio, kStub);

dword_result_t XAudioGetDuckerHoldTime_entry(lpdword_t value) {
  if (value) {
    *value = 0;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XBOXKRNL_EXPORT1(XAudioGetDuckerHoldTime, kAudio, kStub);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Audio);
