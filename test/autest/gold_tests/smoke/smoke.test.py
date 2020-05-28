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
r = Test.TxnBoxTestRun("Smoke test", "smoke.replay.yaml", config_key="meta.txn_box",
                       remap=[('http://example.one', 'http://example.one')]
                       )
#ts = r.Variables.TS
#server = r.Variables.SERVER
