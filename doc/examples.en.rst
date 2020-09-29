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

.. literalinclude:: ../test/autest/gold_tests/example/accept-encoding.replay.yaml
   :start-after: doc.start
   :end-before: doc.end

This acts on the proxy request hook. The "Accept-Encoding" field is extracted and then modified
using the :mod:`else` which modifies the feature only if the feature is null or the empty string.

Traffic Ramping
===============

For this example, there is an existing set of remap rules that map from an externally visible URL to
a specific upstream ("app.example.one") that has the application processing. A new version is being
staged on the host "staging.app.example.one". The goal is to redirect a fixed percentage of traffic
from the existing host to the staging host, in way that makes it easy to change. This should only be
done for the most recent version of the application (1.2) - older versions should continue without
change. In addition, to prevent the staging host from becoming overloaded, cached data should still
be served even for diverted requests.

Static File Serving
===================

|TxB| enables serving defined content. This can be done with the :drtv:`proxy-rsp-body` directive
to replace the existing content with other content. This is enhanced with "text blocks" which
allow obtaining content from external files.

Goal
   Provide a default `security.txt file <https://securitytxt.org>`__ if an upstream doesn't provide
   one.

Example configuration

.. literalinclude:: ../test/autest/gold_tests/static_file/static_file.replay.yaml
   :start-after: Doc 1 open
   :end-before: Doc 1 close

This checks on the upstream response. If the status is 404 (not found) and the path is exactly
"security.txt" then change the response to a 200 and provide a hard wired default for the content.

Goal
   Provide a default `JSON web token <https://tools.ietf.org/html/rfc7519>`__.

The utility here is to bootstrap into an established JWT infrastructure. On a first request into a
CDN the default token is provided to enable access to the resources needed to get a personalized
token. For security reasons the tokens expire on a regular basis which includes the default token.
It would be too expensive to restart or reload |TS| on every expiration. Presuming an infrastructure
that pushes default tokens to the file "/var/www/jwt/default-token.jwt", a text block can be defined
to load that file and check it for changes every 12 hours. If the file is missing, a special marker
"N/A" that signals this problem to the upstream.

.. literalinclude:: ../test/autest/gold_tests/static_file/static_file.replay.yaml
   :start-after: Doc jwt open
   :end-before: Doc jwt close

To use this, the proxy request is checked for the "Author-i-tay" field. If set it is passed through
on the presumption it is a valid token. If not, then the default token is added.

.. literalinclude:: ../test/autest/gold_tests//static_file/static_file.replay.yaml
   :start-after: Doc jwt-apply open
   :end-before: Doc jwt-apply close

Once this is set up, pushes of new tokens to the file system on the production system takes no more
than 12 hours to show up in the default tokens used.
