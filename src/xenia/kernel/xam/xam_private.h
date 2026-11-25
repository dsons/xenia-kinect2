/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2019 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_PRIVATE_H_
#define XENIA_KERNEL_XAM_XAM_PRIVATE_H_

#include "xenia/cpu/export_resolver.h"
#include "xenia/kernel/xam/xam_ordinals.h"

namespace xe {
namespace kernel {
namespace xam {

xe::cpu::Export* RegisterExport_xam(xe::cpu::Export* export_entry);

// Registration functions, one per file.
#define XE_MODULE_EXPORT_GROUP(m, n)                                  \
  void Register##n##Exports(xe::cpu::ExportResolver* export_resolver, \
                            KernelState* kernel_state);
#include "xam_module_export_groups.inc"
#undef XE_MODULE_EXPORT_GROUP

// Kinect identity (xam_nui.cc): the title's callback (message 0x2C009), its
// per-frame skeleton frame (0x2B002) and the Kinect sign-in UI.
void XamNuiIdentitySetCallback(uint32_t callback);
void XamNuiIdentityOnFrame(uint32_t buffer_ptr);
void XamNuiIdentitySignIn(uint32_t user_index);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XAM_XAM_PRIVATE_H_
