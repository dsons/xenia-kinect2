/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/kinect_dialog.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "xenia/app/emulator_window.h"
#include "xenia/base/cvar.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/config.h"
#include "xenia/emulator.h"
#include "xenia/kernel/xboxkrnl/nui_kinect.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

DECLARE_bool(nui_window_on_start);
DECLARE_string(nui_camera);
DECLARE_int32(nui_kinect2_pan);
DECLARE_double(nui_kinect2_zoom);
DECLARE_bool(nui_mic);
DECLARE_string(nui_mic_device);

namespace xe {
namespace app {

using kernel::xboxkrnl::KinectCamera;
using kernel::xboxkrnl::KinectMicrophones;
using kernel::xboxkrnl::KinectSensorKind;
using kernel::xboxkrnl::KinectSensorName;

namespace {

constexpr uint32_t kWidth = KinectCamera::kWidth;
constexpr uint32_t kHeight = KinectCamera::kHeight;
constexpr uint32_t kPreviewWidth = kWidth / 2;
constexpr uint32_t kPreviewHeight = kHeight / 2;

struct Mode {
  const char* cvar;
  const char* label;
};
constexpr Mode kModes[] = {
    {"auto", "Automatic"},
    {"kinect1", "Kinect for Xbox 360"},
    {"kinect2", "Kinect for Xbox One"},
    {"synthetic", "Test figure (no sensor)"},
    {"none", "Off"},
};

bool IsSensorMode(const std::string& mode) {
  return mode == "auto" || mode == "kinect1" || mode == "kinect2";
}

const ImVec4 kGood(0.45f, 0.85f, 0.45f, 1.0f);
const ImVec4 kWarn(0.95f, 0.75f, 0.30f, 1.0f);

// Changes a setting so that config::SaveConfig() writes it: the file is
// written from each setting's config value, not from the variable.
template <typename T>
void SetSetting(const char* name, T value) {
  auto it = cvar::ConfigVars->find(name);
  if (it == cvar::ConfigVars->end()) {
    return;
  }
  if (auto var = dynamic_cast<cvar::ConfigVar<T>*>(it->second)) {
    var->OverrideConfigValue(value);
  }
}

}  // namespace

KinectDialog::KinectDialog(ui::ImGuiDrawer* imgui_drawer,
                           EmulatorWindow& emulator_window)
    : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
  depth_.resize(kWidth * kHeight);
  rgb_.resize(kWidth * kHeight * 3);
  pixels_.resize(kPreviewWidth * kPreviewHeight * 4);
  pan_ = cvars::nui_kinect2_pan;
  zoom_ = float(cvars::nui_kinect2_zoom);
  microphones_ = KinectMicrophones::ListDevices();
  StartSensor(false);
  if (cvars::nui_mic) {
    KinectMicrophones::Get()->Start();
  }
}

KinectDialog::~KinectDialog() {
  if (pending_start_.valid()) {
    pending_start_.wait();
  }
#if XE_PLATFORM_WIN32
  if (setup_process_) {
    CloseHandle(setup_process_);
  }
#endif
  // Without a game the sensor was only on for this window.
  if (!TitleRunning()) {
    KinectCamera::Get()->Stop();
    KinectMicrophones::Get()->Stop();
  }
}

bool KinectDialog::TitleRunning() const {
  return emulator_window_.emulator()->is_title_open();
}

void KinectDialog::StartSensor(bool restart) {
  if (pending_start_.valid()) {
    return;
  }
  const std::string mode = cvars::nui_camera;
  if (!IsSensorMode(mode)) {
    KinectCamera::Get()->Stop();
    return;
  }
  // Opening a sensor takes a moment; keep the window responsive meanwhile.
  pending_start_ = std::async(std::launch::async, [mode, restart]() {
    return restart ? KinectCamera::Get()->Restart(mode)
                   : KinectCamera::Get()->Start(mode);
  });
  started_here_ = true;
}

void KinectDialog::UpdatePreview() {
  if (ImGui::GetTime() < next_preview_time_) {
    return;
  }
  next_preview_time_ = ImGui::GetTime() + 0.1;
  if (!KinectCamera::Get()->Read(depth_.data(), rgb_.data(), &frame_)) {
    return;
  }
  // Shown like a mirror, as players expect to see themselves.
  auto source_x = [](uint32_t x) { return kWidth - 1 - x * 2; };
  for (uint32_t y = 0; y < kPreviewHeight; ++y) {
    for (uint32_t x = 0; x < kPreviewWidth; ++x) {
      const uint8_t* p = &rgb_[((y * 2) * kWidth + source_x(x)) * 3];
      uint8_t* o = &pixels_[(y * kPreviewWidth + x) * 4];
      o[0] = p[0];
      o[1] = p[1];
      o[2] = p[2];
      o[3] = 255;
    }
  }
  color_texture_ = imgui_drawer()->CreateImGuiTexture(
      kPreviewWidth, kPreviewHeight, pixels_.data());

  // Depth: too close red, a good playing distance green, farther blue,
  // nothing measured black. Brighter is nearer.
  for (uint32_t y = 0; y < kPreviewHeight; ++y) {
    for (uint32_t x = 0; x < kPreviewWidth; ++x) {
      const uint16_t d = depth_[(y * 2) * kWidth + source_x(x)];
      uint8_t* o = &pixels_[(y * kPreviewWidth + x) * 4];
      if (!d) {
        o[0] = o[1] = o[2] = 0;
      } else {
        const float shade =
            0.35f +
            0.65f * (1.0f - std::clamp((d - 500.0f) / 4000.0f, 0.0f, 1.0f));
        float r = 0.3f, g = 0.4f, b = 1.0f;
        if (d < 1500) {
          r = 1.0f, g = 0.35f, b = 0.3f;
        } else if (d <= 3200) {
          r = 0.35f, g = 1.0f, b = 0.4f;
        }
        o[0] = uint8_t(255 * r * shade);
        o[1] = uint8_t(255 * g * shade);
        o[2] = uint8_t(255 * b * shade);
      }
      o[3] = 255;
    }
  }
  depth_texture_ = imgui_drawer()->CreateImGuiTexture(
      kPreviewWidth, kPreviewHeight, pixels_.data());

  // Distance of whatever is nearest in the middle of the picture: the
  // player, when they stand there.
  std::vector<uint16_t> middle;
  for (uint32_t y = kHeight / 5; y < kHeight * 4 / 5; y += 4) {
    for (uint32_t x = kWidth / 3; x < kWidth * 2 / 3; x += 4) {
      const uint16_t d = depth_[y * kWidth + x];
      if (d) {
        middle.push_back(d);
      }
    }
  }
  if (middle.size() > 50) {
    auto nth = middle.begin() + middle.size() / 10;
    std::nth_element(middle.begin(), nth, middle.end());
    center_distance_m_ = *nth / 1000.0f;
  } else {
    center_distance_m_ = 0.0f;
  }
  // Show a framing result only once it has held for a second, so the
  // message does not flicker while the player walks into place.
  const Framing seen = CheckFraming(center_distance_m_);
  if (seen != framing_seen_) {
    framing_seen_ = seen;
    framing_seen_since_ = ImGui::GetTime();
  } else if (ImGui::GetTime() - framing_seen_since_ >= 1.0) {
    framing_ = seen;
  }
}

void KinectDialog::DrawSensor() {
  const std::string mode = cvars::nui_camera;
  const char* preview = "Automatic";
  for (const auto& m : kModes) {
    if (mode == m.cvar) {
      preview = m.label;
    }
  }
  ImGui::SetNextItemWidth(260.0f);
  if (ImGui::BeginCombo("Sensor", preview)) {
    for (const auto& m : kModes) {
      const bool selected = mode == m.cvar;
      if (ImGui::Selectable(m.label, selected) && !selected) {
        XELOGI("Kinect window: sensor set to {}", m.cvar);
        SetSetting("nui_camera", std::string(m.cvar));
        Save();
        StartSensor(true);
      }
    }
    ImGui::EndCombo();
  }

  const bool starting = pending_start_.valid() &&
                        pending_start_.wait_for(std::chrono::seconds(0)) !=
                            std::future_status::ready;
  if (pending_start_.valid() && !starting) {
    pending_start_.get();
  }
  auto status = KinectCamera::Get()->GetStatus();
  // Works means frames arrive, not only that the sensor opened.
  const bool working = status.streaming && status.frames_per_second > 0.0f;
  if (mode == "synthetic") {
    ImGui::TextColored(kGood, "Games see a moving test figure.");
  } else if (mode == "none") {
    ImGui::TextColored(kWarn, "Kinect is off. Games will ask for a Kinect.");
  } else if (starting) {
    ImGui::Text("Looking for a Kinect...");
  } else if (working) {
    ImGui::TextColored(kGood, "%s connected, %.0f frames a second.",
                       KinectSensorName(status.kind), status.frames_per_second);
  } else if (status.streaming) {
    ImGui::TextColored(kWarn, "%s found. Waiting for its picture...",
                       KinectSensorName(status.kind));
  }

  if (IsSensorMode(mode)) {
    DrawSetup(starting, status.streaming);
  }
}

void KinectDialog::DrawSetup(bool starting, bool streaming) {
  PollDriverSetup();
  DetectHardware(starting, streaming);
  const bool installing = setup_process_ != nullptr;
  bool offer_setup = false;
  if (installing) {
    ImGui::TextColored(kWarn,
                       "Setting up the Kinect driver. If Windows asks "
                       "for permission, click Yes.");
  } else if (!starting && !streaming) {
#if XE_PLATFORM_WIN32
    if (hardware_.camera && hardware_.needs_driver) {
      ImGui::TextColored(kWarn,
                         "Kinect found. Its driver needs to be set up once "
                         "on this PC.");
      offer_setup = true;
    } else if (!hardware_.camera && hardware_.motor) {
      ImGui::TextColored(kWarn,
                         "Kinect for Xbox 360 found, but its camera is off. "
                         "Plug its power supply into the wall.");
    } else if (!hardware_.camera) {
      ImGui::TextColored(kWarn,
                         "No Kinect found. Plug the Kinect's adapter into "
                         "the wall and into a USB port of this PC.");
    } else {
      ImGui::TextColored(kWarn, "%s",
                         KinectCamera::Get()->GetStatus().message.c_str());
    }
#else
    ImGui::TextColored(kWarn, "%s",
                       KinectCamera::Get()->GetStatus().message.c_str());
#endif
  }
  if (!setup_result_.empty() && !installing && !streaming) {
    ImGui::TextColored(setup_failed_ ? kWarn : kGood, "%s",
                       setup_result_.c_str());
  }

#if XE_PLATFORM_WIN32
  if (offer_setup) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.2f, 1.0f));
    if (ImGui::Button("Set up Kinect driver", ImVec2(260, 0))) {
      InstallDriver();
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Switches the Kinect to the WinUSB driver that comes with "
          "Windows.\nNothing is downloaded. Needed once per PC.");
    }
  }
