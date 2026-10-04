# Copyright 2012-2017 Intel Corporation.
#
# This file is subject to the Intel Sample Source Code License. A copy
# of the Intel Sample Source Code License is included.

The license govering the MYO tutorials is found in the file: COPYING

   MYO Tutorials

I. Steps to Build and Run the tutorials

A. Steps to build the tutorials on Linux:

If you have not previously done so:

1. Extract the contents of the MPSS tar file named mpss-N.M.tar where N and M are the MPSS major and minor version numbers.
2. Follow instructions found in readme.txt from the tar file.

3. Build the tutorials using the 'buildall.sh' script:
  a) Set your environment for the MPSS-sdk:
	    [command prompt]$ . /opt/mpss/N.M/environment-setup-*-linux
	where N and M are major and minor versions of the MPSS package you installed.

  b) Change directory to the root of the myo tutorials directory:
		[command prompt]$ cd ..../tutorials

  c) Then, type:
		[command prompt]$ ./buildall.sh

  d) Verify the buildall.sh script populated the hostSideBinaries and the cardSideBinaries directories with the 10 MYO tutorials:

[command prompt]$ ls cardSideBinaries/ hostSideBinaries/
cardSideBinaries/:
arena_service		helloworld_service
arrayop_service		linkedlist_service	   producerconsumer_service
asyncRPC_service	multicard-subset_service   rafa_service
downloadCSBinaries.bat	multiversionarena_service
globalvariable_service

hostSideBinaries/:
arena_client	 globalvariable_client	  multiversionarena_client
arrayop_client	 helloworld_client	  producerconsumer_client
asyncRPC_client  linkedlist_client	  rafa_client
copy_run.sh	 multicard-subset_client
[command prompt]$

B. Steps to build the tutorials on Windows.

If you have not previously done so:

1. Unzip the Intel(R) Xeon Phi(TM) installation package from zip file mpss-[version number]-windows.zip.

2. Install the "Intel(R) Xeon Phi(TM) Coprocessor.exe" using the default location as the destination to install.

3. Install the "Intel(R) Xeon Phi(TM) coprocessor essentials.exe" using the default location as the destination to install.

4. Install the Composer XE compiler.

5. Install MSVS 2012.

6. Bring up a DOS command window for the Composer XE compiler, using:

   Start / All Programs / Intel Parallel Studio XE 2013 / Command prompt / Parallel Studio XE with Intel Compiler XE / Intel 64 Studio 2012 mode

7. Change directory to the directory: c:\Program Files\Intel\MPSS\sdk\tutorials\myo

8. Then run, the batch script:

.\buildall.bat

The buildall.bat script should result in the cardSideBinaries folder being populated with the 10 card-side MYO tutorial applications.

9. Next, load the solution file c:\Program Files\Intel\MPSS\sdk\tutorials\myo\windows\tutorials\tutorials.sln, and build the Debug version of the solution.

The tutorials.sln build should populate, the folder c:\Program Files\Intel\MPSS\sdk\tutorials\myo\windows\tutorials\x64\Debug with the 10 host-side
MYO tutorial applications.

10. Please copy the executable files from the above folder to the hostSideBinaries folder to execute the files properly.

C. Steps to run the tutorials on Linux

   First, make sure that the MPSS service is started, and make sure that the libmyo-service.so is copied down to the /usr/lib64 directory on all of the cards
   installed on your system.
   [command prompt]$ cd hostSideBinaries
   [command prompt]$ ./copy_run.sh <tutorial_name>

   For example:

   [command prompt]$ ./copy_run.sh helloworld_client

D. Steps to run the tutorials on Windows

   First, make sure that you have started the mpss service, and make sure that the libmyo-service.so is installed in the /usr/lib64 directory on all of the cards
   installed on your system.

   You will need a means to copy files to the card, and to have a command shell on it.  We recommend that you use putty, plink and pscp.
   Also, we recommend that you use an rsa key with putty, plink and pscp.  So, you will need to install an OPENSSH key onto the card,

   Use the script: cardSideBinaries/downloadCSBinaries.bat to download the card-side executables to the /cs_binaries directory on the MIC card.
   You will need to give the downloadCSBinaries.bat script a parameter to indicate the path to your private ssh key, along with the user id and the
   ip address of the card. For example, the commandline arguments would look like:
   <PATH_TO_PRIVATE_KEY> root@192.168.1.100

   Next, bring up two DOS command windows.  Use one DOS command window to initiate putty to start an ssh session with the card, and then cd /cs_binaries.

   In the second window, cd to hostsideBinaries folder.

   Now, for each of the 10 myo tutorials:

   1. Start the card-side of the myo tutorial in the ssh session on the card.
   2. Start the host-side of the myo tutorial in the other DOS command window.


