#!/usr/bin/env bash
set -euo pipefail

deps_dir="${1:?usage: fetch-deps.sh DEPS_DIR}"
libtorrent_version="${PS5TORRENT_LIBTORRENT_VERSION:-v2.0.12}"
boost_version="${PS5TORRENT_BOOST_VERSION:-boost-1.84.0}"
libtorrent_dir="${deps_dir}/libtorrent-${libtorrent_version#v}"
boost_dir="${deps_dir}/${boost_version}"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"

mkdir -p "${deps_dir}"

if [[ ! -d "${libtorrent_dir}/.git" ]]; then
  git clone --depth 1 --branch "${libtorrent_version}" --recurse-submodules \
    --shallow-submodules https://github.com/arvidn/libtorrent.git \
    "${libtorrent_dir}"
fi

git -C "${libtorrent_dir}" submodule update --init --depth 1

patch_file="${repo_root}/src_libtorrent/patches/0001-ps5-arc4random.patch"
if git -C "${libtorrent_dir}" apply --check "${patch_file}" 2>/dev/null; then
  git -C "${libtorrent_dir}" apply "${patch_file}"
fi

if [[ -d "${boost_dir}" && ! -f "${boost_dir}/boost/version.hpp" ]]; then
  rm -rf "${boost_dir}"
fi

if [[ ! -f "${boost_dir}/boost/version.hpp" ]]; then
  archive="${deps_dir}/${boost_version}.tar.xz"
  curl --fail --location --retry 3 \
    "https://github.com/boostorg/boost/releases/download/${boost_version}/${boost_version}.tar.xz" \
    --output "${archive}"
  tar -xf "${archive}" -C "${deps_dir}"
  rm -f "${archive}"
fi

if [[ ! -f "${boost_dir}/boost/version.hpp" ]]; then
  (cd "${boost_dir}" && ./bootstrap.sh && ./b2 headers)
fi

printf 'libtorrent: %s\n' "${libtorrent_dir}"
printf 'boost: %s\n' "${boost_dir}"