#endif
  ImGui::BeginDisabled(starting || installing);
  if (ImGui::Button("Retry")) {
    XELOGI("Kinect window: Retry");
    next_detect_time_ = 0.0;
    StartSensor(true);
  }
#if XE_PLATFORM_WIN32
  if (!offer_setup) {
    ImGui::SameLine();
    if (ImGui::Button("Set up driver again")) {
      InstallDriver();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Sets up the Kinect's driver again, for example after\n"
          "plugging the Kinect into a different USB port.");
    }
  }
#endif
  ImGui::EndDisabled();
}

void KinectDialog::DetectHardware(bool starting, bool streaming) {
  if (auto_start_time_ > 0.0 && ImGui::GetTime() >= auto_start_time_ &&
      !starting) {
    auto_start_time_ = 0.0;
    if (!streaming) {
      XELOGI("Kinect window: opening the sensor");
      StartSensor(true);
    }
  }
  if (streaming || ImGui::GetTime() < next_detect_time_) {
    return;
  }
  next_detect_time_ = ImGui::GetTime() + 2.0;
  const auto found = kernel::xboxkrnl::DetectKinectHardware();
  if (!hardware_checked_ || found.camera != hardware_.camera ||
      found.motor != hardware_.motor ||
      found.needs_driver != hardware_.needs_driver) {
    XELOGI("Kinect window: camera {}, motor {}, driver {}",
           found.camera ? "found" : "not found",
           found.motor ? "found" : "not found",
           found.needs_driver ? "needs setting up" : "ready");
    if (hardware_checked_) {
      kernel::xboxkrnl::LogKinectDevices();
    }
  }
  // Try the sensor again when it becomes usable (plugged in, or its driver
  // set up), not on every check.
  const bool was_ready = hardware_.camera && !hardware_.needs_driver;
  const bool ready = found.camera && !found.needs_driver;
  if (hardware_checked_ && ready && !was_ready) {
    // Windows restarts a device whose driver changed: give it a moment.
    auto_start_time_ = ImGui::GetTime() + 3.0;
  }
  hardware_ = found;
  hardware_checked_ = true;
}

