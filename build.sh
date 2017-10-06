#!/bin/bash -e

echo Checking for required binaries...
for BIN in pigs timeout curl; do
 which $BIN >/dev/null && true
 if [ $? -ne 0 ]; then
  echo ERR: required binary $BIN not found in PATH
  exit 1
 fi
done

echo
echo Building Target...
gcc -o gpio-isr gpio-isr.c -lpigpio -lrt -Wall -Werror

echo
echo Installing Binary...
sudo cp -v ./gpio-isr /usr/local/bin

echo
echo Installing Push Service...
sudo cp -v ./gpio-isr-submitData.sh /usr/local/bin

if [ ! -e /etc/systemd/system/gpio-isr.service ]; then
 echo Installing systemd service...
 sudo cp -v ./gpio-isr.service /etc/systemd/system/
fi

echo
echo Enabling gpio-isr systemd Service...
sudo systemctl enable gpio-isr.service
sudo service gpio-isr start

echo Enabling gpio-isr Push Service...
sudo sh -c "echo '* * * * * pi /usr/local/bin/gpio-isr-submitData.sh' >/etc/cron.d/gpio-isr-submitData"

echo
echo Installation complete!
echo
echo You may want to modify the GPIO pins that are monitored by editing:
echo /etc/systemd/system/gpio-isr.service
echo
echo You can run gpio-isr in monitor mode to check if you receive interrupts:
echo ./gpio-isr -m -U -p 17
echo
echo You may preseed the interrupt totalCount for a pin by editing the files
echo in /var/lib/gpio-isr/pinN.totalCount when the application is stopped.

