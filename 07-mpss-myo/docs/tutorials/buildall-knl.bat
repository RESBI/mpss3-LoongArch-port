@echo off
REM Copyright 2012-2017 Intel Corporation.
REM
REM This file is subject to the Intel Sample Source Code License. A copy
REM of the Intel Sample Source Code License is included.
REM
REM buildall-knl.bat
REM
REM buildall-knl.bat uses build-knl.bat to build all ten of the myo tutorials and places the card-side
REM binaries into the cardSideBinaries folder.
REM

call .\build-knl.bat arena             arena_service.c             ..\cardSideBinaries\arena_service
call .\build-knl.bat arrayop           arrayop_service.c           ..\cardSideBinaries\arrayop_service
call .\build-knl.bat asyncRPC          asyncRPC_service.c          ..\cardSideBinaries\asyncRPC_service
call .\build-knl.bat globalvariable    globalvar_service.c         ..\cardSideBinaries\globalvariable_service
call .\build-knl.bat helloworld        helloworld_service.c        ..\cardSideBinaries\helloworld_service
call .\build-knl.bat linkedlist        linkedlist_service.c        ..\cardSideBinaries\linkedlist_service
call .\build-knl.bat multicard-subset  helloworld_service.c        ..\cardSideBinaries\multicard-subset_service
call .\build-knl.bat multiversionarena mversionarena_service.c     ..\cardSideBinaries\multiversionarena_service
call .\build-knl.bat producerconsumer  producerconsumer_service.c  ..\cardSideBinaries\producerconsumer_service
call .\build-knl.bat rafa              rafa_service.c              ..\cardSideBinaries\rafa_service
