/*
  OutputVars.h - Central registry of variables exposed to output channels.

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
#ifndef OUTPUTVARS_H
#define OUTPUTVARS_H

#include <Arduino.h>

// Single source of truth for variables available to user templates and
// any output module that wants to share value lookup. Each variable knows
// how to render itself into a buffer; lookup is a linear scan keyed on the
// short name (the same name authors use in {curly} substitution).
namespace OutputVars {

// Render the named variable's current value into buf as a NUL-terminated
// string. Returns bytes written (0 on unknown key or render failure).
// keylen is the length of the key in source (may not be NUL-terminated).
size_t render(const char* key, size_t keylen, char* buf, size_t cap);

// Walk a user template and produce its substituted form into buf.
//
// Syntax:
//   {name}     - variable substitution. Unknown vars render as literal
//                "{name}" so misspellings are visible to the author.
//   \n \r \t   - whitespace escapes.
//   \\ \{ \}   - literal backslash, brace.
// Anything else passes through verbatim. Input is SRAM (heap pref shadow
// or compile-time const). Output is always NUL-terminated.
size_t renderTemplate(const char* tpl, char* buf, size_t cap);

// Public introspection for /vars debug + docs generators.
struct VarDef {
  const char* key;
  const char* desc;
};
const VarDef* list();   // sentinel-terminated array

}  // namespace OutputVars

#endif
