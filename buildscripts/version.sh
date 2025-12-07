#!/bin/sh

set -e

version_file=".version"

prev_ver=0
[ -f "${version_file}" ] && prev_ver=$(cat "${version_file}")

ver=$((prev_ver + 1))

echo "${ver}" > "${version_file}"
echo "${ver}"