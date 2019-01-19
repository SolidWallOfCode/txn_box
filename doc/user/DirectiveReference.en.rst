.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _directive_reference:

*******************
Directive Reference
*******************

Directives
**********

.. txb:directive:: set-creq-query

   Sets the query field for the client request.

   Setting this to an empty string will remove the query entirely.

   If the argument is set this affects only query parameters with that :arg:`name`. In that case the
   value can be a list, in which case a query parameter for :arg:`name` is created for each element
   of the list, with the query paramter value set to the corresponding list element.
