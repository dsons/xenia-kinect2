/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Developers. All rights reserved.                      *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// LDI: the cabinet SDK's block-at-a-time LZX decompressor, exported by the
// kernel. Kinect Adventures' skeleton tracker stores its exemplar database
// (database.gmsodf) as a stream of [be16 size][LZX block] records and hands
// each block to LDIDecompress, so NUI cannot start without it.
//
// Each LDIDecompress call carries one whole block: a fresh, 16-bit aligned
// bitstream that decodes to one frame (32 KB, the last one shorter). The
// LZX state (window, trees, repeated offsets) carries over between calls.
// libmspack's lzxd keeps exactly that state; its input is reset per block.

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_private.h"
#include "xenia/xbox.h"

#include "third_party/mspack/lzx.h"
#include "third_party/mspack/mspack.h"

namespace xe {
namespace kernel {
namespace xboxkrnl {

namespace {

// Cabinet SDK MDI_ERROR_* codes.
constexpr uint32_t kLdiSuccess = 0;
constexpr uint32_t kLdiNotEnoughMemory = 1;
constexpr uint32_t kLdiBadParameters = 2;
constexpr uint32_t kLdiFailed = 4;
constexpr uint32_t kLdiConfiguration = 5;

constexpr uint32_t kFrameSize = 0x8000;
// The encoder may leave up to one 16-bit word of lookahead past the end of
// a block; feed zeros there rather than let lzxd see end of input.
constexpr uint32_t kBlockPadding = 16;

struct LdiFile {
  std::vector<uint8_t>* input = nullptr;
  size_t input_offset = 0;
  uint8_t* output = nullptr;
  size_t output_size = 0;
  size_t output_offset = 0;
};

int LdiRead(mspack_file* file, void* buffer, int bytes) {
  auto f = reinterpret_cast<LdiFile*>(file);
  size_t remaining = f->input->size() - f->input_offset;
  size_t n = std::min(remaining, static_cast<size_t>(bytes));
  std::memcpy(buffer, f->input->data() + f->input_offset, n);
  f->input_offset += n;
  return static_cast<int>(n);
}

int LdiWrite(mspack_file* file, void* buffer, int bytes) {
  auto f = reinterpret_cast<LdiFile*>(file);
  size_t remaining = f->output_size - f->output_offset;
  size_t n = std::min(remaining, static_cast<size_t>(bytes));
  std::memcpy(f->output + f->output_offset, buffer, n);
  f->output_offset += n;
  return static_cast<int>(n);
}

void* LdiAlloc(mspack_system*, size_t bytes) { return std::calloc(bytes, 1); }
void LdiFree(void* ptr) { std::free(ptr); }
void LdiCopy(void* src, void* dest, size_t bytes) {
  std::memmove(dest, src, bytes);
}
void LdiMessage(mspack_file*, const char* format, ...) {
  XELOGW("LDI: lzxd warning: {}", format);
}

class LdiContext {
 public:
  explicit LdiContext(uint32_t window_bits) : window_bits_(window_bits) {
    sys_.read = LdiRead;
    sys_.write = LdiWrite;
    sys_.alloc = LdiAlloc;
    sys_.free = LdiFree;
    sys_.copy = LdiCopy;
    sys_.message = LdiMessage;
  }
  ~LdiContext() { Free(); }

  bool Reset() {
    Free();
    produced_ = 0;
    lzx_ = lzxd_init(&sys_, reinterpret_cast<mspack_file*>(&in_),
                     reinterpret_cast<mspack_file*>(&out_),
                     static_cast<int>(window_bits_), 0, 4096, 0, 0);
    return lzx_ != nullptr;
  }

  // Decodes one block to exactly out_size bytes.
  bool Decompress(const uint8_t* src, uint32_t src_size, uint8_t* dst,
                  uint32_t out_size) {
    if (!lzx_ || !out_size || out_size > kFrameSize) {
      return false;
    }
    block_.assign(src, src + src_size);
    block_.resize(block_.size() + kBlockPadding, 0);
    in_.input = &block_;
    in_.input_offset = 0;
    // A fresh bitstream for every block.
    lzx_->i_ptr = lzx_->i_end = &lzx_->inbuf[0];
    lzx_->bit_buffer = 0;
    lzx_->bits_left = 0;
    lzx_->input_end = 0;
    out_.output = dst;
    out_.output_size = out_size;
    out_.output_offset = 0;
    if (out_size < kFrameSize) {
      // The final, short frame: lzxd sizes a frame from the total length.
      lzxd_set_output_length(lzx_, produced_ + out_size);
    }
    // Ask for all but the last byte first. lzxd decodes frames up to the one
    // holding offset + n, so a whole-frame request would start the next
    // frame, whose block has not arrived yet.
    int err = MSPACK_ERR_OK;
    if (out_size > 1) {
      err = lzxd_decompress(lzx_, out_size - 1);
    }
    if (err == MSPACK_ERR_OK) {
      err = lzxd_decompress(lzx_, 1);
    }
    if (err != MSPACK_ERR_OK || out_.output_offset != out_size) {
      XELOGE("LDI: block of {} bytes -> {} failed (lzxd {}, wrote {})",
             src_size, out_size, err, out_.output_offset);
      return false;
    }
    produced_ += out_size;
    return true;
  }

