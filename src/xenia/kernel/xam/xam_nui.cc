/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>

#include "xenia/base/clock.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_flags.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/kernel/xthread.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

DECLARE_bool(nui_mic);

DEFINE_bool(nui_skeleton_log, true,
            "Write every skeleton the game tracks to xenia-skeleton.csv next "
            "to xenia.log (joint positions in metres), to diagnose a pose or "
            "gesture a game does not take. The previous run's file is kept as "
            "xenia-skeleton.previous.csv; a file past 25 MB starts over.",
            "NUI");
DEFINE_bool(nui_kinect_id, true,
            "Kinect ID: enroll a newly tracked player as the first signed-in "
            "user without a Kinect slot, so titles find them signed in, as on "
            "a console where the profile has Kinect sign-in enabled.",
            "NUI");

namespace xe {
namespace kernel {
namespace xboxkrnl {
bool NuiCameraAvailable();  // xboxkrnl_nui_camera.cc
}  // namespace xboxkrnl
namespace xam {
// https://web.cs.ucdavis.edu/~okreylos/ResDev/Kinect/MainPage.html

struct X_NUI_DEVICE_STATUS {
  /* Notes:
     - for one side func of XamNuiGetDeviceStatus
       - if some data addressis less than zero then unk1 = it
       - else another func is called and its return can set unk1 = c0051200 or
     some value involving DetroitDeviceRequest
       - next PsCamDeviceRequest is called and if its return is less than zero
     then X_NUI_DEVICE_STATUS = return of PsCamDeviceRequest
       - else it equals an unknown local_1c
       - finally McaDeviceRequest is called and if its return is less than zero
     then unk2 = return of McaDeviceRequest
       - else it equals an unknown local_14
     - status can be set to X_NUI_DEVICE_STATUS[3] | 0x44 or | 0x40
  */
  xe::be<uint32_t> unk0;
  xe::be<uint32_t> unk1;
  xe::be<uint32_t> unk2;
  xe::be<uint32_t> status;
  xe::be<uint32_t> unk4;
  xe::be<uint32_t> unk5;
};
static_assert(sizeof(X_NUI_DEVICE_STATUS) == 24, "Size matters");

// Get
dword_result_t XamNuiGetDeviceStatus_entry(
    pointer_t<X_NUI_DEVICE_STATUS> status_ptr) {
  /* Notes:
     - it does return a value that is not always used
     - returns values are X_ERROR_SUCCESS, 0xC0050006, and others
     - 1) On func start *status_ptr = 0, status_ptr->unk1 = 0, status_ptr->unk2
     = 0, and status_ptr->status = 0
     - 2) calls XamXStudioRequest(6,&var <- = 0);
     - if return is greater than -1 && var & 0x80000000 != 0 then set
     status_ptr->unk1 = 0xC000009D, status_ptr->unk2 = 0xC000009D, and
     status_ptr->status = status_ptr[3] = 0x20
     - lots of branching functions after
  */

  status_ptr.Zero();

  // The emulated sensor is present when a camera is behind it, whatever the
  // console's out-of-box (KinectInitialized) flag says.
  const bool present = xboxkrnl::NuiCameraAvailable();

  // Bit 0: sensor present. Bit 1: microphone array ready; without it Kinect
  // Sports Season Two never starts the audio source of its speech recognizer.
  status_ptr->status = present ? (cvars::nui_mic ? 0x3u : 0x1u) : 0u;
  return present ? X_ERROR_SUCCESS : 0xC0050006;
}
DECLARE_XAM_EXPORT1(XamNuiGetDeviceStatus, kNone, kStub);

// Kinect identity, the XAM half.
//
// The NUI runtime linked into each game recognises players itself (body and
// face templates in eight "enrollment" slots); XAM only says which signed-in
// user each enrollment slot belongs to, and drives the runtime when the Kinect
// sign-in UI runs. The runtime registers a callback with message 0x2C009 and
// XAM sends it, among others:
//   0x2C006 {tracking id, enrollment index, flags}  enroll that player (flag
//           0x20: into the given slot, which must already map to a user)
//   0x2C007 {tracking id}                           identify that player
//   0x2C00E {tracking id}                           cancel its op
//   0x2C00F {}                                      re-read slot -> user
// 0x2C00F makes the runtime call XamUserNuiGetUserIndex(slot) for all eight
// slots; a tracked skeleton's dwUserIndex comes from that table once the
// player is enrolled or identified into a slot. Fruit Ninja waits for that
// after XamShowNuiSigninUI and otherwise shows "Kinect enrollment: can't see
// you". The runtime also sends XAM its skeleton frame every frame (0x2B002),
// which is where XAM learns the tracking ids.
//
// Here enrollment slot N belongs to user N once that user has been through
// the sign-in UI (or enabled biometric sign-in), or, with --nui_kinect_id,
// once a new player is tracked while user N is signed in without a slot.
namespace {
std::mutex identity_mutex_;
uint32_t identity_callback_ = 0;
uint32_t identity_bound_ = 0;    // bit N: slot N belongs to user N
uint32_t identity_pending_ = 0;  // bit N: enroll user N's player next frame
uint64_t identity_enroll_ms_ = 0;
uint32_t identity_enroll_slot_ = 0;
uint32_t identity_retries_ = 0;
uint32_t identity_auto_id_ = 0;  // tracking id Kinect ID last enrolled
uint32_t identity_last_[3] = {~0u, ~0u, ~0u};  // logged id/enroll/user

bool UserSignedIn(uint32_t user_index) {
  return user_index < XUserMaxUserCount &&
         kernel_state()->xam_state()->GetUserProfile(user_index);
}

// Sends one message to the title's identity callback on this guest thread.
uint32_t CallIdentity(uint32_t message,
                      std::initializer_list<uint32_t> params) {
  auto memory = kernel_state()->memory();
  uint32_t buffer = memory->SystemHeapAlloc(0x20);
  auto p = memory->TranslateVirtual<xe::be<uint32_t>*>(buffer);
  std::memset(p, 0, 0x20);
  size_t i = 0;
  for (uint32_t v : params) {
    p[i++] = v;
  }
  uint64_t args[] = {message, buffer};
  auto result = static_cast<uint32_t>(kernel_state()->processor()->Execute(
      XThread::GetCurrentThread()->thread_state(), identity_callback_, args,
      xe::countof(args)));
  memory->SystemHeapFree(buffer);
  XELOGI("NuiIdentity: message {:05X} -> {:08X}", message, result);
  return result;
}
}  // namespace

void XamNuiIdentitySetCallback(uint32_t callback) {
  std::lock_guard<std::mutex> lock(identity_mutex_);
  identity_callback_ = callback;
}

// The title's skeleton frame, sent with message 0x2B002 (0xAE0 bytes): a
// version (5) and 0x511C, the floor plane and gravity normal at +0x10/+0x20,
// then six NUI_SKELETON_DATA records 0x1C0 apart from +0x30: state (2 =
// tracked), tracking id, enrollment index, user index, position, joints.
// With nui_skeleton_log, every tracked skeleton the game computes goes to
// xenia-skeleton.csv next to xenia.log, for diagnosing games that do not take
// a pose or gesture. Like xenia.log, the previous file is kept as
// xenia-skeleton.previous.csv; past kMaxSkeletonLogBytes the current file
// becomes the previous one, so the latest ~25 minutes are always there.
constexpr long kMaxSkeletonLogBytes = 25 * 1024 * 1024;

static void LogSkeletons(const uint8_t* b) {
  static std::FILE* file = nullptr;
  static bool opened = false;
  const auto path =
      xe::filesystem::GetExecutableFolder() / "xenia-skeleton.csv";
  if (file && std::ftell(file) > kMaxSkeletonLogBytes) {
    std::fclose(file);
    file = nullptr;
    opened = false;
  }
  if (!opened) {
    opened = true;
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
      std::filesystem::rename(
          path,
          xe::filesystem::GetExecutableFolder() / "xenia-skeleton.previous.csv",
          error);
    }
    file = xe::filesystem::OpenFile(path, "w");
    if (!file) {
      XELOGW("NuiSkeleton: cannot write {}", xe::path_to_utf8(path));
      return;
    }
    static const char* const kJoints[20] = {
        "hip_center", "spine",   "shoulder_center", "head",       "shoulder_l",
        "elbow_l",    "wrist_l", "hand_l",          "shoulder_r", "elbow_r",
        "wrist_r",    "hand_r",  "hip_l",           "knee_l",     "ankle_l",
        "foot_l",     "hip_r",   "knee_r",          "ankle_r",    "foot_r"};
    std::fputs("ms,slot,tracking_id,floor_a,floor_b,floor_c,floor_d", file);
    for (const char* joint : kJoints) {
      std::fprintf(file, ",%s_x,%s_y,%s_z,%s_state", joint, joint, joint,
                   joint);
    }
    std::fputc('\n', file);
    XELOGI("NuiSkeleton: writing {}", xe::path_to_utf8(path));
  }
  if (!file) {
    return;
  }
  auto u32 = [&](uint32_t o) { return xe::load_and_swap<uint32_t>(b + o); };
  auto f32 = [&](uint32_t o) { return xe::load_and_swap<float>(b + o); };
  const uint64_t ms = Clock::QueryHostUptimeMillis();
  for (uint32_t s = 0; s < 6; s++) {
    const uint32_t r = 0x30 + s * 0x1C0;
    if (u32(r) != 2) {
      continue;
    }
    // Joints (x, y, z in metres, camera space) at +0x20, their states
    // (0 not tracked, 1 inferred, 2 tracked) at +0x160.
    std::fprintf(file, "%llu,%u,%u,%.4f,%.4f,%.4f,%.4f",
                 static_cast<unsigned long long>(ms), s, u32(r + 4), f32(0x10),
                 f32(0x14), f32(0x18), f32(0x1C));
    for (uint32_t j = 0; j < 20; j++) {
      std::fprintf(file, ",%.4f,%.4f,%.4f,%u", f32(r + 0x20 + j * 16),
                   f32(r + 0x24 + j * 16), f32(r + 0x28 + j * 16),
                   u32(r + 0x160 + j * 4));
    }
    std::fputc('\n', file);
  }
  std::fflush(file);
}

void XamNuiIdentityOnFrame(uint32_t buffer_ptr) {
  if (!buffer_ptr) {
    return;
  }
  auto b =
      kernel_state()->memory()->TranslateVirtual<const uint8_t*>(buffer_ptr);
  auto u32 = [&](uint32_t o) { return xe::load_and_swap<uint32_t>(b + o); };
  if (u32(4) != 5) {  // frame version
    return;
  }
  if (cvars::nui_skeleton_log) {
    LogSkeletons(b);
  }
  const uint32_t base = 0x30;
  uint32_t id = 0, enrollment = ~0u, user = ~0u;
  uint32_t occupied = 0;  // bit N: a tracked player is in enrollment slot N
  for (uint32_t s = 0; s < 6; s++) {
    uint32_t r = base + s * 0x1C0;
    if (u32(r) != 2) {
      continue;
    }
    if (u32(r + 8) < 8) {
      occupied |= 1u << u32(r + 8);
    }
    if (!id) {
      id = u32(r + 4);
      enrollment = u32(r + 8);
      user = u32(r + 12);
    }
  }
  if (id != identity_last_[0] || enrollment != identity_last_[1] ||
      user != identity_last_[2]) {
    identity_last_[0] = id;
    identity_last_[1] = enrollment;
    identity_last_[2] = user;
    XELOGI("NuiIdentity: tracked player id {} enrollment {:X} user {:X}", id,
           enrollment, user);
  }

  std::unique_lock<std::mutex> lock(identity_mutex_);
  if (!identity_callback_ || !id) {
    return;
  }
  // Kinect ID: a player nobody knows yet becomes the first signed-in user no
  // other tracked player is. That includes a player who left and came back
  // under a new tracking id: the runtime's own identify op fails (-5) on them.
  // That op (enrollment -4) is cancelled by the enrollment below.
  if (cvars::nui_kinect_id && id != identity_auto_id_ && !identity_pending_ &&
      !identity_enroll_ms_ && enrollment >= 8 && enrollment != 0xFFFFFFFC) {
    for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
      if (!(occupied & (1u << i)) && UserSignedIn(i)) {
        identity_auto_id_ = id;
        identity_bound_ |= 1u << i;
        identity_pending_ |= 1u << i;
        identity_retries_ = 0;
        XELOGI("NuiIdentity: Kinect ID enrolls player {} as user {}", id, i);
        break;
      }
    }
  }
  if (identity_pending_) {
    // The runtime starts identifying a player as soon as it tracks one, and
    // an enrollment submitted over that op is lost when the op ends. Cancel
    // it (0x2C00E) and enroll on the next frame.
    static bool cancelled = false;
    if (enrollment == 0xFFFFFFFC && !cancelled) {
      cancelled = true;
      lock.unlock();
      CallIdentity(0x2C00E, {id});
      return;
    }
    cancelled = false;
    uint32_t slot = 0;
    while (!(identity_pending_ & (1u << slot))) {
      slot++;
    }
    identity_pending_ &= ~(1u << slot);
    identity_enroll_ms_ = Clock::QueryHostUptimeMillis();
    identity_enroll_slot_ = slot;
    lock.unlock();
    // Map the slot to its user first: an enrollment into a given slot needs
    // the slot to belong to someone.
    CallIdentity(0x2C00F, {});
    CallIdentity(0x2C006, {id, slot, 0x20});
    return;
  }
  // While an enrollment runs, keep the mapping fresh so the player's
  // dwUserIndex follows as soon as the runtime puts them in the slot.
  if (identity_enroll_ms_) {
    uint64_t now = Clock::QueryHostUptimeMillis();
    static uint64_t last_refresh = 0;
    bool done = enrollment < 8 && user < 4;
    // An op that ends in failure (-5, or -1: not tracked) leaves the player
    // unknown; submit it again, a few times.
    if (!done && (enrollment == 0xFFFFFFFB || enrollment == 0xFFFFFFFF) &&
        now - identity_enroll_ms_ > 500 && identity_retries_ < 5) {
      identity_retries_++;
      identity_enroll_ms_ = 0;
      identity_pending_ |= 1u << identity_enroll_slot_;
      XELOGI("NuiIdentity: enrollment failed ({:X}), retry {}", enrollment,
             identity_retries_);
      return;
    }
    if (done || now - identity_enroll_ms_ > 60000) {
      identity_enroll_ms_ = 0;
      XELOGI("NuiIdentity: enrollment {}", done ? "done" : "timed out");
    } else if (now - last_refresh >= 1000) {
      last_refresh = now;
      lock.unlock();
      CallIdentity(0x2C00F, {});
    }
  }
}

// The Kinect sign-in UI: the player in view becomes the signed-in user.
void XamNuiIdentitySignIn(uint32_t user_index) {
  std::lock_guard<std::mutex> lock(identity_mutex_);
  for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
    if ((user_index == i || user_index >= XUserMaxUserCount) &&
        UserSignedIn(i)) {
      if (identity_last_[1] == i && identity_last_[2] == i) {
        XELOGI("NuiIdentity: sign-in UI: the player is already user {}", i);
        break;
      }
      identity_retries_ = 0;
      identity_bound_ |= 1u << i;
      identity_pending_ |= 1u << i;
      XELOGI("NuiIdentity: sign-in UI binds slot {} to user {}{}", i, i,
             identity_callback_ ? "" : " (no callback registered)");
      break;
    }
  }
}

