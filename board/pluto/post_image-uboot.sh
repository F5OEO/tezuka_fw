BOARD_DIR=$(dirname $0)
BIN_DIR=$1
dfu_suffix=$HOST_DIR/bin/dfu-suffix

DEVICE_VID=0x0456
DEVICE_PID=0xb673

echo "generating the boot.img"

GENIMAGE_CFG="${BOARD_DIR}/genimage-boot.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

rm -rf "${GENIMAGE_TMP}"

genimage \
    --rootpath "${TARGET_DIR}" \
    --tmppath "${GENIMAGE_TMP}" \
    --inputpath "${BINARIES_DIR}" \
    --outputpath "${BINARIES_DIR}" \
    --config "${GENIMAGE_CFG}"

echo "generating the boot.frm"
cat $BIN_DIR/boot.img $BIN_DIR/uboot-env.bin $BOARD_DIR/target_mtd_info.key | \
	tee $BIN_DIR/boot.frm | md5sum | cut -d ' ' -f1 | tee -a $BIN_DIR/boot.frm

echo "generating boot.dfu"
cp $BIN_DIR/boot.img $BIN_DIR/boot.bin.tmp
$dfu_suffix -a $BIN_DIR/boot.bin.tmp -v $DEVICE_VID -p $DEVICE_PID
mv $BIN_DIR/boot.bin.tmp $BIN_DIR/boot.dfu

echo "generating uboot-env.dfu"
mkenvimage -s 0x20000 -o $BIN_DIR/uboot-env.bin $BOARD_DIR/uboot-env.txt
cp $BIN_DIR/uboot-env.bin $BIN_DIR/uboot-env.bin.tmp
$dfu_suffix -a $BIN_DIR/uboot-env.bin.tmp -v $DEVICE_VID -p $DEVICE_PID
mv $BIN_DIR/uboot-env.bin.tmp $BIN_DIR/uboot-env.dfu


