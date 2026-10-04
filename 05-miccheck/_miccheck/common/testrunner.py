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
from _miccheck.common import exceptions as ex
from _miccheck.common import printing as prnt 

class TestRunner:
    def __init__(self):
        self._num_tests_run = 0

    def run(self, test, device=-1):
        try:
            # if device == -1, it is a host test, so we don't print mic id
            if device != -1:
                test_output = ('  Test %d (mic%d): %s' % (self._num_tests_run,
                                                  device, test.msg_executing()))
            else:
                test_output = ('  Test %d: %s' % (self._num_tests_run,
                                              test.msg_executing()))

            test.run()
            test_output += ' ... pass'
        except ex.FailedTestException as excp:
            test_output += ' ... fail\n    %s' % str(excp)
            raise
        except Exception as excp:
            test_output += ' ... fail\n    %s' % str(excp)
            raise
        finally:
            self._num_tests_run += 1
            prnt.p_out(test_output)
