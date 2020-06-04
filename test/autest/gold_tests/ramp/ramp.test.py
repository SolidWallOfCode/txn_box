# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
'''
Basic smoke tests.
'''
Test.Summary = '''
Test basic functions and directives.
'''

Test.TxnBoxTestAndRun("Ramping", "ramp.replay.yaml",
                ,remap=[('http://example.one', 'http://example.one', ( '--key=meta.txn_box.remap-pristine', 'ramp.replay.yaml'))]
                )
