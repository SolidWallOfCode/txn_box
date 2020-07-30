.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _extractor_reference:

Extractor Reference
*******************

A feature is created by applying a feature expression, which consists of a mix of literal strings
and extractors.

For convenience, because a single extractor is by far the most common case, unquoted strings
are treated as a single extractor. Consider the extractor :ex:`ua-req-host`. This can be
used in the following feature expressions, presuming the host is "example.one".

============================== ==================================
Feature String                 Extracted Feature
============================== ==================================
``"Host = {ua-req-host}"``     ``Host = example.one``
``"Host = {uareq-host:*<15}"`` ``Host = example.one***``
``ua-req-host``                ``example.one``
``"ua-req-host"``              ``ua-req-host``
``"{ua-req-host}"``            ``example.one``
``"{{ua-req-host}}"``          ``{ua-req-host}``
``!literal "{ua-req-host}"``   ``{ua-req-host}``
============================== ==================================

Extractors may have or require parameters that affect what is extracted which are supplied as
an argument. This enables using these parameters inside a feature expression.

Extractors
**********

HTTP Messages
=============

There is a lot of information in the HTTP messages handled by |TS| and many extractors to access it.
These are divided in to families, each one based around one of the basic messages -

*  User Agent Request - the request sent by the user agent to |TS|.
*  Proxy Request - the request sent by |TS| (the proxy) to the upstream.
*  Upstream Response - the response sent by the upstream to |TS| in response to the Proxy Request.
*  Proxy Response - the response sent by |TS| to the user agent.

User Agent Request
------------------

.. txb:extractor:: ua-req-url
   :result: string

      The URL in the user agent request.

.. txb:extractor:: ua-req-scheme
   :result: string

      The URL scheme in the user agent request URL.

.. txb:extractor:: ua-req-host
   :result: string

      Host for the user agent request. This is retrieved from the URL if present, otherwise from the
      ``Host`` field. This does not include the port.

.. extractor:: ua-req-port
   :result: integer

      The port for the user agent request. This is pulled from the URL if present, otherwise from
      the ``Host`` field. If not specified, the canonical default based on the scheme is used.

.. extractor:: ua-req-url-loc
   :result: string

      The network location for the URL in the user agent request. This is also called the
      `authority <https://tools.ietf.org/html/rfc3986#section-3.2>`__. This is the host and, if
      the port is set, the port.

      ============================== ==================== ==== =====================
      url                             host                port loc
      ============================== ==================== ==== =====================
      http://evil-kow.ex/path        evil-kow.ex          80    evil-kow.ex
      http://evil-kow.ex:4443/path   evil-kow.ex          4443  evil-kow.ex:4443
      ============================== ==================== ==== =====================

.. txb:extractor:: ua-req-path
   :result: string

      The path of the URL in the request. This does not include a leading slash.

.. extractor:: ua-req-query
   :result: string

      The query string for the user agent request.

.. extractor:: ua-req-field
   :result: NULL, string, string list
   :arg: name

      The value of a field in the client request. This requires a field name as a argument. To
      get the value of the "Host" field the extractor would be "ua-req-field<Host>". The field name is
      case insensitive.

      If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
      empty string which is returned if the field is present but has no value. If there are duplicate
      fields then a string list is returned, each element of which corresponds to a field.

Pre-Remap
~~~~~~~~~

   The following extractors also extract data from the user agent request URL, but from the URL as
   it was before URL rewriting ("remapping"). Only the URL is preserved, not any of the fields or
   the method. These are referred to elsewhere as "pristine" but that is a misnomer. If the user
   agent request is altered before URL rewriting, that will be reflected in the data from these
   extractors. These do not necessarily return the URL as it was received by ATS from the user
   agent. All of these have an alias with "pristine" instead of "pre-remap" for old school
   operations staff. There are no directives to modify these values, they are read only.

.. extractor:: ua-pre-remap-url
   :result: string

      The full URL of the pre-remap user agent request.

.. txb:extractor:: ua-pre-remap-path
   :result: string

      The URL path in the pre-remap user agent request URL. This does not include a leading slash.

.. txb:extractor:: ua-pre-remap-host
   :result: string

      The host in the pre-remap user agent request URL. This does not include the port.

.. extractor:: ua-pre-remap-port
   :result: integer

      The port in the pre-remap user agent request URL. If not specified, the canonical default based
      on the scheme is used.

.. txb:extractor:: ua-pre-remap-scheme
   :result: string

      The URL scheme in the pre-remap user agent request URL.

