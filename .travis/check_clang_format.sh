#!/bin/bash
# This script returns zero if and only if clang-format has been properly applied to a check.
# It is assuming current branch branched off of origin/master in order to compile list of to be formatted code.


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

  MERGE_BASE=$(git merge-base HEAD origin/master)
  DIFF_RANGE="HEAD $MERGE_BASE"
else
  # FIXME: for debugging in travis some output is needed:
  git status
  git log -10 --oneline

  if [ "$TRAVIS_PULL_REQUEST" == "false" ]; then
    # prepare travis according to https://github.com/travis-ci/travis-ci/issues/6069
    git config remote.origin.fetch "+refs/heads/*:refs/remotes/origin/*"
    git fetch --quiet

    # diff from current to common root (merge-base) of current and master
    # Note: HEAD is the same as ${TRAVIS_BRANCH} (branch to be merged)
    MERGE_BASE=$(git merge-base HEAD origin/master)
    DIFF_RANGE="HEAD $MERGE_BASE"
  else
    echo "Running PR on travis..."

    echo "TRAVIS_PULL_REQUEST_BRANCH: $TRAVIS_PULL_REQUEST_BRANCH"
    echo "TRAVIS_BRANCH: $TRAVIS_BRANCH"
    echo "--> current branch: ${TRAVIS_PULL_REQUEST_BRANCH:-$TRAVIS_BRANCH}"
    echo "---> diff branches with space: $(git diff --no-pager --name-only $TRAVIS_PULL_REQUEST_BRANCH $TRAVIS_BRANCH)"
    echo "---> diff branches with dots1: $(git diff --no-pager --name-only $TRAVIS_PULL_REQUEST_BRANCH $TRAVIS_PULL_REQUEST_BRANCH...$TRAVIS_BRANCH)"
    echo "---> diff branches with dots2: $(git diff --no-pager --name-only $TRAVIS_PULL_REQUEST_BRANCH...$TRAVIS_BRANCH $TRAVIS_BRANCH)"
    echo "---> diff branches with dots3: $(git diff --no-pager --name-only $TRAVIS_PULL_REQUEST_BRANCH...$TRAVIS_BRANCH)"
    # in case there have been other commits on master in the meantime
    echo "--> common root: $(git merge-base $TRAVIS_PULL_REQUEST_BRANCH $TRAVIS_BRANCH)"

    DIFF_RANGE="HEAD origin/$TRAVIS_BRANCH"
  fi
fi

GITCMD="git diff --name-only --diff-filter=AM $DIFF_RANGE"
echo "GITCMD: $GITCMD --> $($GITCMD)"

FILES_TO_CHECK=$($GITCMD | grep -v -E "^src/third_party/" | grep -E ".*\.(cpp|hpp)$")
echo "FILES_TO_CHECK: $FILES_TO_CHECK"

# if FILES_TO_CHECK is empty git diff will not recognize it as a given parameter and diff everything
if [ -z "${FILES_TO_CHECK}" ]; then
  echo "clang-format: passed (no relevant file changed)."
  exit 0
fi

FORMAT_DIFF=$(git diff -U0 $DIFF_RANGE -- ${FILES_TO_CHECK} | python .travis/clang-format-diff.py -p1 -style=file)

if [ -z "${FORMAT_DIFF}" ]; then
  echo "clang-format: passed."
  exit 0
else
  echo "clang-format: not properly formatted!"
  echo "$FORMAT_DIFF"
  exit 1
fi
