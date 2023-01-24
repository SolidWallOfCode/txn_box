# @file
#
# Copyright 2022, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os

"""SNI-based configuration test."""

Test.Summary = "Test SNI-based configuration."

tr = Test.TxnBoxTestAndRun(
      "Test SNI based configuration"
    , "sni-based-configuration.replay.yaml"
    , config_path='Auto'
    , config_key="meta.txn_box.global"
    , verifier_client_args="--verbose diag"
    , verifier_server_args="--verbose diag"
    , enable_tls=True
)

ts = tr.Variables.TS
ts.Disk.records_config.update({
      'proxy.config.diags.debug.enabled': 1
    , 'proxy.config.diags.debug.tags': 'txn_box|ssl|http'

    # TLS configuration.
    , 'proxy.config.ssl.server.cert.path': f'{ts.Variables.CONFIGDIR}'
    , 'proxy.config.ssl.server.private_key.path': f'{ts.Variables.CONFIGDIR}'
    , 'proxy.config.http.server_ports': f'{ts.Variables.port} {ts.Variables.ssl_port}:ssl'
    , 'proxy.config.ssl.client.verify.server.policy': 'DISABLED'
})
ts.Setup.Copy("../../ssl/server.key", os.path.join(ts.Variables.CONFIGDIR, "server.key"))
ts.Setup.Copy("../../ssl/server.pem", os.path.join(ts.Variables.CONFIGDIR, "server.pem"))
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
