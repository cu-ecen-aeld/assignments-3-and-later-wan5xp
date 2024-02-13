#!/bin/sh

FILESDIR=$1
SEARCHSTR=$2

if [ -z "$FILESDIR" ] || [ -z "$SEARCHSTR" ]; then
	echo "Incomplete parameters"
	exit 1
fi
if [ ! -d "$FILESDIR" ]; then
	echo "Directory does not exists"
	exit 1
fi

X=$(ls -1 "$FILESDIR" | wc -l)
Y=$(grep "$SEARCHSTR" $FILESDIR/* -s | wc -l)

echo "The number of files are $X and the number of matching lines are $Y"
