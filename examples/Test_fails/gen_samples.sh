
# 16 files of 1,100 bytes, 40 KB of tar, into 56 KB free. Each file takes a 4 KB block, so 12 fit.

mkdir -p data

outfile=data_16_files_of_1100_bytes_in_40kb_of_tar.h
tarfile=tar_16_files_of_1100_bytes.tar
targzfile=tgz_16_files_of_1100_bytes.tgz

# generate 16x random 1100 bytes blocks
for p in `seq 1 16`; do
  dd if=/dev/urandom of=data/1100bytes_$p bs=1100 count=1
done

cd data

# tar them up
tar -cf ../$tarfile .
tar -czf ../$targzfile .
cd ..

# to C source
echo "const " > $outfile
xxd -n tgz_data -i $targzfile >> $outfile

ls $targzfile -la

# cleanup
rm -Rf data
rm $tarfile
mkdir data


