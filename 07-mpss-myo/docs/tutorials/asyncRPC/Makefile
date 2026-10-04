# Copyright 2012-2017 Intel Corporation.
#
# This file is subject to the Intel Sample Source Code License. A copy
# of the Intel Sample Source Code License is included.

#.SILENT:

include ../config.mak

# each app should config the following part#
APP_NAME=asyncRPC
CLIENT_SRCS = asyncRPC_client.c 
SERVICE_SRCS = asyncRPC_service.c 
# end of config#


CLIENT_OBJS = $(CLIENT_SRCS:.c=.client.o)
SERVICE_OBJS = $(SERVICE_SRCS:.c=.service.o)

EXE=_service
SIM=sim
ifdef MIC
EXE=.so
SIM=
endif
CLIENT_TARGET = myo_$(APP_NAME)
SERVICE_TARGET = myo_$(APP_NAME)$(EXE)

all: clean client service

client: $(CLIENT_TARGET)
	
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(HOST_CC) $(HOST_LDFLAGS)  $(CLIENT_OBJS) -o $@ -L$(MYO_LIB) -lmyo$(SIM)-client

$(CLIENT_OBJS): %.client.o: %.c
	$(HOST_CC) $(HOST_CXXFLAGS) -I$(MYO_INC)  $^ -o $@ -c

service:  $(SERVICE_TARGET)

$(SERVICE_TARGET): $(SERVICE_OBJS)
	$(CARD_CC) $(CARD_CXXFLAGS) $(CARD_LDFLAGS) -I$(MYO_INC) $< -o $@ -L$(MYO_LIB) -lmyo$(SIM)-service

$(SERVICE_OBJS): %.service.o: %.c
	$(CARD_CC) $(CARD_CXXFLAGS) -I$(MYO_INC)  $< -o $@ -c

clean: 
	/bin/rm -f $(CLIENT_TARGET) $(SERVICE_TARGET) *.o
