commit_path="${BR2_EXTERNAL::-1}"
git add $commit_path/board/*/bitstream/fsbl.elf
git add $commit_path/board/*/bitstream/maia-iio/*
git add $commit_path/board/*/bitstream/overclock/*
git commit -m "Update bistream"$1
