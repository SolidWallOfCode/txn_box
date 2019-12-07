.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _modifier_reference:

*******************
Modifier Reference
*******************

Modifiers are a way to modify or filter features.

Modifiers
*********

else
====

The :code:`else` modifier leaves the feature unchanged unless it is the :code:`NULL` value or is an
empty string. In that case, the feature is changed to be the value of the modifier.

as-integer
==========

Coared the feature in to an Integer type if possible. If the feature is already an Integer, it is
left unchanged. If it is a string, it is parsed as an integer string and if that is successfull, the
feature is changed to be the resulting integer value. If not, the modifier value is used. This is the
:code:`NULL` value if unspecified.
