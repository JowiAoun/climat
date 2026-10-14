#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the Android package from the working tree.
#
#   scripts/android.sh deps         install Qt for Android, the SDK and the NDK
#   scripts/android.sh openssl      build the OpenSSL libraries the package carries
#   scripts/android.sh configure    qt-cmake into build/android
#   scripts/android.sh apk          the debug-signed APK, for a phone on a cable
#   scripts/android.sh aab          the release bundle, which is what Play takes
#   scripts/android.sh install      adb install the APK on the connected phone
#   scripts/android.sh clean        delete build/android
#
# Run it inside `nix develop` for perl and patchelf, which `openssl` needs.
# Everything else comes from outside the devshell, and one of those is a
# surprise: cmake. nixpkgs patches CMake's default search prefixes to leave
# out /usr, which is right on NixOS and wrong for a cross build, where
# `<sysroot>/usr/include` is where the NDK keeps GLES2/gl2.h - so under the
# Nix cmake, Qt6Gui "could not be found because dependency GLESv2 could not
# be found". The SDK ships its own cmake and ninja; `deps` installs them and
# `configure` puts them first on PATH.
#
# Qt for Android is not in nixpkgs either and never will build in CI time, so
# `deps` puts the toolchain where Qt's own installer would, under ~/Qt and
# ~/Android/Sdk. Every path can be overridden:
#
#   ANDROID_SDK_ROOT     ~/Android/Sdk
#   ANDROID_NDK_ROOT     $ANDROID_SDK_ROOT/ndk/27.2.12479018
#   CLIMAT_QT_ANDROID    ~/Qt/6.11.1/android_arm64_v8a
#   CLIMAT_QT_HOST       the desktop Qt of the SAME version, for moc and
#                        qmlimportscanner: ~/Qt/6.11.1/gcc_64
#   QT_ANDROID_ABIS      arm64-v8a
#   CLIMAT_ANDROID_BUILD_DIR   build/android; set it to keep a second ABI's
#                        build beside the first
#   JAVA_HOME            a JDK 21 or newer
#
# The versions are Qt 6.11's requirements, from doc.qt.io's "Configure
# development environment" page: JDK 21, platform 36, build-tools 36.0.0, NDK
# 27.2.12479018. The Qt version matches the flake's, so the QML on a phone is
# the QML in every golden image.
#
# Signing. `aab` produces an unsigned bundle unless the keystore variables Qt
# reads are set when `configure` runs:
#
#   QT_ANDROID_KEYSTORE_PATH QT_ANDROID_KEYSTORE_ALIAS
#   QT_ANDROID_KEYSTORE_STORE_PASS QT_ANDROID_KEYSTORE_KEY_PASS
#
# docs/releasing.md says where the upload key lives and why it is never in
# this repository.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

qt_version="6.11.1"
ndk_version="27.2.12479018"
platform="android-36"
build_tools="36.0.0"
cmdline_tools="16111833"
cmake_version="3.31.6"

sdk="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
ndk="${ANDROID_NDK_ROOT:-$sdk/ndk/$ndk_version}"
qt_android="${CLIMAT_QT_ANDROID:-$HOME/Qt/$qt_version/android_arm64_v8a}"
qt_host="${CLIMAT_QT_HOST:-$HOME/Qt/$qt_version/gcc_64}"
abis="${QT_ANDROID_ABIS:-arm64-v8a}"
build_dir="${CLIMAT_ANDROID_BUILD_DIR:-$root/build/android}"
jobs="${CLIMAT_JOBS:-4}"

if [ -z "${JAVA_HOME:-}" ] && command -v java > /dev/null 2>&1; then
  JAVA_HOME="$(dirname "$(dirname "$(readlink -f "$(command -v java)")")")"
  export JAVA_HOME
fi

need() {
  if ! command -v "$1" > /dev/null 2>&1; then
    echo "android: $1 not found - run this inside \`nix develop\`" >&2
    exit 1
  fi
}

# The devshell points every Qt-finding variable at the Nix store's desktop Qt,
# which is right for every other build in this tree and wrong for this one:
# find_package(Qt6) would take the store's qtbase over the Android kit and fail
# on the first Android-only macro. Every CMake run here drops them, so the
# Android kit is the only Qt in sight.
android_env=(env -u QT_ADDITIONAL_PACKAGES_PREFIX_PATH -u CMAKE_PREFIX_PATH
             -u CMAKE_INCLUDE_PATH -u CMAKE_LIBRARY_PATH
             -u QT_PLUGIN_PATH -u QML_IMPORT_PATH -u QML2_IMPORT_PATH)

command="${1:-apk}"
shift || true

