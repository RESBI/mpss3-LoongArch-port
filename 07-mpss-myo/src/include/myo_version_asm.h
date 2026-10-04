/*
 * Copyright 2010-2017 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, version 2.1.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Disclaimer: The codes contained in these modules may be specific
 * to the Intel Software Development Platform codenamed Knights Ferry,
 * and the Intel product codenamed Knights Corner, and are not backward
 * compatible with other Intel products. Additionally, Intel will NOT
 * support the codes or instruction set in future products.
 *
 * Intel offers no warranty of any kind regarding the code. This code is
 * licensed on an "AS IS" basis and Intel is not obligated to provide
 * any support, assistance, installation, training, or other services
 * of any kind. Intel is also not obligated to provide any updates,
 * enhancements or extensions. Intel specifically disclaims any warranty
 * of merchantability, non-infringement, fitness for any particular
 * purpose, and any other warranty.
 *
 * Further, Intel disclaims all liability of any kind, including but
 * not limited to liability for infringement of any proprietary rights,
 * relating to the use of the code, even if Intel is notified of the
 * possibility of such liability. Except as expressly stated in an Intel
 * license agreement provided with this code and agreed upon with Intel,
 * no license, express or implied, by estoppel or otherwise, to any
 * intellectual property rights is granted herein.
 */
/*Version for Symbols( only Functions currently versioned)
Only that Linux Host Side code is versioned currently*/
#if (! defined MYO_MIC_CARD) && (! defined _WIN32)
   
/*
 * LoongArch 移植：新 binutils 拒绝在未定义该符号的目标文件里用 .symver 声明外部符号的
 * 默认版本。这些公开名的别名改在链接期用 --defsym 建立（见 gen_defsym.py），版本节点
 * 由 linker_script.map 指定。
 */
