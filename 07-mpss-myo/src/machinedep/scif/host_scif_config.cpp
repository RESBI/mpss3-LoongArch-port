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
/*
 Description: Run MYO host over SCIF library
*/

#include <stdio.h>
#include <assert.h>
#include "myo.h"
#include "myoimpl.h"
#include "myothreads.h"
#include "myodebug.h"
#include "myopinnedmem.h"
#include "myoconsistent.h"
#include "myoprint.h"
#include "myoinit.h"
#include <iostream>
#include <fstream>
#include <string.h>
#include "MYOMacros_common.h"
#include "myo_version_asm.h"
#ifdef MYO_HAS_LOAD_SUPPORT
#include "_System.IO.h"
#endif  /* MYO_HAS_LOAD_SUPPORT */
#include <sys/wait.h>

#define MYOI_STAT   stat
#define MYOI_STRDUP strdup

#ifdef __cplusplus
extern "C" {
#endif
extern MyoError _myoiLibInit(void *in_args);
extern void myoiUpdateDeviceList(void *in_args); 
extern void _myoiLibFini();
#ifdef MYO_HAS_LOAD_SUPPORT
extern MyoError myoiDownloadDependencies();
#endif /* MYO_HAS_LOAD_SUPPORT */
#ifdef __cplusplus
}
#endif

#define MYOI_MAX_DEVICES 16
#define MYO_LIB_NAME "libmyo-service.so"

extern MyoError (*_myoiUserInit)(void);
extern unsigned int myoiDeviceList[MYOI_MAX_PROCS];
extern unsigned int myoiNPeers;

#ifdef MYO_HAS_LOAD_SUPPORT

extern MyoError getDynamicLibfromELF(char *in_BinName, MyoiDependencyLib *io_myoiDLibNameList, unsigned int maxListSize);

#define MAX_PATH 260

// out filename - A character string of MAX_PATH length that will hold the path to the card side binary.
// Return -1 if we couldn't find a file.

static int GetDefaultMicBinName(char *filename)
{
    const char *pMicBinSuffix = "_mic";
    unsigned length = readlink("/proc/self/exe", filename, MAX_PATH);
    if(length <= 0 || length >= MAX_PATH)
    {
        return -1;
    }
    // We have a valid host binary name,
    // now build up a card binary name
    char *pDest = filename + length;
    length += strlen(pMicBinSuffix);
    if(length >= MAX_PATH)
    {
        return -1;
    }
    while (( *( pDest++ ) = *( pMicBinSuffix++ )) != '\0' )
    {
        ;
    }
    // Check to make sure the path we created actually points to a file.
    struct stat buf;   
    if(-1 == stat(filename, &buf))
    {
        return -1;
    }
    return length;
}

// out iFoundFile - A flag that will be non-zero if we found a path to a valid file.
// out binName - A character string of MAX_PATH length that will hold the path to the card side binary.

MyoError myoigetMicBinName(int &iFoundFile, char *binName)
{
    logPrintf(MLM_MACHINEDEP,MLL_FOUR,("%s Enter\n",__FUNCTION__));
    MyoError errInfo = MYO_SUCCESS;
    /* Read compiler configuration environment variables.  */
    iFoundFile = 0;
    int iLoadSupport = 0;
    char *tmpStr = getenv("MYO_LOAD_SUPPORT");
    if(tmpStr) {
        if(strcmp(tmpStr, "MYO_LOAD_USER_APP") == 0)
        {
            iLoadSupport = 1;
        }
        else
        {
            // The string was not blank but not valid!
            errPrintf("%s: MYO_LOAD_SUPPORT Cannot be \"%s\".\nTry MYO_LOAD_USER_APP instead.\n", __FUNCTION__, tmpStr);
            errInfo = MYO_INVALID_ENV;
            goto ret;
        }
    }
    if(iLoadSupport) {
        if ( -1 != GetDefaultMicBinName(binName)) {
            errInfo = MYO_SUCCESS; // Made a path to the file.
            iFoundFile = 1;
        } else {
            tmpStr = getenv("MYO_BIN_NAME");
            if( (tmpStr == NULL) || (strcmp(tmpStr,""))==0){
                // The string was blank!
                /*To ensure MYO_LIB_NAME input*/
                errPrintf("%s: Cannot find <prog>_mic card side binary to load.\nDid you forget to set the environment variable MYO_BIN_NAME?\n", __FUNCTION__);
                errInfo = MYO_INVALID_ENV;
                goto ret;
            }
            else
            {
                // The string was not blank.
                strncpy(binName, tmpStr, MAX_PATH-1); // copy up to MAX_PATH-1 chars to binName (last char has to be NULL)
                binName[MAX_PATH-1] = '\0'; // ensure binName is NULL-terminated
                struct MYOI_STAT buf;
                if(-1 == MYOI_STAT(binName, &buf)) {
                    errInfo = MYO_ERROR;
                    errPrintf("%s: Cannot find card side binary file %s\nPlease verify the environment variable MYO_BIN_NAME points at the right file?\n", __FUNCTION__, binName);
                }
                else
                {
                    // We found a file with the name selected.
                    iFoundFile = 1;
                }
            }
        }
    }
ret:
    logPrintf(MLM_MACHINEDEP,MLL_FOUR,("%s Exit\n",__FUNCTION__));
    return errInfo;
}

