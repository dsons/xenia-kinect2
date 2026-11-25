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
#include "xenia/base/platform.h"
#include "xenia/config.h"
#include "xenia/emulator.h"
#include "xenia/kernel/xboxkrnl/nui_kinect.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

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
        cvars::nui_camera = m.cvar;
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
  if (mode == "synthetic") {
    ImGui::TextColored(kGood, "Games see a moving test figure.");
  } else if (mode == "none") {
    ImGui::TextColored(kWarn, "Kinect is off. Games will ask for a Kinect.");
  } else if (starting) {
    ImGui::Text("Looking for a Kinect...");
  } else if (status.streaming) {
    ImGui::TextColored(kGood, "%s connected, %.0f frames a second.",
                       KinectSensorName(status.kind), status.frames_per_second);
  } else {
    ImGui::TextColored(kWarn, "%s", status.message.c_str());
  }

  if (IsSensorMode(mode)) {
    ImGui::BeginDisabled(starting);
    if (ImGui::Button("Retry")) {
      StartSensor(true);
    }
    ImGui::EndDisabled();
#if XE_PLATFORM_WIN32
    ImGui::SameLine();
    if (ImGui::Button("Install driver")) {
      InstallDriver();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "Sets up the Kinect's driver (once per PC and USB port).\n"
          "Windows asks for permission. Press Retry when it is done.");
    }
#endif
  }
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
  ImGui::TextUnformatted(
      "Stand where your whole body is in the picture, in the green area.");
  if (center_distance_m_ > 0.0f) {
    const bool good = center_distance_m_ >= 1.5f && center_distance_m_ <= 3.2f;
    ImGui::TextColored(good ? kGood : kWarn,
                       "Nearest in the middle: %.1f m (%.0f ft). %s",
                       center_distance_m_, center_distance_m_ * 3.28f,
                       center_distance_m_ < 1.5f   ? "Step back."
                       : center_distance_m_ > 3.2f ? "Come closer."
                                                   : "Good distance.");
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
    cvars::nui_kinect2_zoom = zoom_;
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
    cvars::nui_kinect2_pan = pan_;
    Save();
  }
  if (ImGui::Button("Reset view")) {
    pan_ = 0;
    zoom_ = 1.3f;
    KinectCamera::Get()->SetView(pan_, zoom_);
    cvars::nui_kinect2_pan = pan_;
    cvars::nui_kinect2_zoom = zoom_;
    Save();
  }
}

void KinectDialog::DrawMicrophone() {
  ImGui::Separator();
  bool enabled = cvars::nui_mic;
  if (ImGui::Checkbox("Kinect microphone", &enabled)) {
    cvars::nui_mic = enabled;
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
        cvars::nui_mic_device = name;
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
  const auto script =
      xe::filesystem::GetExecutableFolder() / "kinect" / "Setup-Kinect.cmd";
  ShellExecuteW(nullptr, L"open", script.c_str(), nullptr,
                script.parent_path().c_str(), SW_SHOWNORMAL);
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
  ImGui::Separator();
  ImGui::TextDisabled("Settings are saved automatically.");
  ImGui::End();
}

}  // namespace app
}  // namespace xe
