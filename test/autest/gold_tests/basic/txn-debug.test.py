# @file
#
# Copyright 2021, Yahoo!
# SPDX-License-Identifier: Apache-2.0
#
Test.Summary = '''
txn-debug directive
'''
replay_file = "txn-debug.replay.yaml"

tr = Test.TxnBoxTestAndRun("Test txn-debug", replay_file, config_path='Auto',
                           verifier_client_args="--verbose diag --keys debug-expected",
                           verifier_server_args="--verbose diag",
                           config_key="meta.txn_box.global",
                           suffix="debug-enabled")
ts = tr.Variables.TS
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'txn_box'
})

ts.Streams.stderr += Testers.ContainsExpression(
        r"DEBUG: <HttpSM.cc",
        "Verify that there was transaction level debugging.")

tr = Test.TxnBoxTestAndRun("Test txn-debug", replay_file, config_path='Auto',
                           verifier_client_args="--verbose diag --keys debug-not-expected",
                           config_key="meta.txn_box.global",
                           suffix="debug-disabled")
ts = tr.Variables.TS
ts.Disk.records_config.update({
    'proxy.config.diags.debug.enabled': 1,
    'proxy.config.diags.debug.tags': 'txn_box'
})

ts.Streams.stderr += Testers.ExcludesExpression(
        r"DEBUG: <HttpSM.cc",
        "Verify that there was not transaction level debugging.")
