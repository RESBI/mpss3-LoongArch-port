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
import ctypes
import platform

E_MIC_SUCCESS = 0
MAX_STRLEN = 512

if platform.system() == "Linux":
    MICMGMT_LIBRARY = "libmicmgmt.so.0"
else:
    MICMGMT_LIBRARY = "micmgmt.dll"

class MicDevice:
    def __init__(self, dev_num):
        self.mic = ctypes.cdll.LoadLibrary(MICMGMT_LIBRARY)
        self.dev_num = ctypes.c_int(dev_num)
        self.mdh = ctypes.POINTER(ctypes.c_void_p)()
        if self.mic.mic_open_device(ctypes.byref(self.mdh), self.dev_num) != E_MIC_SUCCESS:
            raise LookupError("device %d could not be initialized" % 
	                      self.dev_num.value)

    def __del__(self):
        if not hasattr(self, "mic"):
            return

        self.mic.mic_close_device(self.mdh)

    def mic_is_ras_avail(self):
        enabled = ctypes.c_int(0)
        self.mic.mic_is_ras_avail(self.mdh, ctypes.byref(enabled))
        return bool(enabled.value)

    def mic_get_smc_fwversion(self):
        size = ctypes.c_long(MAX_STRLEN)
        fwversion = ctypes.create_string_buffer(MAX_STRLEN)
        thermal_struct = ctypes.POINTER(ctypes.c_void_p)()

        if self.mic.mic_get_thermal_info(self.mdh,
                                         ctypes.byref(thermal_struct)) \
                                            != E_MIC_SUCCESS:
            raise RuntimeError("failed to get thermal information")

        if self.mic.mic_get_smc_fwversion(thermal_struct,
                                          ctypes.byref(fwversion),
                                          ctypes.byref(size)) != E_MIC_SUCCESS:
            self.mic.mic_free_thermal_info(thermal_struct)
            raise RuntimeError("failed to get smc firmware version")

        self.mic.mic_free_thermal_info(thermal_struct)
        # LoongArch 移植：Py3 下 create_string_buffer().value 是 bytes，
        # 不解码会让上层拿到 "b'...'" 这样的字符串
        value = fwversion.value
        return value.decode('utf-8', 'replace') if isinstance(value, bytes) else value
    
    @staticmethod
    def mic_get_ndevices():
        """Returns the number of active cards"""
        mic = ctypes.cdll.LoadLibrary(MICMGMT_LIBRARY)
        count = ctypes.c_int()
        device_list = ctypes.POINTER(ctypes.c_void_p)()

        if mic.mic_get_devices(ctypes.byref(device_list)) != E_MIC_SUCCESS:
            raise LookupError("could not allocate list of devices available")

        if mic.mic_get_ndevices(device_list, ctypes.byref(count)) != E_MIC_SUCCESS:
            raise LookupError("could not get number of available devices")

        if mic.mic_free_devices(device_list) != E_MIC_SUCCESS:
            raise LookupError("could not de-allocate list of devices available")

        return count.value
