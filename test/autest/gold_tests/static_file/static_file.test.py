'''
Static file serving and handling.
'''
# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

Test.Summary = '''
Server static file as response body.
'''
r = Test.TxnBoxTestRun("Test static file support", "static_file.replay.yaml", config_key="meta.txn_box",
                       remap_configs=[('http://example.one', 'http://example.one',
                                      ['static_file.replay.yaml', 'meta.txn-box-remap'])])
ts = r.Variables.TS
server = r.Variables.SERVER
ts.Setup.Copy("static_file.txt", ts.Variables.CONFIGDIR)
