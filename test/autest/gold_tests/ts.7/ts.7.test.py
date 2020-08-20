# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
'''
ATS 7 tests (for Verizon Media internal use).
'''
Test.Summary = '''
Test basic functions and directives in ATS 7.
'''

Test.SkipUnless(Condition.Condition(
    lambda : Test.Variables.trafficserver_version.Major() == 7,
    "This test requires ATS version 7"))
'''
# Disabled until I can get a check for ATS version running.
Test.TxnBoxTestAndRun("ATS 7", "ts.7.replay.yaml"
                ,regex_map=[('http://[el]x.one/', 'http://ex.two/', ('--key=meta.txn_box.remap-first', 'ts.7.replay.yaml'))
                       ]
                )
'''
