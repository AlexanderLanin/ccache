// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "FormatNonstdStringView.hpp"
#include "StatisticEnum.hpp"

#include "third_party/fmt/core.h"
#include "third_party/nonstd/optional.hpp"

#include <stdexcept>
#include <string>
#include <utility>

// Don't throw or catch ErrorBase directly, use a subclass.
class ErrorBase : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

// Throw an Error to indicate a potentially non-fatal error that may be caught
// and handled by callers. An uncaught Error that reaches the top level will be
// treated similar to Fatal.
class Error : public ErrorBase
{
public:
  // Special case: If given only one string, don't parse it as a format string.
  Error(const std::string& message);

  // `args` are forwarded to `fmt::format`.
  template<typename... T> inline Error(T&&... args);
};

inline Error::Error(const std::string& message) : ErrorBase(message)
{
}

template<typename... T>
inline Error::Error(T&&... args)
  : ErrorBase(fmt::format(std::forward<T>(args)...))
{
}

// Throw a Fatal to make ccache print the error message to stderr and exit
// with a non-zero exit code.
class Fatal : public ErrorBase
{
public:
  // Special case: If given only one string, don't parse it as a format string.
  Fatal(const std::string& message);

  // `args` are forwarded to `fmt::format`.
  template<typename... T> inline Fatal(T&&... args);
};

inline Fatal::Fatal(const std::string& message) : ErrorBase(message)
{
}

template<typename... T>
inline Fatal::Fatal(T&&... args)
  : ErrorBase(fmt::format(std::forward<T>(args)...))
{
}

// Throw a Failure if ccache did not succeed in getting or putting a result in
// the cache. If `exit_code` is set, just exit with that code directly,
// otherwise execute the real compiler and exit with its exit code. Also updates
// statistics counter `statistic` if it's not `Statistic::none`.
class Failure : public std::exception
{
public:
  Failure(Statistic statistic,
          nonstd::optional<int> exit_code = nonstd::nullopt);

  nonstd::optional<int> exit_code() const;
  Statistic statistic() const;

private:
  Statistic m_statistic;
  nonstd::optional<int> m_exit_code;
};

inline Failure::Failure(Statistic statistic, nonstd::optional<int> exit_code)
  : m_statistic(statistic), m_exit_code(exit_code)
{
}

inline nonstd::optional<int>
Failure::exit_code() const
{
  return m_exit_code;
}

inline Statistic
Failure::statistic() const
{
  return m_statistic;
}
