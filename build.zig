const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const mod = b.createModule(.{
        .root_source_file = b.path("src/main.zig"),
        .target = target,
        .optimize = optimize,
        .link_libc = true,
    });

    const lib = b.addLibrary(.{
        .name = "samar",
        .linkage = .dynamic,
        .root_module = mod,
    });
    b.installArtifact(lib);

    const tests = b.addTest(.{ .root_module = mod });
    const run_tests = b.addRunArtifact(tests);

    if (b.args) |args| {
        if (args.len > 0) run_tests.setEnvironmentVariable("SAMAR_OUT", args[0]);
    }

    run_tests.has_side_effects = true;

    const test_step = b.step("test", "running all of them");
    test_step.dependOn(&run_tests.step);
}
