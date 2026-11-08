#!/usr/bin/env bash
set -Eeuo pipefail

APP_NAME="Chiptune Studio"
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
INSTALL_PREFIX="${INSTALL_PREFIX:-${HOME}/.local}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
RAVEN_COMMIT="89197438a59edde8efc38c9b48223e6721ec1816"

say() { printf '[%s] %s\n' "$APP_NAME" "$*"; }
fail() { printf '[%s] ERROR: %s\n' "$APP_NAME" "$*" >&2; exit 1; }
on_error() {
    local status=$?
    printf '[%s] ERROR: command failed at line %s (exit %s). Installation stopped.\n' \
        "$APP_NAME" "${BASH_LINENO[0]:-unknown}" "$status" >&2
    exit "$status"
}
trap on_error ERR

[[ "$(uname -s)" == Linux ]] || fail "This setup script supports Linux only."
[[ -f "$SCRIPT_DIR/CMakeLists.txt" ]] || fail "CMakeLists.txt was not found beside setup.sh."
if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
else
    fail "Cannot identify this Linux distribution (/etc/os-release is missing)."
fi

case "${ID:-}" in
    fedora)
        if command -v dnf5 >/dev/null 2>&1; then PKG_MANAGER=dnf5
        elif command -v dnf >/dev/null 2>&1; then PKG_MANAGER=dnf
        else fail "Fedora was detected, but neither dnf5 nor dnf is available."; fi
        BUILD_PACKAGES=(cmake gcc-c++ make git libX11-devel vulkan-headers alsa-lib-devel)
        RUNTIME_PACKAGES=(alsa-lib vulkan-loader libwayland-client libxkbcommon libdecor)
        ;;
    debian|ubuntu|linuxmint|pop)
        command -v apt-get >/dev/null 2>&1 || fail "${PRETTY_NAME:-$ID} was detected, but apt-get is unavailable."
        PKG_MANAGER=apt-get
        BUILD_PACKAGES=(cmake build-essential git libx11-dev libasound2-dev libvulkan-dev)
        RUNTIME_PACKAGES=(libvulkan1 libwayland-client0 libxkbcommon0 libdecor-0-0)
        ;;
    arch|manjaro|endeavouros)
        command -v pacman >/dev/null 2>&1 || fail "${PRETTY_NAME:-$ID} was detected, but pacman is unavailable."
        PKG_MANAGER=pacman
        BUILD_PACKAGES=(cmake gcc make git libx11 vulkan-headers alsa-lib)
        RUNTIME_PACKAGES=(vulkan-icd-loader wayland libxkbcommon libdecor)
        ;;
    *)
        fail "Unsupported distribution '${ID:-unknown}'. Supported distributions: Fedora, Debian/Ubuntu, Linux Mint/Pop!_OS, Arch/Manjaro/EndeavourOS."
        ;;
esac

if [[ "$PKG_MANAGER" == apt-get ]]; then
    if apt-cache show libasound2t64 >/dev/null 2>&1; then
        RUNTIME_PACKAGES+=(libasound2t64)
    else
        RUNTIME_PACKAGES+=(libasound2)
    fi
fi

run_privileged() {
    if (( EUID == 0 )); then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        fail "Installing missing system packages or files under $INSTALL_PREFIX requires root privileges; install sudo or rerun setup.sh as root."
    fi
}

package_installed() {
    local package=$1
    case "$PKG_MANAGER" in
        dnf|dnf5) rpm -q "$package" >/dev/null 2>&1 ;;
        apt-get) dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed' ;;
        pacman) pacman -Q "$package" >/dev/null 2>&1 ;;
    esac
}

missing_packages=()
for package in "${BUILD_PACKAGES[@]}" "${RUNTIME_PACKAGES[@]}"; do
    if package_installed "$package"; then
        say "Dependency already installed: $package"
    else
        say "Dependency missing: $package"
        missing_packages+=("$package")
    fi
done

