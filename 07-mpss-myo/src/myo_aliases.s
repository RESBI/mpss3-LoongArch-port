/* LoongArch 移植：公开 ABI 名字的别名（原为 .symver，见各自的版本汇编头） */
.globl myoArenaCreate
.set myoArenaCreate, myoArenaCreate1
.globl myoArenaDestroy
.set myoArenaDestroy, myoArenaDestroy1
.globl myoArenaMalloc
.set myoArenaMalloc, myoArenaMalloc1
.globl myoArenaFree
.set myoArenaFree, myoArenaFree1
.globl myoArenaAlignedMalloc
.set myoArenaAlignedMalloc, myoArenaAlignedMalloc1
.globl myoArenaAlignedFree
.set myoArenaAlignedFree, myoArenaAlignedFree1
.globl myoArenaAcquire
.set myoArenaAcquire, myoArenaAcquire1
.globl myoArenaRelease
.set myoArenaRelease, myoArenaRelease1
.globl myoArenaAcquireOwnership
.set myoArenaAcquireOwnership, myoArenaAcquireOwnership1
.globl myoArenaReleaseOwnership
.set myoArenaReleaseOwnership, myoArenaReleaseOwnership1
.globl myoArenaGetHandle
.set myoArenaGetHandle, myoArenaGetHandle1
.globl myoSharedMalloc
.set myoSharedMalloc, myoSharedMalloc1
.globl myoSharedFree
.set myoSharedFree, myoSharedFree1
.globl myoSharedAlignedMalloc
.set myoSharedAlignedMalloc, myoSharedAlignedMalloc1
.globl myoSharedAlignedFree
.set myoSharedAlignedFree, myoSharedAlignedFree1
.globl myoAcquire
.set myoAcquire, myoAcquire1
.globl myoRelease
.set myoRelease, myoRelease1
.globl myoAcquireOwnership
.set myoAcquireOwnership, myoAcquireOwnership1
.globl myoReleaseOwnership
.set myoReleaseOwnership, myoReleaseOwnership1
.globl myoMutexCreate
.set myoMutexCreate, myoMutexCreate1
.globl myoMutexLock
.set myoMutexLock, myoMutexLock1
.globl myoMutexUnlock
.set myoMutexUnlock, myoMutexUnlock1
.globl myoMutexTryLock
.set myoMutexTryLock, myoMutexTryLock1
.globl myoMutexDestroy
.set myoMutexDestroy, myoMutexDestroy1
.globl myoSemCreate
.set myoSemCreate, myoSemCreate1
.globl myoSemWait
.set myoSemWait, myoSemWait1
.globl myoSemPost
.set myoSemPost, myoSemPost1
.globl myoSemTryWait
.set myoSemTryWait, myoSemTryWait1
.globl myoSemDestroy
.set myoSemDestroy, myoSemDestroy1
.globl myoBarrierCreate
.set myoBarrierCreate, myoBarrierCreate1
.globl myoBarrierWait
.set myoBarrierWait, myoBarrierWait1
.globl myoBarrierDestroy
.set myoBarrierDestroy, myoBarrierDestroy1
.globl myoMyId
.set myoMyId, myoMyId1
.globl myoNumNodes
.set myoNumNodes, myoNumNodes1
.globl myoTicks
.set myoTicks, myoTicks1
.globl myoWallTime
.set myoWallTime, myoWallTime1
.globl myoStatOn
.set myoStatOn, myoStatOn1
.globl myoStatOff
.set myoStatOff, myoStatOff1
.globl myoGetMemUsage
.set myoGetMemUsage, myoGetMemUsage1
.globl myoHTimeOn
.set myoHTimeOn, myoHTimeOn1
.globl myoiRemoteFuncRegister
.set myoiRemoteFuncRegister, myoiRemoteFuncRegister1
.globl myoiRemoteFuncLookupByName
.set myoiRemoteFuncLookupByName, myoiRemoteFuncLookupByName1
.globl myoiRemoteFuncLookupByAddr
.set myoiRemoteFuncLookupByAddr, myoiRemoteFuncLookupByAddr1
.globl myoiHostFptrTableRegister
.set myoiHostFptrTableRegister, myoiHostFptrTableRegister1
.globl myoiRemoteCall
.set myoiRemoteCall, myoiRemoteCall1
.globl myoiRemoteThunkCall
.set myoiRemoteThunkCall, myoiRemoteThunkCall1
.globl myoiCheckResult
.set myoiCheckResult, myoiCheckResult1
.globl myoiGetResult
.set myoiGetResult, myoiGetResult1
.globl myoiVarRegister
.set myoiVarRegister, myoiVarRegister1
.globl myoiHostVarTablePropagate
.set myoiHostVarTablePropagate, myoiHostVarTablePropagate1
.globl myoiHostSharedMallocTableRegister
.set myoiHostSharedMallocTableRegister, myoiHostSharedMallocTableRegister1
.globl myoiLibInit
.set myoiLibInit, myoiLibInit1
.globl myoiSupportsFeature
.set myoiSupportsFeature, myoiSupportsFeature1
.globl myoiLibFini
.set myoiLibFini, myoiLibFini1
.globl myoiSetMemNonConsistent
.set myoiSetMemNonConsistent, myoiSetMemNonConsistent1
.globl myoiSetMemConsistent
.set myoiSetMemConsistent, myoiSetMemConsistent1
.globl myoiThreadCreate
.set myoiThreadCreate, myoiThreadCreate1
.globl myoiThreadExit
.set myoiThreadExit, myoiThreadExit1
.globl myoiThreadDetach
.set myoiThreadDetach, myoiThreadDetach1
.globl myoiThreadJoin
.set myoiThreadJoin, myoiThreadJoin1
.globl myoiThreadSelf
.set myoiThreadSelf, myoiThreadSelf1
.globl myoiThreadMutexInit
.set myoiThreadMutexInit, myoiThreadMutexInit1
.globl myoiThreadMutexDestroy
.set myoiThreadMutexDestroy, myoiThreadMutexDestroy1
.globl myoiThreadMutexLock
.set myoiThreadMutexLock, myoiThreadMutexLock1
.globl myoiThreadMutexTryLock
.set myoiThreadMutexTryLock, myoiThreadMutexTryLock1
.globl myoiThreadMutexUnlock
.set myoiThreadMutexUnlock, myoiThreadMutexUnlock1
.globl myoiThreadCondInit
.set myoiThreadCondInit, myoiThreadCondInit1
.globl myoiThreadCondDestroy
.set myoiThreadCondDestroy, myoiThreadCondDestroy1
.globl myoiThreadCondWait
.set myoiThreadCondWait, myoiThreadCondWait1
.globl myoiThreadCondSignal
.set myoiThreadCondSignal, myoiThreadCondSignal1
.globl myoiThreadCondBroadCast
.set myoiThreadCondBroadCast, myoiThreadCondBroadCast1
.globl myoiThreadSemaphoreInit
.set myoiThreadSemaphoreInit, myoiThreadSemaphoreInit1
.globl myoiThreadSemaphoreDestroy
.set myoiThreadSemaphoreDestroy, myoiThreadSemaphoreDestroy1
.globl myoiThreadSemaphorePost
.set myoiThreadSemaphorePost, myoiThreadSemaphorePost1
.globl myoiThreadSemaphoreWait
.set myoiThreadSemaphoreWait, myoiThreadSemaphoreWait1
.globl myoiThreadSemaphoreTryWait
.set myoiThreadSemaphoreTryWait, myoiThreadSemaphoreTryWait1
.globl myoiThreadBarrierInit
.set myoiThreadBarrierInit, myoiThreadBarrierInit1
.globl myoiThreadBarrierDestroy
.set myoiThreadBarrierDestroy, myoiThreadBarrierDestroy1
.globl myoiThreadBarrierWait
.set myoiThreadBarrierWait, myoiThreadBarrierWait1
.globl myoiThreadLocalCreate
.set myoiThreadLocalCreate, myoiThreadLocalCreate1
.globl myoiThreadLocalDestroy
.set myoiThreadLocalDestroy, myoiThreadLocalDestroy1
.globl myoiThreadLocalSet
.set myoiThreadLocalSet, myoiThreadLocalSet1
.globl myoiThreadLocalGet
.set myoiThreadLocalGet, myoiThreadLocalGet1
.globl myoiThreadSetAffinityMask
.set myoiThreadSetAffinityMask, myoiThreadSetAffinityMask1
