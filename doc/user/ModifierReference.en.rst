.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _modifier_reference:

*******************
Modifier Reference
*******************

Modifiers are a way to modify or filter features.

Modifiers
*********

.. modifier:: else

   The :mod:`else` modifier leaves the feature unchanged unless it is the :code:`NULL` value or is an
   empty string. In that case, the feature is changed to be the value of the modifier.

.. modifier:: hash

   Compute the hash of a feature and take the result modulo the value. This modifier requires a value
   that is a positive integer.

.. modifier:: as-integer

   Coerce the feature to an Integer type if possible. If the feature is already an Integer, it is
   left unchanged. If it is a string, it is parsed as an integer string and if that is successfull, the
   feature is changed to be the resulting integer value. If not, the modifier value is used. This is the
   :code:`NULL` value if unspecified.

.. modifier:: as-ip-addr

   Convert to an IP address or range. If the conversion can not be done, the result is :code:`NULL`.

.. modifier:: ip-space
   :arg: IP Space name

   This modifier must be used in conjunction with :drtv:`ip-space-define`. The latter defines and
   loads an IP space. This modifier changes an IP address feature into a row from the named IP
   Space. The value for the modifier is a feature expression in which :ex:`ip-col` can be used to
   extract data from the row. The value of that expression replaces the IP address.

.. modifier:: filter

   Filter is intended to operate on lists, although it will work on a single value as if it were a
   list of length 1.

   The purpose of filter is to create a new list based on the input list. The common form of this is
   to remove elements.

   The modifier takes a list of comparisons, each of which can have an attached action as a key. The
   valid action keys are

   pass
      Pass the item through to the new list unchanged.

   drop
      Do not pass the item to the new list, in effect removing it.

   replace
      Add an item to the new list in place of the original element. This key takes a feature expression
      which is the new value to put in the new list.

   If no action is specified with a comparison, a match will ``pass`` the item. If an action has no
   comparison, it always matches. The comparisons are checked in order for each element and the first
   match determines the action for that element. If there are no matches, the item is droppe. This
   makes it easy to remove elements. To remove any "set-cookie" field with the loopback address in it -
   ::

      [ proxy-rsp-field<set-cookie> , { filter: [ { contains: "127.0.0.1", drop: }, { pass: } ] } ]

   or long hand ::

      -  proxy-rsp-field<set-cookie>
      -  filter:
         -  contains: "127.0.0.1"
            drop:
         -  pass:

   It is not permitted to have a :code:`do` key with any of the comparisons.

   See :ref:`filter-guide` for examples of using this modifier.
