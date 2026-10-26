#!/usr/bin/env bash
set -euo pipefail

########################################
# Defaults
########################################
BUILD_TYPE="Release"
ARCH="x86_64"
STAGE_DIR="stage"
PROFILE="default"
BUILD_DOCS=0
CLEAN=0
GENERATOR=""
DEV_SDK=0

########################################
# Usage
########################################
usage() {
    echo "Usage: $0 -s <stage_dir> [-t Debug|Release] [-a <arch>] [-p <conan profile>] [-c] [-d] [-u] [-h]"
    echo ""
    echo "Options:"
    echo "  -s DIR        Production staging directory (required)"
    echo "  -t TYPE       Debug | Release (default: Release)"
    echo "  -a            Architecture (e.g. x86, x86_64, armv7hf), default: x86_64"
    echo "  -p PROFILE    Conan profile (default: default)"
    echo "  -c            Clean build"
    echo "  -d            Include development symlink (libims.so)"
    echo "  -u            Create User Documentation"
    echo "  -h            Help"
    exit 1
}

########################################
# Parse args
########################################
while getopts "s:t:a:p:cduh" opt; do
    case ${opt} in
        s) STAGE_DIR="$OPTARG" ;;
        t) BUILD_TYPE="$OPTARG" ;;
        a) ARCH="$OPTARG" ;;
        p) PROFILE="$OPTARG" ;;
        c) CLEAN=1 ;;
        d) DEV_SDK=1 ;;   # development SDK
        u) BUILD_DOCS=1 ;; # create documentation
        h) usage ;;
        *) usage ;;
    esac
done

if [[ -z "$STAGE_DIR" ]]; then usage; fi
if [[ "$BUILD_TYPE" != "Debug" && "$BUILD_TYPE" != "Release" ]]; then
    echo "Build type must be Debug or Release"
    exit 1
fi

ROOT_BUILD_DIR="build"
BUILD_DIR="$ROOT_BUILD_DIR/$BUILD_TYPE"
TOOLCHAIN_FILE="$BUILD_DIR/generators/conan_toolchain.cmake"

# Detect OS and architecture
OS_NAME="$(uname -s | tr '[:upper:]' '[:lower:]')"  # linux, darwin, etc.

# MSYS2 (MinGW) builds use the MSYS2 packages for Boost and libxml2 instead of Conan
MSYS_BUILD=0
case "$OS_NAME" in
    mingw*|msys*)
        MSYS_BUILD=1
        OS_NAME="windows"
        if [[ "${MSYSTEM:-MSYS}" == "MSYS" ]]; then
            echo "Error: run this script from an MSYS2 UCRT64 / MINGW64 / CLANG64 shell, not the plain MSYS shell"
            exit 1
        fi
        ;;
esac
#ARCH="$(uname -m)"                                   # x86_64, aarch64, i386
OS_ARCH="${OS_NAME}_${ARCH}"

# Prepare directories
INCLUDE_DIR="${STAGE_DIR}/include"
LIB_DIR="${STAGE_DIR}/${OS_ARCH}/lib"
DOCS_DIR="${STAGE_DIR}/docs"

mkdir -p "$INCLUDE_DIR" "$LIB_DIR" "$DOCS_DIR"

########################################
# Select build generator
########################################
if command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
elif command -v make >/dev/null 2>&1; then
    GENERATOR="Unix Makefiles"
else
    echo "Error: No suitable build tool found (Ninja or Make required)"
    exit 1
fi

echo "======================================="
echo "Build Type      : $BUILD_TYPE"
echo "Stage Dir       : $STAGE_DIR"
echo "Conan Profile   : $PROFILE"
echo "CMake Generator : $GENERATOR"
echo "======================================="

########################################
# Clean build
########################################
if [[ $CLEAN -eq 1 ]]; then
    echo "Cleaning build folders..."
    rm -rf "$ROOT_BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"

########################################
# Conan install
########################################
if [[ $MSYS_BUILD -eq 1 ]]; then
    echo "MSYS2 build: using MSYS2 packages for dependencies (skipping Conan)"
    echo "  Requires: pacman -S --needed \$MINGW_PACKAGE_PREFIX-{gcc,cmake,ninja,boost,libxml2}"
else
    echo "Running Conan install..."
    # Toolchain will be generated in build/generators
    conan install . --profile "$PROFILE" \
        -s build_type="$BUILD_TYPE" \
        -s arch="$ARCH" \
        -s compiler.cppstd=17 \
        --build=missing

    if [[ ! -f "$TOOLCHAIN_FILE" ]]; then
        echo "Error: Conan toolchain file not found: $TOOLCHAIN_FILE"
        exit 1
    fi
fi

