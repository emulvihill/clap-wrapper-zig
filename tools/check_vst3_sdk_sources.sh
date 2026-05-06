#!/usr/bin/env bash
#
# Validates that buildsupport/vst3_sdk_sources.zig stays in sync with the actual
# VST3 SDK at the version pinned in cmake/base_sdks.cmake (currently
# v3.8.0_build_66 — see the GIT_TAG line in guarantee_vst3sdk).
#
# Three classes of check:
#   1. Every path in the Zig list must exist in the SDK checkout.
#   2. Every .cpp under the four CMake `file(GLOB ...)` roots must appear
#      in the Zig list — otherwise the original CMake build would compile
#      a file that the Zig build silently drops.
#   3. The two curated dirs (public.sdk/source/vst, .../vst/utility) are
#      hand-picked. New .cpp files that are neither listed nor on the
#      known-skip list are flagged so a human decides whether to include
#      them or extend the skip list.
#
# Usage: tools/check_vst3_sdk_sources.sh <VST3_SDK_ROOT>
#
# Intended for CI after any SDK pin bump.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: $0 <VST3_SDK_ROOT>" >&2
    exit 2
fi

ROOT="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SOURCES_ZIG="$SCRIPT_DIR/../buildsupport/vst3_sdk_sources.zig"

if [ ! -f "$SOURCES_ZIG" ]; then
    echo "error: cannot find $SOURCES_ZIG" >&2
    exit 2
fi
if [ ! -d "$ROOT" ]; then
    echo "error: VST3_SDK_ROOT '$ROOT' is not a directory" >&2
    exit 2
fi

# Extract every double-quoted .cpp path from the Zig data file.
mapfile -t LISTED < <(grep -oE '"[^"]+\.cpp"' "$SOURCES_ZIG" | tr -d '"' | sort -u)

errors=0

# Helper: is "$1" present in the LISTED array?
in_listed() {
    printf '%s\n' "${LISTED[@]}" | grep -qFx "$1"
}

# 1. Every listed file exists in the SDK.
for f in "${LISTED[@]}"; do
    if [ ! -f "$ROOT/$f" ]; then
        echo "MISSING in SDK: $f" >&2
        errors=$((errors + 1))
    fi
done

# 2. Every .cpp in the four globbed dirs must be in the list.
GLOB_DIRS=(
    "base/source"
    "base/thread/source"
    "public.sdk/source/common"
    "pluginterfaces/base"
)
for dir in "${GLOB_DIRS[@]}"; do
    if [ ! -d "$ROOT/$dir" ]; then
        echo "MISSING dir in SDK: $dir" >&2
        errors=$((errors + 1))
        continue
    fi
    while IFS= read -r f; do
        rel="${f#"$ROOT"/}"
        if ! in_listed "$rel"; then
            echo "GLOB DRIFT (CMake compiles, Zig list omits): $rel" >&2
            errors=$((errors + 1))
        fi
    done < <(find "$ROOT/$dir" -maxdepth 1 -name '*.cpp' 2>/dev/null)
done

# 3. Curated dirs: known-skipped basenames (without .cpp suffix).
KNOWN_VST_SKIPS=(
    vsteditcontroller
    vstgui_linux_runloop_support
    vstgui_win32_bundle_support
    vstguieditor
    vstpresetfile
    vstrepresentation
)
KNOWN_UTIL_SKIPS=(
    dataexchange
    mpeprocessor
    systemtime
    testing
    vst2persistence
)

check_curated() {
    local dir="$1"
    shift
    local -a skips=("$@")
    if [ ! -d "$ROOT/$dir" ]; then
        echo "MISSING dir in SDK: $dir" >&2
        errors=$((errors + 1))
        return
    fi
    while IFS= read -r f; do
        rel="${f#"$ROOT"/}"
        base="$(basename "$rel" .cpp)"
        if in_listed "$rel"; then
            continue
        fi
        if printf '%s\n' "${skips[@]}" | grep -qFx "$base"; then
            continue
        fi
        echo "NEW IN SDK (decide include vs. skip): $rel" >&2
        errors=$((errors + 1))
    done < <(find "$ROOT/$dir" -maxdepth 1 -name '*.cpp' 2>/dev/null)
}

check_curated "public.sdk/source/vst"         "${KNOWN_VST_SKIPS[@]}"
check_curated "public.sdk/source/vst/utility" "${KNOWN_UTIL_SKIPS[@]}"

# 4. Compiled-via-#include chains. `vsteditcontroller.cpp` is intentionally
#    omitted from the source list because vstsinglecomponenteffect.cpp pulls
#    it in via `#include "...cpp"`. If the SDK drops that include in a future
#    upgrade, our static lib would silently lose the EditController symbols.
INCLUDE_CHAINS=(
    "public.sdk/source/vst/vstsinglecomponenteffect.cpp:public.sdk/source/vst/vsteditcontroller.cpp"
)
for chain in "${INCLUDE_CHAINS[@]}"; do
    includer="${chain%%:*}"
    included="${chain##*:}"
    if [ ! -f "$ROOT/$includer" ]; then
        echo "BROKEN CHAIN (includer missing): $includer expected to #include $included" >&2
        errors=$((errors + 1))
        continue
    fi
    if ! grep -qF "#include \"$included\"" "$ROOT/$includer"; then
        echo "BROKEN CHAIN: $includer no longer #includes $included — symbols would vanish" >&2
        errors=$((errors + 1))
    fi
done

if [ "$errors" -ne 0 ]; then
    echo "FAIL: $errors discrepancies between buildsupport/vst3_sdk_sources.zig and SDK at $ROOT" >&2
    exit 1
fi

echo "OK: buildsupport/vst3_sdk_sources.zig matches SDK at $ROOT (${#LISTED[@]} files listed)"
