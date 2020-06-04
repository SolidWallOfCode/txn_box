'''
Verify txn_box can filter fields as expected.
'''
# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

Test.Summary = '''
Verify txn_box can filter fields as expected.
'''
r = Test.TxnBoxTestAndRun("Test HTTP field manipulation", "ct_header.replay.yaml"
                          , config_path='Auto'
                          , config_key="meta.txn_box"
                          , remap=[ [ "http://example.one/" ]
                                 , [ "http://s.protected.com" ]
                                 ])
