# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Verification for YTS-3489: Redirect only one path for an upstream.
'''

replay_file="yts-3489.replay.yaml"

tr = Test.TxnBoxTestAndRun("Redirect", replay_file
                , remap=[ ['http://base.ex/',  ( '--key=meta.txn_box.remap', replay_file) ]
                        ]
                , verifier_client_args="--verbose info"
                )

ts = tr.Variables.TS

ts.Setup.Copy(replay_file, ts.Variables.CONFIGDIR) # because it's remap only - not auto-copied.

ts.Disk.records_config.update({
      'proxy.config.diags.debug.enabled': 1
    , 'proxy.config.diags.debug.tags': 'txn_box'
    , 'proxy.config.http.cache.http':  0
})
