# @file
#
# Copyright 2022, Apache Software Foundation
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Test certificate handling.
'''

tr = Test.TxnBoxTestAndRun("TLS Certs", "tls-cert.replay.yaml"
                          , config_path='Auto', config_key="meta.txn_box.global"
                          , enable_tls=True
                          , remap=[ ['https://alpha.ex/' , "https://alpha.ex/"] ]
                          )

ts = tr.Variables.TS

ts.Setup.Copy("tls-cert.replay.yaml", ts.Variables.CONFIGDIR) # because it's remap only - not auto-copied.
ts.Setup.Copy("../../ssl/server.key", os.path.join(ts.Variables.CONFIGDIR, "server.key"))
ts.Setup.Copy("../../ssl/server.pem", os.path.join(ts.Variables.CONFIGDIR, "server.pem"))

ts.Disk.records_config.update({
      'proxy.config.diags.debug.enabled': 1
    , 'proxy.config.diags.debug.tags': 'txn_box|http|ssl'
    , 'proxy.config.http.cache.http':  0

    , 'proxy.config.ssl.server.cert.path': ts.Variables.CONFIGDIR
    , 'proxy.config.ssl.server.private_key.path': ts.Variables.CONFIGDIR
    # enable ssl port
    , 'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port)
    , 'proxy.config.ssl.client.certification_level': 0
    , 'proxy.config.ssl.client.verify.server.policy': 'DISABLED'
})
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