// Which user an enrollment slot belongs to.
dword_result_t XamUserNuiGetUserIndex_entry(dword_t enrollment_index,
                                            lpdword_t user_index) {
  uint32_t e = enrollment_index;
  bool bound;
  {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    bound = e < XUserMaxUserCount && (identity_bound_ & (1u << e));
  }
  if (!bound || !UserSignedIn(e)) {
    return X_E_NO_SUCH_USER;
  }
  if (user_index) {
    *user_index = e;
  }
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserNuiGetUserIndex, kNone, kImplemented);

dword_result_t XamUserNuiGetUserIndexForSignin_entry(lpdword_t index) {
  for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
    auto profile = kernel_state()->xam_state()->GetUserProfile(i);
    if (profile) {
      *index = i;
      return X_E_SUCCESS;
    }
  }

  return X_E_ACCESS_DENIED;
}
DECLARE_XAM_EXPORT1(XamUserNuiGetUserIndexForSignin, kNone, kImplemented);

dword_result_t XamUserNuiGetUserIndexForBind_entry(lpdword_t index) {
  return X_E_FAIL;
}
DECLARE_XAM_EXPORT1(XamUserNuiGetUserIndexForBind, kNone, kStub);

dword_result_t XamNuiGetDepthCalibration_entry(lpdword_t unk1) {
  /* Notes:
     - Possible returns X_STATUS_NO_SUCH_FILE, and 0x10000000
  */
  return X_STATUS_NO_SUCH_FILE;
}
DECLARE_XAM_EXPORT1(XamNuiGetDepthCalibration, kNone, kStub);

