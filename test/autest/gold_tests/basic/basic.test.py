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

Test.TxnBoxTestAndRun("Test basics", "basic.replay.yaml", config_path='Auto', config_key="meta.txn_box.global"
                ,remap=[('http://remap.ex', 'http://remap.ex', ('--key=meta.txn_box.remap-1', 'basic.replay.yaml'))
                       ]
                )
