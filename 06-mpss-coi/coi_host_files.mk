# Copyright 2010-2017 Intel Corporation.
#
# This library is free software; you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License as published
# by the Free Software Foundation, version 2.1.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
#
# Disclaimer: The codes contained in these modules may be specific
# to the Intel Software Development Platform codenamed Knights Ferry,
# and the Intel product codenamed Knights Corner, and are not backward
# compatible with other Intel products. Additionally, Intel will NOT
# support the codes or instruction set in future products.
#
# Intel offers no warranty of any kind regarding the code. This code is
# licensed on an "AS IS" basis and Intel is not obligated to provide
# any support, assistance, installation, training, or other services
# of any kind. Intel is also not obligated to provide any updates,
# enhancements or extensions. Intel specifically disclaims any warranty
# of merchantability, non-infringement, fitness for any particular
# purpose, and any other warranty.
#
# Further, Intel disclaims all liability of any kind, including but
# not limited to liability for infringement of any proprietary rights,
# relating to the use of the code, even if Intel is notified of the
# possibility of such liability. Except as expressly stated in an Intel
# license agreement provided with this code and agreed upon with Intel,
# no license, express or implied, by estoppel or otherwise, to any
# intellectual property rights is granted herein.

HOST_LIB		= libcoi_host.so
HOST_LIB_SRC1	= api/buffer/buffer_source.cpp \
	api/buffer/buffer_sink.cpp \
	api/engine/engine_source.cpp \
	api/engine/engine_common.cpp \
	api/event/event_source.cpp \
	api/event/event_common.cpp \
	api/debug/sanitychecks.cpp \
	api/pipeline/pipeline_source.cpp \
	api/pipeline/pipeline_sink.cpp \
	api/process/process_source.cpp \
	api/process/process_sink.cpp \
	api/result/result_common.cpp \
	api/sysinfo/sysinfo_common.cpp \
	mechanism/buffer/region_allocator.cpp \
	mechanism/buffer/buffer.cpp \
	mechanism/buffer/buffernodes.cpp \
	mechanism/buffer/normalbuffer.cpp \
	mechanism/buffer/subbuffer.cpp \
	mechanism/buffer/svasbuffer.cpp \
	mechanism/buffer/hugetlbbuffer.cpp \
	mechanism/buffer/sinkmemorybuffer.cpp \
	mechanism/dma/dma.cpp \
	mechanism/engine/engine.cpp \
	mechanism/event/event.cpp \
	mechanism/log/log.cpp \
	mechanism/pipeline/pipeline_sink.cpp \
	mechanism/pipeline/pipeline_source.cpp \
	mechanism/process/process_source.cpp \
	mechanism/process/process_sink.cpp \
	mechanism/proxy/uproxy_host.cpp \
	policy/scheduler/dependency_dag.cpp \
	legal/elf_headers_license.cpp \
	legal/queue_license.cpp

HOST_LIB_SRC = $(HOST_LIB_SRC1:%.cpp=src/%.cpp)
HOST_LIB_OBJ = $(HOST_LIB_SRC1:%.cpp=$(DIR_BUILD)%.o)

HOST_LIB_OBJ_ALL = $(HOST_LIB_OBJ)

