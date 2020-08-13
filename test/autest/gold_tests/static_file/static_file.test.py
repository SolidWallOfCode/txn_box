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

r = Test.TxnBoxTestAndRun("Test static file support", "static_file.replay.yaml"
                          , config_path='Auto', config_key="meta.txn_box",
                       remap=[['http://example.one', 'http://example.one',
                              [ '--key=meta.txn-box-remap', 'static_file.replay.yaml' ]]])
ts = r.Variables.TS
ts.Setup.Copy("static_file.txt", ts.Variables.CONFIGDIR)
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1
    , 'proxy.config.diags.debug.tags' : 'txn_box'
})