#endif  /* MYO_HAS_LOAD_SUPPORT */

/** @FUNC myoiLibInit1
 * Init entry of the MYO library 
 * @RETURN:
 *      MYO_SUCCESS;
 *      MYO_ERROR;
 **/
extern "C" MYOACCESSAPI MyoError SYMBOL_VERSION (myoiLibInit, 1)(void *in_args, void * userInitFunc /*MyoError (*userInitFunc)(void)*/)
{
    MyoError ret;
    if(userInitFunc != NULL) _myoiUserInit = (MyoError (*)(void))userInitFunc;

    startTimer(1,libinit_time);
    ret = _myoiLibInit(in_args);
    stopTimer(1,libinit_time);
    return(ret);
}

#ifdef MYO_HAS_LOAD_SUPPORT

void myoiFreeDependencyMem(MyoiDependencyLib &myoiDLibNameList)
{
    for (unsigned int j = 0; j< myoiDLibNameList.num; j++){
        if (myoiDLibNameList.myoiDLibNameList[j]){
            free(myoiDLibNameList.myoiDLibNameList[j]);
        }
    }
}
#endif  /* MYO_HAS_LOAD_SUPPORT */

/** @FUNC myoiLibFini
 * Finalize the MYO library 
 * @RETURN:
 **/
extern "C" void SYMBOL_VERSION(myoiLibFini,1)()
{
    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Enter!\n", __FUNCTION__));
    startTimer(1,libfini_time);
    _myoiLibFini();
    stopTimer(1,libfini_time);
    logPrintf(MLM_ALL_OTHERS,MLL_THREE, ("%s: Exit!\n", __FUNCTION__));
}

void myoiUpdateDeviceList(void *in_args)
{
    MyoiUserParams *iuserParams = (MyoiUserParams *)in_args;
    unsigned int i = 1, j = 0, devidcnt = 0;
 
    if (iuserParams != NULL) {
        while(iuserParams[j].type != MYOI_USERPARAMS_LAST_MSG)
        {
            if (iuserParams[j].type == MYOI_USERPARAMS_DEVID)
            {
                myoiDeviceList[i++] = iuserParams[j].nodeid;
                devidcnt++;
            }
            j++;
        }
        if (devidcnt > 0)
          {
            assert(i <= myoiNPeers);
            myoiNPeers = i;
          }
    }
}

/* To make myoiLibLocallyInit execute as early as possible, cause it to be
   executed in the context of a static initializer: */

extern
       "C"
            MyoError myoiLibLocallyInit(int);

static class StaticInitializerClass
{
public:
  StaticInitializerClass()
  {
    MyoError errInfo = myoiLibLocallyInit(0);
    // If the init failed for reasons like not being able to load and start the card side image, 
    // exit so that we don't continue on to run the application and get stuck waiting in limbo 
    // for a handshake with the card.
    if (MYO_SUCCESS != errInfo) {
        errPrintf("%s: myoiLibLocallyInit() failed with error code %d!\n", __FUNCTION__, errInfo );
        exit(1);
    }
    atexit((void (*)(void))myoiArenaModuleFini);
  };
} __staticInitializerObject;