// Follows the player, the nearest thing in the middle of the picture, up and
// down a band of columns around them. A row belongs to the player when the
// band is mostly at their distance and the columns beside it are not; the
// floor at their feet is at their distance across the whole width. Below the
// legs there should be floor: something much nearer is in front of the legs,
// and legs that reach the picture's edge (or its part without depth) are cut
// off. Dance games compare the whole body, so they need the feet.
KinectDialog::Framing KinectDialog::CheckFraming(float distance_m) const {
  if (distance_m < 0.8f || distance_m > 4.5f) {
    return Framing::kUnknown;
  }
  const int d0 = int(distance_m * 1000.0f);
  const float px_per_m = 525.0f / distance_m;
  // Where the player is across the picture, from their torso rows.
  double sum_x = 0.0;
  int count = 0;
  for (uint32_t y = kHeight / 3; y < kHeight * 2 / 3; y += 4) {
    for (uint32_t x = 0; x < kWidth; x += 4) {
      const int d = depth_[y * kWidth + x];
      if (d && std::abs(d - d0) < 400) {
        sum_x += x;
        ++count;
      }
    }
  }
  if (count < 40) {
    return Framing::kUnknown;
  }
  const int cx = int(sum_x / count);
  const int inner = std::max(6, int(0.22f * px_per_m));
  const int gap = std::max(4, int(0.18f * px_per_m));
  const int outer = std::max(8, int(0.35f * px_per_m));
  struct Row {
    float inside = 0.0f;   // share of the band at the player's distance
    float outside = 0.0f;  // the same beside the band
    float valid = 0.0f;    // share of the band with any depth
    float nearer = 0.0f;   // share of the band well in front of the player
  };
  auto row = [&](int y) {
    Row r;
    int in_n = 0, out_n = 0;
    for (int x = cx - inner; x <= cx + inner; x += 2) {
      if (x < 0 || x >= int(kWidth)) {
        continue;
      }
      const int d = depth_[y * kWidth + x];
      ++in_n;
      if (d) {
        r.valid += 1.0f;
        if (std::abs(d - d0) < 450) {
          r.inside += 1.0f;
        } else if (d < d0 - 450) {
          r.nearer += 1.0f;
        }
      }
    }
    for (int side : {-1, 1}) {
      for (int k = gap; k <= gap + outer; k += 2) {
        const int x = cx + side * (inner + k);
        if (x < 0 || x >= int(kWidth)) {
          continue;
        }
        const int d = depth_[y * kWidth + x];
        ++out_n;
        if (d && std::abs(d - d0) < 450) {
          r.outside += 1.0f;
        }
      }
    }
    if (in_n) {
      r.inside /= in_n;
      r.valid /= in_n;
      r.nearer /= in_n;
    }
    if (out_n) {
      r.outside /= out_n;
    }
    return r;
  };
  auto is_player = [](const Row& r) {
    return r.inside > 0.3f && r.outside < 0.5f;
  };
  // Down from the torso to the feet.
  Framing below = Framing::kFeetCut;
  int misses = 0;
  for (int y = int(kHeight) / 2; y < int(kHeight); y += 2) {
    const Row r = row(y);
    if (is_player(r)) {
      misses = 0;
      continue;
    }
    if (r.inside > 0.3f && r.outside >= 0.5f) {
      below = Framing::kGood;  // the floor at their feet
      break;
    }
    if (r.nearer > 0.4f) {
      below = Framing::kLegsBlocked;  // a table, a chair, a couch arm
      break;
    }
    if (r.valid < 0.3f) {
      below = Framing::kFeetCut;  // the edge of what the Kinect sees
      break;
    }
    if (++misses > 12) {
      below = Framing::kGood;  // legs ended: the gap between them, or feet
      break;
    }
  }
  if (below != Framing::kGood) {
    return below;
  }
  // Up from the torso: the head should end below the top of the picture.
  for (int y = int(kHeight) / 2; y >= 0; y -= 2) {
    const Row r = row(y);
    if (is_player(r)) {
      if (y <= 6) {
        return Framing::kHeadCut;
      }
      continue;
    }
    if (r.valid < 0.3f && r.inside == 0.0f && y > 0) {
      // Rows without depth above a player who is still there: the edge.
      const Row next = row(std::max(0, y - 2));
      if (next.valid < 0.3f) {
        return y < int(kHeight) / 6 ? Framing::kHeadCut : Framing::kGood;
      }
    }
    if (r.inside < 0.1f) {
      return Framing::kGood;
    }
  }
  return Framing::kGood;
}

