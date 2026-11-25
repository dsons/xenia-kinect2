/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_KINECT_DIALOG_H_
#define XENIA_APP_KINECT_DIALOG_H_

#include <future>
#include <memory>
#include <string>
#include <vector>

#include "xenia/kernel/xboxkrnl/nui_kinect.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace app {

class EmulatorWindow;

// Kinect setup and testing: which sensor, whether it works, a live picture,
// the Kinect 2 view and the microphone. Works with or without a game.
class KinectDialog final : public ui::ImGuiDialog {
 public:
  KinectDialog(ui::ImGuiDrawer* imgui_drawer, EmulatorWindow& emulator_window);
  ~KinectDialog();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void StartSensor(bool restart);
  void UpdatePreview();
  void DrawSensor();
  void DrawPreview();
  void DrawView();
  void DrawMicrophone();
  void DrawSetup(bool starting, bool streaming);
  void DetectHardware(bool starting, bool streaming);
  void InstallDriver();
  void PollDriverSetup();
  void Save();
  bool TitleRunning() const;

  EmulatorWindow& emulator_window_;

  std::future<bool> pending_start_;
  bool started_here_ = false;

  std::vector<uint16_t> depth_;
  std::vector<uint8_t> rgb_;
  std::vector<uint8_t> pixels_;
  uint32_t frame_ = 0;
  double next_preview_time_ = 0.0;
  std::unique_ptr<ui::ImmediateTexture> color_texture_;
  std::unique_ptr<ui::ImmediateTexture> depth_texture_;
  float center_distance_m_ = 0.0f;

  // Whether the player's whole body is in the depth picture.
  enum class Framing { kUnknown, kGood, kFeetCut, kHeadCut, kLegsBlocked };
  Framing CheckFraming(float distance_m) const;
  Framing framing_ = Framing::kUnknown;

  int pan_ = 0;
  float zoom_ = 1.3f;

  std::vector<std::string> microphones_;

  kernel::xboxkrnl::KinectHardware hardware_;
  bool hardware_checked_ = false;
  double next_detect_time_ = 0.0;
  double auto_start_time_ = 0.0;
  void* setup_process_ = nullptr;
  std::string setup_result_;
  bool setup_failed_ = false;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_KINECT_DIALOG_H_
