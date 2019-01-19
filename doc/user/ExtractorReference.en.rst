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
<https://docs.python.org/3.7/library/string.html#format-string-syntax>`__.

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
``!literal "{creq-host}"``    ``{creq-host}``
============================= ==================================

Every feature can be expressed as a string, but can also be extracted as a different type. The
currently supported types are

``STRING``
   A string, the default type.

``INTEGER``
   An integral value.

``BOOLEAN``
   A value that is either :code:`true` or :code:`false`.

``IP_ADDRESS``
   An IP address.

``LIST``
   A list of strings.

``TUPLE``
  A tuple of features, each of which can be a distinct type.

All quoted feature strings yield a ``STRING`` when extracted. Only unquoted singleton feature strings
can have a different type, which is determined by the extractor.

Extractors
*************

.. txb:extractor:: creq-host

   Host for the request. This is retrieved from the URL if present, otherwise from the ``Host``
   field.

.. txb:extractor:: creq-query

   The query string from the client request.

.. txb:extractor:: creq-url

   The client request URL.
