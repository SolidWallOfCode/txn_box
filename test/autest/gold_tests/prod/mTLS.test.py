# @file
#
# Copyright 2020, Verizon Media
# SPDX-License-Identifier: Apache-2.0
#

import os.path

Test.Summary = '''
Athenz style mTLS testing.
'''

ts = Test.TxnBoxTest("mTLS.replay.yaml"
                    , config_path="mTLS.txnbox.yaml", config_key="txn_box"
                    , remap=[ ['https://base.ex/' , "http://base.ex/"] ]
                    , enable_tls=True
                    , verifier_server_args='--format "{url}"'
                    )


tr_alpha = Test.TxnBoxRun("Athenz mTLS alpha"
                         , replay_path="mTLS-alpha.replay.yaml"
                         , ssl_cert="../../ssl/alpha-signed.cert"
                         , suffix="alpha"
                         , verifier_client_args="--verbose info"
                         )


# These are intended to connect but not be "authorized" because the issuer isn't alpha.
tr_bravo = Test.TxnBoxRun("Athenz mTLS bravo"
                         , replay_path="mTLS-bravo.replay.yaml"
                         , ssl_cert="../../ssl/bravo-signed.cert"
                         , suffix="bravo"
                         , verifier_client_args="--verbose info"
                         )

ts.Setup.Copy("../../ssl/server.key", os.path.join(ts.Variables.CONFIGDIR, "server.key"))
ts.Setup.Copy("../../ssl/server.pem", os.path.join(ts.Variables.CONFIGDIR, "server.pem"))
# The bundle contains roots for alpha, bravo, and charlie.
ts.Setup.Copy("../../ssl/ca-bundle.pem", os.path.join(ts.Variables.CONFIGDIR, "ca-bundle.pem"))

ts.Disk.records_config.update({
      'proxy.config.diags.debug.enabled': 1
    , 'proxy.config.diags.debug.tags': 'txn_box|ssl'
    , 'proxy.config.http.cache.http':  0

    , 'proxy.config.ssl.server.cert.path': ts.Variables.CONFIGDIR
    , 'proxy.config.ssl.server.private_key.path': ts.Variables.CONFIGDIR
    , 'proxy.config.ssl.CA.cert.path': ts.Variables.CONFIGDIR
    , 'proxy.config.ssl.CA.cert.filename': 'ca-bundle.pem'
    , 'proxy.config.ssl.client.certification_level': 2
    , 'proxy.config.http.server_ports': '{0} {1}:ssl'.format(ts.Variables.port, ts.Variables.ssl_port)
})
ts.Disk.ssl_multicert_config.AddLine(
    'dest_ip=* ssl_cert_name=server.pem ssl_key_name=server.key'
)
