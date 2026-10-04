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
import subprocess
import os
import re
import _miccheck
from _miccheck.common.micdevice import MicDevice
from _miccheck.common import exceptions as ex
from _miccheck.common import printing as prnt
from _miccheck.common import tests as ctests

SYSFS_VERSION = '/sys/class/mic/ctrl/version'

def read_file(path):
    data = None
    with open(path) as attrib:
        data = attrib.read()
    return data.rstrip(' \t\n\r')


def sysfs_device_read_attr(dev_num, attr):
    return read_file('/sys/class/mic/mic%d/%s' % (dev_num, attr))


def execute_program(command):
    command_list = command.split()
    # LoongArch 移植：MPSS 把外部命令写死成 /sbin/xxx（Red Hat 风格）。这里在绝对
    # 路径不存在时退回按名字查 PATH，免得为了一个 lspci 到处建符号链接。
    if command_list and command_list[0].startswith('/') \
            and not os.path.exists(command_list[0]):
        command_list[0] = os.path.basename(command_list[0])
    ld_lib = None

    if os.environ.get('LD_LIBRARY_PATH'):
        ld_lib = os.environ['LD_LIBRARY_PATH']

    # need to clear the LD_LIBRARY_PATH variable before trying to run an
    # external program. this is because pyinstaller sets the LD_LIBRARY_PATH
    # in the bootloader stage to a predefined location, and if by chance that
    # location contains a library that the binary we want to execute needs, it will
    # pick up that library, probably breaking things with the binary program
    # quite badly.
    os.environ['LD_LIBRARY_PATH'] = ''
    popen = subprocess.Popen(command_list, shell=False, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)

    if ld_lib:
        os.environ['LD_LIBRARY_PATH'] = ld_lib

    outputs = popen.communicate()
    # LoongArch 移植：Py3 下 communicate() 返回 bytes，原代码按 str 处理会抛
    # TypeError: a bytes-like object is required, not 'str'
    outputs = [o.decode('utf-8', 'replace') if isinstance(o, bytes) else o
               for o in outputs]

    if popen.returncode != 0:
        raise ex.ExecProgramException('Failed to execute \'%s\': \'%s\'' %
                                   (command, outputs[1].rstrip(' \t\n\r')))
    return outputs[0].rstrip(' \t\n\r')


def num_mics_pci():
    try:
        # "-d 8086:" will only display devices with 8086 as deviceid
        # -n prevents lspci to translate device ids to company names
        # -m enables machine readble output
        output = execute_program('/sbin/lspci -d 8086: -n -m')
    except OSError:
        raise ex.ExecProgramException('/sbin/lspci could not be found in '
                                      'the system')

    # the output will be similar to:
    # 84:00.0 "0b40" "8086" "2250" -r11 "8086" "2500"
    # the device id will be always in the range 2250-225f
    mics = re.findall(r"""
        (.*)                                # anything at the beginning
        (\")(8086)(\"\s)                    # find device id of intel in quotes
        (\")(225[a-f0-9])(\")               # find device id in quotes
        (.*)                                # anything at the end
        """, output, re.X | re.MULTILINE | re.IGNORECASE)
    return len(mics)


def is_micdriver_loaded():
    try:
        output = execute_program('/sbin/lsmod')
    except OSError:
        raise ex.ExecProgramException('/sbin/lsmod could not be found in '
                                      'the system')
    # the output will be similar to:
    # mic                   596637  12
    drivers = re.findall(r"""
        ^mic\s+\d+\s+\d+     # should start with mic, followed by at least one
                             # space, at least 1 digit, at least 1 space and
                             # at least 1 digit.
        """, output, re.X | re.MULTILINE)

    if len(drivers) > 1:
        raise ex.FailedTestException('incorrect number of mic driver detected')

    if len(drivers) == 0:
        return False

    return True


def is_mpssd_running():
    try:
        execute_program('/bin/ps -Cmpssd')
        return True
    except OSError:
        raise ex.ExecProgramException('/bin/ps could not be found in '
                                      'the system')
    except ex.ExecProgramException:
        return False


# test pci device detection
class PciDevicesTest:
    def __init__(self):
        pass

    @staticmethod
    def run():
        if num_mics_pci() < 1:
            raise ex.FailedTestException('no Intel(R) Xeon Phi(TM) coprocessors'
                                         ' devices detected')

    @staticmethod
    def msg_executing():
        return "Check number of devices the OS sees in the system"

