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

.. txb:extractor:: ua-req-url
   :result: string

   The client request URL.

.. txb:extractor:: ua-req-host
   :result: string

   Host for the request. This is retrieved from the URL if present, otherwise from the ``Host``
   field. This does not include the port.

.. extractor:: ua-req-port
   :result: integer

   The port for the request.

.. txb:extractor:: ua-req-path
   :result: string

   The path of the URL in the client request. This does not include a leading slash.

.. txb:extractor:: ua-req-scheme
   :result: string

   The scheme of client request.

.. txb:extractor:: ua-req-field
   :result: NULL, string, string list
   :arg: name

   The value of a field in the client request. This requires a field name as a argument. To
   get the value of the "Host" field the extractor would be "ua-req-field<Host>". The field name is
   case insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

.. extractor:: proxy-req-url
   :result: string

   The URL in the proxy request. Note this may or may not include the scheme and host, depending
   on the original user agent request.

.. extractor:: proxy-req-field
   :result: NULL, string, string list
   :arg: name

   The value of a field in the proxy request. This requires a field name as a argument. To get the
   value of the "Host" field the extractor would be "proxy-req-field<Host>". The field name is case
   insensitive.

   If the field is not present, the ``NULL`` value is returned. Note this is distinct from the
   empty string which is returned if the field is present but has no value. If there are duplicate
   fields then a string list is returned, each element of which corresponds to a field.

.. extractor:: inbound-addr-remote
   :result: IP address

   The remote address for the inbound connection. This is also known as the "client address", the
   address from which the connection originates.

.. txb:extractor:: inbound-sni
   :result: string

   The SNI name sent on the inbound session.

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

.. extractor:: is-internal
   :result: boolean

   This returns a boolean value, ``true`` if the request is an internal request, and ``false`` if not.

.. extractor:: ip-col
   :arg: Column name or index

   This must be used in the context of the modifier :mod:`ip-space` which creates the row context
   needed to extract the column value for that row. The argument can be the name of the column, if
   it has a name, or the index. Note index 0 is the IP address range, and data columns start at
   index 1.
