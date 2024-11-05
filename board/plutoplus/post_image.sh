TARGET=pluto
BOARD_DIR=$(dirname $0)
BIN_DIR=$1
mkimage=$HOST_DIR/bin/mkimage
dfu_suffix=$HOST_DIR/bin/dfu-suffix

DEVICE_VID=0x0456
DEVICE_PID=0xb673

cp $BOARD_DIR/pluto.its $BIN_DIR/pluto.its

echo "# entering $BIN_DIR for the next command"
(cd $BIN_DIR && $mkimage -f pluto.its pluto.itb)

echo "generating the pluto.frm"
md5sum $BIN_DIR/pluto.itb | cut -d ' ' -f 1 > $BIN_DIR/pluto.md5
cat $BIN_DIR/pluto.itb  $BIN_DIR/pluto.md5 > $BIN_DIR/pluto.frm

echo "generating pluto.dfu"
$dfu_suffix -a $BIN_DIR/pluto.itb -v $DEVICE_VID -p $DEVICE_PID
mv $BIN_DIR/pluto.itb $BIN_DIR/pluto.dfu

echo "generatind sd"
SDIMGDIR=$BIN_DIR/sdimg
mkdir -p $SDIMGDIR
touch 	$SDIMGDIR/boot.bif
cp $BIN_DIR/u-boot $SDIMGDIR/u-boot.elf
cp $BIN_DIR/system_top.bit $SDIMGDIR/
cp $BOARD_DIR/bitstream/fsbl.elf $SDIMGDIR/
echo "img : {[bootloader] $SDIMGDIR/fsbl.elf  $SDIMGDIR/system_top.bit  $SDIMGDIR/u-boot.elf}" >  $SDIMGDIR/boot.bif
bootgen -image $SDIMGDIR/boot.bif -w -o i $SDIMGDIR/BOOT.bin
rm $SDIMGDIR/fsbl.elf  $SDIMGDIR/system_top.bit  $SDIMGDIR/u-boot.elf $SDIMGDIR/boot.bif
cp $BIN_DIR/rootfs.cpio.gz $SDIMGDIR/ramdisk.image.gz
$mkimage -A arm -T ramdisk -C gzip -d $SDIMGDIR/ramdisk.image.gz $SDIMGDIR/uramdisk.image.gz
rm $SDIMGDIR/ramdisk.image.gz
cp $BIN_DIR/pluto.dfu $SDIMGDIR/uImage
cp $BIN_DIR/zynq-plutoplus.dtb $SDIMGDIR/devicetree.dtb
cp $BOARD_DIR/uboot-env.txt $SDIMGDIR/

cd $BIN_DIR && zip tezuka.zip boot.dfu boot.frm pluto.frm pluto.dfu sdimg/*
