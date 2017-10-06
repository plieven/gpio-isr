#!/bin/bash

MAX_PINS=54
TIMEOUT=10
VOLATILE_DIR=/var/run/gpio-isr
STATIC_DIR=/var/lib/gpio-isr
[ -z "$API" ] && API=http://home.dlhnet.de/api/s0meter

for FILE in $STATIC_DIR/pin*.uuid; do
 PIN=$(basename $FILE .uuid)
 UUID=$(cat $STATIC_DIR/$PIN.uuid)
 if [ -e $VOLATILE_DIR/$PIN.lastPeriod -a -e $VOLATILE_DIR/$PIN.totalCount ]; then
  LASTPERIOD=$(cat $VOLATILE_DIR/$PIN.lastPeriod)
  TOTALCOUNT=$(cat $VOLATILE_DIR/$PIN.totalCount)
  /usr/bin/timeout -s KILL 10 /usr/bin/curl -s -d "totalCount=$TOTALCOUNT&lastPeriod=$LASTPERIOD" -X POST $API/$UUID/submitData 2>&1 | logger -i -t 'gpio-isr-submitData.sh'
 fi
done
