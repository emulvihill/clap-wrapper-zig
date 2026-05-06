//! AUv2 codegen prototype.
//!
//! Source of truth: cmake/wrap_auv2.cmake:73-196.
//!
//! In the CMake build, every AUv2 wrapper target has a paired
//! `${TARGET}-build-helper` executable. The wrapper's compile step depends
//! on two files emitted by running that helper:
//!     - auv2_Info.plist           (consumed by the bundle install step)
//!     - generated_entrypoints.hxx (`#include`d into wrapasauv2.cpp)
//!
//! The helper runs in two modes (cmake/wrap_auv2.cmake:121-195):
//!     --explicit  <name> <ver> <type> <subt> <manu_code> <manu_name>
//!         No CLAP file — emits plist+entrypoints from CLI args alone.
//!     --fromclap  <name> <clapfile> <ver> <manu_code> <manu_name>
//!                 <itype> <subt>
//!         Loads a real .clap via dlopen and introspects it. Used in the
//!         normal flow where the wrapper hosts a known CLAP impl.
//!
//! This prototype demonstrates the simpler `--explicit` path end-to-end:
//!   1. Compile the helper exe from src/detail/auv2/build-helper/build-helper.cpp,
//!      linked against clap-wrapper-shared-detail + Foundation/CoreFoundation.
//!   2. Run it with hardcoded `--explicit` args, writing into a generated
//!      output directory whose path is known to the build graph.
//!   3. Expose the generated files as `LazyPath`s so a downstream step can
//!      depend on them — proving the dependency-graph wiring works.
//!
//! [UNVERIFIED LOCALLY] This sandbox lacks both Zig (download blocked) and
//! macOS (helper requires Foundation framework). Verified by reading vs.
//! the cmake source; needs a `zig build -Dauv2-prototype` run on macOS
//! before relying on it. Likely-fragile spots are flagged with [VERIFY].

const std = @import("std");
const options_mod = @import("options.zig");
const shared = @import("clap_wrapper_shared.zig");

pub const Args = struct {
    target: std.Build.ResolvedTarget,
    optimize: std.builtin.OptimizeMode,
    shared: shared.Result,
    opts: options_mod.Options,
};

pub const PrototypeResult = struct {
    helper_exe: *std.Build.Step.Compile,
    /// LazyPath to the directory the helper wrote into. Files inside:
    ///   - auv2_Info.plist
    ///   - generated_entrypoints.hxx
    output_dir: std.Build.LazyPath,
    info_plist: std.Build.LazyPath,
    entrypoints_hxx: std.Build.LazyPath,
    /// Install steps for the two generated artifacts. Callers should make
    /// their top-level step `dependOn(&install_plist.step)` and
    /// `dependOn(&install_entrypoints.step)`.
    install_plist: *std.Build.Step.InstallFile,
    install_entrypoints: *std.Build.Step.InstallFile,
};

pub fn buildPrototype(b: *std.Build, args: Args) !PrototypeResult {
    const arena = b.allocator;

    // 1. Compile the build-helper executable.
    const helper = b.addExecutable(.{
        .name = "auv2-build-helper-prototype",
        .target = args.target,
        .optimize = args.optimize,
    });
    helper.linkLibCpp();

    var flags = std.ArrayList([]const u8).init(arena);
    try flags.appendSlice(try options_mod.publicFlags(arena, args.opts));
    try flags.appendSlice(try options_mod.privateFlags(arena, args.opts));
    try flags.appendSlice(options_mod.macosFilesystemFlags(args.opts.platform));

    helper.addCSourceFile(.{
        .file = b.path("src/detail/auv2/build-helper/build-helper.cpp"),
        .flags = flags.items,
    });

    // Helper transitively uses clap-wrapper-shared-detail symbols
    // (clap_proxy / fsutil / sha1) plus CLAP headers via the shared lib.
    helper.linkLibrary(args.shared.lib);
    args.shared.applyPublicIncludes(b, helper);

    // macOS frameworks the helper directly references.
    helper.linkFramework("Foundation");
    helper.linkFramework("CoreFoundation");

    // 2. Run the helper to emit auv2_Info.plist and generated_entrypoints.hxx.
    //
    // The helper writes both files using std::ofstream with hardcoded
    // basenames in CWD (build-helper.cpp:333, :355). To capture them as
    // LazyPaths in the Zig build graph, we point the run's CWD at a
    // generated directory.
    //
    // Pattern: WriteFiles.getDirectory() yields a LazyPath to a fresh
    // build-cache directory; setCwd() points the Run there; the helper's
    // file writes land in that directory; we expose those files via
    // gen_dir.path(b, "<name>") to downstream consumers.
    //
    // [VERIFY] The CWD-as-output-dir trick relies on Zig's caching
    // recognizing files materialized into a WriteFiles directory by an
    // external process. If Zig demands explicit `addOutputFileArg`
    // declarations, the alternative is to teach the helper an
    // `--out-dir <path>` flag and use `addOutputFileArg("plist")` /
    // `addOutputFileArg("entrypoints")` directly. The helper's CLI is
    // currently positional-only (build-helper.cpp:216-260) so adding
    // `--out-dir` is a small surgical change worth doing if needed.
    const wf = b.addWriteFiles();
    const gen_dir = wf.getDirectory();

    const run = b.addRunArtifact(helper);
    run.setCwd(gen_dir);

    // Match `--explicit` from wrap_auv2.cmake:191-195. Hardcoded values
    // here are placeholder identification just to exercise the codegen.
    run.addArgs(&.{
        "--explicit",
        "ZigPortPrototype", // OUTPUT_NAME
        "0.0.1", // BUNDLE_VERSION
        "aufx", // INSTRUMENT_TYPE (effect)
        "ZpRt", // SUBTYPE_CODE
        "ClpW", // MANUFACTURER_CODE
        "Clap Wrapper Zig Port", // MANUFACTURER_NAME
    });

    const info_plist = gen_dir.path(b, "auv2_Info.plist");
    const entrypoints_hxx = gen_dir.path(b, "generated_entrypoints.hxx");

    // Install both for inspection (so a developer can confirm the prototype
    // produced sane output without spelunking into the cache). These are
    // opt-in via the `auv2-prototype` step; we deliberately do NOT chain
    // them onto the default install step, so plain `zig build` never runs
    // the helper.
    const install_plist = b.addInstallFile(info_plist, "auv2-prototype/auv2_Info.plist");
    install_plist.step.dependOn(&run.step);
    const install_hxx = b.addInstallFile(entrypoints_hxx, "auv2-prototype/generated_entrypoints.hxx");
    install_hxx.step.dependOn(&run.step);

    return .{
        .helper_exe = helper,
        .output_dir = gen_dir,
        .info_plist = info_plist,
        .entrypoints_hxx = entrypoints_hxx,
        .install_plist = install_plist,
        .install_entrypoints = install_hxx,
    };
}
