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
    "client1", ts, "replay_files/ct_header",
    http_ports=[ts.Variables.port])
server = r.AddVerifierServerProcess("server1", "replay_files/ct_header")
r.ConfigureTsForTxnBox(ts, server, "replay_files/ct_header/ct_header.yaml")
