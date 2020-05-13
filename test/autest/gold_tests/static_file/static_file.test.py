'''
Static file serving and handling.
'''
# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os

Test.Summary = '''
Server static file as response body.
'''

ts = Test.MakeATSProcess("ts")

#
# Test 1: Verify that a field can be changed in both upstream and downstream
#
r = Test.AddTestRun("Serve static file content.")
client = r.AddVerifierClientProcess(
    "client1", ts, "static_file.replay.yaml",
    http_ports=[ts.Variables.port])
server = r.AddVerifierServerProcess("server1", "static_file.replay.yaml")
r.ConfigureTsForTxnBox(ts, server, "static_file.replay.yaml")
r.Setup.Copy("static_file.txt", Test.RunDirectory)
ts.Disk.remap_config.AddLine(
    'map http://example.one http://exmaple.one @plugin=txn_box.so @pparam=static_file.replay.yaml @pparam=meta.txn-box-remap'
)
ts.Disk.records_config.update({
    'proxy.config.plugin.dynamic_reload_mode': 0,
})
