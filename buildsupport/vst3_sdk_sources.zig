//! Curated source list for the future `base-sdk-vst3` static library
//! produced by the in-progress Zig port of the CMake build.
//!
//! Source of truth: cmake/base_sdks.cmake:129-167 — `guarantee_vst3sdk()`.
//! Targets steinbergmedia/vst3sdk @ v3.8.0_build_66 with submodules
//! `base`, `public.sdk`, `pluginterfaces`, `cmake`.
//!
//! Lives under `buildsupport/` rather than `build/` because the latter is
//! gitignored (CMake-output convention) while the CMake build still exists.
//!
//! Run `tools/check_vst3_sdk_sources.sh <VST3_SDK_ROOT>` to validate this
//! list against an SDK checkout. CI should invoke that check after each
//! SDK pin bump.
//!
//! All paths are relative to VST3_SDK_ROOT. The four "always-compiled"
//! sections below mirror the four CMake `file(GLOB ...)` invocations; the
//! per-platform sections mirror the `if (APPLE)` / `elseif (UNIX)` / `else()`
//! branch that picks the entry-point .cpp.

/// Files compiled on every platform.
///
/// Note on platform-suffixed filenames (`*_linux.cpp`, `*_win32.cpp`):
/// each is wrapped in `#if SMTG_OS_LINUX` / `#if SMTG_OS_WINDOWS`, so on
/// the wrong OS they expand to an empty translation unit. We feed them all
/// to the compiler unconditionally to match the CMake glob behavior.
pub const common = [_][]const u8{
    // base/source/*.cpp
    "base/source/baseiids.cpp",
    "base/source/fbuffer.cpp",
    "base/source/fdebug.cpp",
    "base/source/fdynlib.cpp",
    "base/source/fobject.cpp",
    "base/source/fstreamer.cpp",
    "base/source/fstring.cpp",
    // SDK < 3.7.9 has broken Linux timer code; CMake removes timer.cpp on
    // Linux in that case. v3.8.0_build_66 is fixed, so we keep it. If the
    // SDK pin is ever rolled back below 3.7.9, reintroduce the conditional
    // drop in the consumer (build.zig).
    "base/source/timer.cpp",
    "base/source/updatehandler.cpp",

    // base/thread/source/*.cpp
    "base/thread/source/fcondition.cpp",
    "base/thread/source/flock.cpp",

    // public.sdk/source/common/*.cpp
    "public.sdk/source/common/commoniids.cpp",
    "public.sdk/source/common/commonstringconvert.cpp",
    "public.sdk/source/common/memorystream.cpp",
    "public.sdk/source/common/openurl.cpp",
    "public.sdk/source/common/pluginview.cpp",
    "public.sdk/source/common/readfile.cpp",
    "public.sdk/source/common/systemclipboard_linux.cpp",
    "public.sdk/source/common/systemclipboard_win32.cpp",
    "public.sdk/source/common/threadchecker_linux.cpp",
    "public.sdk/source/common/threadchecker_win32.cpp",

    // pluginterfaces/base/*.cpp
    "pluginterfaces/base/conststringtable.cpp",
    "pluginterfaces/base/coreiids.cpp",
    "pluginterfaces/base/funknown.cpp",
    "pluginterfaces/base/ustring.cpp",

    // public.sdk/source/main (always; per-platform entry below)
    "public.sdk/source/main/pluginfactory.cpp",
    "public.sdk/source/main/moduleinit.cpp",

    // public.sdk/source/vst — DELIBERATELY CURATED. Excluded files fall
    // into three categories:
    //   1. Compiled-via-`#include` from another TU; listing them here too
    //      would produce duplicate symbols at link time:
    //        vsteditcontroller.cpp  (included from vstsinglecomponenteffect.cpp)
    //   2. Require linking against VSTGUI:
    //        vstgui_linux_runloop_support.cpp,
    //        vstgui_win32_bundle_support.cpp,
    //        vstguieditor.cpp
    //   3. Functionality unused by the wrapper:
    //        vstpresetfile.cpp, vstrepresentation.cpp
    // Do NOT replace this list with a glob — categories 1 and 2 would break.
    // Note: `vsteditcontroller`'s symbols ARE present in the resulting static
    // lib, supplied via the `#include` from vstsinglecomponenteffect.cpp.
    "public.sdk/source/vst/vstinitiids.cpp",
    "public.sdk/source/vst/vstnoteexpressiontypes.cpp",
    "public.sdk/source/vst/vstsinglecomponenteffect.cpp",
    "public.sdk/source/vst/vstaudioeffect.cpp",
    "public.sdk/source/vst/vstcomponent.cpp",
    "public.sdk/source/vst/vstcomponentbase.cpp",
    "public.sdk/source/vst/vstbus.cpp",
    "public.sdk/source/vst/vstparameters.cpp",

    // public.sdk/source/vst/utility — also curated. Excluded:
    //   dataexchange.cpp, mpeprocessor.cpp, systemtime.cpp, testing.cpp,
    //   vst2persistence.cpp
    "public.sdk/source/vst/utility/stringconvert.cpp",
};

/// macOS-only entry point.
pub const macos = [_][]const u8{
    "public.sdk/source/main/macmain.cpp",
};

/// Linux-only entry point.
pub const linux = [_][]const u8{
    "public.sdk/source/main/linuxmain.cpp",
};

/// Windows-only entry point.
pub const windows = [_][]const u8{
    "public.sdk/source/main/dllmain.cpp",
};

/// Public include directories for any target that links `base-sdk-vst3`,
/// matching `target_include_directories(base-sdk-vst3 PUBLIC ...)` at
/// cmake/base_sdks.cmake:172.
pub const public_include_dirs = [_][]const u8{
    "",
    "public.sdk",
    "pluginterfaces",
};
