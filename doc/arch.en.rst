.. include:: common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _arch:

************
Architecture
************

Extending
*********

|TxB| is intended to be easily extendible with regard to adding new directives, comparisons,
and extractors. For each such extension there are two major phases that must be supported,
loading and invoking.

Loading is parsing data in the configuration file. Invoking happens during transaction processing.

Extractor
=========

An extractor gathers data and provides it to the plugin. While this is usually data in a transaction
that is not required. The gathered data is called a :term:`feature`. Every extractor must be able to
provide its feature in string format. It can also provide the feature in one of a few predefined
feature types -

*  :code:`INTEGER`, a signed integral value.

*  :code:`BOOL`, a boolean value that is the equivalent of :code:`true` and :code:`false`.

*  :code:`IP_ADDR`, an IP address.

Other feature types may be supported in the future.

An extractor must inherit from :txb:`Extractor`.

