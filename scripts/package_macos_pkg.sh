#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: package_macos_pkg.sh <version> <stage-root> <output-pkg>" >&2
  exit 1
fi

version="$1"
stage_root="$2"
output_pkg="$3"

if [[ ! -d "$stage_root" ]]; then
  echo "Stage root does not exist: $stage_root" >&2
  exit 1
fi

identifier="com.sytechnologies.lumiere"
binary_path="$stage_root/usr/local/bin/lumiere"

if [[ ! -f "$binary_path" ]]; then
  echo "Expected staged binary at $binary_path" >&2
  exit 1
fi

pkgbuild \
  --root "$stage_root" \
  --identifier "$identifier" \
  --version "$version" \
  --install-location / \
  --ownership recommended \
  "$output_pkg"
