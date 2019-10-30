.. include:: common.defs

.. highlight:: yaml
.. default-domain:: yaml

.. _examples:

********
Examples
********

Traffic Ramping
===============

For this example, there is an existing set of remap rules that map from externally visible URLs to a
specific upstream that has the application processing. A new version is being staged on the host
"staging.app.example.one". The goal is to redirect a fixed percentage of traffic from the existing
host to the staging host, in way that makes it easy to change. This should only be done for the most
recent version of the application (1.2) - older versions should continue without change. In
addition, to prevent the staging host from becoming overloaded, cached data should still be served
even for diverted requests.

This is the configuration to be attached to the remap rules for the application.

.. literalinclude:: "../example/ramp-remap.yaml"
