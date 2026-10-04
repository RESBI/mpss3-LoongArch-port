GOTO EndLicense
/*
 * Copyright 2012-2017 Intel Corporation.
 *
 * This file is subject to the Intel Sample Source Code License. A copy
 * of the Intel Sample Source Code License is included.
 */
:EndLicense

set MPSS_SDK="C:\Program Files\Intel\MPSS\sdk"

set TUT_LIST= (buffer_references buffer_with_user_memory buffers_with_pipeline_function coi_simple hello_world multiple_pipeline_explicit multiple_pipeline_implicit user_event)
 
FOR %%I in %TUT_LIST% do ( 
	pushd %%I
	echo "Doing %%I" 
	icl /Qmic %%I_sink.cpp -c -o %%I_sink.o
	icl /Qmic -rdynamic %%I_sink.o -o %%I_sink_mic -pthread -lcoi_device 
	copy ..\x64\Release\%%I_source.exe
	
	popd 
)
