#!/bin/sh


for i in $(find -L /sys/bus/iio/devices -maxdepth 2 -name name)
do
  dev_name=$(cat $i)
  if [ "$dev_name" = "ad9361-phy" ]; then
     phy_path=$(echo $i | sed 's:/name$::')
     cd $phy_path
     break
  fi
done

if [ "$dev_name" != "ad9361-phy" ]; then
 exit
fi


  echo "Stop sweep"
  iio_attr -D ad9361-phy adi,rx-fastlock-pincontrol-enable 0
  freq=$(cat out_altvoltage0_RX_LO_frequency)
  echo 0 > out_altvoltage0_RX_LO_fastlock_store
  #In order to be set, we need a fastlock_recall (bad AD implementation)
  echo 0 > out_altvoltage0_RX_LO_fastlock_recall


