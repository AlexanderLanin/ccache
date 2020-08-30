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

#pragma once

#include "system.hpp"

#include "NonCopyable.hpp"
#include "Util.hpp"

#include "third_party/nonstd/optional.hpp"
#include "third_party/nonstd/string_view.hpp"

#include <deque>
#include <string>

class Args
{
public:
  Args() = default;
  Args(const Args& other) = default;
  Args(Args&& other) noexcept;

  static Args from_argv(int argc, const char* const* argv);
  static Args from_string(const std::string& command);
  static nonstd::optional<Args> from_gcc_atfile(const std::string& filename);

  Args& operator=(const Args& other) = default;
  Args& operator=(Args&& other) noexcept;

  bool operator==(const Args& other) const;
  bool operator!=(const Args& other) const;

  // Registers param as a multi value parameter where "param value" means
  // "param=value". Such parameters will be coverted to the "param=value" form.
  // Will only be used by parse function, not by the others like push_back etc!
  void add_param(nonstd::string_view param);

  // To be used if add_param needs to be accounted for.
  // This is the only function that accounts for add_param!
  void parse(const char* const* argv);

  bool empty() const;
  size_t size() const;
  const std::string& operator[](size_t i) const;
  std::string& operator[](size_t i);

  // Return the argument list as a vector of raw string pointers. Callers can
  // use `const_cast<char* const*>(args.to_argv().data())` to get an array
  // suitable to pass to e.g. execv(2).
  std::vector<const char*> to_argv() const;

  // Return a space-delimited argument list in string form. No quoting of spaces
  // in arguments is performed.
  std::string to_string() const;

  // Remove all arguments with prefix `prefix`.
  void erase_with_prefix(nonstd::string_view prefix);

  // Insert arguments in `args` at position `index`.
  void insert(size_t index, const Args& args);

  // Remove the last `count` arguments.
  void pop_back(size_t count = 1);

  // Remove the first `count` arguments.
  void pop_front(size_t count = 1);

  // Add `arg` to the end.
  void push_back(const std::string& arg);

  // Add `args` to the end.
  void push_back(const Args& args);

  // Add `arg` to the front.
  void push_front(const std::string& arg);

  // Replace the argument at `index` with all arguments in `args`.
  void replace(size_t index, const Args& args);

  std::deque<std::string>::const_iterator begin() const;
  std::deque<std::string>::const_iterator end() const;

  void splitSingleDashFlags();

private:
  std::deque<std::string> m_args;
  std::vector<std::string> m_multiValueParams;
};

inline bool
Args::operator==(const Args& other) const
{
  return m_args == other.m_args;
}

inline bool
Args::operator!=(const Args& other) const
{
  return m_args != other.m_args;
}

inline bool
Args::empty() const
{
  return m_args.empty();
}

inline size_t
Args::size() const
{
  return m_args.size();
}

// clang-format off
inline const std::string&
Args::operator[](size_t i) const
// clang-format on
{
  return m_args[i];
}

// clang-format off
inline std::string&
Args::operator[](size_t i)
// clang-format on
{
  return m_args[i];
}

inline std::deque<std::string>::const_iterator
Args::begin() const
{
  return m_args.begin();
}

inline std::deque<std::string>::const_iterator
Args::end() const
{
  return m_args.end();
}
