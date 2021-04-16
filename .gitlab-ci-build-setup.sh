#!/bin/sh

# Ensure this script aborts on errors
set -ex

pip install -r $CI_PROJECT_DIR/tools/requirements.txt

# Clear any stale builds
rm -rf build
