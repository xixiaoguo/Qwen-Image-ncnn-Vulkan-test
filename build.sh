#!/usr/bin/env bash
# Build qwenimage-gui on Kubuntu 26.04
set -e

echo "==> checking build dependencies"
missing=()
for p in cmake g++ pkg-config; do
    command -v "$p" >/dev/null 2>&1 || missing+=("$p")
done

# FLTK 1.4 or newer is required: 1.4 is the first release with a Wayland
# backend, and this program runs on both X11 and Wayland. A 1.3 installation
# also answers pkg-config, so the version is checked, not just its presence.
#
# Three sources are tried in order, because installations differ: the local SDK
# (via FLTK_DIR), pkg-config, and finally the headers themselves. A distribution
# install has /usr/include/FL but ships no fltk.pc at all, so pkg-config alone
# would report "not found" on a perfectly good system.
fltk_ver=""
fltk_src=""
if [ -n "$FLTK_DIR" ] && [ -f "$FLTK_DIR/FLTKConfig.cmake" ]; then
    # The local SDK sets FLTK_DIR, and this cmake package is what the build
    # below actually consumes, so it is the authoritative version source.
    # Its pkg-config file may be stale and report an older version.
    fltk_ver=$(sed -n \
        's/^set(FLTK_VERSION[[:space:]]*\([0-9][0-9.]*\).*/\1/p' \
        "$FLTK_DIR/FLTKConfig.cmake" | head -n1)
    fltk_src="$FLTK_DIR"
fi
if [ -z "$fltk_ver" ]; then
    fltk_ver=$(pkg-config --modversion fltk 2>/dev/null || true)
    if [ -n "$fltk_ver" ]; then fltk_src="pkg-config"; fi
fi
if [ -z "$fltk_ver" ]; then
    for inc in /usr/include /usr/local/include; do
        [ -f "$inc/FL/Enumerations.H" ] || continue
        major=$(sed -n 's/^#define FL_MAJOR_VERSION[[:space:]]*\([0-9]*\).*/\1/p' \
                "$inc/FL/Enumerations.H")
        minor=$(sed -n 's/^#define FL_MINOR_VERSION[[:space:]]*\([0-9]*\).*/\1/p' \
                "$inc/FL/Enumerations.H")
        patch=$(sed -n 's/^#define FL_PATCH_VERSION[[:space:]]*\([0-9]*\).*/\1/p' \
                "$inc/FL/Enumerations.H")
        if [ -n "$major" ]; then
            if [ -n "$patch" ]; then
                fltk_ver="$major.$minor.$patch"
            else
                fltk_ver="$major.$minor"
            fi
            fltk_src="$inc"
            break
        fi
    done
fi

fltk_ok=0
if [ -n "$fltk_ver" ]; then
    # sort -V orders 1.4 before 1.4.4 and before 1.10 alike.
    if [ "$(printf '%s\n1.4\n' "$fltk_ver" | sort -V | head -n1)" = "1.4" ]; then
        fltk_ok=1
    else
        echo "Found FLTK $fltk_ver, but 1.4 or newer is required (Wayland backend)."
    fi
fi
if [ "$fltk_ok" = 0 ]; then
    missing+=("libfltk1.4-dev")
fi

# FLTK's own headers pull in <cairo.h> (Fl_Cairo.H), so the cairo development
# files are required even though nothing here calls Cairo directly. Without them
# the build dies inside FLTK's headers with a confusing "cairo.h: No such file".
if ! pkg-config --exists cairo 2>/dev/null; then
    missing+=("libcairo2-dev")
fi

if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing: ${missing[*]}"
    echo "Install with: sudo apt install build-essential cmake libfltk1.4-dev libcairo2-dev libpng-dev"
    echo "Or point FLTK_DIR at an existing FLTK 1.4 install (see README)."
    exit 1
fi
echo "    FLTK $fltk_ver from $fltk_src"

echo "==> configuring"
# FLTK_DIR may come from the environment (see the local SDK's env.sh). Passing
# it explicitly beats CMake's search, which would otherwise pick up any other
# FLTK installation it finds first.
extra=()
if [ -n "$FLTK_DIR" ]; then
    extra+=("-DFLTK_DIR=$FLTK_DIR")
    echo "    using FLTK from $FLTK_DIR"
fi
cmake -S "$(dirname "$0")" -B "$(dirname "$0")/build" -DCMAKE_BUILD_TYPE=Release "${extra[@]}"

echo "==> building"
cmake --build "$(dirname "$0")/build" -j"$(nproc)"

echo
echo "Build finished: $(dirname "$0")/build/qwenimage-gui"
echo "Run it with:   ./build/qwenimage-gui"
