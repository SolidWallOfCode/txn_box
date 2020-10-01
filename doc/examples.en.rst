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

For the purposes of this example, there is presumed to exist a remap rule for a specific externally
visible host, "base.ex". A new version is being staged on the host "stage.video.ex". The goal is to
redirect a fixed percentage of traffic from the existing host to the staging host, in way that is
easy to change. In addition it should be easy to have multiple ramping values for different URL
paths. The paths for the two hosts are identical, only the host for the request needs to be changed.

The simplest way to do this would be

.. literalinclude:: ../test/autest/gold_tests/ramp/multi-ramp-1.cfg.yaml
   :start-after: doc.start
   :end-before: doc.end

This has two buckets, the first at 30% and the second at 10%. :ex:`random` is used to generate
random numbers in the range 0..99 which means the extracted value is :code:`lt: 30` roughly 30 times
out of every hunder, or 30% of the time. The buckets are selected by first checking the pre-remap
path (so that it is not affected by other plugins which may run earlier in remapping). Two paths
are in the 30% bucket and one in the 10% bucket. Adding additiona paths is easy, as is changing
the percent diversion. Other buckets can also be added easily.

This can be done in another way by generating the random value once and checking it multiple times.
Given the no backtrack rule, this is challenging to do by checking the percentage first. Instead the
use of tuples makes it possible to check both the value and the path together.

.. literalinclude:: ../test/autest/gold_tests/ramp/multi-ramp-2.cfg.yaml
   :start-after: doc.start
   :end-before: doc.end

The :drtv:`with` is provided with a tuple of size 2, the random value and the pre-remap path. Each
comparison uses :cmp:`as-tuple` to perform parallel comparisons on the tuple. The first comparison
is applied to the first tuple element, the value, and the second comparison to the second value, the
path. Because there is no nested :drtv:`with` there is no need to backtrack.

It might be reasonable to split every path in to a different bucket to make adjusting the percentage
easier. In that case the previous example could be changed to look like

.. literalinclude:: ../test/autest/gold_tests/ramp/multi-ramp-3.cfg.yaml
   :lines: 1-12,16-17

This style presumes the bucket action is identical for all buckets. If not, the previous style would
be better. Note the :code:`do` is attached to the :cmp:`any-of` so that if any of the nested comparisons
succeed the action is performed.

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
