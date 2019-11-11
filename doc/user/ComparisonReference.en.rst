.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _directive_reference:

********************
Comparison Reference
********************

Comparisons
************

String Comparisons
==================

.. txb:comparison:: match

   Exact string match. Successful if the feature is exactly the string value. For regular expressions
   this performs an anchored search - the expresison much match the entire feaure.

.. txb:comparison:: prefix

   Prefix string match. Successful if the value is a prefix of the feature.

.. txb:comparison:: suffix

   Suffix string match. Successful if the value is a suffix of the feature.

.. txb:comparison:: contains

   Substring checking. This matches if the feature has the value as a substring. In the regular
   expression case this is simply an unanchored regular expression.

   If the feature is "potzrebie" then ::

      -  contains: "pot" # match
      -  contains: "zri" # no match
      -  contains: "zreb" # match

Numeric Comparisons
===================

.. txb:comparison:: eq

   Equal. Successful if the value is equal to the feature. This works for numeric and IP address features.

.. txb:comparison:: ne

   Not equal. Successful if the value is not equal to the feature. This is valid for Integers and
   IP Addresses.

.. txb:comparison:: lt

   Less than. Successful if the feature is numerically less than the value.

.. txb:comparison:: le

   Less than or equal. Successful if the feature is numerically less than or equal to the value.

.. txb:comparison:: gt

   Greater than. Successful if feature is numerically greater than the value.

.. txb:comparison:: ge

   Greater than or equal. Successful if the feature is greater than or equal to the value.

.. txb:comparison:: in

   Check if the feature is in a range. The value must be a tuple of two values, the minimum and
   the maximum. This matches if the feature is at least the minimum and no more than the maximum.
   The comparison ::

      in: [ 10, 20 ]

   is identical to ::

      all-of:
      -  le: 10
      -  ge: 20

   This is valid for Integers and IP Addresses. If the feature is (the Integer) 8, then ::

      in: [ 1, 10 ] # match
      in: [ 9, 20 ] # no match
      in: [ 1, 6 ] # no match
      in: [ 8, 8 ] # match

   For IP Addresses, the range can be a single value that is a string representing a range. This
   can be

   *  A single value, repreenting a single value range.

   *  A dash separated pair of IP addresses, representing an inclusive range. These are equivalent ::

         in: "192.168.56.1-192.168.56.99"
         in: [ 192.168.56.1, 192.168.56.99 ]

   *  A CIDR notation network, which is treated a range that contains exactly the network. These are
      equivalent ::

         in: [ 172.16.23.0 , 172.16.23.127 ]
         in: "172.16.23.0-127.16.23.127"
         in: "172.16.23.0/25"

Compound Comparisons
====================

These comparisons do not directly compare values. They combine or change other comparisons.

.. txb:comparison:: any-of

   This has a list of comparisons and matches if *any* of the comparisons in the list match. This
   is another term for "or". This stops doing comparisons in the list as soon as one matches.

.. txb:comparison:: all-of

   This has a list of comparisons and matches if *all* of the comparisons in the list match. This
   is another term for "and". This stops doing comparisons in the list as soon as one does not match.

.. txb:comparison:: none-of

   This has a list of comparisons and matches if *none* of the comparisons in the list match.

   This serves as a "not" if the list of length 1. For instance, if the goal was to find features
   that do not have one of a set of strings, this could be done as ::

      none-of:
      -  match@rx: "(?:one)|(?:two)|(?:three)"

   This could be done as a "negative regular expression" but those can create stack explosions. This
   approach is much more robust and generally much faster.
