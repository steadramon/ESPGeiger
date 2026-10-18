/*
  EGTimer1Nmi.h - NMI stack sizes, shared by EGTimer1.cpp and EGTimer1Nmi.S

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

// defines only, the .S includes this
#ifndef EG_TIMER1_NMI_H
#define EG_TIMER1_NMI_H

#define EGT1_NMI_STACK 256   // bytes, below the frame
#define EGT1_NMI_FRAME 64    // saved registers

#endif