if ((${#missing_packages[@]})); then
    say "Installing ${#missing_packages[@]} missing package(s) with $PKG_MANAGER."
    case "$PKG_MANAGER" in
        dnf|dnf5)
            run_privileged "$PKG_MANAGER" install -y --setopt=install_weak_deps=False "${missing_packages[@]}"
            ;;
        apt-get)
            run_privileged apt-get update
            run_privileged apt-get install -y --no-install-recommends "${missing_packages[@]}"
            ;;
        pacman)
            run_privileged pacman -S --needed --noconfirm "${missing_packages[@]}"
            ;;
    esac
else
    say "All required build and runtime dependencies are already installed."
fi

command -v cmake >/dev/null 2>&1 || fail "CMake is still unavailable after dependency installation."
command -v c++ >/dev/null 2>&1 || command -v g++ >/dev/null 2>&1 || fail "A C++ compiler is unavailable after dependency installation."
command -v git >/dev/null 2>&1 || fail "Git is required to fetch the Raven UI source."

mkdir -p -- "$BUILD_DIR"
BUILD_DIR="$(cd -- "$BUILD_DIR" && pwd)"
RAVEN_SOURCE_DIR="$BUILD_DIR/raven-src"
if [[ -d "$RAVEN_SOURCE_DIR/.git" ]]; then
    current_raven_commit="$(git -C "$RAVEN_SOURCE_DIR" rev-parse HEAD)"
    if [[ "$current_raven_commit" != "$RAVEN_COMMIT" ]]; then
        if [[ -n "$(git -C "$RAVEN_SOURCE_DIR" status --porcelain)" ]]; then
            fail "The Raven dependency directory has local changes: $RAVEN_SOURCE_DIR"
        fi
        say "Updating the cached Raven source to the pinned revision."
        git -C "$RAVEN_SOURCE_DIR" fetch --depth=1 origin "$RAVEN_COMMIT"
        git -C "$RAVEN_SOURCE_DIR" checkout --detach FETCH_HEAD
    else
        say "Reusing the pinned Raven UI source."
    fi
elif [[ -e "$RAVEN_SOURCE_DIR" ]]; then
    fail "Raven dependency path exists but is not a Git checkout: $RAVEN_SOURCE_DIR"
else
    say "Fetching Raven UI source at revision $RAVEN_COMMIT."
    git init "$RAVEN_SOURCE_DIR"
    git -C "$RAVEN_SOURCE_DIR" remote add origin https://github.com/oscar-soirey/Raven.git
    git -C "$RAVEN_SOURCE_DIR" fetch --depth=1 origin "$RAVEN_COMMIT"
    git -C "$RAVEN_SOURCE_DIR" checkout --detach FETCH_HEAD
fi

say "Configuring CMake project in $BUILD_DIR."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DFETCHCONTENT_SOURCE_DIR_RAVEN="$RAVEN_SOURCE_DIR" \
    -DCHIPTUNE_BUILD_APP=ON \
    -DCHIPTUNE_BUILD_WAV_INFO=ON

say "Building Chiptune Studio."
cmake --build "$BUILD_DIR" --parallel

say "Installing into $INSTALL_PREFIX."
install_without_sudo=0
if [[ -d "$INSTALL_PREFIX" ]]; then
    [[ -w "$INSTALL_PREFIX" ]] && install_without_sudo=1
else
    install_parent="$(dirname -- "$INSTALL_PREFIX")"
    [[ -w "$install_parent" ]] && install_without_sudo=1
fi
if (( install_without_sudo )); then
    mkdir -p -- "$INSTALL_PREFIX"
    cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
else
    run_privileged cmake --install "$BUILD_DIR" --prefix "$INSTALL_PREFIX"
fi

say "Installation complete."
APP_EXECUTABLE="$INSTALL_PREFIX/bin/chiptune_studio"
say "Installed application: $APP_EXECUTABLE"
say "The WAV inspection utility is also available at: $INSTALL_PREFIX/bin/chiptune_wav_info"
say "Build directory: $BUILD_DIR (reused on the next run)"
say "Desktop launcher: Chiptune Studio (installed in $INSTALL_PREFIX/share/applications)"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$INSTALL_PREFIX/share/applications" >/dev/null 2>&1 || true
fi
if [[ "${RUN_APP:-1}" == 1 ]]; then
    say "Launching Chiptune Studio."
    "$APP_EXECUTABLE"
else
    say "RUN_APP=0 skipped launching Chiptune Studio."
fi
