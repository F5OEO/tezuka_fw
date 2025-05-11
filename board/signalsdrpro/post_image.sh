TARGET=pluto
BOARD_DIR=$(dirname $0)
BIN_DIR=$1
mkimage=$HOST_DIR/bin/mkimage
dfu_suffix=$HOST_DIR/bin/dfu-suffix

DEVICE_VID=0x0456
DEVICE_PID=0xb673

cp $BOARD_DIR/signalsdrpro.its $BIN_DIR/signalsdrpro.its

echo "# entering $BIN_DIR for the next command"
(cd $BIN_DIR && $mkimage -f signalsdrpro.its signalsdrpro.itb)

echo "generating the pluto.frm"
md5sum $BIN_DIR/signalsdrpro.itb | cut -d ' ' -f 1 > $BIN_DIR/signalsdrpro.md5
cat $BIN_DIR/signalsdrpro.itb  $BIN_DIR/pluto.md5 > $BIN_DIR/signalsdrpro.frm

echo "generating pluto.dfu"
$dfu_suffix -a $BIN_DIR/pluto.itb -v $DEVICE_VID -p $DEVICE_PID
mv $BIN_DIR/signalsdrpro.itb $BIN_DIR/signalsdrpro.dfu

rm -f $BIN_DIR/signalsdrpro.its $BIN_DIR/*.md5
