//! Builds the `clap-wrapper-shared-detail` static library — the core wrapper
//! code shared by every plugin format.
//!
//! Source of truth: cmake/shared_prologue.cmake:164-201
//! (`guarantee_clap_wrapper_shared`).
//!
//! Public include path: `src/` (so consumers can `#include "clap_proxy.h"`).
//! Public deps: clap headers, fmt headers (`libs/fmt`), the
//! `clap-wrapper-extensions` interface (just `include/`).

const std = @import("std");
const options = @import("options.zig");

pub const BuildArgs = struct {
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    /// Path to a CLAP SDK checkout. Must contain `include/clap/clap.h`.
    /// Mirrors cmake's CLAP_SDK_ROOT.
    clap_sdk_root: std.Build.LazyPath,
    opts: options.Options,
};

pub const Result = struct {
    lib: *std.Build.Step.Compile,
    args: BuildArgs,

    /// Apply this library's public include paths to a consumer target.
    /// Mirrors cmake's PUBLIC include propagation. Consumers still need to
    /// (1) call `consumer.linkLibrary(result.lib)`, (2) call
    /// `consumer.linkLibCpp()` themselves (Zig has no PUBLIC linkLibCpp),
    /// and (3) add the public compile flags via `options.publicFlags(...)`
    /// to their own per-source `flags` arrays.
    pub fn applyPublicIncludes(self: Result, b: *std.Build, consumer: *std.Build.Step.Compile) void {
        consumer.addIncludePath(b.path("src"));
        consumer.addIncludePath(b.path("include"));
        consumer.addIncludePath(b.path("libs/fmt"));
        consumer.addIncludePath(self.args.clap_sdk_root.path(b, "include"));
    }
};

pub fn build(b: *std.Build, args: BuildArgs) !Result {
    const arena = b.allocator;

    const lib = b.addStaticLibrary(.{
        .name = "clap-wrapper-shared-detail",
        .target = args.target,
        .optimize = args.optimize,
    });
    lib.linkLibCpp();

    // Public + private compile flags, combined for the lib's own TUs.
    var flags = std.ArrayList([]const u8).init(arena);
    try flags.appendSlice(try options.publicFlags(arena, args.opts));
    try flags.appendSlice(try options.privateFlags(arena, args.opts));
    try flags.appendSlice(options.macosFilesystemFlags(args.opts.platform));
    if (args.opts.enable_sanitizer) {
        try flags.appendSlice(&options.sanitizer_flags);
        // Note: sanitizer flags also need to be on the *link* step of the
        // final shared library / executable that consumes this static lib.
        // That's handled by whichever wrapper format step links us.
    }

    // Always-compiled C++ sources. Mirrors shared_prologue.cmake:173-181.
    lib.addCSourceFiles(.{
        .files = &.{
            "src/clap_proxy.cpp",
            "src/detail/shared/sha1.cpp",
            "src/detail/clap/fsutil.cpp",
        },
        .flags = flags.items,
    });

    // macOS-only Objective-C++ source. shared_prologue.cmake:195-200.
    if (args.opts.platform == .mac) {
        var mm_flags = std.ArrayList([]const u8).init(arena);
        try mm_flags.appendSlice(flags.items);
        // Force Obj-C++ language mode (Zig's clang infers from the .mm
        // extension, but being explicit is harmless and documents intent).
        try mm_flags.appendSlice(&.{ "-x", "objective-c++", "-fobjc-arc" });
        lib.addCSourceFile(.{
            .file = b.path("src/detail/clap/mac_helpers.mm"),
            .flags = mm_flags.items,
        });
        lib.linkFramework("Foundation");
        lib.linkFramework("CoreFoundation");
    }

    // Public include directories — replicated on every consumer of this lib.
    lib.addIncludePath(b.path("src"));
    lib.addIncludePath(b.path("include"));
    lib.addIncludePath(b.path("libs/fmt"));
    lib.addIncludePath(args.clap_sdk_root.path(b, "include"));

    return .{ .lib = lib, .args = args };
}
