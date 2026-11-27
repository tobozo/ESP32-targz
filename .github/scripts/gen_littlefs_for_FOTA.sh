#!/usr/bin/env bash

# This script is used by the CI to create the custom targz file
# some examples will need when testing custom routing in ESP32-targz.
#
# It takes a targz template file, adds a precompiled firmware, and
# saves the updated archive into a given data folder for later
# uploading using the LittleFS Filesystem Uploader plugin, or to use
# with mklittlefs/mkspiffs and create a data partition for qemu.

usage() {
  echo
  echo "Usage:"
  echo "$0 \ "
  echo " -b /path/to/firmware.bin \ "
  echo " -f firmware_name_in_tar_gz.bin \ "
  echo " -d /dest/project/dir/data \ "
  echo " -t /path/to/template/archive.tar.gz"
  echo
  exit
}


PWD=`pwd`

function onexit {
  cd $PWD
}
# cd back to the initial folder after exit or crash
trap onexit EXIT
trap onexit SIGINT

while getopts ":b:f:d:t:" Option
do
  case $Option in
    b) firmware_full_path=${OPTARG};; # full path to the firmware we need to add to the archive
    d) destination_data_dir=${OPTARG};; # destination data dir, where the updated archive will be saved
    t) targz_sample_path=${OPTARG};; # targz sample file path
    f) firmware_basename_in_archive=${OPTARG};; # optional: firmware basename in the archive, defaults to "firmware_example.ino.bin"
    :) echo "Missing option argument for -$Option" >&2; exit;;
    *) usage;;
  esac
done

# Path to the precompiled binary
# If $firmware_full_path wasn't provided, try to find it
if [ -z "$firmware_full_path" ]; then
  echo "ERROR: Path to firmware wasn't provided"
  exit
  # firmware_full_path=`find ~/.cache/arduino/sketches | grep Test_tgz_FOTA_stage2.ino.bin`
fi

if [ ! -f "$firmware_full_path" ]; then
  echo "ERROR: Firmware file '$firmware_full_path' not found"
  exit
fi

# Stage 2 firmware file will be renamed to this in the tar.gz archive
# If $firmware_basename_in_archive wasn't provided, assign a default one
if [ -z "$firmware_basename_in_archive" ]; then
  firmware_basename_in_archive="firmware_example.ino.bin"
  echo "NOTICE: Using default firmware basename '$firmware_basename_in_archive'"
fi

# Destination LittleFS/SPIFFS data folder for the custom targz_example.tar.gz this script will generate
if [ -z "$destination_data_dir" ]; then
  echo "ERROR: destination data dir wasn't provided"
  exit
  # Id $destination_data_dir wasn't provided, assign one
  # destination_data_dir="../../examples/ESP32/Test_tgz_FOTA_stage1/data"
  # echo "WARNING: Using default data dir '$destination_data_dir'"
fi

# targz template file to receive the precompiled firmware
if [ -z "$targz_sample_path" ]; then
  # Filename/path to the tar.gz template.
  targz_sample_dir="../../examples/Test_tar_gz_tgz/data"
  targz_sample_file="targz_example.tar.gz"
  echo "WARNING: Using default targz template path '$targz_sample_dir/$targz_sample_file'"
else
  if [ ! -f "$targz_sample_path" ]; then
    echo "ERROR: targz sample path '$targz_sample_path' not found"
    exit
  fi
  targz_sample_dir=`dirname ${targz_sample_path}`
  targz_sample_file=`basename ${targz_sample_path}`
fi

# Create the data dir if it doesn't exist
[ -d "$destination_data_dir" ] || mkdir -p $destination_data_dir

# temp path and filename
targz_temp_file="temp.tar.gz"
targz_temp_dir="temp"
# Create the temp dir if it doesn't exist
[ -d "$targz_temp_dir" ] || mkdir -p $targz_temp_dir

# Copy the Test_tgz_FOTA_stage2 firmware
cp $firmware_full_path $targz_temp_dir/$firmware_basename_in_archive

# Copy the targz file from the other example (has a 'zombo.com' folder)
cp $targz_sample_dir/$targz_sample_file $targz_temp_dir/

cd $targz_temp_dir

  # Extract files from targz
  tar -xf $targz_sample_file
  # Delete targz file
  rm $targz_sample_file
  # Recompress with the added firmware
  tar -czf ../$targz_temp_file ./

cd ..

# Copy the new archive to the LittleFS data folder for later uploading
cp -f $targz_temp_file $destination_data_dir/$targz_sample_file

# Cleanup
rm $targz_temp_file
rm -rf $targz_temp_dir
