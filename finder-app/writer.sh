#!/bin/bash

WRITEFILE=$1
WRITESTR=$2

if [ -z "$WRITEFILE" ] || [ -z "$WRITESTR" ]; then
	echo "Incomplete parameters"
	exit 1
fi

exec 2>/dev/null
mkdir -p $(dirname $WRITEFILE)
echo "$WRITESTR" >| $WRITEFILE
EXITCODE=$(echo "$?")
if [ $EXITCODE != "0" ]; then
	echo "File cannot be created ($WRITEFILE, $EXITCODE)"
	exit 1
fi

