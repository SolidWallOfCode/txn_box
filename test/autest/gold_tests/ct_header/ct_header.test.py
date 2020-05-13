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

ts = Test.MakeATSProcess("ts")

#
# Test 1: Verify that a field can be changed in both upstream and downstream
#
r = Test.AddTestRun("Verify txn_box can filter fields as expected.")
client = r.AddVerifierClientProcess(
    "client1", ts, "ct_header.replay.yaml",
    http_ports=[ts.Variables.port])
server = r.AddVerifierServerProcess("server1", "ct_header.replay.yaml")
r.ConfigureTsForTxnBox(ts, server, "ct_header.replay.yaml")
