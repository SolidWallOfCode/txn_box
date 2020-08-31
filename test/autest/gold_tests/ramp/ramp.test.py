# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#
import os.path

Test.Summary = '''
Test traffic ramping.
'''

class State:
    RepeatCount = 1000
    # 30% split expected, +- 5%.
    UpperLimit = int((7 * RepeatCount)/20)
    LowerLimit = int((5 * RepeatCount)/20)
    ResultCount = 0
    Target = "ex.two"
    Description = "Checking count."

    def validate(self):
        return ( True, self.Description, "OK") \
            if self.LowerLimit <= self.ResultCount and self.ResultCount <= self.UpperLimit \
            else ( False, self.Description, "{} is outside {}..{}".format(self.ResultCount, self.LowerLimit, self.UpperLimit))

    def log_check(self, log_path):
        try:
            with open(log_path) as log:
                lines = log.readlines()
                if len(lines) >= self.RepeatCount :
                    for l in lines :
                        if self.Target in l:
                            self.ResultCount += 1
                    return True;
        except:
            pass
        return False

state = State()

tr = Test.TxnBoxTestAndRun("Ramping", "ramp.replay.yaml"
                , remap=[
                        ('http://ex.one', 'http://ex.three',( '--key=meta.txn_box.remap', 'ramp.replay.yaml') )
                       ]
                , verifier_client_args="--verbose diag --repeat {}".format(state.RepeatCount)
                )

ts = tr.Variables.TS
ts.Setup.Copy("ramp.replay.yaml", ts.Variables.CONFIGDIR)
ts.Setup.Copy("ramp.logging.yaml", os.path.join(ts.Variables.CONFIGDIR, "logging.yaml"))
ts.Disk.records_config.update({
    'proxy.config.log.max_secs_per_buffer': 1
})
pv_client = tr.Variables.CLIENT

# Final process to force ready checks for previous ones.
trailer = tr.Processes.Process("trailer")
trailer.Command = "sh -c :"

# Log watcher to wait until the log is finalized.
watcher = tr.Processes.Process("log-watch")
watcher.Command = "sleep 100"
# This doesn't work either
#watcher.Ready = lambda : LogCheck(os.path.join(ts.Variables.LOGDIR, "ramp.log" ))
# ready flag doesn't work here.
#watcher.StartBefore(pv_client, ready=lambda : LogCheck(os.path.join(ts.Variables.LOGDIR, "ramp.log" )))
# Only this works
pv_client.StartAfter(watcher, ready=lambda : state.log_check(os.path.join(ts.Variables.LOGDIR, "ramp.log" )))
# ready flag doesn't work here.
watcher.StartAfter(trailer)
watcher.Streams.All.Content = Testers.Lambda(lambda info,tester : state.validate() )
