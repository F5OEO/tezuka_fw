TARGET=pluto
BOARD_DIR=$(dirname $0)
BIN_DIR=$1
mkimage=$HOST_DIR/bin/mkimage
dfu_suffix=$HOST_DIR/bin/dfu-suffix

DEVICE_VID=0x0456
DEVICE_PID=0xb673

cp $BOARD_DIR/plutomaia.its $BIN_DIR/plutomaia.its

echo "# entering $BIN_DIR for the next command"
(cd $BIN_DIR && $mkimage -f plutomaia.its pluto.itb)

echo "generating the pluto.frm"
md5sum $BIN_DIR/pluto.itb | cut -d ' ' -f 1 > $BIN_DIR/pluto.md5
cat $BIN_DIR/pluto.itb  $BIN_DIR/pluto.md5 > $BIN_DIR/pluto.frm

cp $BIN_DIR/pluto.itb $BIN_DIR/plutocopy.itb
echo "generating pluto.dfu"
$dfu_suffix -a $BIN_DIR/pluto.itb -v $DEVICE_VID -p $DEVICE_PID
mv $BIN_DIR/pluto.itb $BIN_DIR/pluto.dfu


