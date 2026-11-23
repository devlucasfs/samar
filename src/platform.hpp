#pragma once
#include <string>

static std::string detect_arch() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__amd64__)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__riscv) && (__riscv_xlen == 64)
    return "riscv64";
#elif defined(__riscv)
    return "riscv32";
#elif defined(__powerpc64__)
    return "ppc64";
#elif defined(__powerpc__)
    return "ppc";
#elif defined(__mips__)
    return "mips";
#elif defined(__s390x__)
    return "s390x";
#elif defined(__wasm32__)
    return "wasm32";
#elif defined(__wasm64__)
    return "wasm64";
#else
    return "unknown";
#endif
}

static std::string detect_os() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__ANDROID__)
    return "android";
#elif defined(__linux__)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__NetBSD__)
    return "netbsd";
#elif defined(__DragonFly__)
    return "dragonfly";
#elif defined(__sun)
    return "solaris";
#elif defined(__EMSCRIPTEN__)
    return "emscripten";
#else
    return "unknown";
#endif
}

std::string platform() {
    return detect_arch() + "-" + detect_os();
}
