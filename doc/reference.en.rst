.. include:: common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _reference:

********
Glossary
********

.. glossary::
   :sorted:

   transaction
      A :term:`request` and the corresponding :term:`response`.

   request
      A request from a client to an :term:`upstream`.

   response
      The response or result of a :term:`request`.

   upstream
      A node which is the destination of a network connection.

   feature
      Data which is derived from the transaction and treated as a unit.

   extraction
      Using data from the transaction to create a :term:`feature`.

   extractor
      A mechanism which can performat :term:`extraction`.

   action
      A description of something to perform while processing a transaction.

   selection
      Making a choice based on a :term:`feature`.

   comparison operator
      An operator that compares local data to a feature and indicates if the data matches the feature.