II. Introduction

  This file describes how to use MYO APIs to write MYO applications without compiler support. We use a set of example applications as case study. BKMs for workloads porting are also covered.
  The included examples are:
    helloworld
    global-variable
    asyncRPC
    arrayop
    linked-list
    producer-consumer
    arena
    multiversion-arena
    rafa
    multicard-subset
III. Environment


  First, you need to build the MYO libraries for client(host) and service(CARD) side. Obtain the MYO source code distribution, and follow the instructions in the src/README directory.

IV. Tutorial
-----------------------------------------------------------------

        -----------------------------------------------
        |                       |                     |
        |        Host           |         CARD        |
        |  Private Memory Space | Private Memory Space|
        |                       |                     |
        |                       |                     |
        -----------------------------------------------
        |              Shared Memory Space            |
        -----------------------------------------------

-----------------------------------------------------------------
        Figure 1. MYO partial shared virtual memory

  In figure 1, MYO partially shares the virtual space between host and card. Only the data allocated by MYO API(for example myoSharedMalloc) is accessible in both host and card. MYO maintains consistency of the virtual shared memory. In this release, we only describe the usage of MYO in release consistency, although we also provide sequential consistency model. In release consistency model, stores by an agent(host or card become globally visible only at software controlled explicit ordering points. The runtime system exports 2 API class - one flushes all the prior stores to be globally visible(for example, myoRelease), while the other obtains all stores that have been made globally visible(for example, myoAcquire). Those 2 APIs guarantee the released data must be seen by the other after acquire, but do NOT guarantee they cannot be seen before the acquire. The software can put in these API calls directly to enforce the ordering actions. There are some obvious synchronization events, such as mutex acquire and release, barrier synchronization and remote function call and return. For example, there should be a release action before posting a semaphore, unlocking a mutex and waiting for a barrier, vice versa the acquire action.

1. First simple example: helloworld

  In the helloworld example, host stores one string "hello world" to a shared buffer, then calls card function which changes the string to "world hello".
  In both host and card program, main function should call myoiLibInit and myoiLibFini to initialize and finalize MYO runtime. Also the user needs to implement one callback function, myoiUserinit, which will be called at MYO runtime initialization stage. In card side, the user needs to register a remote function with one name string which is then used by myoiRemoteCall on host side.

  In "helloworld" example, host first allocates one buffer in the shared virtual space by myoSharedMalloc.
  After the host populates some data in the shared buffer, it calls myoRelease to flush the shared data and then calls myoiRemoteCall to call remote card function.  The address of the shared buffer is passed to the remote function as one parameter.
  When card receives the remote function call, myoAcquire is called to update its view of the shared data. And then card side processes the shared data. Before the remote function returns, it calls myoRelease to flush all the data to global memory.
  The myoiGetResult API is used in host side to wait the asynchronous remote function call. And before it prints out the result, host side calls myoAcquire to get host local data updated.
  In summary, this example introduces how to initialize/finalize the MYO runtime, how to call remote function, and how to allocate and manipulate one shared buffer between host and card. The code is under .../tutorials/helloworld.
----------------------------pseudo-code----------------------------
  //At host side:
  int main() {
    myoiLibInit(NULL); //initialize MYO runtime
    buffer = myoSharedMalloc(buffersize); //allocate shared buffer
    ... //some operations on the shared buffer
    myoRelease();// release before remote function call
    handle = myoiRemoteCall("kernel", sharedbuffer); //pass shared pointer to remote function
    myoiGetResult(handle); //wait for remote function
    myoAcquire(); //acquire after remote function return
    printf("result:%s\n", sharedbuffer);
    myoiLibFini(); //finalize MYO runtime
  }
  MyoError myoiUserInit(void) {
  }

  //At card side:
  void kernel(void * buffer) {
    myoAcquire(); //acquire in the service
    ... //some operations on the shared buffer
    myoRelease(); //release before return
  }
  int main() {
    myoiLibInit(NULL); //initialize MYO runtime
    myoiLibFini(); //finalize MYO runtime
  }
  MyoError myoiUserInit(void) {
    myoiRemoteFuncRegister((MyoiRemoteFuncType) kernel, "kernel");
  }
-------------------------------------------------------------------


2. Example for global shared variable

  In this section, one global variable for the shared buffer is declared in the "helloworld" example.
  On both host and card, after MYO runtime initialization, the global variable "buffer" will point to the same virtual address. In the hook function myoiUserInit, the user needs to register the global shared variable and allocate it in the shared space using myoSharedMalloc on host.
  Instead of passing the shared address in myoiRemoteCall, remote function in card can access the shared data via the global variable.
  In summary, this example shows how to define one global shared variable which is accessible in both host and card. the code is under .../tutorials/globalvar.
----------------------------pseudo-code----------------------------
  //At host side:
  shared char * buffer; //global shared variable
  int main() {
    ... //some operations on the shared buffer
    myoRelease();// release before remote function call
    handle = myoiRemoteCall("kernel", NULL);
    myoiGetResult(handle); //wait for remote function
    myoAcquire(); //acquire after remote function return
    printf("result:%s\n", buffer);
    ...
  }
  MyoError myoiUserInit(void) {
    buffer = (char*) myoiShareMalloc(buffersize); //allocate buffer for global shared variable
    myoiVarRegister((void*) &buffer, "buffer"); //register shared variable
  }

  //At card side:
  shared char * buffer; //global shared variable
  void kernel() {
    myoAcquire(); //acquire in the service
    ... //some operations on the shared buffer
    myoRelease(); //release before return
  }
  MyoError myoiUserInit(void) {
    myoiRemoteFuncRegister((MyoiRemoteFuncType) kernel, "kernel");
    myoiVarRegister((void*) &buffer, "buffer"); //register shared variable
  }
-------------------------------------------------------------------
3. Asynchronous remote function call

  In this section, one simple implementation of producer/consumer example is used to explain how to program with asynchronous remote function call.
  To keep it simple, there is one producer and one consumer playing with buffer[2]. The producer(on host) generates data and saves the data in one slot of buffer[2]. By switching the buffer, the producer can save another data, while the consumer can consume the data in the other buffer slot.
  After myoiRemoteCall, host can generate another data and save in another buffer slot. myoiGetResult will wait there until the remote function finishes while myoiCheckResult just checks the execution of the remote function and returns no matter the remote function finishes or not.
  In summary, this example shows how to program with asynchronous RPC and fine-grained access the shared data. Compared with offloading mode(for example, CUDA), when card process the RPC request, host can access the shared data simultaneously. The Code is under .../tutorials/asyncRPC.
----------------------------pseudo-code----------------------------
  //At host side:
  shared int * buffer; //global shared variable, buffer[2]
  shared int * result;
  int producer() {
    ...
    for() {
      index = (index +1) %2; //switch the index between the 2 buffers
      if(rpchandle[index]) { //wait for consumer done
        myoiGetResult(rpchandle[index]);
        rpchandle[index] = NULL;
        myoAcquire(); //acquire after remote function return
      }
      buffer[index] = produce(); //produce
      myoRelease(); //release before remote function
      //invoke asynchronous remote call to consume the data
      rpchandle[index] = myoiRemoteCall("consumer", index);
    }
    ...
  }
  MyoError myoiUserInit(void) {
    buffer = (int*) myoiShareMalloc(buffersize); //allocate buffer for global shared variable
    result = (int*) myoiShareMalloc(resultsize); //allocate buffer for global shared variable
    myoiVarRegister((void*) &buffer, "buffer"); //register shared variable
    myoiVarRegister((void*) &result, "result"); //register shared variable
  }

  //At card side:
  shared int * buffer; //global shared variable, buffer[2]
  shared int * result;
  void consumer(int index) {
    myoAcquire(); //acquire in the service
    //some operations on the shared buffer
    *result += buffer[index];
    myoRelease(); //release before return
  }
  MyoError myoiUserInit(void) {
    myoiRemoteFuncRegister((MyoiRemoteFuncType) consumer, "consumer");
    myoiVarRegister((void*) &buffer, "buffer"); //register shared variable
    myoiVarRegister((void*) &result, "result"); //register shared variable
  }
-------------------------------------------------------------------

4. linked-list example

  In this example, we show one complex structure data(linkedlist) shared between host and card without the need of explicit marshalling.
  On host side, one linkedlist is initialized and linked with one pointer(head). All the elements(data and pointers) are allocated by myoSharedMalloc in shared space. The card remote function can manipulate the linked-list, because those pointers points to the shared space which is guaranteed to be coherent by MYO runtime.
  In summary, if the user wants to manipulate pointer-contained structure between host and card without virtual shared memory, the complex data structure has to be marshalled and reconstructed besides data transfer back and forth. The code is under .../tutorials/linkedlist.
----------------------------pseudo-code----------------------------
  typedef struct _LinkedListNode {
    int data;
    struct _LinkedListNode * next;
  } LinkedListNode;
  typedef LinkedListNode LinkedList;

  //At host side:
  int main() {
    ...
    shared LinkedList * head = initLinkList(); //generate the linked-list in shared space
    myoRelease(); //release before remote function
    rpchandle = myoiRemoteCall("caculateInMIC", head);
    myoiGetResult(rpchandle);
    myoAcquire(); //acquire after remote function return
    printLinkedList(head);
    ...
  }

  //At card side:
  void caculateInMIC(LinkedList * head) {
    myoAcquire(); //acquire in service
    ...//manipulate the linked-list
    myoRelease(); //release before return
  }
  MyoError myoiUserInit(void) {
    myoiRemoteFuncRegister((MyoiRemoteFuncType) caculateInMIC, "caculateInMIC");
  }
-------------------------------------------------------------------


5. Synchronization primitives

  MYO runtime provides some global synchronization primitives across the host
and card, such as mutex, semaphore and barrier.
  This is one producer-consumer example implemented with MyoSem and MyoMutex.
There is one ring-buffer guarded by MyoMutex for multiple producers and
consumers. Producers generate data and insert to the head of the ring buffer,
and consumers consume those data from the rear of the ring buffer. The
producers and consumers are synchronized by MyoSem. i.e. once producers
generate data, it will post the MyoSem while consumers wait for MyoSem. We use
multiple asynchronous remote function calls to generate multiple consumers.
  Note that the global synchronization primitive is data synchronization order
points in nature. i.e. when using global synchronization primitives, the user
need to take care of the acquire and release of the shared data.
  The code is under .../tutorials/producerconsumer.
----------------------------pseudo-code----------------------------
  //At host side:
  shared int * buffer; //shared buffer[MAXARRAY];
  shared MyoMutex * mutex; // protect the buffer
  shared MyoSem * productsem; //init as 0;
  shared MyoSem * spacesem; //init as MAXARRAY;
  shared int * result;
  int main() {
    ...
    //create the consumers in card
    for(i=0; i<MAXCONSUMER; i++) {
      rpchandle[i] = myoiRemoteCall("consumer", i);
    }
    producer();
    for(i=0; i<MAXCONSUMER; i++) {
      myoiGetResult(rpchandle[i]);
    }
    myoAcquire();//acquire after remote function return
    printf("result:%d\n", *result);
	 ...
  }
  void producer() {
    for() {
      myoSemWait(*spacesem);
      myoAcquire(); // acquire the shared buffer
      produce(buffer); //generate data to the shared buffer
      myoRelease(); //release the buffer
      myoSemPost(*productsem);
    }
  }

  //At card side:
  shared int * buffer; //shared buffer[MAXARRAY];
  shared MyoMutex * mutex; // protect the buffer
  shared MyoSem * productsem; //init as 0;
  shared MyoSem * spacesem; //init as MAXARRAY;
  shared int * result;
  void consumer(int threadid) {
    for() {
        myoSemWait(*productsem);
        myoMutexLock(*mutex);
        myoAcquire(); //acquire in service
        data = retrieveProduct(); //read the buffer
        myoMutexUnlock(*mutex);
        myoSemPost(*spacesem); //recycle the buffer
        //consume the data
        myoMutexLock(*resultmutex);
        *result += data;
        myoRelease(); //release *result
        myoMutexUnlock(*resultmutex);
    }
  }
-------------------------------------------------------------------





5. Arena-based memory management

  MYO runtime provides a series of arena-based API to allow finer grain coherence management (instead of the whole shared memory space). The user can allocate the shared data in separate arenas and manage data coherence differently for those arenas.
  This example is based on the example in "Asynchronous remote function call".
We use one arena for the shared data "result". So card remote function just needs to release the arena which stores the "result", instead of releasing all the shared data. Also host side only needs to acquire the arena for the "result".
  In summary, arena-based memory management provides more flexible programmability over coherence control as well as more chance for high performance. The code is under .../tutorials/arena.

6. Multi-version arena example

  In MYO arena implementation, the user can specify type and property of the arena. Within the supported types and properties, some focus on runtime performance while some others relate to application data access patterns. Multi-version arena is suitable for the pipeline characteristic of the data access pattern.
  This is one optimization on the producer-consumer example in "Synchronization primitives". In the concept of multi-version arena, the runtime maintain the versions of the data: myoArenaRelease will release the data to global data and increase the version number; myoArenaAcquire will acquire next version of the data. So in this example, the producer and consumer don't need to maintain the ring-buffer. Host directly generates the data into the shared buffer and releases one version with myoArenaRelease, while card acquires the next version of the data with myoArenaAcquire.
  In summary, multi-version arena simplify the code for producer-consumer case and with high performance. The code is under .../tutorials/mversionarena.

7.Reverse/Forward Acceleration

 With MYO the user has the flexibility of executing a remotefunction either on the card side (forward accelaration) or on the host side (reverse accelartion) by specifing the card number. The host is indexed as -1.

The code is under .../tutorials/rafa.

8.Multicard Subset

  The user has the flexibility to run the MYO application on select cards by passing a parameter to the initialization function in the host.In this
  example the user runs the helloworld on the second card(This would run only if the system has atleast 2 cards).
