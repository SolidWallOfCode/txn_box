.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _extractor_reference:

Extractor Reference
*******************

To obtain data for a feature an :term:`extractor` is used.

For convenience, because a single extractor is by far the most common case, unquoted strings
are treated as a single extractor. Consider the extractor :txb:extractor:`ua-req-host`. This can be
used in the following feature strings, presuming the host is "example.one".

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
   field.

.. txb:extractor:: ua-req-path
   :result: string

   The path of the URL in the client request. This does not include a leading slash.

.. txb:extractor:: ua-req-scheme
   :result: string

   The scheme of client request.

.. txb:extractor:: ua-req-field
   :result: string
   :arg: name

   The value of a field in the client request. This requires a field name as a argument. To
   get the value of the "Host" field the extractor would be "ua-req-field<Host>". The field name is
   case insensitive.

.. txb:extractor:: inbound-sni
   :result: string

   The SNI name for the client session.

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
