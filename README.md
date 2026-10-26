# iMS Control Library

## Overview

The iMS Control Library is a modern C++ library used to develop control system software for **Isomet Acousto-Optic driver electronics**.

Isomet designs and manufactures precision acousto-optic devices and driver systems for OEM scientific and industrial applications.

🌐 Company website: https://www.isomet.com

This library provides the core control and hardware abstraction layer used by Isomet software systems and partner integrations.

---

## Purpose

The IMS Control Library enables developers to:

- Interface with Isomet acousto-optic driver electronics
- Abstract low-level hardware communication
- Control frequency, amplitude, timing, and channel configuration
- Integrate Isomet hardware into custom applications
- Build higher-level software systems on a stable control API

It is intended for use in:

- Desktop control software
- Industrial control systems
- Laboratory instrumentation software
- Embedded control applications
- Real-time control environments

---

## Embedded & Real-Time Suitability

In addition to desktop applications, the library is suitable for deployment in embedded and real-time environments, including:

- Embedded Linux platforms
- QNX Neutrino RTOS
- Industrial control PCs
- Dedicated hardware controller systems

Design characteristics supporting embedded and RTOS deployment:

- No external runtime dependencies
- Deterministic control paths
- Clear hardware abstraction boundaries
- CMake-based cross-compilation support
- Support for static or shared linking

The codebase is structured to allow integration into production embedded control systems where reliability and predictable behavior are critical.

---

## Key Features

- Modern C++ implementation
- Cross-platform support (Windows / Linux / Embedded Linux / QNX)
- CMake-based build system
- Optional shared library (DLL / SO) build
- Suitable for static linking in embedded systems
- Doxygen-based API documentation
- Designed for integration into larger systems

---

# Quick Start

There are three ways of developing C++ software using the iMS Library

1. Install the pre-compiled library from the Isomet SDK
1. Build the library from source
1. Integrate the library code directly into your build process

## Option 1 - Install pre-compiled library binaries

For simple build requirements on popular platforms (e.g. Windows 10/11), this is by far the quickest and easiest option.

### Windows 10 / 11

