.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _examples:

********
Examples
********

|TxB| is a large, complex plugin and it can be challenging to get started. This section provides a
number of example uses, all of which are based on actual production use of |TxB|.

Default Accept-Encoding
=======================

Goal
   Force all proxy requests to have a value for the "Accept-Encoding" field. If not already set, it
   should be set to "identity".

.. literalinclude:: "../test/autest/accept-encoding/config.yaml"

This acts on the proxy request hook. The "Accept-Encoding" field is extracted and then modified
using the :mod:`else` which

Traffic Ramping
===============

For this example, there is an existing set of remap rules that map from externally visible URLs to a
specific upstream ("app.example.one") that has the application processing. A new version is being
staged on the host "staging.app.example.one". The goal is to redirect a fixed percentage of traffic
from the existing host to the staging host, in way that makes it easy to change. This should only be
done for the most recent version of the application (1.2) - older versions should continue without
change. In addition, to prevent the staging host from becoming overloaded, cached data should still
be served even for diverted requests.

This is the configuration to be attached to the remap rules for the application.

.. literalinclude:: "../example/ramp-remap.yaml"
