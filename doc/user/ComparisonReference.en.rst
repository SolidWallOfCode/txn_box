.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _directive_reference:

********************
Comparison Reference
********************

Comparisons
************

.. txb:comparison:: match

   Exact string match. Successful if the feature is exactly the string value.

.. txb:comparison:: prefix

   Prefix string match. Successful if the value is a prefix of the feature.

.. txb:comparison:: suffix

   Suffix string match. Successful if the value is a suffix of the feature.

.. txb:comparison:: eq

   Equal. Successful if the value is numerically equal to the feature.

.. txb:comparison:: lt

   Less than. Successful if the value is numerically less than the feature.

.. txb:comparison:: le

   Less than or equal. Successful if the value is numerically less than or equal to the feature.
