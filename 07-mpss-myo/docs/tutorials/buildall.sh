
# Copyright 2012-2017 Intel Corporation.
#
# This file is subject to the Intel Sample Source Code License. A copy
# of the Intel Sample Source Code License is included.
#
# buildall.sh
#
# buildall.sh uses buildService.sh and buildClient.sh to build all ten of the myo tutorials and places the card-side
# binaries into the cardSideBinaries directory, and places the host-side binaries into the hostSideBinaries directory.
#

# First, remove the previous binaries:

rm -f cardSideBinaries/*_service hostSideBinaries/*_client

# Next, make sure that the two directories: cardSideBinaries and hostSideBinaries exist before attempting to
# build the binaries:

if [ ! -d cardSideBinaries ]; then
    mkdir cardSideBinaries
fi

if [ ! -d hostSideBinaries ]; then
    mkdir hostSideBinaries
fi

# Next, build the card-side of the tutorials:

./buildService.sh arena             arena_service.c             ../cardSideBinaries/arena_service
./buildService.sh arrayop           arrayop_service.c           ../cardSideBinaries/arrayop_service
./buildService.sh asyncRPC          asyncRPC_service.c          ../cardSideBinaries/asyncRPC_service
./buildService.sh globalvariable    globalvar_service.c         ../cardSideBinaries/globalvariable_service
./buildService.sh helloworld        helloworld_service.c        ../cardSideBinaries/helloworld_service
./buildService.sh linkedlist        linkedlist_service.c        ../cardSideBinaries/linkedlist_service
./buildService.sh multicard-subset  helloworld_service.c        ../cardSideBinaries/multicard-subset_service
./buildService.sh multiversionarena mversionarena_service.c     ../cardSideBinaries/multiversionarena_service
./buildService.sh producerconsumer  producerconsumer_service.c  ../cardSideBinaries/producerconsumer_service
./buildService.sh rafa              rafa_service.c              ../cardSideBinaries/rafa_service

# Now, build the host-side of the tutorials:

./buildClient.sh  arena             arena_client.c              ../hostSideBinaries/arena_client
./buildClient.sh  arrayop           arrayop_client.c            ../hostSideBinaries/arrayop_client
./buildClient.sh  asyncRPC          asyncRPC_client.c           ../hostSideBinaries/asyncRPC_client
./buildClient.sh  globalvariable    globalvar_client.c          ../hostSideBinaries/globalvariable_client
./buildClient.sh  helloworld        helloworld_client.c         ../hostSideBinaries/helloworld_client
./buildClient.sh  linkedlist        linkedlist_client.c         ../hostSideBinaries/linkedlist_client
./buildClient.sh  multicard-subset  helloworld_client.c         ../hostSideBinaries/multicard-subset_client
./buildClient.sh  multiversionarena mversionarena_client.c      ../hostSideBinaries/multiversionarena_client
./buildClient.sh  producerconsumer  producerconsumer_client.c   ../hostSideBinaries/producerconsumer_client
./buildClient.sh  rafa              rafa_client.c               ../hostSideBinaries/rafa_client
