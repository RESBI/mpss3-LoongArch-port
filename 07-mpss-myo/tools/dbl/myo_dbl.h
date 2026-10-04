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
/*myo_dbl.h*/

#ifndef MYO_DBL_H
#define MYO_DBL_H


/*! MYO Dbg lib typedefs */

typedef int           _MYOD_error_code;
typedef int           _MYOD_node_handle;
typedef void         *_MYOD_VA; /*this is for address in different process*/
typedef size_t        _MYOD_size_t;
typedef unsigned char _MYOD_byte;
typedef char         *_MYOD_string;
typedef _MYOD_VA      _MYOD_TargetAddress;
typedef int           _MYOD_Version;

/*! MYO Dbg lib errors */

/**@cond NEVER*/
#define MYOD_ERR_MY_NOT_OWN   1 
#define MYOD_ERR_OUR_NOT_OWN  2 
#define MYOD_ERR_NOT_OWN      3 
#define MYOD_WARN_READONLY    5
/**@endcond */
#define MYOD_ERR_NONSHARED    4 /*!< The entity does not reside in shared memory */
#define MYOD_ERROR           -1
#define MYOD_SUCCESS          0 /*!< No error occurred */

/*! MYO Dbg access type */
typedef enum DbgAccessEnumTagType {
  MYO_READONLY = 1, /*!< Read only */
    MYO_READWRITE   /*!< Read write */
}DbgAccessType;

#ifdef __cplusplus
extern "C" {
#endif

/**@cond NEVER*/
#       define MYOACCESSAPI /* nothing */
/**@endcond*/

/*! MYO Dbg lib callback type defs */
typedef	_MYOD_error_code (*_MYOD_GetCurrentDebuggerCB)(
        _MYOD_node_handle      *owner
	);

typedef _MYOD_error_code (*_MYOD_MemReadCB)(
        _MYOD_VA                dbgCtx,
        _MYOD_node_handle       owner,
        _MYOD_VA                address,
        _MYOD_size_t            bytes,
        _MYOD_byte*             buffer
        );

typedef	_MYOD_error_code (*_MYOD_MemWriteCB)(
        _MYOD_VA                dbgCtx,
        _MYOD_node_handle       owner,
	_MYOD_VA		address,
	_MYOD_size_t	        bytes,
	_MYOD_byte*		buffer
	);

typedef	_MYOD_error_code (*_MYOD_GetSymbolAddressCB)(
       _MYOD_VA                 dbgCtx,
       _MYOD_node_handle	owner,
       const _MYOD_string	name,
       _MYOD_VA 	       *address
	);

/** @fn extern const char** getSymbolList(void)
 * @brief Returns the complete list of symbols that the client must provide the MYO Dbg library to operate.
 * @return
 *      NULL terminated list of symbols.  This function never fails.
 **/
MYOACCESSAPI const char** getSymbolList(void);


/** @fn extern _MYOD_error_code initRuntime(
      _MYOD_VA                    dbgCtx,
      _MYOD_Version              *version,
      _MYOD_MemReadCB             readCB,
      _MYOD_MemWriteCB            writeCB,
      _MYOD_GetSymbolAddressCB    symbolCB,
      _MYOD_string                signalSEGVtlsSymbolName,
      _MYOD_TargetAddress        *sharedAddressBase,
      _MYOD_TargetAddress        *sharedAddressEnd)
 * @brief Client initializes the MYO Dbg library by providing the three callbacks. The MYO Dbg library in turn attempts to initialize. Note that 
 *        to initialize, the MYO Dbg library needs to get the symbol address of all symbols during operation at the time of initialization.
 * @return
 *      MYOD_SUCCESS on success.
 *      Some other value on failure.
 **/
MYOACCESSAPI _MYOD_error_code initRuntime(
      _MYOD_VA                    dbgCtx,
      _MYOD_Version              *version,
      _MYOD_MemReadCB             readCB,
      _MYOD_MemWriteCB            writeCB,
      _MYOD_GetSymbolAddressCB    symbolCB,
      _MYOD_string                signalSEGVtlsSymbolName, /*out parameter, tell debugger MYO tls var name*/
      _MYOD_TargetAddress        *sharedAddressBase, /*out parameter, tell debugger MYO shared addr range*/
      _MYOD_TargetAddress        *sharedAddressEnd /*out parameter, tell debugger MYO shared addr range*/

);
/** @fn extern _MYOD_error_code bindOwner(_MYOD_node_handle owner)
 * @brief Client binds the ownership to the indicated owner.
 * @return
 *      MYOD_SUCCESS on success.
 *      Some other value on failure.
 **/
MYOACCESSAPI _MYOD_error_code bindOwner(_MYOD_node_handle owner);
/** @fn extern _MYOD_error_code unBindOwner(_MYOD_node_handle owner)
 * @brief Client unbinds the ownership to the indicated owner.
 * @return
 *      MYOD_SUCCESS on success.
 *      Some other value on failure.
 **/
MYOACCESSAPI _MYOD_error_code unBindOwner(_MYOD_node_handle owner);

/** @fn extern _MYOD_error_code checkAddress(
    _MYOD_VA              dbgCtx,
    _MYOD_VA              address,
    _MYOD_size_t          size,
    _MYOD_size_t         *ownedBytes,
    _MYOD_node_handle    *owner,
    DbgAccessType         mode)
 * @brief Client queries for the specific address to determine if it resides in shared memory.
 * @return
 *      MYOD_SUCCESS on success.
 *      Some other value on failure.
 **/
MYOACCESSAPI _MYOD_error_code checkAddress(
    _MYOD_VA              dbgCtx,
    _MYOD_VA              address,
    _MYOD_size_t          size,
    _MYOD_size_t         *ownedBytes,
    _MYOD_node_handle    *owner,
    DbgAccessType         mode
);

#ifdef __cplusplus
}
#endif

#endif /*MYO_DBL_H*/