#define IPLENGTH 128
using namespace std;
#ifdef MYO_HAS_LOAD_SUPPORT
using namespace System::IO;
#endif /* MYO_HAS_LOAD_SUPPORT */

#ifdef USETELNET
void createFtpService(char* deviceIP)
{
    string ftppath = string("/usr/bin/ftp");
    string ftparg0 = string("/usr/bin/ftp");
    string ftparg1 = string("-iv");
    string ftparg2 = string(deviceIP);
    if (execl(ftppath.c_str(), ftparg0.c_str(), ftparg1.c_str(), ftparg2.c_str(), NULL) == -1)
    {
        errPrintf("%s execl failed\n",__FUNCTION__);
        assert(0);
    }
}

void createTelnetService()
{
    string telnetpath = string("/usr/bin/expect");
    string telnetarg0 = string("/usr/bin/expect");
    if (execl(telnetpath.c_str(), telnetarg0.c_str(),NULL) == -1)
    {
        errPrintf("%s execl failed\n",__FUNCTION__);
        assert(0);
    }
}

MyoError FtpLibraryandExe(int fd_out, char *myoiMicBinName)
{
    string command ="";
    string filename ;
    int j;
    command.append("mic\n");

    // download library
    for (j = 0; j < myoiDLibNameList.num; j++) {
        Path::GetFile(string(myoiDLibNameList.myoiDLibNameList[j]), filename);
        command.append("put " + string(myoiDLibNameList.myoiDLibNameList[j]) + " /lib64/" + filename + "\n");
    }
    
    //download exe
    Path::GetFile(string(myoiMicBinName), filename);
    command.append("put " + string(myoiMicBinName) + " " +  filename + "\n");
    
    // send bye to ftp
    command.append(string("bye\n"));
    write(fd_out, command.c_str(), (strlen(command.c_str())));
    logPrintf(MLM_MACHINEDEP,MLL_FOUR,("%s command is %s\n",__FUNCTION__,command.c_str()));

    return MYO_SUCCESS;
}

MyoError runExeFile(int fd_out, char *deviceIP)
{
    string command ="";
    string filename;
    
    using namespace System::IO;
    Path::GetFile(string(myoiMicBinName), filename);
    command.append(string("set timeout 80\n"));
    command.append(string("spawn telnet " + string(deviceIP) + "\n"));
    command.append(string("expect \"~ #\"\n"));
    command.append(string("send \"chmod +x ") + "./" + filename + "\r\"\n");
    command.append(string("expect \"~ #\"\n"));
    command.append(string("send \" ") + "./" + filename + "\r\"\n");
    command.append(string("expect \"~ #\"\n"));
    command.append(string("send \"exit\"\n"));
    command.append(string("exit\n"));
    write(fd_out, command.c_str(), (strlen(command.c_str())));
    logPrintf(MLM_MACHINEDEP,MLL_FOUR,("%s command is %s\n",__FUNCTION__,command.c_str()));
    return MYO_SUCCESS;
}
#endif

#ifdef MYO_HAS_LOAD_SUPPORT


