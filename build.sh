#!/bin/bash -e

echo Building Target...
gcc -o gpio-isr gpio-isr.c -lpigpio -lrt -Wall -Werror

echo Installing Binary...
sudo cp -v ./gpio-isr /usr/local/bin

if [ ! -e /etc/systemd/system/gpio-isr.service ]; then
 echo Installing systemd service...
 sudo cp -v ./gpio-isr.service /etc/systemd/system/
fi

echo Enabling Service...
sudo systemctl enable gpio-isr.service

echo
echo Installation complete!
echo
echo You may want to modify the GPIO pins that are monitored by editing:
echo /etc/systemd/system/gpio-isr.service
echo
echo You can find a listing of all pins with the command: gpio readall
echo gpio-isr expects the wiringPi pin id.
echo
echo You can run gpio-isr in monitor mode to check if you receive interrupts:
echo ./gpio-isr -m -p 0 -p 3
echo
echo You may preseed the interrupt totalCount for a pin by editing the files
echo in /var/lib/gpio-isr/pinN.totalCount when the application is stopped.