// Skeleton
qword_result_t XamNuiSkeletonGetBestSkeletonIndex_entry(int_t unk) {
  return 0xffffffffffffffff;
}
DECLARE_XAM_EXPORT1(XamNuiSkeletonGetBestSkeletonIndex, kNone, kStub);

/* XamNuiCamera Notes
   - most require message calls to xam in 0x0002Bxxx area
*/

// A virtual tilt motor. The host sensor is not moved by the title, so the
// motor here moves instantly and the picture never changes; what matters is
// that it answers. Kinect Adventures' auto-tilt polls
// XamNuiCameraTiltGetStatus and XamNuiCameraElevationGetAngle every frame and
// fails into error 0x1F ("launch the Kinect Tuner") whenever either fails,
// which also leaves the menus without a hand cursor.
//
// Tilt status block (0x50 bytes, the caller fills size and 'XtSs' magic):
//   +0x20 int32 accelerometer x, y, z. KA takes the angle as
//         atan2(z, y) in degrees and the gravity normal as (-x, y, z)/|v|.
//   +0x2C flags: bits 0-1 motor moving, bit 3 fault, bit 4 seen with
//         auto-tilt.
//   +0x30 the floor plane last given to XamNuiCameraRememberFloor (float4;
//         w == 0 means none, and KA then sweeps for the floor).
//   +0x40 bit 0: KA skips auto-tilt entirely.
namespace {
int32_t tilt_angle_ = 0;
uint8_t tilt_floor_[16] = {};
}  // namespace

