# @file
#
# Copyright 2021, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Production use case: Manipulate specific query parameters.
'''

replay_file="query-delete-regex.replay.yaml"

tr = Test.TxnBoxTestAndRun("Query Delete with RXP", replay_file
                          , config_path='Auto', config_key="meta.txn-box"
                          , verifier_client_args="--verbose info"
                          )

ts = tr.Variables.TS

ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR) # because it's remap only - not auto-copied.

ts.Disk.records_config.update({
      'proxy.config.diags.debug.enabled': 1
    , 'proxy.config.diags.debug.tags': 'txn_box'
    , 'proxy.config.http.cache.http':  0
})
