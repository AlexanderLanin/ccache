#!/bin/bash
# This script returns zero if and only if clang-format has been properly applied to a check.


# Copyright (C) 2020 Joel Rosdahl and other contributors
#
# See doc/AUTHORS.adoc for a complete list of contributors.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 3 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

if [ -z "$TRAVIS_BRANCH" ]; then
  echo "Running locally..."
  DIFFBRANCH="origin/master"
  #RANGE="origin/master"
elif [ "$TRAVIS_PULL_REQUEST" == "false" ]; then
  # how to determine where we branched of if there is no 'master'?
  #RANGE="$(git merge-base HEAD master)..HEAD"
  echo "Running non PR on travis..."
  DIFFBRANCH="HEAD^1"
  #RANGE="HEAD"
else
  echo "Running PR on travis..."
  DIFFBRANCH=$TRAVIS_BRANCH
  #RANGE="HEAD..$TRAVIS_BRANCH"
fi

GITCMD="git diff --name-only --diff-filter=AM HEAD..$DIFFBRANCH"
echo "GITCMD: $GITCMD"

FILES_TO_CHECK=$($GITCMD | grep -v -E "^src/third_party/" | grep -E ".*\.(cpp|hpp)$")
echo "FILES_TO_CHECK: $FILES_TO_CHECK"

# if FILES_TO_CHECK is empty git diff will not recognize it as a given parameter and diff everything
if [ -z "${FILES_TO_CHECK}" ]; then
  echo "clang-format: passed (no relevant file changed)."
  exit 0
fi

FORMAT_DIFF=$(git diff -U0 $DIFFBRANCH -- ${FILES_TO_CHECK} | python .travis/clang-format-diff.py -p1 -style=file)

if [ -z "${FORMAT_DIFF}" ]; then
  echo "clang-format: passed."
  exit 0
else
  echo "clang-format: not properly formatted!"
  echo "$FORMAT_DIFF"
  exit 1
fi
