/*
  EGTinySerialStream.h - Stream facade over a port.

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

// For a third party device library that insists on a Stream&. Include it only
// where that is true: the virtuals pin the transmit path and Print's number
// formatting into the build, which is exactly what Core avoids.

#ifndef EG_TINYSERIAL_STREAM_H
#define EG_TINYSERIAL_STREAM_H

#include <Stream.h>
#include "EGTinySerial.h"

namespace EGTinySerial {

class StreamAdapter : public Stream {
public:
  explicit StreamAdapter(Core& port) : m_port(port) {}

  int    available() override            { return m_port.available(); }
  int    read() override                 { return m_port.read(); }
  int    peek() override                 { return m_port.peek(); }
  void   flush() override                {}   // transmit is synchronous
  size_t write(uint8_t b) override       { return m_port.write(b); }
  size_t write(const uint8_t* b, size_t n) override { return m_port.write(b, n); }
  using Print::write;

private:
  Core& m_port;
};

} // namespace EGTinySerial

#endif
