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
                       remap=[('http://example.one', 'http://example.one',
                             [ '--key=meta.txn-box-remap', 'static_file.replay.yaml' ])])
ts = r.Variables.TS
server = r.Variables.SERVER
ts.Setup.Copy("static_file.txt", ts.Variables.CONFIGDIR)
