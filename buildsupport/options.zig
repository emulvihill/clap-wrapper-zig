//! Compile-flag and macro helpers used by the Zig port of the CMake build.
//!
//! Mirrors the three CMake INTERFACE libraries:
//!   - clap-wrapper-compile-options-public  (defines + flags every consumer needs)
//!   - clap-wrapper-compile-options         (private warnings/errors)
//!   - clap-wrapper-sanitizer-options       (optional asan/ubsan)
//!
//! Source of truth: cmake/shared_prologue.cmake:46-100.

const std = @import("std");

pub const Platform = enum {
    mac,
    lin,
    win,
    ems,

    pub fn cmakeMacro(self: Platform) []const u8 {
        return switch (self) {
            .mac => "MAC",
            .lin => "LIN",
            .win => "WIN",
            .ems => "EMS",
        };
    }
};

pub fn detectPlatform(target: std.Target) Platform {
    if (target.cpu.arch.isWasm()) return .ems;
    return switch (target.os.tag) {
        .macos, .ios, .tvos, .watchos => .mac,
        .windows => .win,
        else => .lin,
    };
}

pub const Toolchain = enum { gcc_clang, msvc, clang_msvc };

pub fn detectToolchain(target: std.Target) Toolchain {
    return switch (target.abi) {
        .msvc => .msvc,
        else => .gcc_clang,
    };
}

pub const Options = struct {
    platform: Platform,
    toolchain: Toolchain,
    cxx_standard: u8,
    enable_sanitizer: bool,
    wrapper_version: []const u8,
};

/// Flags propagated to every consumer (matches cmake-wrapper-compile-options-public).
/// Caller owns the returned slice via `arena`.
pub fn publicFlags(arena: std.mem.Allocator, opts: Options) ![]const []const u8 {
    var list = std.ArrayList([]const u8).init(arena);

    // Platform define and version, applied to every target.
    try list.append(try std.fmt.allocPrint(arena, "-D{s}=1", .{opts.platform.cmakeMacro()}));
    try list.append(try std.fmt.allocPrint(arena, "-DCLAP_WRAPPER_VERSION=\"{s}\"", .{opts.wrapper_version}));

    switch (opts.toolchain) {
        .gcc_clang => {
            // C++20 char8_t suppression — VST3 SDK isn't compatible.
            if (opts.cxx_standard >= 20) {
                try list.append("-fno-char8_t");
            }
        },
        .msvc, .clang_msvc => {
            try list.append("/utf-8");
            try list.append("/Zc:__cplusplus");
            if (opts.cxx_standard >= 20) try list.append("/Zc:char8_t-");
        },
    }

    return list.toOwnedSlice();
}

/// Private warning/error flags (matches clap-wrapper-compile-options minus
/// the public flags it inherits from -public). Source: shared_prologue.cmake:62-76.
pub fn privateFlags(arena: std.mem.Allocator, opts: Options) ![]const []const u8 {
    var list = std.ArrayList([]const u8).init(arena);

    if (opts.toolchain == .gcc_clang) {
        try list.appendSlice(&.{
            "-Wall",
            "-Wextra",
            "-Wno-unused-parameter",
            "-Wpedantic",
        });
        // CMake conditionally drops -Werror on Windows-Clang because of
        // VST3 SDK warnings; everywhere else it's an error. We don't yet
        // build VST3 here so -Werror is reasonable on every gcc/clang.
        try list.append("-Werror");
        if (opts.platform == .win) {
            // Match shared_prologue.cmake:69-71 (gcc-on-Windows quirks).
            try list.appendSlice(&.{
                "-Wno-expansion-to-defined",
                "-Wno-unknown-pragmas",
            });
        }
    }

    return list.toOwnedSlice();
}

/// AddressSanitizer/UBSan flags — applied as both compile and link flags
/// when -Dsanitize is passed. Source: shared_prologue.cmake:84-98.
pub const sanitizer_flags = [_][]const u8{
    "-fsanitize=address,undefined,float-divide-by-zero",
    "-fsanitize-address-use-after-return=always",
    "-fsanitize-address-use-after-scope",
};

/// Whether the target's deployment SDK supports std::filesystem natively.
/// CMake conditionally swaps in ghc_filesystem on macOS < 10.15. We assume
/// modern macOS for now and emit the matching `-DMACOS_USE_STD_FILESYSTEM`
/// flag when applicable. If the project ever needs to build for 10.13/14
/// again, gulrak/filesystem can be added as a Zig package dep.
pub fn macosFilesystemFlags(platform: Platform) []const []const u8 {
    return switch (platform) {
        .mac => &.{"-DMACOS_USE_STD_FILESYSTEM"},
        else => &.{},
    };
}
