/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_NUI_KINECT2_CONVERT_H_
#define XENIA_KERNEL_XBOXKRNL_NUI_KINECT2_CONVERT_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace xe {
namespace kernel {
namespace xboxkrnl {

// Turns Kinect 2 frames into the frames an Xbox 360 Kinect (Kinect 1) makes.
//
// Input: 960x540 colour and depth registered onto it (one pinhole, focal
// length 540.685 px, centre 479.75, 269.75). Output: 640x480 with the
// Kinect 1's pinhole (focal length 525 px, centre 319.5, 239.5). With a shared
// camera centre each output pixel's ray lands on one input pixel, so the
// conversion is a remap: nearly a centred crop.
//
// Depth also gets what makes time-of-flight depth look like structured light
// to the NUI tracker: "flying pixels" (the mix of foreground and background
// along every edge) are removed, as is everything outside the Kinect 1's
// 0.4-4.5 m working range.
//
// pan slides the window sideways (input pixels, + = the camera's right).
// zoom K is a virtual step back: the window takes in K times the view and
// every depth is multiplied by K, which is the picture of the same person
// standing K times farther away. Past the input's view the window's edges
// read as no depth, as out-of-range does on a Kinect 1.
class Kinect2ToKinect1 {
 public:
  static constexpr int kInWidth = 960;
  static constexpr int kInHeight = 540;
  static constexpr int kOutWidth = 640;
  static constexpr int kOutHeight = 480;
  static constexpr float kInFocal = 540.685f;
  static constexpr float kInCenterX = 479.75f;
  static constexpr float kInCenterY = 269.75f;
  static constexpr float kOutFocal = 525.0f;
  static constexpr float kOutCenterX = 319.5f;
  static constexpr float kOutCenterY = 239.5f;
  static constexpr int kMaxPan = 40;
  static constexpr uint16_t kMinDepth = 400;
  static constexpr uint16_t kMaxDepth = 4500;

  Kinect2ToKinect1() { Configure(0, 1.0); }

  void Configure(int pan, double zoom) {
    pan_ = std::clamp(pan, -kMaxPan, kMaxPan);
    zoom_ = std::clamp(zoom, 1.0, 1.6);
    nearest_.resize(size_t(kOutWidth) * kOutHeight);
    linear_.resize(size_t(kOutWidth) * kOutHeight);
    const float scale = float(kInFocal * zoom_ / kOutFocal);
    for (int v = 0; v < kOutHeight; ++v) {
      const float my = kInCenterY + (v - kOutCenterY) * scale;
      for (int u = 0; u < kOutWidth; ++u) {
        const float mx = kInCenterX + pan_ + (u - kOutCenterX) * scale;
        const size_t i = size_t(v) * kOutWidth + u;
        const long nx = std::lrint(mx);
        const long ny = std::lrint(my);
        nearest_[i] = (nx >= 0 && nx < kInWidth && ny >= 0 && ny < kInHeight)
                          ? int32_t(ny * kInWidth + nx)
                          : -1;
        // Bilinear taps with 1/32 px weights.
        const long fx = std::lrint(mx * 32.0f);
        const long fy = std::lrint(my * 32.0f);
        Linear& l = linear_[i];
        l.x = int32_t(fx >> 5);
        l.y = int32_t(fy >> 5);
        l.ax = uint8_t(fx & 31);
        l.ay = uint8_t(fy & 31);
      }
    }
  }

  int pan() const { return pan_; }
  double zoom() const { return zoom_; }

  // depth_in: kInWidth x kInHeight mm. depth_out: kOutWidth x kOutHeight mm.
  void ConvertDepth(const uint16_t* depth_in, uint16_t* depth_out) {
    filtered_.resize(size_t(kInWidth) * kInHeight);
    for (int y = 0; y < kInHeight; ++y) {
      const int y0 = std::max(y - 1, 0);
      const int y1 = std::min(y + 1, kInHeight - 1);
      for (int x = 0; x < kInWidth; ++x) {
        const int x0 = std::max(x - 1, 0);
        const int x1 = std::min(x + 1, kInWidth - 1);
        uint16_t lo = 0xFFFF, hi = 0;
        for (int yy = y0; yy <= y1; ++yy) {
          const uint16_t* row = depth_in + size_t(yy) * kInWidth;
          for (int xx = x0; xx <= x1; ++xx) {
            lo = std::min(lo, row[xx]);
            hi = std::max(hi, row[xx]);
          }
        }
        const uint16_t d = depth_in[size_t(y) * kInWidth + x];
        // Keep a pixel whose 3x3 depth range is within max(60 mm, d / 25).
        const uint32_t limit = std::max<uint32_t>(60, (2u * d + 25) / 50);
        const bool keep =
            uint32_t(hi - lo) <= limit && d >= kMinDepth && d <= kMaxDepth;
        filtered_[size_t(y) * kInWidth + x] = keep ? d : 0;
      }
    }
    const size_t out_pixels = size_t(kOutWidth) * kOutHeight;
    for (size_t i = 0; i < out_pixels; ++i) {
      const int32_t src = nearest_[i];
      uint16_t d = src >= 0 ? filtered_[size_t(src)] : 0;
      if (d && zoom_ != 1.0) {
        d = uint16_t(std::min(65535.0, std::nearbyint(d * zoom_)));
      }
      depth_out[i] = d;
    }
  }

  // rgb_in: kInWidth x kInHeight RGB24. rgb_out: kOutWidth x kOutHeight RGB24.
  void ConvertColor(const uint8_t* rgb_in, uint8_t* rgb_out) const {
    const size_t out_pixels = size_t(kOutWidth) * kOutHeight;
    for (size_t i = 0; i < out_pixels; ++i) {
      const Linear& l = linear_[i];
      const int w[4] = {(32 - l.ax) * (32 - l.ay), l.ax * (32 - l.ay),
                        (32 - l.ax) * l.ay, l.ax * l.ay};
      const int tx[4] = {l.x, l.x + 1, l.x, l.x + 1};
      const int ty[4] = {l.y, l.y, l.y + 1, l.y + 1};
      int sum[3] = {512, 512, 512};
      for (int t = 0; t < 4; ++t) {
        if (!w[t] || tx[t] < 0 || tx[t] >= kInWidth || ty[t] < 0 ||
            ty[t] >= kInHeight) {
          continue;
        }
        const uint8_t* p = rgb_in + (size_t(ty[t]) * kInWidth + tx[t]) * 3;
        sum[0] += p[0] * w[t];
        sum[1] += p[1] * w[t];
        sum[2] += p[2] * w[t];
      }
      uint8_t* o = rgb_out + i * 3;
      o[0] = uint8_t(sum[0] >> 10);
      o[1] = uint8_t(sum[1] >> 10);
      o[2] = uint8_t(sum[2] >> 10);
    }
  }

 private:
  struct Linear {
    int32_t x, y;
    uint8_t ax, ay;
  };
  int pan_ = 0;
  double zoom_ = 1.0;
  std::vector<int32_t> nearest_;
  std::vector<Linear> linear_;
  std::vector<uint16_t> filtered_;
};

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XBOXKRNL_NUI_KINECT2_CONVERT_H_
