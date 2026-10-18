/*
  EGTimer1Nmi.S - lean NMI entry for the EGTimer1 handler

  Copyright (C) 2026 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

// the SDK's NMI steps, saving only a1-a11 and SAR
// patch_nmi_vector() points here

#if defined(ESP8266) && defined(EG_TIMER1)

#include "EGTimer1Nmi.h"

#define DPORT_BASE  0x3ff00000
#define FRC1_BASE   0x60000600
#define FRC1_INT    12

  .section .iram.text.egt1_nmi_entry, "ax", @progbits
  .literal_position
  .align 4
  .global egt1_nmi_entry
  .type egt1_nmi_entry, @function
egt1_nmi_entry:
  // interrupted a0 in EXCSAVE3, stack below the frame
  movi    a0, egt1_nmi_stack + EGT1_NMI_STACK
  s32i    a1, a0, 4
  s32i    a2, a0, 8
  s32i    a3, a0, 12
  s32i    a4, a0, 16
  s32i    a5, a0, 20
  s32i    a6, a0, 24
  s32i    a7, a0, 28
  s32i    a8, a0, 32
  s32i    a9, a0, 36
  s32i    a10, a0, 40
  s32i    a11, a0, 44
  // SDK value back in EXCSAVE3, a0 to the frame
  movi    a2, _Pri_3_HandlerAddress
  l32i    a2, a2, 0
  xsr.excsave3 a2
  s32i    a2, a0, 0
  rsr.sar a2
  s32i    a2, a0, 48
  mov     a1, a0
  movi    a2, 0x23              // INTLEVEL 3, UM, EXCM clear: as the SDK
  wsr.ps  a2
  rsync
  // ack: source 0 off, wait till it reads off
  movi    a3, DPORT_BASE
  movi    a4, -32
  memw
  l32i    a2, a3, 0
  and     a2, a2, a4
  movi    a4, 14
  or      a2, a2, a4
  memw
  s32i    a2, a3, 0
1:
  memw
  l32i    a2, a3, 0
  bbsi    a2, 0, 1b
  call0   egt1_nmi
  movi    a2, 8
  wsr.intclear a2
  movi    a3, FRC1_BASE
  movi    a4, -2
  memw
  l32i    a2, a3, FRC1_INT
  and     a2, a2, a4
  memw
  s32i    a2, a3, FRC1_INT
  movi    a2, 0x33              // EXCM set again before the restore: as the SDK
  wsr.ps  a2
  rsync
  l32i    a2, a1, 48
  wsr.sar a2
  l32i    a4, a1, 16
  l32i    a5, a1, 20
  l32i    a6, a1, 24
  l32i    a7, a1, 28
  l32i    a8, a1, 32
  l32i    a9, a1, 36
  l32i    a10, a1, 40
  l32i    a11, a1, 44
  l32i    a0, a1, 0
  // source 0 back on, as the SDK
  movi    a2, DPORT_BASE
  movi    a3, 15
  s32i    a3, a2, 0
  l32i    a2, a1, 8
  l32i    a3, a1, 12
  l32i    a1, a1, 4
  rfi     3
  .size egt1_nmi_entry, . - egt1_nmi_entry

#endif
