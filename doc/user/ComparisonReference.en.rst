.. include:: /common.defs

.. highlight:: yaml

.. _comparison_reference:

********************
Comparison Reference
********************

Comparisons
************

Comparison match again the active feature. For most comparisons the value to compare is the value of
the comparison key. In a few cases the comparison value is implicit in the comparison (e.g.
:txb:cmp:`true`).

Each comparison can compares its value against features of specific types. A feature that is not one
of those types is never matched. In some cases the value can be a list, in which each value is
compared against the active feature and the comparison matches if any element of the list matches.
These are marked as such, for example :txb:cmp:`match`.

String Comparisons
==================

String comparisons can set the active index group extractors. The string comparisons are marked with
which groups, if any, are set by the comparison if it matches.

======= ==========================================================
Index   Content
======= ==========================================================
0       The matched string.
1..N    Regular expression capture groups.
*       The unmatched string.
======= ==========================================================

.. txb:comparison:: match
   :type: string
   :tuple:
   :groups: 0,*

   Exact string match.

.. txb:comparison:: prefix
   :type: string
   :tuple:
   :groups: 0,*

   Prefix string match. Successful if the value is a prefix of the feature.

.. txb:comparison:: suffix
   :type: string
   :tuple:
   :groups: 0,*

   Suffix string match. Successful if the value is a suffix of the feature.

.. txb:comparison:: tld

    Top level domain matching. This is similar to :txb:cmp:`suffix` but has a special case for
    the "." separator for domains. It will match the exact feature, or as a suffix if there is
    an intervening ".". It is equivalent to ::

      any-of:
      - suffix: ".yahoo.com"
      - match: "yahoo.com"

.. txb:comparison:: contains

   Substring checking. This matches if the feature has the value as a substring. In the regular
   expression case this is simply an unanchored regular expression.

   If the feature is "potzrebie" then ::

      -  contains: "pot" # match
      -  contains: "zri" # no match
      -  contains: "zreb" # match

.. txb:comparison:: rxp

    Regular expression comparison. If this matches the the index scoped extractors are set. Index 0
    is the entire match, index 1 is the first capture group, etc.

Numeric Comparisons
===================

.. txb:comparison:: eq
   :type: integer, boolean, IP address

   Equal. Successful if the value is equal to the feature. This works for numeric and IP address features.

.. txb:comparison:: ne
   :type: integer, boolean, IP address

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
   :type: integer, IP address

   Check if the feature is in a range. The value must be a tuple of two values, the minimum and
   the maximum. This matches if the feature is at least the minimum and no more than the maximum.
   The comparison ::

      in: [ 10, 20 ]

   is identical to ::

      all-of:
      -  le: 10
      -  ge: 20

   If the feature is (the Integer) 8, then ::

      in: [ 1, 10 ] # match
      in: [ 9, 20 ] # no match
      in: [ 1, 6 ] # no match
      in: [ 8, 8 ] # match

   For IP Addresses, the value is a range. It can be specified as a string that can be parsed as
   an IP range.

        *  single address - "172.16.23.8"
        *  a CIDR network - "172.16.23.8/29"
        *  two addresses separated by a dash - "172.16.23.8-172.16.23.15"

   *  A single value, repreenting a single value range.

   *  A dash separated pair of IP addresses, representing an inclusive range. These are equivalent ::

         in: "192.168.56.1-192.168.56.99"
         in: [ 192.168.56.1, 192.168.56.99 ]

   *  A CIDR notation network, which is treated a range that contains exactly the network. These are
      equivalent ::

         in: [ 172.16.23.0 , 172.16.23.127 ]
         in: "172.16.23.0-127.16.23.127"
         in: "172.16.23.0/25"

Boolean Comparisons
===================

.. txb:comparison:: true

    Matches if the active feature is a boolen that has the value ``true``. The value, if any, is ignored.

.. txb:comparison:: false

    Matches if the active feature is a boolen that has the value ``false``. The value, if any, is ignored.

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

   This serves as a "not" if the list is of length 1. For instance, if the goal was to find features
   that do not have one of a set of strings, this could be done as ::

      none-of:
      -  rxp: "(?:one)|(?:two)|(?:three)"

   This could be done as a "negative regular expression" but those can create stack explosions. This
   approach is much more robust and generally much faster.

.. txb:comparison:: as-tuple

    Compare a tuple as a tuple. This requires a list of comparisons which are applied to the tuple
    elements in the same order.


