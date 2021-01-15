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

#include "CacheFile.hpp"

#include "Manifest.hpp"
#include "Result.hpp"
#include "Util.hpp"

CacheFile::Type
CacheFile::type() const
{
  if (Util::ends_with(path(), Manifest::k_file_suffix)) {
    return Type::manifest;
  } else if (Util::ends_with(path(), Result::k_file_suffix)) {
    return Type::result;
  } else {
    return Type::unknown;
  }
}
