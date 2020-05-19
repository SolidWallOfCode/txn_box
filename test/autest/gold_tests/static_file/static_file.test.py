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
r = Test.TxnBoxTestRun("Test static file support", "static_file.replay.yaml", config_key="meta.txn_box")
ts = r.Variables.TS
ts.Setup.Copy("static_file.txt", ts.Variables.CONFIGDIR)
ts.Disk.remap_config.AddLine(
    'map http://example.one http://example.one @plugin=txn_box.so @pparam=static_file.replay.yaml @pparam=meta.txn-box-remap'
)
