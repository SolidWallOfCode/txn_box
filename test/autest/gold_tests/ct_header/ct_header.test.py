# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Verify txn_box can filter fields as expected.
'''

r = Test.TxnBoxTestAndRun("Test HTTP field manipulation", "ct_header.replay.yaml"
                          , config_path='Auto'
                          , config_key="meta.txn_box"
                          , remap=[ [ "http://example.one/" ]
                                  , [ "http://s.protected.com" ]
                                  ]
                          , enable_tls=True
                          )
ts = r.Variables.TS

ts.Setup.Copy("../../ssl/server.key", os.path.join(ts.Variables.CONFIGDIR, "server.key"))
ts.Setup.Copy("../../ssl/server.pem", os.path.join(ts.Variables.CONFIGDIR, "server.pem"))

ts.Disk.records_config.update({
      'proxy.config.diags.debug.enabled': 1
    , 'proxy.config.diags.debug.tags': 'txn_box|http|ssl'
    , 'proxy.config.http.cache.http':  0

    , 'proxy.config.ssl.server.cert.path': '{0}'.format(ts.Variables.CONFIGDIR)
    , 'proxy.config.ssl.server.private_key.path': '{0}'.format(ts.Variables.CONFIGDIR)
    # enable ssl port
    , 'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port)
    , 'proxy.config.ssl.client.verify.server': 0
})
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