 private:
  void Free() {
    if (lzx_) {
      lzxd_free(lzx_);
      lzx_ = nullptr;
    }
  }

  uint32_t window_bits_;
  mspack_system sys_ = {};
  LdiFile in_;
  LdiFile out_;
  std::vector<uint8_t> block_;
  lzxd_stream* lzx_ = nullptr;
  off_t produced_ = 0;
};

std::mutex ldi_mutex;
std::unordered_map<uint32_t, std::unique_ptr<LdiContext>> ldi_contexts;
uint32_t ldi_next_handle = 0x1D10001;

LdiContext* LookupContext(uint32_t handle) {
  std::lock_guard<std::mutex> lock(ldi_mutex);
  auto it = ldi_contexts.find(handle);
  return it == ldi_contexts.end() ? nullptr : it->second.get();
}

}  // namespace

// Kinect Adventures passes (&block_max, &{window size, cpu type}, alloc, free,
// work memory, &src_buffer_min, &handle); alloc and free are null and the
// work memory is the game's, which the host decoder does not need.
dword_result_t LDICreateDecompression_entry(lpdword_t block_max_ptr,
                                            lpdword_t config_ptr,
                                            dword_t alloc_fn, dword_t free_fn,
                                            lpvoid_t work_memory,
                                            lpdword_t src_buffer_min_ptr,
                                            lpdword_t handle_ptr) {
  if (!config_ptr || !handle_ptr) {
    return kLdiBadParameters;
  }
  uint32_t window_size = config_ptr.value();
  uint32_t window_bits;
  if (!xe::bit_scan_forward(window_size, &window_bits) ||
      (1u << window_bits) != window_size || window_bits < 15 ||
      window_bits > 21) {
    XELOGE("LDICreateDecompression: unsupported window size {:X}", window_size);
    return kLdiConfiguration;
  }
  auto context = std::make_unique<LdiContext>(window_bits);
  if (!context->Reset()) {
    return kLdiNotEnoughMemory;
  }
  if (block_max_ptr &&
      (block_max_ptr.value() == 0 || block_max_ptr.value() > kFrameSize)) {
    *block_max_ptr = kFrameSize;
  }
  if (src_buffer_min_ptr) {
    // The cabinet SDK's worst case for one compressed block.
    *src_buffer_min_ptr = kFrameSize + 6144;
  }
  uint32_t handle;
  {
    std::lock_guard<std::mutex> lock(ldi_mutex);
    handle = ldi_next_handle++;
    ldi_contexts[handle] = std::move(context);
  }
  *handle_ptr = handle;
  XELOGI("LDICreateDecompression: window {:X} -> handle {:08X}", window_size,
         handle);
  return kLdiSuccess;
}
DECLARE_XBOXKRNL_EXPORT1(LDICreateDecompression, kNone, kImplemented);

dword_result_t LDIDecompress_entry(dword_t handle, lpvoid_t src,
                                   dword_t src_size, lpvoid_t dst,
                                   lpdword_t dst_size_ptr) {
  auto context = LookupContext(handle);
  if (!context || !src || !dst || !dst_size_ptr) {
    return kLdiBadParameters;
  }
  uint32_t out_size = dst_size_ptr.value();
  if (!context->Decompress(src.as<uint8_t*>(), src_size, dst.as<uint8_t*>(),
                           out_size)) {
    *dst_size_ptr = 0;
    return kLdiFailed;
  }
  *dst_size_ptr = out_size;
  return kLdiSuccess;
}
DECLARE_XBOXKRNL_EXPORT1(LDIDecompress, kNone, kImplemented);

dword_result_t LDIResetDecompression_entry(dword_t handle) {
  auto context = LookupContext(handle);
  if (!context) {
    return kLdiBadParameters;
  }
  return context->Reset() ? kLdiSuccess : kLdiNotEnoughMemory;
}
DECLARE_XBOXKRNL_EXPORT1(LDIResetDecompression, kNone, kImplemented);

dword_result_t LDIDestroyDecompression_entry(dword_t handle) {
  std::lock_guard<std::mutex> lock(ldi_mutex);
  return ldi_contexts.erase(handle) ? kLdiSuccess : kLdiBadParameters;
}
DECLARE_XBOXKRNL_EXPORT1(LDIDestroyDecompression, kNone, kImplemented);

}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe

DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(Ldi);
