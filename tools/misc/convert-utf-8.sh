#!/bin/bash

set -e

enc=$(file --mime-encoding "$1" | sed -E 's/.*: //g')
if [[ $enc == "binary" ]]; then
	exit 0
fi
if [[ $enc != "utf-8" ]]; then
	iconv -f $enc -t utf-8 "$1" -o "$1.tmp"
	cat "$1.tmp" > "$1"
	rm "$1.tmp"
fi
