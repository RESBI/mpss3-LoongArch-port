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
import win32com.client
import pywintypes
import re
import _miccheck
from _miccheck.common import exceptions as ex
from _miccheck.common.micdevice import MicDevice
from _miccheck.common import printing as prnt
from distutils.version import LooseVersion
from _miccheck.common import tests as ctests

def num_mics_pci():
    matches = 0
    wmi = win32com.client.GetObject(r"winmgmts:\root\cimv2").InstancesOf("win32_pnpentity")

    for entity in wmi:
        match = re.match(r'PCI\\VEN_8086&DEV_225.*', entity.DeviceID)
        if match:
            matches = matches + 1

    return matches


def get_wmi_mic_instances():
    return win32com.client.GetObject(r"winmgmts:\root\wmi").InstancesOf("MIC")


def num_mics_wmi():
    try:
        wmi = get_wmi_mic_instances()
        return len(wmi)
    except pywintypes.com_error:
        return 0


def wmi_get_driver_version():
    wmi = get_wmi_mic_instances()
    return wmi[0].driver_version


# test pci device detection
class PciDevicesTest:
    def __init__(self):
        pass

    @staticmethod
    def run():
        devices = num_mics_pci()

        if devices < 1:
            raise ex.FailedTestException('no MIC devices detected')

    @staticmethod
    def msg_executing():
        return "Check number of devices the OS sees in the system"

# compare pci num devices with wmi num devices
class WmiDevicesTest:
    def __init__(self):
        pass

    @staticmethod
    def run():
        try:
            num_dev_pci = num_mics_pci()
            num_dev_wmi = num_mics_wmi()

            if num_dev_wmi != num_dev_pci:
                raise ex.FailedTestException('WMI num devices does not match '
                                             'PCI num devices')
        except ValueError as excp:
            raise ex.FailedTestException('incorrect value of wmi devices: %s' %
                                         str(excp))

    @staticmethod
    def msg_executing():
        return "Check number of devices driver sees in the system"


# test mic driver detection
class MicDriverTest:
    def __init__(self):
        pass

    @staticmethod
    def run():
        num_devices = num_mics_wmi()

        if num_devices < 1:
            raise ex.FailedTestException('mic driver not loaded')

    @staticmethod
    def msg_executing():
        return "Check mic driver is loaded"

def ltrim_version(string, num_dots):
    return ".".join(string.split(".")[:num_dots])

# tests the driver version is correct
# if a is the current driver version and b is the miccheck version,
# then "a == b" is an acceptable scenario; "b < a" and "a < b"
# will be acceptable only if the first two sub-versions are equal;
# e.g. a=3.2.40, b=3.2.50 is acceptable, a=3.1.40, b=3.2.50
# is not.
class DriverVersionTest:
    @staticmethod
    def run():
        build_version = _miccheck.__version__ # b
        wmi_version = wmi_get_driver_version() # a
        err_msg = 'loaded driver version incorrect: \'{0}\', ' \
                  'reference is \'{1}\'.'.format(wmi_version, build_version)

        # if either is empty, LooseVersion() will fail
        if not build_version or not wmi_version:
            raise ex.FailedTestException(err_msg)

        if LooseVersion(build_version) != LooseVersion(wmi_version):
            trim_build = ltrim_version(build_version, 2)
            trim_wmi = ltrim_version(wmi_version, 2)

	    if LooseVersion(trim_build) != LooseVersion(trim_wmi):
                raise ex.FailedTestException(err_msg)

        prnt.p_out_debug('    loaded driver version \'{0}\','
                         ' reference is \'{1}\'.'
                         .format(wmi_version, build_version))

    @staticmethod
    def msg_executing():
        return "Check loaded driver version is correct"


# test device in online mode and postcode FF
class StateTest:
    def __init__(self, dev_num):
        self._dev_num = dev_num

    def run(self): # not static, because it is a device test
        mics = get_wmi_mic_instances()
        mic = mics[self._dev_num]

        if mic.state != 4:
            raise ex.FailedTestException('device is not online, current '
                                         'state is %d' % mic.state)

        if mic.post_code != 'FF':
            raise ex.FailedTestException('device postcode is not FF: ' +
                                         mic.post_code)

    @staticmethod
    def msg_executing():
        return "Check device is in online state and its postcode is FF"


# test device has RAS daemon available
class RasTest:
    def __init__(self, dev_num):
        self._dev_num = dev_num

    def run(self): # not static because it is a device test
        device = MicDevice(self._dev_num)

        if not device.mic_is_ras_avail():
            raise ex.FailedTestException('ras daemon is not available')

    @staticmethod
    def msg_executing():
        return "Check ras daemon is available in device"


def default_host_tests(runner, settings):
    prnt.p_out('Executing default tests for host')

    if settings.pci_devices:
        # make sure we have devices attached to the host
        runner.run(PciDevicesTest())

    if settings.driver_loaded:
        # make sure mic driver is loaded
        runner.run(MicDriverTest())

    if settings.driver_devices:
        # make sure driver detected the same num of devices as the
        # pci buses did
        runner.run(WmiDevicesTest())


def optional_host_tests(runner, settings):
    if settings.driver_ver:
        prnt.p_out('Executing optional tests for host')

    if settings.driver_ver:
        # check the loaded driver version
        runner.run(DriverVersionTest())


def default_device_tests(runner, devices, settings):
    device_failed = False

    for device in devices:
        try:
            prnt.p_out('Executing default tests for device: %d' % device)

            if settings.dev_state:
                # make sure the device is online with postcode FF
                runner.run(StateTest(device), device)

            if settings.dev_rasdaemon:
                # make sure ras daemon is running on device
                runner.run(RasTest(device), device)

            if settings.smc_ver:
                # make sure smc fw version is correct
                runner.run(ctests.SmcFirmwareTest(device), device)

        except (ex.FailedTestException, RuntimeError):
            # so we continue testing other devices if present
            device_failed = True

    if device_failed:
        raise ex.FailedTestException('A device test failed')


def optional_device_tests(runner, devices, settings):
    pass