static void FillTiltStatus(uint8_t* status, uint32_t size) {
  if (size > 8) {
    std::memset(status + 8, 0, std::min<uint32_t>(size, 0x50) - 8);
  }
  if (size < 0x40) {
    return;
  }
  const double a = tilt_angle_ * 3.14159265358979323846 / 180.0;
  // About 819 counts per g, like the Kinect 1 accelerometer.
  xe::store_and_swap<int32_t>(status + 0x20, 0);
  xe::store_and_swap<int32_t>(
      status + 0x24, static_cast<int32_t>(std::lround(819 * std::cos(a))));
  xe::store_and_swap<int32_t>(
      status + 0x28, static_cast<int32_t>(std::lround(819 * std::sin(a))));
  std::memcpy(status + 0x30, tilt_floor_, sizeof(tilt_floor_));
}

dword_result_t XamNuiCameraTiltGetStatus_entry(lpvoid_t status) {
  if (!status) {
    return X_E_INVALIDARG;
  }
  uint8_t* block = status.as<uint8_t*>();
  FillTiltStatus(block, xe::load_and_swap<uint32_t>(block));
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiCameraTiltGetStatus, kNone, kImplemented);

dword_result_t XamNuiCameraElevationGetAngle_entry(lpdword_t angle,
                                                   lpdword_t moving) {
  // KA reads the angle as an int32 from the first pointer.
  if (angle) {
    *angle = static_cast<uint32_t>(tilt_angle_);
  }
  if (moving) {
    *moving = 0;
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiCameraElevationGetAngle, kNone, kImplemented);

dword_result_t XamNuiCameraElevationSetAngle_entry(int_t angle) {
  // The Kinect 1 motor range; KA itself refuses anything past +-27.
  tilt_angle_ = std::clamp<int32_t>(angle, -27, 27);
  XELOGI("XamNuiCameraElevationSetAngle({}) -> virtual motor at {}",
         static_cast<int32_t>(angle), tilt_angle_);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiCameraElevationSetAngle, kNone, kImplemented);

dword_result_t XamNuiCameraElevationStopMovement_entry() {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiCameraElevationStopMovement, kNone, kImplemented);

dword_result_t XamNuiCameraRememberFloor_entry(qword_t plane_xy,
                                               qword_t plane_zw, dword_t unk) {
  // The floor plane arrives as two registers holding a big-endian float4.
  xe::store_and_swap<uint64_t>(tilt_floor_, plane_xy);
  xe::store_and_swap<uint64_t>(tilt_floor_ + 8, plane_zw);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiCameraRememberFloor, kNone, kImplemented);

dword_result_t XamNuiCameraTiltReportStatus_entry(dword_t message,
                                                  lpvoid_t data) {
  // Telemetry (KA sends 0x2B006 / 0x2B00C with the auto-tilt result).
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiCameraTiltReportStatus, kNone, kStub);

dword_result_t XamNuiCameraGetTiltControllerType_entry() {
  /* Notes:
     - calls DetroitDeviceRequest(unk) -> result
     - returns (ulonglong)(LZCOUNT(result) << 0x20) >> 0x25
  */
  return X_E_FAIL;
}
DECLARE_XAM_EXPORT1(XamNuiCameraGetTiltControllerType, kNone, kStub);

dword_result_t XamNuiCameraSetFlags_entry(qword_t unk1, dword_t unk2) {
  /* Notes:
     - if XamNuiCameraGetTiltControllerType returns 1 then operation is done
     - else 0xffffffff8007048f
  */
  return X_E_DEVICE_NOT_CONNECTED;
}
DECLARE_XAM_EXPORT1(XamNuiCameraSetFlags, kNone, kStub);

dword_result_t XamNuiCameraTiltSetCallback_entry(lpvoid_t callback,
                                                 lpvoid_t context) {
  // Registers a routine the tilt motor calls when it finishes moving. There
  // is no motor here, so it is never called.
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiCameraTiltSetCallback, kNone, kStub);

dword_result_t XamCacheReset_entry(dword_t unk) { return X_ERROR_SUCCESS; }
DECLARE_XAM_EXPORT1(XamCacheReset, kNone, kStub);

dword_result_t XamIsNuiUIActive_entry() {
  return kernel_state()->xam_state()->is_xam_dialog_present_.load();
}
DECLARE_XAM_EXPORT1(XamIsNuiUIActive, kNone, kImplemented);

dword_result_t XamNuiIsDeviceReady_entry() {
  /* device_state Notes:
   - used with XNotifyBroadcast(kXNotificationSystemNUIHardwareStatusChanged,
   device_state)
   - known values:
     - 0x0001
     - 0x0004
     - 0x0040
  */
  const bool kinect_initialized =
      kernel_state()->xconfig()->ReadSetting<uint32_t>(
          X_CONFIG_CATEGORY::XCONFIG_USER_CATEGORY, XCONFIG_USER_RETAIL_FLAGS) &
      X_RETAIL_FLAGS::KinectInitialized;

  return kinect_initialized >> 1 & 1;
}
DECLARE_XAM_EXPORT1(XamNuiIsDeviceReady, kNone, kImplemented);

dword_result_t XamIsNuiAutomationEnabled_entry(unknown_t unk1, unknown_t unk2) {
  /* Notes:
     - XamIsNuiAutomationEnabled = XamIsNatalPlaybackEnabled
     - Always returns X_E_SUCCESS? Maybe check later versions
     - Recieves param but never interacts with them
     - No Operations
  */
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT2(XamIsNuiAutomationEnabled, kNone, kStub, kHighFrequency);

dword_result_t XamIsNatalPlaybackEnabled_entry(unknown_t unk1, unknown_t unk2) {
  /* Notes:
     - XamIsNuiAutomationEnabled = XamIsNatalPlaybackEnabled
     - Always returns X_E_SUCCESS? Maybe check later versions
     - Recieves param but never interacts with them
     - No Operations
  */
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT2(XamIsNatalPlaybackEnabled, kNone, kStub, kHighFrequency);

dword_result_t XamNuiIsChatMicEnabled_entry() {
  /* Notes:
     - calls a second function with a param of uint local_20 [4];
     - Second function calls ExGetXConfigSetting(7,9,local_30,0x1c,local_40);
     - Result is sent to *local_20[0] = ^
     - Once sent back to XamNuiIsChatMicEnabled it looks for byte that
     correlates to NUI mic setting
     - return uVar2 = (~(ulonglong)local_20[0] << 0x20) >> 0x23 & 1;
     - unless the second function returns something -1 or less then
     XamNuiIsChatMicEnabled 1
  */
  return false;
}
DECLARE_XAM_EXPORT1(XamNuiIsChatMicEnabled, kNone, kImplemented);

/* HUD Notes:
   - XamNuiHudGetEngagedTrackingID, XamNuiHudIsEnabled,
   XamNuiHudSetEngagedTrackingID, XamNuiHudInterpretFrame, and
   XamNuiHudGetEngagedEnrollmentIndex all utilize the same data address
   - engaged_tracking_id set second param of XamShowNuiTroubleshooterUI
*/
uint32_t nui_unknown_1 = 0;
uint32_t engaged_tracking_id = 0;
char nui_unknown_2 = '\0';

dword_result_t XamNuiHudSetEngagedTrackingID_entry(dword_t id) {
  if (!id) {
    return X_STATUS_SUCCESS;
  }

  if (nui_unknown_1 != 0) {
    engaged_tracking_id = id;
    return X_STATUS_SUCCESS;
  }

  return X_E_FAIL;
}
DECLARE_XAM_EXPORT1(XamNuiHudSetEngagedTrackingID, kNone, kImplemented);

qword_result_t XamNuiHudGetEngagedTrackingID_entry() {
  if (nui_unknown_1 != 0) {
    return engaged_tracking_id;
  }

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamNuiHudGetEngagedTrackingID, kNone, kImplemented);

dword_result_t XamNuiHudIsEnabled_entry() {
  /* Notes:
     - checks if XamNuiIsDeviceReady false, if nui_unknown_1 exists, and
     nui_unknown_2 is equal to null terminated string
     - only returns true if one check fails and allows for other NUI functions
     to progress
  */
  bool result = XamNuiIsDeviceReady_entry();
  if (nui_unknown_1 != 0 && nui_unknown_2 != '\0' && result) {
    return true;
  }
  return false;
}
DECLARE_XAM_EXPORT1(XamNuiHudIsEnabled, kNone, kImplemented);

uint32_t XeXamNuiHudCheck(dword_t unk1) {
  uint32_t check = XamNuiHudIsEnabled_entry();
  if (check == 0) {
    return X_ERROR_ACCESS_DENIED;
  }

  check = XamNuiHudSetEngagedTrackingID_entry(unk1);
  if (check != 0) {
    return X_ERROR_FUNCTION_FAILED;
  }
  return X_STATUS_SUCCESS;
}

dword_result_t XamNuiHudGetInitializeFlags_entry() {
  /* HUD_Flags Notes:
     - set by 0x2B003
     - set to 0 by unnamed func alongside version_id
     - known values:
       - 0x40000000
       - 0x200
  */
  return 0;
}
DECLARE_XAM_EXPORT1(XamNuiHudGetInitializeFlags, kNone, kImplemented);

void XamNuiHudGetVersions_entry(lpqword_t unk1, lpqword_t unk2) {
  /* version_id Notes:
     - set by 0x2B003
     - set to 0 by unnamed func alongside HUD_Flags
  */
  if (unk1) {
    *unk1 = 0;
  }
  if (unk2) {
    *unk2 = 0;
  }
}
DECLARE_XAM_EXPORT1(XamNuiHudGetVersions, kNone, kImplemented);

// UI
dword_result_t XamShowNuiTroubleshooterUI_entry(dword_t user_index,
                                                dword_t tracking_id,
                                                dword_t flags) {
  /* Notes:
     - calls XamPackageManagerGetExperienceMode(&var) with var = 1
     - If returns less than zero or (var & 1) == 0 then get error message:
       - if XamPackageManagerGetExperienceMode = 0 then call XamShowMessageBoxUI
         - if XamShowMessageBoxUI returns 0x3e5 then XamShowNuiTroubleshooterUI
     returns 0
       - else XamShowNuiTroubleshooterUI returns 0x65b and call another func
     - else:
       - call XamNuiHudSetEngagedTrackingID(tracking_id) and doesn't care aboot
     return and set var2 = 2
       - checks if (flag & 0x800000) == 0
         - if true call XamNuiGetDeviceStatus.
           - if XamNuiGetDeviceStatus != 0 set var2 = 3
       - else var2 = 4
       - XamAppRequestLoadEx(var2);
       - if return = 0 then XamShowNuiTroubleshooterUI returns 5
       - else set buffer[8] and call
     XMsgSystemProcessCall(0xfe,0x21028,buffer,0xc);
     - XamNuiNatalCameraUpdateComplete calls
     XamShowNuiTroubleshooterUI(0xff,0,0) if param = -0x7ff8fffe
  */

  // The Kinect window (HID > Kinect...) stands in for the system's Kinect
  // Tuner: it shows what the Kinect sees and says what is wrong, if anything.
  if (!cvars::headless) {
    kernel_state()->emulator()->on_kinect_ui_requested();
  }
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowNuiTroubleshooterUI, kNone, kStub);

dword_result_t XamShowNuiHardwareRequiredUI_entry(unknown_t unk1) {
  if (unk1 != 0) {
    return X_ERROR_INVALID_PARAMETER;
  }

  return XamShowNuiTroubleshooterUI_entry(0xff, 0, 0x400000);
}
DECLARE_XAM_EXPORT1(XamShowNuiHardwareRequiredUI, kNone, kImplemented);

dword_result_t XamShowNuiGuideUI_entry(unknown_t unk1, unknown_t unk2) {
  /* Notes:
   - calls an unnamed function that checks XamNuiHudIsEnabled and
   XamNuiHudSetEngagedTrackingID
     - if XamNuiHudIsEnabled returns false then fuctions fails return
   X_ERROR_ACCESS_DENIED
     - else calls XamNuiHudSetEngagedTrackingID and if returns less than 0 then
   returns X_ERROR_FUNCTION_FAILED
     - else return X_ERROR_SUCCESS
   - if return offunc is X_ERROR_SUCCESS then call up ui screen
   - else return value of func
  */

  // decompiler error stops me from knowing which param gets used here
  uint32_t result = XeXamNuiHudCheck(0);
  if (!result) {
    // operations here
    // XMsgSystemProcessCall(0xfe,0x21030, undefined local_30[8] ,0xc);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamShowNuiGuideUI, kNone, kStub);

/* XamNuiIdentity Notes:
   - most require message calls to xam in 0x0002Cxxx area
*/
uint64_t NUI_Session_Id = 0;

qword_result_t XamNuiIdentityGetSessionId_entry() {
  if (NUI_Session_Id == 0) {
    // xboxkrnl::XeCryptRandom_entry(NUI_Session_Id, 8);
    NUI_Session_Id = 0xDEADF00DDEADF00D;
  }
  return NUI_Session_Id;
}
DECLARE_XAM_EXPORT1(XamNuiIdentityGetSessionId, kNone, kImplemented);

dword_result_t XamNuiIdentityEnrollForSignIn_entry(dword_t unk1, qword_t unk2,
                                                   qword_t unk3, dword_t unk4) {
  /* Notes:
     - Decompiler issues so double check
  */
  if (XamNuiHudIsEnabled_entry() == false) {
    return X_E_FAIL;
  }
  // buffer [2]
  // buffer[0] = unk
  // var = unk4
  // return func(0xfe,0x2c010,buffer,0xc,unk3);
  return X_E_FAIL;
}
DECLARE_XAM_EXPORT1(XamNuiIdentityEnrollForSignIn, kNone, kStub);

dword_result_t XamNuiIdentityAbort_entry(dword_t unk) {
  if (XamNuiHudIsEnabled_entry() == false) {
    return X_E_FAIL;
  }
  // buffer [4]
  // buffer[0] = unk
  // return func(0xfe,0x2c00e,buffer,4,0)
  return X_E_FAIL;
}
DECLARE_XAM_EXPORT1(XamNuiIdentityAbort, kNone, kStub);

// Other
// Biometric (Kinect ID) sign-in on or off for a profile: on binds the user's
// enrollment slot.
dword_result_t XamUserNuiEnableBiometric_entry(dword_t user_index,
                                               int_t enable) {
  XELOGI("XamUserNuiEnableBiometric(user {}, {})",
         static_cast<uint32_t>(user_index), static_cast<int32_t>(enable));
  if (user_index >= XUserMaxUserCount ||
      !kernel_state()->xam_state()->GetUserProfile(user_index)) {
    return X_E_INVALIDARG;
  }
  std::lock_guard<std::mutex> lock(identity_mutex_);
  if (enable) {
    identity_bound_ |= 1u << user_index;
  } else {
    identity_bound_ &= ~(1u << user_index);
  }
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserNuiEnableBiometric, kNone, kSketchy);

// The Kinect enrollment slot of a signed-in user, once bound: its own index.
dword_result_t XamUserNuiGetEnrollmentIndex_entry(dword_t user_index,
                                                  lpdword_t enrollment_index) {
  XELOGI("XamUserNuiGetEnrollmentIndex(user {})",
         static_cast<uint32_t>(user_index));
  if (!UserSignedIn(user_index)) {
    return X_E_NO_SUCH_USER;
  }
  {
    std::lock_guard<std::mutex> lock(identity_mutex_);
    if (!(identity_bound_ & (1u << user_index))) {
      return X_E_NO_SUCH_USER;
    }
  }
  if (enrollment_index) {
    *enrollment_index = static_cast<uint32_t>(user_index);
  }
  return X_E_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamUserNuiGetEnrollmentIndex, kNone, kSketchy);

// Stored biometric templates per XUID: there are none to read, and a written
// one is accepted and dropped (enrollment repeats each session).
dword_result_t XamReadBiometricData_entry(qword_t xuid, lpdword_t version,
                                          lpvoid_t buffer, lpdword_t size,
                                          lpvoid_t overlapped) {
  return X_ERROR_NOT_FOUND;
}
DECLARE_XAM_EXPORT1(XamReadBiometricData, kNone, kStub);

dword_result_t XamWriteBiometricData_entry(qword_t xuid, lpdword_t version,
                                           lpvoid_t buffer, lpdword_t size,
                                           lpvoid_t overlapped) {
  XELOGI("XamWriteBiometricData({:016X})", static_cast<uint64_t>(xuid));
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamWriteBiometricData, kNone, kStub);

dword_result_t XamCacheOpenFile_entry(lpvoid_t params) {
  return X_ERROR_FILE_NOT_FOUND;
}
DECLARE_XAM_EXPORT1(XamCacheOpenFile, kNone, kStub);

dword_result_t XamCacheCloseFile_entry(lpvoid_t params, dword_t unk) {
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamCacheCloseFile, kNone, kStub);

void XamNuiPlayerEngagementUpdate_entry(qword_t unk1, unknown_t unk2,
                                        lpunknown_t unk3) {
  /* Notes:
     - Only calls a second function with the params unk3, 0, and 0x1c in that
     order
  */
}
DECLARE_XAM_EXPORT1(XamNuiPlayerEngagementUpdate, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(NUI);
