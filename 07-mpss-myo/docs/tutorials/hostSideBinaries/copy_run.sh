#! /bin/bash

# Copyright 2012-2017 Intel Corporation.
#
# This file is subject to the Intel Sample Source Code License. A copy
# of the Intel Sample Source Code License is included.

if [[ $# -lt 1 ]]
then
	echo $#
	echo "Usage copy_run.sh <exe name>"
	exit
fi

micexe=`echo $1 | sed 's/_client$/_service/'`

if [ "$1" = "multicard-subset_client" ]; then
   nCards=1
else
   nCards=`micctrl -s | grep 'online' | wc | awk '{print $1;}'`
fi

while [ $nCards -gt 0 ]; do
   scp ../cardSideBinaries/$micexe 172.31.${nCards}.1:/tmp
   ssh 172.31.${nCards}.1 "/tmp/$micexe" &
   let nCards-=1
done

./$1
