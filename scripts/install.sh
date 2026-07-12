#!/usr/bin/env sh

set -eu

REPO="${LUMIERE_INSTALL_REPO:-SY-Technologies/lumiere}"
VERSION="latest"
PREFIX="${HOME}/.local"
BIN_DIR=""

usage() {
    cat <<EOF
Install the Lumiere CLI from GitHub Releases.

Usage:
  install.sh [--version v0.1.4] [--prefix ~/.local] [--bin-dir ~/.local/bin]

Options:
  --version   Release tag to install. Defaults to the latest release.
  --prefix    Install prefix used when --bin-dir is not provided. Defaults to ~/.local.
  --bin-dir   Directory where the lumiere binary will be installed.
  --help      Show this help message.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --version)
            VERSION="$2"
            shift 2
            ;;
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --bin-dir)
            BIN_DIR="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if [ -z "${BIN_DIR}" ]; then
    BIN_DIR="${PREFIX}/bin"
fi

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Missing required command: $1" >&2
        exit 1
    fi
}

need_cmd curl
need_cmd tar
need_cmd mktemp
need_cmd uname
need_cmd install

resolve_version() {
    if [ "${VERSION}" != "latest" ]; then
        printf '%s\n' "${VERSION}"
        return
    fi

    api_url="https://api.github.com/repos/${REPO}/releases/latest"
    resolved="$(curl -fsSL "${api_url}" | sed -n 's/.*"tag_name"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | head -n 1)"
    if [ -z "${resolved}" ]; then
        echo "Unable to resolve the latest release for ${REPO}" >&2
        exit 1
    fi
    printf '%s\n' "${resolved}"
}

platform_label() {
    os="$(uname -s)"
    arch="$(uname -m)"

    case "${os}" in
        Darwin)
            case "${arch}" in
                arm64)
                    printf 'macos-arm64\n'
                    ;;
                x86_64)
                    printf 'macos-x86_64\n'
                    ;;
                *)
                    echo "Unsupported macOS architecture: ${arch}" >&2
                    exit 1
                    ;;
            esac
            ;;
        Linux)
            case "${arch}" in
                x86_64|amd64)
                    printf 'linux-x86_64\n'
                    ;;
                *)
                    echo "Unsupported Linux architecture: ${arch}" >&2
                    exit 1
                    ;;
            esac
            ;;
        *)
            echo "Unsupported operating system: ${os}" >&2
            exit 1
            ;;
    esac
}

tag="$(resolve_version)"
label="$(platform_label)"
archive_name="lumiere-${tag}-${label}.tar.gz"
download_url="https://github.com/${REPO}/releases/download/${tag}/${archive_name}"

tmp_dir="$(mktemp -d)"
archive_path="${tmp_dir}/${archive_name}"
trap 'rm -rf "${tmp_dir}"' EXIT INT TERM

echo "Downloading ${download_url}"
curl -fL "${download_url}" -o "${archive_path}"

echo "Extracting ${archive_name}"
tar -xzf "${archive_path}" -C "${tmp_dir}"

binary_path="$(find "${tmp_dir}" -type f -name lumiere | head -n 1)"
if [ -z "${binary_path}" ]; then
    echo "Could not find the lumiere binary inside ${archive_name}" >&2
    exit 1
fi

mkdir -p "${BIN_DIR}"
install -m 755 "${binary_path}" "${BIN_DIR}/lumiere"

echo "Installed to ${BIN_DIR}/lumiere"
"${BIN_DIR}/lumiere" --version

case ":$PATH:" in
    *:"${BIN_DIR}":*)
        ;;
    *)
        echo "Add ${BIN_DIR} to your PATH if it is not already there."
        ;;
esac
