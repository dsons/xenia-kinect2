/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <cstring>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

DECLARE_bool(nui_mic);

namespace xe {
namespace kernel {
namespace xboxkrnl {
// The Kinect microphone array lives in xboxkrnl_audio.cc.
uint32_t xeMicArraySubmit(uint32_t packet, uint32_t event);
uint32_t xeMicArrayUnderruns();
}  // namespace xboxkrnl
namespace xam {

dword_result_t XamVoiceIsActiveProcess_entry() {
  // Returning 0 here will short-circuit a bunch of voice stuff.
  return 0;
}
DECLARE_XAM_EXPORT1(XamVoiceIsActiveProcess, kNone, kStub);

dword_result_t XamVoiceCreate_entry(dword_t user_index,
                                    dword_t max_attached_packets,  // 0xF
                                    lpdword_t out_voice_ptr) {
  // Null out the ptr.
  out_voice_ptr.Zero();
  return X_ERROR_ACCESS_DENIED;
}
DECLARE_XAM_EXPORT1(XamVoiceCreate, kNone, kStub);

dword_result_t XamVoiceClose_entry(lpunknown_t voice_ptr) { return 0; }
DECLARE_XAM_EXPORT1(XamVoiceClose, kNone, kStub);

dword_result_t XamVoiceHeadsetPresent_entry(lpunknown_t voice_ptr) { return 0; }
DECLARE_XAM_EXPORT1(XamVoiceHeadsetPresent, kNone, kStub);

dword_result_t XamVoiceSubmitPacket_entry(lpdword_t unk1, dword_t unk2,
                                          lpdword_t unk3) {
  // also may return 0xD000009D
  return 0x800700AA;
}
DECLARE_XAM_EXPORT1(XamVoiceSubmitPacket, kNone, kStub);

dword_result_t XamVoiceGetMicArrayStatus_entry() {
  // Returning 0 here tells caller mic is not connected
  return cvars::nui_mic ? 1 : 0;
}
DECLARE_XAM_EXPORT1(XamVoiceGetMicArrayStatus, kNone, kSketchy);

dword_result_t XamVoiceGetMicArrayAudioEx_entry(lpvoid_t packet,
                                                lpvoid_t event) {
  if (!cvars::nui_mic) {
    return X_E_FAIL;
  }
  return xboxkrnl::xeMicArraySubmit(packet.guest_address(),
                                    event.guest_address());
}
DECLARE_XAM_EXPORT1(XamVoiceGetMicArrayAudioEx, kNone, kSketchy);

dword_result_t XamVoiceGetMicArrayUnderrunStatus_entry(lpdword_t flags,
                                                       lpdword_t count) {
  if (flags) {
    *flags = 0;
  }
  if (count) {
    *count = xboxkrnl::xeMicArrayUnderruns();
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamVoiceGetMicArrayUnderrunStatus, kNone, kSketchy);

dword_result_t XamVoiceSetMicArrayIdleUsers_entry(dword_t users) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamVoiceSetMicArrayIdleUsers, kNone, kStub);

dword_result_t XamNuiGetDeviceSerialNumber_entry(lpvoid_t buffer,
                                                 dword_t size) {
  // A fixed made-up serial; Kinect Sports Season Two asks for 0x11 bytes.
  static const char kSerial[] = "XENIAKINECT00001";
  if (!buffer || size < sizeof(kSerial)) {
    return X_E_INVALIDARG;
  }
  std::memcpy(buffer, kSerial, sizeof(kSerial));
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiGetDeviceSerialNumber, kNone, kSketchy);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Voice);
