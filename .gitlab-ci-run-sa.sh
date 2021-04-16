#!/bin/sh

# Ensure this script aborts on errors
set -ex

scons all=1 enable_sa=1 build/$1/$2/sa-html -j4

set +x
mkdir sa-results
find build -type f -name '*.html' | xargs -n 1 -I HTML cp HTML sa-results/
# Fail if any results were generated
if [ -n "$(ls -A sa-results/)" ]; then echo "SA Failed"; exit 1; else echo "SA Passed"; fi