########################################
# Configure CMake
########################################
echo "Configuring CMake..."
CMakeArgs=(-S . -B "$BUILD_DIR" -G "$GENERATOR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE")
if [[ $MSYS_BUILD -eq 0 ]]; then
    CMakeArgs+=(-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE")
fi
if [ "$BUILD_DOCS" -eq 1 ]; then
    CMakeArgs+=("-DBUILD_DOCS=ON")
fi
cmake "${CMakeArgs[@]}"

########################################
# Build
########################################
if [ "$BUILD_DOCS" -ne 1 ]; then
    echo "Building project..."
    # Only pass --config for multi-config generators (Visual Studio)
    if [[ "$GENERATOR" == "Ninja" || "$GENERATOR" == "Unix Makefiles" ]]; then
        cmake --build "$BUILD_DIR"
    else
        cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"
    fi

    ########################################
    # Stage artifacts
    ########################################
    echo "Staging include headers -> $INCLUDE_DIR"
    echo "Staging libraries -> $LIB_DIR"

    LIB_SRC_DIR="$BUILD_DIR"

    if [[ $MSYS_BUILD -eq 1 ]]; then
        # MinGW produces libimsLibrary[d].dll plus its import library libimsLibrary[d].dll.a
        if [[ "$BUILD_TYPE" == "Debug" ]]; then
            LIB_BASE="libimsLibraryd"
        else
            LIB_BASE="libimsLibrary"
        fi
        if [[ ! -f "$LIB_SRC_DIR/${LIB_BASE}.dll" ]]; then
            echo "Error: Could not locate ${LIB_BASE}.dll"
            exit 1
        fi
        cp -fv "$LIB_SRC_DIR/${LIB_BASE}.dll" "$LIB_SRC_DIR/${LIB_BASE}.dll.a" "$LIB_DIR/"
    else
    # Determine library base name
    if [[ "$BUILD_TYPE" == "Debug" ]]; then
        LIB_BASE="libimsd"
    else
        LIB_BASE="libims"
    fi

    # Find real versioned .so file (not symlink)
    REAL_LIB=$(find "$LIB_SRC_DIR" -maxdepth 1 -type f -name "${LIB_BASE}.so.*" | sort | head -n 1)

    if [[ -z "$REAL_LIB" ]]; then
        echo "Error: Could not locate versioned ${LIB_BASE}.so.*"
        exit 1
    fi

    REAL_LIB_NAME=$(basename "$REAL_LIB")

    # Extract major version (e.g. 2 from libims.so.2.0.7)
    MAJOR_VERSION=$(echo "$REAL_LIB_NAME" | sed -E 's/.*\.so\.([0-9]+).*/\1/')
    SONAME="${LIB_BASE}.so.$MAJOR_VERSION"

    # Copy real library
    cp -fv "$REAL_LIB" "$LIB_DIR/$REAL_LIB_NAME"

    # Create SONAME symlink (runtime linker needs this)
    ln -sf "$REAL_LIB_NAME" "$LIB_DIR/$SONAME"

    # Development symlink (optional)
    if [[ $DEV_SDK -eq 1 ]]; then
        ln -sf "$REAL_LIB_NAME" "$LIB_DIR/${LIB_BASE}.so"
    fi
    fi
    ########################################
    # Copy public headers from ims_api_h.cmake
    ########################################
    CMAKE_HEADER_FILE="cmake/ims_api_h.cmake"
    PUBLIC_HEADERS=()
    INSIDE_SET=0

    while IFS= read -r line; do
        line=$(echo "$line" | xargs)  # trim
        # Remove any trailing carriage return (\r)
        line=${line//$'\r'/}    
        if [[ "$line" =~ ^set\(\s*ims_public_api_header_files ]]; then
            INSIDE_SET=1
            continue
        fi
        if [[ $INSIDE_SET -eq 1 ]]; then
            if [[ "$line" =~ ^\) ]]; then
                INSIDE_SET=0
                continue
            fi
            # Strip ${api_include_dir}/ or ${api_lib_dir}/
            clean_line=$(echo "$line" | sed -E 's/\$\{[^}]+\}\///')
            if [[ -n "$clean_line" ]]; then
                PUBLIC_HEADERS+=("$clean_line")
            fi
        fi
    done < "$CMAKE_HEADER_FILE"

    # Copy public headers to staging area
    for header in "${PUBLIC_HEADERS[@]}"; do
        src="include/$header"
        dest="$INCLUDE_DIR/$header"
        mkdir -p "$(dirname "$dest")"
        cp "$src" "$dest"
    done
else
    # === Build Documentation Only ===
    echo "Building documentation (HTML)..."
    cmake --build "$BUILD_DIR" --target docs --config "$BUILD_TYPE"

    echo "Building documentation (PDF)..."
    if ! cmake --build "$BUILD_DIR" --target docs_pdf --config "$BUILD_TYPE"; then
        echo "PDF build failed, skipping..."
    fi

    # Read SDK version from CMakeCache.txt
    ProjectNumber="0.0.0"
    if [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
        ProjectNumber=$(grep "PROJECT_VERSION:STATIC=" "$BUILD_DIR/CMakeCache.txt" | cut -d '=' -f2)
    fi

    # Copy HTML and PDF to staging
    cp -r "$BUILD_DIR/docs/html" "$DOCS_DIR/"
    if [ -f "$BUILD_DIR/docs/latex/refman.pdf" ]; then
        VersionedPdf="$DOCS_DIR/IMS_SDK_v${ProjectNumber}_${BUILD_TYPE}.pdf"
        cp "$BUILD_DIR/docs/latex/refman.pdf" "$VersionedPdf"
    fi
fi

echo "======================================="
echo "Staging complete. Artifacts ready at '$STAGE_DIR'"
echo "======================================="
