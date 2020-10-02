// Copyright (C) 2020 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#include "Arg.hpp"

#include "FormatNonstdStringView.hpp"

static const std::string emptyString;

Arg::Arg(nonstd::string_view full)
  : m_full(full), m_split_char(ArgSplit::not_split)
{
  const size_t sep_pos = m_full.find('=');
  if (sep_pos != nonstd::string_view::npos) {
    m_split_char = ArgSplit::equal_sign;
    m_key = nonstd::string_view(m_full).substr(0, sep_pos);
    m_value = nonstd::string_view(m_full).substr(sep_pos + 1);
  }
}

static std::string
to_string(const nonstd::string_view key,
          const ArgSplit split_char,
          const nonstd::string_view value)
{
  switch (split_char) {
  case ArgSplit::not_split:
    assert(false);
    return "internal error";
  case ArgSplit::equal_sign:
    return fmt::format("{}={}", key, value);
  case ArgSplit::space:
    return fmt::format("{} {}", key, value);
  case ArgSplit::written_together:
    return fmt::format("{}{}", key, value);
  }
  assert(false);
  return "internal error";
}

Arg::Arg(const nonstd::string_view key,
         const ArgSplit split_char,
         const nonstd::string_view value)
  : m_split_char(split_char)
{
  if (split_char == ArgSplit::not_split) {
    throw "invalid usage of constructor";
  }

  m_full = to_string(key, split_char, value);
  m_key = nonstd::string_view(m_full).substr(0, key.length());
  m_value = nonstd::string_view(m_full).substr(
    key.length() + (split_char != ArgSplit::written_together));
}

Arg::Arg(const Arg& other)
  : m_full(other.m_full), m_split_char(other.m_split_char)
{
  if (other.has_been_split()) {
    m_key = nonstd::string_view(m_full).substr(0, other.key().size());
    m_value = nonstd::string_view(m_full).substr(
      other.key().size() + (m_split_char != ArgSplit::written_together));
  }
  assert(*this == other);
}

Arg&
Arg::operator=(const Arg& other)
{
  if (&other == this) {
    return *this;
  }

  m_full = other.full();
  m_split_char = other.m_split_char;
  if (other.has_been_split()) {
    m_key = nonstd::string_view(m_full).substr(0, other.key().size());
    m_value = nonstd::string_view(m_full).substr(
      other.key().size() + (m_split_char != ArgSplit::written_together));
  } else {
    m_key = "";
    m_value = "";
  }
  assert(*this == other);
  return *this;
}
