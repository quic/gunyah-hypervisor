#!/bin/bash
# Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
#
# SPDX-License-Identifier: BSD-3-Clause
#
# Build open-source components out of tree and package an FVP image.

set -euo pipefail
HYP_DIR=$(cd "$(dirname "$0")/../.." && pwd)
OSS_DIR=$(dirname "$HYP_DIR")
: "${LLVM:?Set LLVM to an installed LLVM toolchain}"
: "${LOCAL_SYSROOT:=$OSS_DIR/gunyah-app-sysroot}"
: "${FVP_VENV:=$HYP_DIR/build-fvp-venv}"
: "${QUALITY:=debug}"
: "${FEATURESET:=gunyah-baseline}"
export LLVM LOCAL_SYSROOT
export VIRTUAL_ENV="$FVP_VENV"
export PATH="$FVP_VENV/bin:$LLVM/bin:/usr/bin:/bin"
unset QCOM_LLVM QCOM_DTC PYTHONPATH PYTHONHOME
test -x "$FVP_VENV/bin/python"
test -d "$LOCAL_SYSROOT"
BUILD="$HYP_DIR/build-fvp"
for component in hyp resource-manager runtime; do
    mkdir -p "$BUILD/$component"
done
cd "$BUILD/hyp"
python "$HYP_DIR/configure.py" platform=fvp-aem "featureset=$FEATURESET" "quality=$QUALITY"
ninja -j "${BUILD_JOBS:-8}"
cd "$BUILD/resource-manager"
python "$OSS_DIR/resource-manager/configure.py" platform=fvp-aem "quality=$QUALITY"
ninja -j "${BUILD_JOBS:-8}"
cd "$BUILD/runtime"
python "$OSS_DIR/musl-c-runtime/configure.py" platform=fvp-aem "quality=$QUALITY"
ninja -j "${BUILD_JOBS:-8}"
RM="$BUILD/resource-manager/fvp-aem/$QUALITY/resource-manager"
RT="$BUILD/runtime/$QUALITY/runtime"
"$LLVM/bin/llvm-strip" -o "$RM.strip" "$RM"
"$LLVM/bin/llvm-strip" -o "$RT.strip" "$RT"
IFS=, read -r -a featuresets <<< "$FEATURESET"
for featureset in "${featuresets[@]}"; do
    OUT="$BUILD/hyp/$QUALITY/fvp-aem/$featureset"
    if [ "$featureset" = unittests ]; then
        # This featureset runs without a root VM, RM, or runtime package.
        python "$HYP_DIR/tools/elf/elf_to_bin.py" -o "$OUT/hypvm.bin" "$OUT/hyp.elf"
    else
        python "$HYP_DIR/tools/elf/package_apps.py" -a "$RM.strip" -r "$RT.strip" \
            -o "$OUT/hypvm.elf" "$OUT/hyp.elf"
        python "$HYP_DIR/tools/elf/elf_to_bin.py" -o "$OUT/hypvm.bin" "$OUT/hypvm.elf"
    fi
    {
        echo "platform=fvp-aem quality=$QUALITY featureset=$featureset"
        "$LLVM/bin/clang" --version
        python --version
        dtc --version
        for repo in "$HYP_DIR" "$OSS_DIR/resource-manager" "$OSS_DIR/musl-c-runtime" "$LOCAL_SYSROOT"; do
            echo "Source: $repo"
            if git -C "$repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
                git -C "$repo" rev-parse HEAD
                git -C "$repo" status --short
            else
                echo "Installed input without Git revision metadata"
            fi
        done
        sha256sum "$OUT/hyp.elf" "$OUT/hypvm.bin" "$RM.strip" "$RT.strip"
    } > "$OUT/build-inputs.txt"
done
echo "FVP packages: $BUILD/hyp/$QUALITY/fvp-aem"
