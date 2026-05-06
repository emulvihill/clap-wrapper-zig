//! Top-level build for the in-progress Zig port of the CMake build system.
//!
//! Currently wired up:
//!   - `clap-wrapper-shared-detail` static library (from
//!     buildsupport/clap_wrapper_shared.zig)
//!   - AUv2 codegen prototype, behind `-Dauv2-prototype` (macOS only;
//!     see buildsupport/auv2_codegen.zig)
//!
//! Not yet ported (still build only via CMake):
//!   - VST3, AUv2 (full), CLAP, Standalone, WCLAP wrappers
//!   - tests/ subdirectory
//!   - dependency manifest in build.zig.zon (SDK paths via -D options)

const std = @import("std");
const options_mod = @import("buildsupport/options.zig");
const shared = @import("buildsupport/clap_wrapper_shared.zig");
const auv2_codegen = @import("buildsupport/auv2_codegen.zig");

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Options mirroring cmake/CMakeLists.txt + base_sdks.cmake. Each maps
    // 1:1 to the cmake equivalent so users can transfer their existing
    // configuration.

    // Path-style options. CMake fallback chain (libs/, ., ..) is not
    // replicated yet; we require an explicit path via -D for now.
    const clap_sdk_root_str = b.option(
        []const u8,
        "clap-sdk-root",
        "Path to the CLAP SDK (CMake CLAP_SDK_ROOT). Required.",
    ) orelse defaultIfExists(b, "libs/clap") orelse {
        std.debug.print(
            \\error: -Dclap-sdk-root=<path> is required.
            \\Pass the path to a CLAP SDK checkout (must contain include/clap/clap.h),
            \\or place one at libs/clap inside this repo.
            \\
        , .{});
        return error.MissingClapSdkRoot;
    };
    const clap_sdk_root: std.Build.LazyPath = if (std.fs.path.isAbsolute(clap_sdk_root_str))
        .{ .cwd_relative = clap_sdk_root_str }
    else
        b.path(clap_sdk_root_str);

    const cxx_standard = b.option(u8, "cxx-standard", "C++ standard (CMake CLAP_WRAPPER_CXX_STANDARD)") orelse 17;
    const enable_sanitizer = b.option(bool, "sanitize", "Enable address+ub sanitizers (CMake CLAP_WRAPPER_ENABLE_SANITIZER)") orelse false;
    const wrapper_version = b.option([]const u8, "wrapper-version", "Override CLAP_WRAPPER_VERSION") orelse "0.12.1";

    // Prototype-only flags (these aren't permanent — they exist to gate
    // scaffolding while the full port is incomplete).
    const auv2_prototype = b.option(bool, "auv2-prototype", "Wire up the AUv2 codegen prototype (macOS only)") orelse false;

    const platform = options_mod.detectPlatform(target.result);
    const toolchain = options_mod.detectToolchain(target.result);

    const opts: options_mod.Options = .{
        .platform = platform,
        .toolchain = toolchain,
        .cxx_standard = cxx_standard,
        .enable_sanitizer = enable_sanitizer,
        .wrapper_version = wrapper_version,
    };

    const shared_result = try shared.build(b, .{
        .target = target,
        .optimize = optimize,
        .clap_sdk_root = clap_sdk_root,
        .opts = opts,
    });
    b.installArtifact(shared_result.lib);

    // Default `zig build` step builds the shared lib. Adds a named alias
    // so the intent is explicit.
    const shared_step = b.step("shared", "Build clap-wrapper-shared-detail (the foundation lib)");
    shared_step.dependOn(&shared_result.lib.step);

    // AUv2 codegen prototype — proves the build-helper exe -> generated
    // sources -> downstream consumer chain. macOS only; no-op elsewhere.
    if (auv2_prototype) {
        if (platform == .mac) {
            const proto = try auv2_codegen.buildPrototype(b, .{
                .target = target,
                .optimize = optimize,
                .shared = shared_result,
                .opts = opts,
            });
            const proto_step = b.step("auv2-prototype", "Run the AUv2 codegen prototype (macOS only)");
            proto_step.dependOn(&proto.install_plist.step);
            proto_step.dependOn(&proto.install_entrypoints.step);
        } else {
            std.debug.print("note: -Dauv2-prototype is macOS-only; ignoring on this target.\n", .{});
        }
    }
}

fn defaultIfExists(b: *std.Build, candidate: []const u8) ?[]const u8 {
    const abs = b.pathFromRoot(candidate);
    std.fs.accessAbsolute(abs, .{}) catch return null;
    return candidate;
}