void KinectDialog::DrawPreview() {
  UpdatePreview();
  const auto status = KinectCamera::Get()->GetStatus();
  if (!status.streaming || !color_texture_ || !depth_texture_) {
    return;
  }
  ImGui::Separator();
  const ImVec2 size{float(kPreviewWidth), float(kPreviewHeight)};
  ImGui::Image(reinterpret_cast<ImTextureID>(color_texture_.get()), size);
  ImGui::SameLine();
  ImGui::Image(reinterpret_cast<ImTextureID>(depth_texture_.get()), size);
  DrawCalibration();
}

void KinectDialog::DrawCalibration() {
  if (!calibrating_) {
    if (ImGui::Button("Calibrate")) {
      calibrating_ = true;
      good_since_ = -1.0;
      XELOGI("Kinect window: calibration started");
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(
        "Check where you stand (do this if a game does not "
        "see you or take your moves).");
    return;
  }
  ImGui::TextUnformatted(
      "Stand where you will play, facing the Kinect, with your whole body in "
      "the picture, head to feet.");
  if (center_distance_m_ > 0.0f) {
    const bool good = center_distance_m_ >= 1.5f && center_distance_m_ <= 3.2f;
    ImGui::TextColored(good ? kGood : kWarn,
                       "Nearest in the middle: %.1f m (%.0f ft). %s",
                       center_distance_m_, center_distance_m_ * 3.28f,
                       center_distance_m_ < 1.5f   ? "Step back."
                       : center_distance_m_ > 3.2f ? "Come closer."
                                                   : "Good distance.");
  }
  switch (framing_) {
    case Framing::kGood:
      ImGui::TextColored(kGood, "Whole body in view.");
      break;
    case Framing::kFeetCut:
      ImGui::TextColored(kWarn,
                         "Your feet are out of the picture. Tilt the Kinect "
                         "down a little, or stand further back.");
      break;
    case Framing::kHeadCut:
      ImGui::TextColored(kWarn,
                         "Your head is out of the picture. Tilt the Kinect up "
                         "a little, or stand further back.");
      break;
    case Framing::kLegsBlocked:
      ImGui::TextColored(kWarn,
                         "Something is in front of your legs. Move it, or "
                         "move the Kinect so it sees your feet.");
      break;
    default:
      break;
  }
  // A good position: the whole body in view at a good distance, held 2 s.
  const bool good = framing_ == Framing::kGood && center_distance_m_ >= 1.5f &&
                    center_distance_m_ <= 3.2f;
  if (!good) {
    good_since_ = -1.0;
  } else if (good_since_ < 0.0) {
    good_since_ = ImGui::GetTime();
  }
  if (ImGui::Button("Cancel")) {
    calibrating_ = false;
    return;
  }
  if (good && ImGui::GetTime() - good_since_ >= 2.0) {
    ImGui::OpenPopup("Good position");
  }
  if (ImGui::BeginPopupModal("Good position", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextColored(kGood, "Good position. You're ready to play!");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::TextUnformatted("Play from this spot.");
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      XELOGI("Kinect window: calibration done, good position");
      calibrating_ = false;
      close_requested_ = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void KinectDialog::DrawView() {
  const auto status = KinectCamera::Get()->GetStatus();
  if (status.kind != KinectSensorKind::kKinect2) {
    return;
  }
  ImGui::Separator();
  ImGui::TextUnformatted("Kinect for Xbox One view");
  ImGui::SetNextItemWidth(260.0f);
  if (ImGui::SliderFloat("Step back", &zoom_, 1.0f, 1.6f, "%.2fx")) {
    KinectCamera::Get()->SetView(pan_, zoom_);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    SetSetting("nui_kinect2_zoom", double(zoom_));
    Save();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Games see you this many times farther away than you stand.\n"
        "Raise it in a small room or if your hands leave the picture.");
  }
  ImGui::SetNextItemWidth(260.0f);
  if (ImGui::SliderInt("Shift sideways", &pan_, -40, 40)) {
    KinectCamera::Get()->SetView(pan_, zoom_);
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    SetSetting("nui_kinect2_pan", int32_t(pan_));
    Save();
  }
  if (ImGui::Button("Reset view")) {
    pan_ = 0;
    zoom_ = 1.3f;
    KinectCamera::Get()->SetView(pan_, zoom_);
    SetSetting("nui_kinect2_pan", int32_t(pan_));
    SetSetting("nui_kinect2_zoom", double(zoom_));
    Save();
  }
}

void KinectDialog::DrawMicrophone() {
  ImGui::Separator();
  bool enabled = cvars::nui_mic;
  if (ImGui::Checkbox("Kinect microphone", &enabled)) {
    SetSetting("nui_mic", enabled);
    Save();
    if (enabled) {
      KinectMicrophones::Get()->Restart();
    } else {
      KinectMicrophones::Get()->Stop();
    }
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Some games listen for voice commands.");
  }
  if (!enabled) {
    return;
  }
  auto* mics = KinectMicrophones::Get();
  const std::string current = mics->device_name();
  ImGui::SetNextItemWidth(260.0f);
  if (ImGui::BeginCombo("Microphone", current.empty()
                                          ? cvars::nui_mic_device.c_str()
                                          : current.c_str())) {
    for (const auto& name : microphones_) {
      if (ImGui::Selectable(name.c_str(), name == current)) {
        SetSetting("nui_mic_device", name);
        Save();
        mics->Restart();
      }
    }
    ImGui::EndCombo();
  }
  if (current.empty()) {
    ImGui::TextColored(kWarn,
                       "Microphone not found. A Kinect for Xbox 360's "
                       "microphone cannot be used; choose another one.");
  } else {
    ImGui::ProgressBar(std::min(1.0f, mics->level() * 2.0f), ImVec2(260, 0),
                       "Level");
    ImGui::SameLine();
    ImGui::TextUnformatted("Speak to test");
  }
}

void KinectDialog::InstallDriver() {
#if XE_PLATFORM_WIN32
  if (setup_process_) {
    return;
  }
  const auto script =
      xe::filesystem::GetExecutableFolder() / "kinect" / "Setup-Kinect.cmd";
  const auto folder = script.parent_path();
  SHELLEXECUTEINFOW info = {};
  info.cbSize = sizeof(info);
  info.fMask = SEE_MASK_NOCLOSEPROCESS;
  info.lpVerb = L"open";
  info.lpFile = script.c_str();
  info.lpParameters = L"-NoPause";
  info.lpDirectory = folder.c_str();
  info.nShow = SW_SHOWNORMAL;
  setup_result_.clear();
  if (ShellExecuteExW(&info) && info.hProcess) {
    setup_process_ = info.hProcess;
    XELOGI(
        "Kinect window: driver setup started (log: kinect\\Setup-Kinect.log)");
  } else {
    XELOGW("Kinect window: driver setup could not be started (error {})",
           GetLastError());
    setup_failed_ = true;
    setup_result_ =
        "The driver setup could not be started. Run Setup-Kinect.cmd in "
        "the kinect folder.";
  }
#endif
}

void KinectDialog::PollDriverSetup() {
#if XE_PLATFORM_WIN32
  if (!setup_process_ ||
      WaitForSingleObject(setup_process_, 0) != WAIT_OBJECT_0) {
    return;
  }
  DWORD code = 1;
  GetExitCodeProcess(setup_process_, &code);
  CloseHandle(setup_process_);
  setup_process_ = nullptr;
  XELOGI("Kinect window: driver setup finished with exit code {}", code);
  // Exit codes of Setup-Kinect.ps1.
  setup_failed_ = code != 0;
  switch (code) {
    case 0:
      setup_result_ = "The Kinect driver is set up.";
      break;
    case 2:
      setup_result_ =
          "The driver setup found no Kinect. Check that it is plugged in "
          "and its adapter has power.";
      break;
    case 3:
      setup_result_ =
          "Windows did not give permission. Press the button again and "
          "click Yes.";
      break;
    default:
      setup_result_ =
          "The driver setup did not finish. Unplug the Kinect, plug it in "
          "again and try once more.";
      break;
  }
  next_detect_time_ = 0.0;
  if (code == 0) {
    auto_start_time_ = ImGui::GetTime() + 3.0;
  }
#endif
}

void KinectDialog::Save() { config::SaveConfig(); }

void KinectDialog::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.95f);
  bool open = true;
  if (!ImGui::Begin(
          "Kinect", &open,
          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }
  if (!open) {
    Close();
    ImGui::End();
    emulator_window_.ToggleKinectDialog();
    return;
  }
  DrawSensor();
  DrawPreview();
  DrawView();
  DrawMicrophone();
  if (close_requested_) {
    Close();
    ImGui::End();
    emulator_window_.ToggleKinectDialog();
    return;
  }
  ImGui::Separator();
  bool on_start = cvars::nui_window_on_start;
  if (ImGui::Checkbox("Open this window when Xenia starts", &on_start)) {
    SetSetting("nui_window_on_start", on_start);
    Save();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Only when a Kinect is plugged in, and not when Xenia is started\n"
        "with a game (a shortcut or a game launcher).");
  }
  ImGui::TextDisabled("Settings are saved automatically.");
#if XE_PLATFORM_WIN32
  ImGui::SameLine();
  if (ImGui::SmallButton("Show log files")) {
    // Explorer with xenia.log selected, for attaching to a problem report.
    const auto log = xe::filesystem::GetExecutableFolder() / "xenia.log";
    const std::wstring arguments = L"/select,\"" + log.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arguments.c_str(), nullptr,
                  SW_SHOWNORMAL);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Opens the folder with xenia.log. Attach it (and\n"
        "xenia.previous.log, kinect\\Setup-Kinect.log if there)\n"
        "to a problem report.");
  }
#endif
  ImGui::End();
}

}  // namespace app
}  // namespace xe