MyoError myoiDownloadDependencies()
{
    MyoError errInfo = MYO_ERROR;
    char micBinName[MAX_PATH];
    int iFoundFile = 0;
    errInfo = myoigetMicBinName(iFoundFile, micBinName);
    if (MYO_SUCCESS != errInfo)
    {
        goto ret;
    }
    if (iFoundFile)
    {
        MyoiDependencyLib myoiDLibNameList = { 0, { 0 }};
        char deviceIP[IPLENGTH];
        unsigned int i;
        errInfo = getDynamicLibfromELF(micBinName,&myoiDLibNameList,MAXDLIBNUMS);
        if (errInfo != MYO_SUCCESS) goto ret;
        // Output a trace to show the libraries.
        unsigned int j=0;
        for (j = 0; j< myoiDLibNameList.num; j++)
        {
           logPrintf(MLM_MACHINEDEP,MLL_FOUR,("%s found library %s \n", 
                     __FUNCTION__,myoiDLibNameList.myoiDLibNameList[j]));
        }

        for (i = 1; i < myoiNPeers; i++)
        {
#ifdef USETELNET
            sprintf(deviceIP, "192.168.%d.100",myoiDeviceList[i]); 
            
            int j=0,status;
            pid_t cpid_1,cpid_2;
            int fd[2],fd1[2];  // stdin, stdout
            pipe(fd);          // for communication between ftp child and parent
            pipe(fd1);         // for communication between telnet (ssh) and parent
            cpid_1 = fork();
            if (cpid_1 == -1)
            {
                errPrintf(" %s Failed to create fork cpid_1\n", __FUNCTION__);
                goto ret; 
            }
            else if (cpid_1 == 0)
            {
                fflush(stdin);
                fflush(stdout);
                fflush(stderr);
                int fdnull;
                fdnull = open("/dev/null", O_RDWR);
                if (fdnull != -1) dup2(fdnull,STDOUT_FILENO);
                close(fd[1]);                      // close output side of pipe
                dup2(fd[0],STDIN_FILENO);
                createFtpService(deviceIP);
            }   
            else {
                close(fd[0]);           // close input side of pipe
                errInfo = FtpLibraryandExe(fd[1],micBinName);
                /* Wait for ftp child to finish. */
                if ( -1 == waitpid(cpid_1,&status,0))
                {
                    errPrintf("%s Ftp Libraray and Exe failed\n");
                    goto ret;
                } 
                
                cpid_2 = fork();
                if (cpid_2 == -1)
                {
                    errPrintf("%s Failed to create fork cpid_2\n",__FUNCTION__);
                    goto ret;
                }
                else if (cpid_2 == 0)
                {
                    close(fd1[1]);              // Close output side of pipe
                    dup2(fd1[0],STDIN_FILENO);
                    createTelnetService();
                }
                else {
                    close(fd1[0]);              // Close input side of pipe
                    runExeFile(fd1[1],deviceIP);
                }
            }
#else  /* USETELNET */
            using namespace System::IO;
            string filename;
            string command ="";
            unsigned int j;
            // Find name for card[i]
            sprintf(deviceIP, "mic%d",myoiDeviceList[i]-1); 
            // download library
            for (j = 0; j < myoiDLibNameList.num; j++) {
                Path::GetFile(string(myoiDLibNameList.myoiDLibNameList[j]), filename);
                command.clear();
                command.append("sudo scp " + string(myoiDLibNameList.myoiDLibNameList[j]) 
                        + " " + deviceIP  + ":/lib64/"+ filename + "\n");
                if ( 0 != system(command.c_str()))
                {
                    errPrintf(" %s ", command.c_str());
                    errInfo = MYO_ERROR;
                    goto ret;
                }
            }
            // Download cardside binary.
            Path::GetFile(string(micBinName), filename);
            command.clear();
            command.append("scp " + string(micBinName) + " " + deviceIP + ":./" + filename + "\n");
            if ( 0 != system(command.c_str()))
            {
                errPrintf(" %s ", command.c_str());
                errInfo = MYO_ERROR;
                goto ret;
            }
            command.clear();
            command.append("ssh " + string(deviceIP) + " chmod +x  ./" + filename );
            if ( 0 != system(command.c_str()))
            {
                errPrintf(" %s \n", command.c_str());
                errInfo = MYO_ERROR;
                goto ret;
            }
            command.clear();
            command.append("ssh " + string(deviceIP) + " ./" + filename + "&");
            if ( 0 != system(command.c_str()))
            {
                errPrintf(" %s \n", command.c_str());
                errInfo = MYO_ERROR;
                goto ret;
            }
#endif  /* USETELNET */
        }  /* end of loop */
        myoiFreeDependencyMem(myoiDLibNameList);
    }   /* end of iFoundFile */
    errInfo = MYO_SUCCESS;
ret:
    return errInfo;
}
#endif /* MYO_HAS_LOAD_SUPPORT */

