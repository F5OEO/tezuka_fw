#!/bin/sh
FREQ_CENTRAL=260000000
SPAN=480000000

FREQ_MINI=47000000
SR_MINI=2100000

if [ $1 ]; then
    FREQ_CENTRAL=$1
else
  echo "please provide central frequency : using default $FREQ_CENTRAL"
fi

if [ $2 ]; then
    SPAN=$2
else
  echo "please provide Span  : using default $SPAN"
fi

#SPAN SHOULD BE AN INTEGER !!!!
#SPAN=$(printf "%0.f")
SR=$(($SPAN / 8 ))


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

if [ $3 == "0" ]; then
  echo "Stop sweep"
  iio_attr -D ad9361-phy adi,rx-fastlock-pincontrol-enable 0
  #In order to be set, we need a fastlock_recall (bad AD implementation)
  echo 0 > out_altvoltage0_RX_LO_fastlock_recall
  echo $FREQ_CENTRAL > out_altvoltage0_RX_LO_frequency
  exit 1
fi

if [ "$SR" -lt "$SR_MINI" ]; then
  SR=$SR_MINI
fi

#disable fir if any
echo 0 > in_out_voltage_filter_fir_en
echo "Setting samlerate $SR"
echo $SR > in_voltage_sampling_frequency

#Setup 8 Profiles SR spaced

FREQ1=$(($FREQ_CENTRAL-$SR*3-$SR/2))

if [ "$FREQ1" -lt "$FREQ_MINI" ]; then
  echo "Correct freq mini"
  FREQ1=$FREQ_MINI
  FREQ_CENTRAL=$(($FREQ1+$SR*3+$SR/2))
fi

echo "Setup sweep staring at $FREQ1"
for i in `seq 0 7`
do
  FREQ=$(($FREQ1 + $i * $SR ))
  echo $FREQ > out_altvoltage0_RX_LO_frequency
  echo "Initializing PROFILE $i at $FREQ "
  echo $i > out_altvoltage0_RX_LO_fastlock_store
done

echo $FREQ_CENTRAL
# Just to inform "normal client" what is the central frequency
echo $FREQ_CENTRAL > out_altvoltage0_RX_LO_frequency


#Enable Fastlock Mode
iio_attr -D ad9361-phy adi,rx-fastlock-pincontrol-enable 1
#In order to be set, we need a fastlock_recall (bad AD implementation)
echo 0 > out_altvoltage0_RX_LO_fastlock_recall
#echo 0x25A 0x83 > /sys/kernel/debug/iio/iio:device0/direct_reg_access
#echo 0x25A 0x00 > /sys/kernel/debug/iio/iio:device0/direct_reg_access


# Mandatory as the HDL make a OR so 0 IS should be set
GPIO_BASE=906
cd /sys/class/gpio

if [ $GPIO_BASE -ge 0 ]
then
  GPIO_CTRL_IN1=`expr $GPIO_BASE + 63`
  GPIO_CTRL_IN2=`expr $GPIO_BASE + 64`
  GPIO_CTRL_IN3=`expr $GPIO_BASE + 65`
  #Export the CTRL_IN GPIOs
  echo $GPIO_CTRL_IN1 > export 2> /dev/null
  echo $GPIO_CTRL_IN2 > export 2> /dev/null
  echo $GPIO_CTRL_IN3 > export 2> /dev/null
else
  echo ERROR: Wrong board?
  exit
fi

CTRL_IN1=gpio${GPIO_CTRL_IN1}/direction
CTRL_IN2=gpio${GPIO_CTRL_IN2}/direction
CTRL_IN3=gpio${GPIO_CTRL_IN3}/direction

echo low > $CTRL_IN1
echo low > $CTRL_IN2 
echo low > $CTRL_IN3  
