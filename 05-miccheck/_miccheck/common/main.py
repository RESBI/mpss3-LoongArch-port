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
import sys
import traceback
import optparse as op
import platform
import textwrap
from _miccheck.common import testrunner
if platform.system() == "Linux":
    from _miccheck.linux import tests as pltfm
elif platform.system() == "Windows":
    from _miccheck.windows import tests as pltfm
from _miccheck.common import printing as prnt
from _miccheck.common import exceptions as ex
import _miccheck


class MiccheckOptionParser(op.OptionParser):
    def error(self, msg):
        self.print_help(sys.stdout)
        self.exit(2, "\n%s: error: %s\n" % (self.get_prog_name(), msg))


def parse_command_line(argv):
    desc = textwrap.dedent("""\
        Utility which performs software sanity checks on a host machine with
        Intel(R) Xeon Phi(TM) coprocessors installed, by running a suite of
        diagnostic tests. By default, a subset of all available tests are run;
        additional tests can be enabled individually. The default behavior is
        to run all enabled tests applicable to the host system first, and then
        those applicable to the Intel(R) Xeon Phi(TM) coprocessors in turn.""")

    parser = MiccheckOptionParser(formatter=op.TitledHelpFormatter(width=79),
                                  add_help_option=None, description=desc)
    # options
    options_group = op.OptionGroup(parser, 'General')
    options_group.add_option('-h', '--help', action='help',
                             help='Show this help message.')
    #options_group.add_option('-f', '--file', type='string',
    #                         dest="file",
    #                         help='Read command line options from input file.')
    options_group.add_option('-v', '--verbose', dest="verbose",
                             action='store_true', default=False,
                             help='Enables verbosity.')
    options_group.add_option('-d', '--device',
                             dest='device', default='all',
                             help='Select device on which to run. Example: '
                                  '--device=0. Only one device can be selected.')
    # tests
    tests_group = op.OptionGroup(parser, 'Tests available')
    tests_group.add_option('', '--pci_numdev', dest='pci_devices',
                           action='store_true', default=True,
                           help='Check whether Intel(R) Xeon Phi(TM) coprocessors are'
                                ' detected over PCI [enabled=%default].')
    tests_group.add_option('', '--driver_loaded', dest='driver_loaded',
                           action='store_true', default=True,
                           help='Check whether Intel(R) Xeon Phi(TM) driver is loaded '
                                ' in the host [enabled=%default].')
    tests_group.add_option('', '--driver_numdev', dest='driver_devices',
                           action='store_true', default=True,
                           help='Check whether driver detected the same '
                                'number of devices as PCI enumeration did '
                                '[enabled=%default].')

    if platform.system() == "Linux":
        tests_group.add_option('', '--mpssd_loaded', dest='mpssd_loaded',
                               action='store_true', default=True,
                               help='Check whether MPSS daemon is running '
                                    '[enabled=%default].')

    tests_group.add_option('', '--driver_ver', dest='driver_ver',
                           action='store_true', default=False,
                           help='Check whether loaded driver version is correct '
                                '[enabled=%default].')
    tests_group.add_option('', '--dev_state', dest='dev_state',
                           action='store_true', default=True,
                           help='Check whether device state is online and '
                                'its postcode is FF [enabled=%default].')
    tests_group.add_option('', '--dev_rasdaemon', dest='dev_rasdaemon',
                           action='store_true', default=True,
                           help='Check whether RAS daemon is available in device '
                                '[enabled=%default].')
    if platform.system() == "Linux":
        tests_group.add_option('', '--flash_ver', dest='flash_ver',
                               action='store_true', default=True,
                               help='Check whether running flash version of device is correct '
                                    '[enabled=%default].')

    tests_group.add_option('', '--smc_ver', dest='smc_ver',
			   action='store_true', default=True,
			   help='Check whether running SMC firmware version of device is correct '
				'[enabled=%default].')

    if platform.system() == "Linux":
        tests_group.add_option('', '--ping', dest='ping',
                               action='store_true', default=False,
                               help='Check whether network interface of device can '
                                    'be pinged [enabled=%default].')
        tests_group.add_option('', '--ssh', dest='ssh',
                               action='store_true', default=False,
                               help='Check whether network interface of device can '
                                    'be accessed through ssh [enabled=%default].')

    parser.add_option_group(options_group)
    parser.add_option_group(tests_group)

    settings, args = parser.parse_args(argv)

    # validate parsed args
    if args:
        parser.error('options not supported: "%s"' % (args,))

    if settings.verbose:
        prnt.set_debug()

    settings.device = select_devices(settings.device)
    return settings, parser


def select_devices(devices):
    device_list = []
    # depending on platform, get the number of devices detected over pci
    num_devices = pltfm.num_mics_pci()

    if devices == 'all':
        device_list = [i for i in range(num_devices)]
    else:
        # a single device
        try:
            device = int(devices)
        except ValueError as excp:
            raise Exception('invalid device argument: %s' % excp)

        if device > (num_devices - 1) or device < 0:
            raise Exception('device cannot be greater than available '
                            'devices or less than 0')

        device_list = [device, ]

    prnt.p_out_debug('Discovered device(s) = %s' % device_list)
    return device_list


def main():
    try:
        banner = 'MicCheck {0}\nCopyright (c) 2016, Intel Corporation.\n'
        prnt.p_out(banner.format(_miccheck.__version__))
        settings, args = parse_command_line(sys.argv[1:])  # parse command line

        test_runner = testrunner.TestRunner()
        pltfm.default_host_tests(test_runner, settings)
        pltfm.optional_host_tests(test_runner, settings)
        pltfm.default_device_tests(test_runner, settings.device, settings)
        pltfm.optional_device_tests(test_runner, settings.device, settings)

        prnt.p_out('\nStatus: OK')
        return 0
    except Exception as excp:
        prnt.p_out('\nStatus: FAIL')
        traceback.print_exc()
        prnt.p_err('Failure: ' + str(excp))
        return 1
