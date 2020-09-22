# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
import os.path

Test.Summary = '''
Multi-bucketing (style 2).
'''

RepeatCount = 1000

with open("gold_tests/ramp/multi_ramp_common.py") as f:
    code = compile(f.read(), "multi_ramp_common.py", 'exec')
    exec(code)


CFG_PATH = "multi-ramp-2.cfg.yaml"
tr = Test.TxnBoxTestAndRun("Multi bucketing 2", "multi-ramp.replay.yaml"
                           , remap=[
                                     ['http://base.ex/', 'http://base.ex/', [ CFG_PATH ] ]
                                   , ['https://base.ex/', 'https://base.ex/', [ CFG_PATH ] ]
                                   ]
                           , verifier_client_args="--verbose info --repeat {}".format(RepeatCount)
                           , enable_tls=True
                           )

ramp_test_fixup(tr)
ts = tr.Variables.TXNBOX_TS
ts.Setup.Copy(CFG_PATH, ts.Variables.CONFIGDIR)