Download the Windows 10 / 11 Isomet iMS SDK from the Isomet website: [iMS SDK](https://isomet.com/ims4_sw.html)

### Ubuntu Linux

We are working on support for installing the iMS Control Library directly from the Ubuntu apt server.  Please check back later.

## Option 2 - Build the library from source

Use this option when support for a specific OS, runtime library or compiler is required.

### 1. Clone the Repository

```bash
git clone https://github.com/Isomet-Corporation/ims-lib
cd ims-lib
```

---

### 2. Build the Library

The library uses a CMake build front end with Conan dependency management. Build scripts in the repository automate the configuration and build process.

#### Windows (Visual Studio)

In a Powershell prompt:

```powershell
.\build.ps1 -BuildType Release
```

Optional flags:
 
```powershell
   -BuildType [Debug|Release]
   -BuildDocs                  <= Only Create the user documentation
   -CompilerVersion            <= Select MSVC version (e.g. 193 = VS2022)
   -Arch                       <= Select Platform Architecutre (e.g. x86, x86_64, armv7hf)
   -StageDir [dir]             <= Change the build artifact location
   -Clean                      <= Cleans the build folder before running the build
   -Profile [profile]          <= Choose a non-default Conan profile
``` 

#### Windows (MSYS2 / MinGW, 64-bit only)

From an MSYS2 **UCRT64** (or MINGW64 / CLANG64) shell, install the toolchain and dependencies, then run the same script as Linux. Conan is not used; Boost and libxml2 come from MSYS2 packages.

```bash
pacman -S --needed $MINGW_PACKAGE_PREFIX-{gcc,cmake,ninja,boost,libxml2}
./build.sh -s <stage_dir>
```

This produces `libimsLibrary.dll` and its import library `libimsLibrary.dll.a`. The FTDI driver is linked dynamically, so at runtime `ftd2xx.dll` (installed with the FTDI D2XX driver) must be available, along with the MSYS2 runtime DLLs for Boost, libxml2 and libstdc++.

#### Linux / Embedded Linux

```bash
./build.sh -s <stage_dir>
```

Optional flags:

```bash
   -c                       <= Cleans the build folder before running the build
   -a                       <= Select the architecture (e.g. x86, x86_64, armv7hf)
   -t <Debug|Release>       <= Change the build configuration
   -p <profile>             <= Choose a non-default Conan profile
   -d                       <= Include a library symlink (libims.so)
   -u                       <= Build User Documentation
```

Artifacts will be staged to stage_dir (default 'stage' in the current working directory)

```
stage/
 ├── include/
 ├── <os>_<arch>/lib/
 └── docs/
```

In your application project, include headers from:

```
stage/include
```

and link against the library located in:

```
stage/<os>_<arch>/lib
```

---

## Option 3 - Integrate Directly Into Your Application

Instead of building a separate dynamic library, you can integrate the library source code from this repository as a Git submodule:

```bash
git submodule add https://github.com/Isomet-Corporation/ims-lib external/ims-lib
```

In your `CMakeLists.txt`:

```cmake
add_subdirectory(external/ims-lib)
target_link_libraries(MyApplication PRIVATE ims)
```

Advantages of this approach:

- No separate SDK packaging during development
- Unified compiler and build configuration
- Easier cross-compilation
- Improved optimization and debugging
- Reduced deployment complexity for embedded targets

---

## Alternative Options

The iMS Control LIbrary is written in C++ as this provides:

- Low latency hardware control for real-time operation
- Superior performance for performance critical applications
- Direct control of hardware and device drivers
- Easy integration with manufacturer device libraries

Where high performance application design is critical, we recommend designing your iMS application in C++ linked to the iMS Control Library using one of the above options.

However, we recognise that C++ native application design is not a straightforward engineering task, therefore the following options for iMS application design are also available:

1. Python.  The iMS Lib Python extension [here](https://github.com/Isomet-Corporation/imslib-python) allows straightforward programming of iMS systems using simple Python scripts or more extensive Python applications alongside the full ecosystem of Python frameworks and libraries.  The imslib Python extension is a wrapper around the C++ library and exposes the full library functionality without the requirement to program in C++.
2. C#.  The iMS C# extension is installed as part of the Isomet SDK.  It is a wrapper around the C++ library exposing the full library functionality and allows GUI development of iMS applications in the cross-platform .NET environment.
3. iMS Hardware Server.  This application is built on top of the C++ library and runs as a background process, connecting to attached iMS hardware.  The server provides a [gRPC](https://grpc.io/) service over any private or public network.  Client applications written in any gRPC-supported language can communicate with the server and instruct it to perform a function (e.g. control iMS power, single tone mode, download compensation tables, play images). 

# Build Requirements

## Core Build Dependencies

### Windows

- Visual Studio (MSVC toolchain; 2022 or newer recommended)
- CMake (>= 3.20 recommended)
- Ninja (optional but recommended)
- Conan
- Git

### Linux / Embedded Linux

- GCC (>= 7) or Clang toolchain
- CMake (>= 3.20)
- Ninja (optional but recommended)
- Conan
- Git
- build-essential
- xxd

Example (Ubuntu):

```bash
sudo apt install build-essential cmake ninja-build git xxd
```

For QNX Neutrino RTOS builds, use the appropriate QNX toolchain file with CMake.

### Conan

The project use Conan for build dependency resolution.  Please install conan using the conan documentation as a guide: https://docs.conan.io/2/installation.html

Conan is usually installed using the Python Pip package manager but can be installed from a binary package downloaded from the above location

---

# Documentation Build (Optional)

Documentation generation requires Doxygen and LaTeX.

These dependencies are **not required** for building the control library itself.

## Windows

- Doxygen
- Graphviz (optional for diagrams)
- MiKTeX (with `pdflatex` available in PATH)

## Linux (Ubuntu)

```bash
sudo apt install \
  doxygen \
  graphviz \
  texlive-latex-base \
  texlive-latex-recommended \
  texlive-latex-extra \
  texlive-fonts-recommended
```

---

## Build Documentation Only

### Windows

```powershell
.\build.ps1 -BuildDocs
```

### Linux

```bash
./build.sh -u
```

This enables `BUILD_DOCS=ON` in CMake and builds:

- `docs` (HTML)
- `docs_pdf` (PDF)

Generated files are staged to:

```
stage/docs/
```

Built documentation is deployed online to Github Pages using the deploy.sh and deploy.ps1 scripts (repo write access required)
---

# Output Structure

```
stage/
 ├── include/
 ├── windows_x64/lib/
 ├── linux_x86_64/lib/
 ├── linux_arm64/lib/
 └── docs/
```

The structure supports multi-architecture staging for deployment packaging.

---

# Releases

Pushing a tag `v<version>` (a release) or `v<version>-<label>` (a pre-release, e.g. `v2.0.10-rc1`) runs `.github/workflows/ci.yml`, which:

1. Checks the tag against `IMS_API_MAJOR/MINOR/PATCH` in `include/LibVersion.h`, so bump the version before tagging.
2. Creates a GitHub Release with the Windows (x86_64, x86) and Linux libraries as zips.
3. Pushes a tag `ims-lib-<tag>` to [imslib-python](https://github.com/refeyn/imslib-python), on a commit that moves its `ims-lib` submodule to this tag. That starts its wheel build, which publishes the wheels as an imslib-python GitHub Release.

Step 3 needs two deploy keys, since `GITHUB_TOKEN` cannot reach another repository:

| Used for | Public half | Private half (Actions secret) |
|---|---|---|
| This repository's CI pushes the trigger tag to imslib-python | imslib-python deploy key, write access | `IMSLIB_PYTHON_DEPLOY_KEY` in ims-lib |
| imslib-python's CI fetches this repository as its submodule | ims-lib deploy key, read-only | `IMS_LIB_DEPLOY_KEY` in imslib-python |

The trigger commit is made on imslib-python's `master`. To test an imslib-python branch first, set the ims-lib Actions variable `IMSLIB_PYTHON_BRANCH` to it.

---

# Example Integration: iMS Hardware Server

The iMS Hardware Server integrates this repository as a Git submodule and compiles it directly into the application.

This avoids distributing standalone DLL/SO files during development and ensures consistent build configuration across environments.

---

# Related Projects

This library forms the foundation for:

- iMS Hardware Server
- iMS Studio
- the Python wrapped library (ctypes / pybind11)
- C# wrapped library (P/Invoke / C++/CLI bridge)

Repository links to be provided.

---

# Online Documentation

Live API documentation:  https://isomet-corporation.github.io/ims-lib/

The online documentation mirrors the local Doxygen-generated HTML documentation.

---

# License

This project is licensed under the **MIT License**.

The MIT License is a permissive open source license that:

- Allows commercial use
- Allows modification and redistribution
- Allows static or dynamic linking
- Provides limited liability protection
- Requires preservation of copyright and license notice

See the `LICENSE` file in this repository for the full license text.

---

# Support

For hardware or product inquiries:

https://www.isomet.com

For software issues or feature requests, please open a GitHub issue in this repository,
or contact Isomet directly