#if defined(__x86_64__) || defined(__i386__)
   __asm__(".symver myoArenaCreate1,myoArenaCreate@@MYO_1.0");  
   __asm__(".symver myoArenaDestroy1,myoArenaDestroy@@MYO_1.0"); 
   __asm__(".symver myoArenaMalloc1,myoArenaMalloc@@MYO_1.0"); 
   __asm__(".symver myoArenaFree1,myoArenaFree@@MYO_1.0"); 
   __asm__(".symver myoArenaAlignedMalloc1,myoArenaAlignedMalloc@@MYO_1.0"); 
   __asm__(".symver myoArenaAlignedFree1,myoArenaAlignedFree@@MYO_1.0"); 
   __asm__(".symver myoArenaAcquire1,myoArenaAcquire@@MYO_1.0"); 
   __asm__(".symver myoArenaRelease1,myoArenaRelease@@MYO_1.0"); 
   __asm__(".symver myoArenaAcquireOwnership1,myoArenaAcquireOwnership@@MYO_1.0"); 
   __asm__(".symver myoArenaReleaseOwnership1,myoArenaReleaseOwnership@@MYO_1.0"); 
   __asm__(".symver myoArenaGetHandle1,myoArenaGetHandle@@MYO_1.0"); 
   __asm__(".symver myoSharedMalloc1,myoSharedMalloc@@MYO_1.0"); 
   __asm__(".symver myoSharedFree1,myoSharedFree@@MYO_1.0"); 
   __asm__(".symver myoSharedAlignedMalloc1,myoSharedAlignedMalloc@@MYO_1.0"); 
   __asm__(".symver myoSharedAlignedFree1,myoSharedAlignedFree@@MYO_1.0"); 
   __asm__(".symver myoAcquire1,myoAcquire@@MYO_1.0"); 
   __asm__(".symver myoRelease1,myoRelease@@MYO_1.0"); 
   __asm__(".symver myoAcquireOwnership1,myoAcquireOwnership@@MYO_1.0"); 
   __asm__(".symver myoReleaseOwnership1,myoReleaseOwnership@@MYO_1.0"); 
   __asm__(".symver myoMutexCreate1,myoMutexCreate@@MYO_1.0"); 
   __asm__(".symver myoMutexLock1,myoMutexLock@@MYO_1.0"); 
   __asm__(".symver myoMutexUnlock1,myoMutexUnlock@@MYO_1.0"); 
   __asm__(".symver myoMutexTryLock1,myoMutexTryLock@@MYO_1.0"); 
   __asm__(".symver myoMutexDestroy1,myoMutexDestroy@@MYO_1.0"); 
   __asm__(".symver myoSemCreate1,myoSemCreate@@MYO_1.0"); 
   __asm__(".symver myoSemWait1,myoSemWait@@MYO_1.0"); 
   __asm__(".symver myoSemPost1,myoSemPost@@MYO_1.0"); 
   __asm__(".symver myoSemTryWait1,myoSemTryWait@@MYO_1.0"); 
   __asm__(".symver myoSemDestroy1,myoSemDestroy@@MYO_1.0"); 
   __asm__(".symver myoBarrierCreate1,myoBarrierCreate@@MYO_1.0"); 
   __asm__(".symver myoBarrierWait1,myoBarrierWait@@MYO_1.0"); 
   __asm__(".symver myoBarrierDestroy1,myoBarrierDestroy@@MYO_1.0"); 
   __asm__(".symver myoMyId1,myoMyId@@MYO_1.0"); 
   __asm__(".symver myoNumNodes1,myoNumNodes@@MYO_1.0"); 
   __asm__(".symver myoTicks1,myoTicks@@MYO_1.0"); 
   __asm__(".symver myoWallTime1,myoWallTime@@MYO_1.0"); 
   __asm__(".symver myoStatOn1,myoStatOn@@MYO_1.0"); 
   __asm__(".symver myoStatOff1,myoStatOff@@MYO_1.0"); 
   __asm__(".symver myoGetMemUsage1,myoGetMemUsage@@MYO_1.0"); 
   __asm__(".symver myoHTimeOn1,myoHTimeOn@@MYO_1.0"); 
   __asm__(".symver myoiRemoteFuncRegister1,myoiRemoteFuncRegister@@MYO_1.0"); 
   __asm__(".symver myoiRemoteFuncLookupByName1,myoiRemoteFuncLookupByName@@MYO_1.0"); 
   __asm__(".symver myoiRemoteFuncLookupByAddr1,myoiRemoteFuncLookupByAddr@@MYO_1.0"); 
   __asm__(".symver myoiHostFptrTableRegister1,myoiHostFptrTableRegister@@MYO_1.0"); 
   __asm__(".symver myoiRemoteCall1,myoiRemoteCall@@MYO_1.0"); 
   __asm__(".symver myoiRemoteThunkCall1,myoiRemoteThunkCall@@MYO_1.0"); 
   __asm__(".symver myoiCheckResult1,myoiCheckResult@@MYO_1.0"); 
   __asm__(".symver myoiGetResult1,myoiGetResult@@MYO_1.0"); 
   __asm__(".symver myoiVarRegister1,myoiVarRegister@@MYO_1.0"); 
   __asm__(".symver myoiHostVarTablePropagate1,myoiHostVarTablePropagate@@MYO_1.0"); 
   __asm__(".symver myoiHostSharedMallocTableRegister1,myoiHostSharedMallocTableRegister@@MYO_1.0"); 
   __asm__(".symver myoiLibInit1,myoiLibInit@@MYO_1.0");
   __asm__(".symver myoiSupportsFeature1,myoiSupportsFeature@@MYO_1.0");
   __asm__(".symver myoiLibFini1,myoiLibFini@@MYO_1.0"); 
   __asm__(".symver myoiSetMemNonConsistent1,myoiSetMemNonConsistent@@MYO_1.0"); 
   __asm__(".symver myoiSetMemConsistent1,myoiSetMemConsistent@@MYO_1.0"); 
   __asm__(".symver myoiThreadCreate1,myoiThreadCreate@@MYO_1.0"); 
   __asm__(".symver myoiThreadExit1,myoiThreadExit@@MYO_1.0"); 
   __asm__(".symver myoiThreadDetach1,myoiThreadDetach@@MYO_1.0"); 
   __asm__(".symver myoiThreadJoin1,myoiThreadJoin@@MYO_1.0"); 
   __asm__(".symver myoiThreadSelf1,myoiThreadSelf@@MYO_1.0"); 
   __asm__(".symver myoiThreadMutexInit1,myoiThreadMutexInit@@MYO_1.0"); 
   __asm__(".symver myoiThreadMutexDestroy1,myoiThreadMutexDestroy@@MYO_1.0"); 
   __asm__(".symver myoiThreadMutexLock1,myoiThreadMutexLock@@MYO_1.0"); 
   __asm__(".symver myoiThreadMutexTryLock1,myoiThreadMutexTryLock@@MYO_1.0"); 
   __asm__(".symver myoiThreadMutexUnlock1,myoiThreadMutexUnlock@@MYO_1.0"); 
   __asm__(".symver myoiThreadCondInit1,myoiThreadCondInit@@MYO_1.0"); 
   __asm__(".symver myoiThreadCondDestroy1,myoiThreadCondDestroy@@MYO_1.0"); 
   __asm__(".symver myoiThreadCondWait1,myoiThreadCondWait@@MYO_1.0"); 
   __asm__(".symver myoiThreadCondSignal1,myoiThreadCondSignal@@MYO_1.0"); 
   __asm__(".symver myoiThreadCondBroadCast1,myoiThreadCondBroadCast@@MYO_1.0"); 
   __asm__(".symver myoiThreadSemaphoreInit1,myoiThreadSemaphoreInit@@MYO_1.0"); 
   __asm__(".symver myoiThreadSemaphoreDestroy1,myoiThreadSemaphoreDestroy@@MYO_1.0"); 
   __asm__(".symver myoiThreadSemaphorePost1,myoiThreadSemaphorePost@@MYO_1.0"); 
   __asm__(".symver myoiThreadSemaphoreWait1,myoiThreadSemaphoreWait@@MYO_1.0"); 
   __asm__(".symver myoiThreadSemaphoreTryWait1,myoiThreadSemaphoreTryWait@@MYO_1.0"); 
   __asm__(".symver myoiThreadBarrierInit1,myoiThreadBarrierInit@@MYO_1.0"); 
   __asm__(".symver myoiThreadBarrierDestroy1,myoiThreadBarrierDestroy@@MYO_1.0"); 
   __asm__(".symver myoiThreadBarrierWait1,myoiThreadBarrierWait@@MYO_1.0"); 
   __asm__(".symver myoiThreadLocalCreate1,myoiThreadLocalCreate@@MYO_1.0"); 
   __asm__(".symver myoiThreadLocalDestroy1,myoiThreadLocalDestroy@@MYO_1.0"); 
   __asm__(".symver myoiThreadLocalSet1,myoiThreadLocalSet@@MYO_1.0"); 
   __asm__(".symver myoiThreadLocalGet1,myoiThreadLocalGet@@MYO_1.0"); 
   __asm__(".symver myoiThreadSetAffinityMask1,myoiThreadSetAffinityMask@@MYO_1.0"); 
#endif /* MYO_SKIP_SYMVER */
     
 #endif 
