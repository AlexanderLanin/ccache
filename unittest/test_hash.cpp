// Copyright (C) 2010-2019 Joel Rosdahl and other contributors
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

// This file contains tests for functions in hash.c.

#include "../src/ccache.hpp"
#include "../src/hash.hpp"
#include "framework.hpp"

TEST_SUITE(hash)

TEST(test_known_strings)
{
  char d[DIGEST_STRING_BUFFER_SIZE];

  {
    Hash h;
    hash_string(h, "");
    hash_result_as_string(h, d);
    CHECK_STR_EQ("3345524abf6bbe1809449224b5972c41790b6cf2", d);
  }

  {
    Hash h;
    hash_string(h, "a");
    hash_result_as_string(h, d);
    CHECK_STR_EQ("948caa2db61bc4cdb4faf7740cd491f195043914", d);
  }

  {
    Hash h;
    hash_string(h, "message digest");
    hash_result_as_string(h, d);
    CHECK_STR_EQ("6bfec6f65e52962be863d6ea1005fc5e4cc8478c", d);
  }

  {
    Hash h;
    hash_string(
      h,
      "1234567890123456789012345678901234567890123456789012345678901234567890"
      "1"
      "234567890");
    hash_result_as_string(h, d);
    CHECK_STR_EQ("c2be0e534a67d25947f0c7e78527b2f82abd260f", d);
  }
}

TEST(hash_result_should_not_alter_state)
{
  char d[DIGEST_STRING_BUFFER_SIZE];
  Hash h;
  hash_string(h, "message");
  hash_result_as_string(h, d);
  hash_string(h, " digest");
  hash_result_as_string(h, d);
  CHECK_STR_EQ("6bfec6f65e52962be863d6ea1005fc5e4cc8478c", d);
}

TEST(hash_result_should_be_idempotent)
{
  char d[DIGEST_STRING_BUFFER_SIZE];
  Hash h;
  hash_string(h, "");
  hash_result_as_string(h, d);
  CHECK_STR_EQ("3345524abf6bbe1809449224b5972c41790b6cf2", d);
  hash_result_as_string(h, d);
  CHECK_STR_EQ("3345524abf6bbe1809449224b5972c41790b6cf2", d);
}

TEST(hash_copy_retains_result)
{
  char d[DIGEST_STRING_BUFFER_SIZE];
  Hash h;
  hash_string(h, "");
  Hash h2 = h;

  hash_result_as_string(h, d);
  CHECK_STR_EQ("3345524abf6bbe1809449224b5972c41790b6cf2", d);
  hash_result_as_string(h2, d);
  CHECK_STR_EQ("3345524abf6bbe1809449224b5972c41790b6cf2", d);
}

TEST(hash_copy_does_not_affect_original)
{
  char d[DIGEST_STRING_BUFFER_SIZE];
  Hash h;
  hash_string(h, "");
  Hash h2 = h;
  hash_string(h2, "a");

  hash_result_as_string(h, d);
  CHECK_STR_EQ("3345524abf6bbe1809449224b5972c41790b6cf2", d);
  hash_result_as_string(h2, d);
  CHECK_STR_EQ("948caa2db61bc4cdb4faf7740cd491f195043914", d);
}

TEST(hash_result_as_bytes)
{
  Hash h;
  hash_string(h, "message digest");
  struct digest d;
  hash_result_as_bytes(h, &d);
  uint8_t expected[sizeof(d.bytes)] = {0x6b, 0xfe, 0xc6, 0xf6, 0x5e, 0x52, 0x96,
                                       0x2b, 0xe8, 0x63, 0xd6, 0xea, 0x10, 0x05,
                                       0xfc, 0x5e, 0x4c, 0xc8, 0x47, 0x8c};
  CHECK_DATA_EQ(d.bytes, expected, sizeof(d.bytes));
}

TEST_SUITE_END
