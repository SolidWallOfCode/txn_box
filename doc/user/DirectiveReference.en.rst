.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _directive_reference:

*******************
Directive Reference
*******************

Directives
**********

.. txb:directive:: creq-path

   Set the path in the client request URL.

.. txb:directive:: apply-remap-rule

   Valid only in the `REMAP` hook, this applies the URL rewriting of the remap rule that matched.
   The use of this is for backwards compatibility between ATS 9 and previous versions. Earlier
   versions would not apply the rule URL rewriting until after the first remap plugin had
   been called, and dependent on the return value from that call. Starting with ATS 9, the URL
   rewrite is always applied before any remap plugin is called. This directive enables simulating
   the ATS 9 behavior in earlier versions by making this the first directive when |TxB| is the
   first remap plugin. Unfortunately correct use requires knowing this, but it's the best that
   can be done.

   There are two key points -

   *  If only a portion of the URL should be changed, then this needs to be used to prevent ATS from
      wiping out that change, while still getting the effect of updating the post-rewrite URL.

   *  When matching on the client request, note this (and the pre-ATS 9 URL rewriting) changes the
      client request URL and therefore changes what should be matched.