case "$command" in
  deps)
    need python3
    need curl
    need unzip
    if [ -z "${JAVA_HOME:-}" ]; then
      echo "android: no JDK - install one (21 or newer) and set JAVA_HOME" >&2
      exit 1
    fi

    # aqtinstall, in a venv of its own under build/, because it is the one
    # thing here that comes from PyPI and it should not touch the user's
    # python. It downloads Qt's own packages from download.qt.io.
    venv="$root/build/android-tools"
    if [ ! -x "$venv/bin/aqt" ]; then
      python3 -m venv "$venv"
      "$venv/bin/pip" install --quiet aqtinstall
    fi
    if [ ! -x "$qt_android/bin/qt-cmake" ]; then
      "$venv/bin/aqt" install-qt all_os android "$qt_version" android_arm64_v8a \
        -m qtpositioning -O "$(dirname "$(dirname "$qt_android")")"
    fi
    if [ ! -x "$qt_host/bin/moc" ] && [ ! -x "$qt_host/libexec/moc" ]; then
      "$venv/bin/aqt" install-qt linux desktop "$qt_version" linux_gcc_64 \
        -m qtpositioning -O "$(dirname "$(dirname "$qt_host")")"
    fi

    # The SDK. sdkmanager is what Qt's docs name; the command line tools zip
    # is what carries it. Licences are accepted by the `yes`, which is what
    # accepting them means on a machine with no window.
    tools="$sdk/cmdline-tools/latest/bin/sdkmanager"
    if [ ! -x "$tools" ]; then
      mkdir -p "$sdk/cmdline-tools"
      curl -sS -L -o "$sdk/cmdline-tools/tools.zip" \
        "https://dl.google.com/android/repository/commandlinetools-linux-${cmdline_tools}_latest.zip"
      (cd "$sdk/cmdline-tools" && unzip -q -o tools.zip && rm tools.zip \
         && rm -rf latest && mv cmdline-tools latest)
    fi
    yes 2> /dev/null | "$tools" --sdk_root="$sdk" \
      "platform-tools" "platforms;$platform" "build-tools;$build_tools" \
      "ndk;$ndk_version" "cmake;$cmake_version" \
      || true
    [ -d "$ndk" ] || { echo "android: the NDK did not install at $ndk" >&2; exit 1; }
    echo "android: Qt at $qt_android, host Qt at $qt_host, SDK at $sdk, NDK at $ndk"
    ;;

  openssl)
    # shellcheck disable=SC2086
    ANDROID_NDK_ROOT="$ndk" CLIMAT_JOBS="$jobs" "$here/android-openssl.sh" ${abis//,/ }
    ;;

  configure)
    for path in "$qt_android/bin/qt-cmake" "$qt_host/lib/cmake/Qt6/Qt6Config.cmake" \
                "$ndk/toolchains/llvm/prebuilt" "$sdk/platforms/$platform" \
                "$sdk/cmake/$cmake_version/bin/cmake"; do
      if [ ! -e "$path" ]; then
        echo "android: $path is missing - run \`scripts/android.sh deps\` first" >&2
        exit 1
      fi
    done
    if [ -z "${JAVA_HOME:-}" ]; then
      echo "android: no JDK - install one (21 or newer) and set JAVA_HOME" >&2
      exit 1
    fi

    signing=()
    if [ -n "${QT_ANDROID_KEYSTORE_PATH:-}" ]; then
      signing=(-DQT_ANDROID_SIGN_AAB=ON -DQT_ANDROID_SIGN_APK=ON)
      echo "android: signing with $QT_ANDROID_KEYSTORE_PATH"
    else
      echo "android: no QT_ANDROID_KEYSTORE_PATH - the bundle will be unsigned"
    fi

    # Release, tests off, dev tools off: the phone runs the product, and the
    # gallery and the probes are desktop programs that import Qt Test.
    PATH="$sdk/cmake/$cmake_version/bin:$PATH" \
    "${android_env[@]}" \
    "$qt_android/bin/qt-cmake" -S "$root" -B "$build_dir" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DQT_HOST_PATH="$qt_host" \
      -DANDROID_SDK_ROOT="$sdk" \
      -DANDROID_NDK_ROOT="$ndk" \
      -DQT_ANDROID_ABIS="$abis" \
      -DCLIMAT_BUILD_TESTS=OFF \
      -DCLIMAT_BUILD_GALLERY=OFF \
      -DCLIMAT_DEV_TOOLS=OFF \
      "${signing[@]}" "$@"
    ;;

  apk|aab)
    [ -f "$build_dir/build.ninja" ] || "$0" configure
    # The same clean environment as `configure`: a change to any CMakeLists.txt
    # makes ninja run CMake again from inside this build, and with the
    # devshell's variables still set that run finds the Nix store's QtDBus and
    # the build fails on a desktop header.
    "${android_env[@]}" \
    "$sdk/cmake/$cmake_version/bin/cmake" --build "$build_dir" --target "$command" -j "$jobs"
    echo
    find "$build_dir/app/android-build" -name '*.apk' -o -name '*.aab' | sort
    ;;

  install)
    apk="$(find "$build_dir/app/android-build" -name '*.apk' | head -1)"
    [ -n "$apk" ] || { echo "android: no APK - run \`scripts/android.sh apk\` first" >&2; exit 1; }
    "$sdk/platform-tools/adb" install -r "$apk"
    ;;

  clean)
    rm -rf "$build_dir"
    ;;

  *)
    sed -n '5,13p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' >&2
    exit 2
    ;;
esac
