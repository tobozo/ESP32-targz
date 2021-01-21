#!/bin/bash

#
# This bash script generates the contents "test_files.h"
# for the ESP32-Targz suite, it should be run from inside
# the folder to scan before running the test suite

# relative or full path to the sketch folder
H_FILE=../test_files.h

if [ -f $H_FILE ]; then
  rm $H_FILE
fi

LINES=()

for i in $(find . | sort); do
    if [ -f "$i" ]; then
    REL=${i:2}
    MD=$(md5sum -b "$i" | cut -d" " -f1)
    SIZE=$(stat --format=%s "$i")
    LINES+=("  { `printf \"%-12d,\" $SIZE` \"$MD\", \"$REL\" },")
    fi
done

OUTPUT=()
OUTPUT+=("fileMeta myFiles["${#LINES[@]}"] = ")
OUTPUT+=("{")
for LINE in "${LINES[@]}"; do
    OUTPUT+=("$LINE")
done
OUTPUT+=("};")
OUTPUT+=("packageMeta myPackage = ")
OUTPUT+=("{")
OUTPUT+=("  nullptr, "${#LINES[@]}", myFiles")
OUTPUT+=("};")

for LINE in "${OUTPUT[@]}"; do
  echo "$LINE" >>$H_FILE
  echo "$LINE"
done