.. extractor:: ua-pre-remap-query
   :result: string

      The query string for the pre-remap user agent request URL.

Remap
-----

   During URL rewriting there are two extra URLs available, `the "target" and the "replacement"
   URL
   <https://docs.trafficserver.apache.org/en/latest/admin-guide/files/remap.config.en.html#format>`__.
   These are fixed values from the rule itself, not the user agent. For this reason there are
   extractors for them but no directives to modify these URLs. These values are available only for
   the "remap" hook, that is directives invoked from a rule in "remap.config". Query values are
   not permitted in these URLs and so no extractor for that is provided.

.. extractor:: remap-target-url
   :result: string

      The full target URL of the remap rule.

.. txb:extractor:: remap-target-path
   :result: string

      The URL path in the target URL of the remap rule.

.. txb:extractor:: remap-target-host
   :result: string

      The host in the target URL of the remap rule.

.. extractor:: remap-target-port
   :result: integer

      The port in the target URL of the remap rule.. If not specified, the canonical default based
      on the scheme is used.

.. txb:extractor:: remap-target-scheme
   :result: string

      The URL scheme in the target URL of the remap rule..

.. extractor:: remap-replacement-url
   :result: string

      The full replacement URL of the remap rule.

.. txb:extractor:: remap-replacement-path
   :result: string

      The URL path in the replacement URL of the remap rule.

.. txb:extractor:: remap-replacement-host
   :result: string

      The host in the replacement URL of the remap rule.

.. extractor:: remap-replacement-port
   :result: integer

      The port in the replacement URL of the remap rule.. If not specified, the canonical default based
      on the scheme is used.

.. txb:extractor:: remap-replacement-scheme
   :result: string

      The URL scheme in the replacement URL of the remap rule..

Proxy Request
-------------

.. extractor:: proxy-req-url
   :result: string

   The URL in the request.

.. txb:extractor:: proxy-req-path
   :result: string

   The path of the URL in the request. This does not include a leading slash.

.. extractor:: proxy-req-host
   :result: string

   Host for the request. This is retrieved from the URL if present, otherwise from the ``Host``
   field. This does not include the port.

.. extractor:: proxy-req-port
   :result: integer

   The port for the request. This is pulled from the URL if present, otherwise from the ``Host``
   field.

.. extractor:: proxy-req-query
   :result: string

   The query string in the proxy request URL.

.. extractor:: proxy-req-field
   :result: NULL, string, string list
   :arg: name

   The value of a field. This requires a field name as a argument. To get the value of the "Host"
   field the extractor would be "proxy-req-field<Host>". The field name is case insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

Upstream Response
-----------------

.. extractor:: upstream-rsp-field
   :result: NULL, string, string list
   :arg: name

   The value of a field. This requires a field name as a argument. The field name is case
   insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

Proxy Response
--------------

.. extractor:: proxy-rsp-field
   :result: NULL, string, string list
   :arg: name

   The value of a field. This requires a field name as a argument. The field name is case
   insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

Transaction
===========

.. extractor:: is-internal
   :result: boolean

   This returns a boolean value, ``true`` if the request is an internal request, and ``false`` if not.

Session
=======

.. extractor:: inbound-addr-remote
   :result: IP address

   The remote address for the inbound connection. This is also known as the "client address", the
   address from which the connection originates.

.. txb:extractor:: inbound-sni
   :result: string

   The SNI name sent on the inbound session.

Utility
=======

This is an ecletic collection of extractors that do not depend on transaction or session data.

.. extractor:: ...
   :result: any

   The feature for the most recent :drtv:`with`.

.. txb:extractor:: random
  :result: integer

   Generate a random integer in a uniform distribution. The default range is 0..99 because the most
   common use is for a percentage. This can be changed by adding arguments. A single number argument
   changes the upper bound. Two arguments changes the range. E.g.

   :code:`random<199>` generates integers in the range 0..199.

   :code:`random<1,100>` generates integers in the range 1..100.

   The usual style for using this in a percentage form is ::

      with: random
      select:
      - lt: 5 # match 5% of the time
        do: # ...
      - lt: 25: # match 20% of the time - 25% less the previous 5%
        do: # ...

.. extractor:: ip-col
   :arg: Column name or index

   This must be used in the context of the modifier :mod:`ip-space` which creates the row context
   needed to extract the column value for that row. The argument can be the name of the column, if
   it has a name, or the index. Note index 0 is the IP address range, and data columns start at
   index 1.
