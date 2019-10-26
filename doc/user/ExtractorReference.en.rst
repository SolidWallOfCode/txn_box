.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _extractor_reference:

*******************
Extractor Reference
*******************

:term:`Extractor`\s are the data access mechanism for |TxB|. They are used to compose :term:`feature
string`\s. Such a string describes how a :term:`feature` is extracted. The general form is a double
quoted string containing a mix of literal text and extractors. Extractors are marked by being
enclosed in braces, in a manner heavily based on `Python string formating
<https://docs.python.org/3.7/library/string.html#format-string-syntax>`__. Braces can be escaped
by doubling them, so that "{{" is treated as a single literal "{" without metasyntatic meaning.

For convenience, because a single extractor is by far the most common case, unquoted strings
are treated as a single extractor. Consider the extractor :txb:extractor:`creq-host`. This can be
used in the following feature strings, presuming the host is "example.one".

============================= ==================================
Feature String                Extracted Feature
============================= ==================================
``"Host = {creq-host}"``      ``Host = example.one``
``"Host = {creq-host:*<15}"`` ``Host = example.one***``
``creq-host``                 ``example.one``
``"creq-host"``               ``creq-host``
``"{creq-host}"``             ``example.one``
``"{{creq-host}}"``           ``{creq-host}``
``!literal "{creq-host}"``    ``{creq-host}``
============================= ==================================

Every feature can be expressed as a string, but somecan also be extracted as a different type. The
currently supported types are

``STRING``
   A string, the default type.

``INTEGER``
   An integral value.

``BOOLEAN``
   A value that is either :code:`true` or :code:`false`.

``IP_ADDRESS``
   An IP address.

``CONS``
   A cons cell.

``TUPLE``
  A tuple of features, each of which can be a distinct type.

All quoted feature strings yield a ``STRING`` when extracted. Only unquoted singleton feature strings
can have a different type, which is determined by the extractor.

Extractors may have or require parameters that affect what is extracted. The arguments supplied for
these parameters are separated from the extractor name by the character '@', the same as with
directives.

Extractors
**********

.. txb:extractor:: creq-url

   The client request URL.

.. txb:extractor:: creq-host

   Host for the request. This is retrieved from the URL if present, otherwise from the ``Host``
   field.

.. txb:extractor:: creq-path

   The path of the URL in the client request. This does not include a leading slash.

.. txb:extractor:: creq-scheme

   The scheme of client request.

.. txb:extractor:: creq-field

   The value of a field in the client request. This requires a field name as a argument. To
   get the value of the "Host" field the extractor would be "creq-field@Host". The field name is
   case insensitive.

.. txb:extractor:: cssn-sni

   The SNI name for the client session.

.. txb:extractor:: random

   Generate a random integer in a uniform distribution. The default range is 0..99 because the most
   common use is for a percentage. This can be changed by adding arguments. A single number argument
   changes the upper bound. Two arguments changes the range. E.g.

   :code:`random@199` generates integers in the range 0..199.

   :code:`random@1,100` generates integers in the range 1..100.

   The usual style for using this in a percentage form is ::

      with: random
      select:
      - lt: 5 # match 5% of the time
        do: # ...
      - lt: 25: # match 20% of the time - 25% less the previous 5%
        do: # ...