# test mic driver number of devices
class ScifDevicesTest:
    def __init__(self):
        pass

    @staticmethod
    def run():
        try:
            num_dev_pci = num_mics_pci()
            num_dev_scif = MicDevice.mic_get_ndevices()

            if num_dev_scif != num_dev_pci:
                raise ex.FailedTestException('SCIF nodes do not match number'
                                          ' of PCI detected devices')
        except ValueError as excp:
            raise ex.FailedTestException('incorrect value of scif nodes: %s' %
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
        if not is_micdriver_loaded():
            raise ex.FailedTestException('mic driver not loaded')

    @staticmethod
    def msg_executing():
        return "Check mic driver is loaded"


# test mpssd daemon running
class MpssRunTest:
    @staticmethod
    def run():
        if not is_mpssd_running():
            raise ex.FailedTestException('mpssd daemon not running')

    @staticmethod
    def msg_executing():
        return "Check mpssd daemon is running"


# tests the driver version is correct
class DriverVersionTest:
    @staticmethod
    def run():
        miccheck_version = _miccheck.__version__
        driver_version = read_file(SYSFS_VERSION)

        driver_is_eng = True
        miccheck_is_eng = True

        if miccheck_version.find('+git') == -1:
            miccheck_is_eng = False # miccheck is release (or development)

        if driver_version.find('+git') == -1:
            driver_is_eng = False # driver is release

        if driver_is_eng != miccheck_is_eng:
            raise ex.FailedTestException('miccheck and driver build types do'
                                         ' not match')

        if driver_is_eng:
            # both driver and miccheck are eng
            miccheck_eng_ver = miccheck_version[0:miccheck_version.find('+git')]
            driver_eng_ver = driver_version[0:driver_version.find('+git')]

            if miccheck_eng_ver != driver_eng_ver:
                raise ex.FailedTestException('miccheck and driver eng versions'
                                             ' do not match')

        else:
            # both miccheck and driver are release
            idx = miccheck_version.find('-')

            if idx == -1:
                # in case - is not part of miccheck version, take the whole str
                idx = len(miccheck_version)

            miccheck_version = miccheck_version[0:idx]

            if miccheck_version not in driver_version:
                raise ex.FailedTestException('miccheck and driver release '
                                             'version do not match')

    @staticmethod
    def msg_executing():
        return "Check loaded driver version is correct"


# test device in online mode and postcode FF
class StateTest:
    def __init__(self, dev_num):
        self._dev_num = dev_num

    def run(self): # not static, because it is a device test
        try:
            state = sysfs_device_read_attr(self._dev_num, 'state')
            postcode = sysfs_device_read_attr(self._dev_num, 'post_code')
        except Exception as excp:
            raise ex.FailedTestException(str(excp))

        if state != 'online':
            raise ex.FailedTestException('device is not online: ' + state)

        if postcode != 'FF':
            raise ex.FailedTestException('device postcode is not FF: ' +
                                         postcode)
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


# check the flash version of the device
class FlashVersionTest:
    def __init__(self, dev_num):
        self._dev_num = dev_num

    def run(self): # not static because it is a device test
        device = MicDevice(self._dev_num)

        built_flashver = _miccheck.__flash_version__

        # yocto reports the version with hotfix, but sysfs does not have hotfix
        # so we need to remove it from yocto flash version
        if built_flashver.find('-') != -1:
            built_flashver = built_flashver[0:built_flashver.find('-')]

        curr_flashver = sysfs_device_read_attr(self._dev_num, 'flashversion')

        if built_flashver != curr_flashver:
            raise ex.FailedTestException('device flash version does not match,'
                                          ' should be \'{0}\', it is \'{1}\'.'.
                                          format(built_flashver, curr_flashver))
        else:
            prnt.p_out_debug('    device flash version: \'{0}\''.
                             format(curr_flashver))

    @staticmethod
    def msg_executing():
        return "Check running flash version is correct"


# test device can be pinged
class PingTest:
    def __init__(self, dev_num):
        self._dev_num = dev_num
        self._dev_name = "mic%d" % self._dev_num

    def run(self): # not static because it is a device test
        try:
            timeout = 3
            output = execute_program('/bin/ping -c1 -w%d %s' % (timeout, self._dev_name))
        except OSError:
            raise ex.ExecProgramException('/bin/ping could not be found in '
                                          'the system')
        except ex.ExecProgramException as excp:
            raise ex.FailedTestException('interface %s did not respond to '
                                         'ping request' % self._dev_name)

    @staticmethod
    def msg_executing():
        return "Check device can be pinged over its network interface"


# test device can be accessed through ssh
class SshTest:
    def __init__(self, dev_num):
        self._dev_num = dev_num
        self._dev_name = "mic%d" % self._dev_num

    def run(self): # not static because it is a device test
        try:
            timeout = 3
            exec_line = ('/usr/bin/ssh -oConnectTimeout=%d -oBatchMode=yes '
                        '-oStrictHostKeyChecking=no %s echo hello' %
                        (timeout, self._dev_name))
            output = execute_program(exec_line)
        except OSError:
            raise ex.ExecProgramException('/usr/bin/ssh could not be found in '
                                          'the system')
        except ex.ExecProgramException as excp:
            raise ex.FailedTestException('interface %s could not be accessed '
                                         'through ssh' % self._dev_name)

    @staticmethod
    def msg_executing():
        return "Check device can be accessed through ssh"


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
        runner.run(ScifDevicesTest())

    if settings.mpssd_loaded:
        # make sure mpss daemon is running
        runner.run(MpssRunTest())


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

            if settings.flash_ver:
                # make sure flash version is correct
                runner.run(FlashVersionTest(device), device)

            if settings.smc_ver:
                # make sure smc fw version is correct
                runner.run(ctests.SmcFirmwareTest(device), device)

        # MicDevice objects can throw RuntimeError and cause premature termination
        except (ex.FailedTestException, RuntimeError):
            # so we continue testing other devices if present
            device_failed = True

    if device_failed:
        raise ex.FailedTestException('A device test failed')


def optional_device_tests(runner, devices, settings):
    device_failed = False

    for device in devices:
        try:
            if settings.ping or settings.ssh:
                prnt.p_out('Executing optional tests for device: %d' % device)

            if settings.ping:
                runner.run(PingTest(device), device)

            if settings.ssh:
                runner.run(SshTest(device), device)

        # MicDevice objects can throw RuntimeError and cause premature termination
        except (ex.FailedTestException, RuntimeError):
            # so we continue testing other devices if present
            device_failed = True

    if device_failed:
        raise ex.FailedTestException('An optional device test failed')
