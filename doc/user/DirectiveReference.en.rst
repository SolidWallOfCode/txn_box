.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _directive_reference:

*******************
Directive Reference
*******************

Directives
**********

Client Request
==============

.. txb::directive:: creq-url

   Set the URL of the client request. This needs to be the full, parsable URL. To set more specific
   elements of the URL use the more specific directives.

.. txb:directive:: creq-host

   Set the host for the client request.

.. txb:directive:: creq-path

   Set the path in the client request URL.

.. txb:directive:: creq-field

   Set the value of a field in the client request. This requires an argument which is the field name.
   For example, to set the "X-Swoc" field to "Potzrebie" ::

      creq-field@X-Swoc: "Potzrebie"

Transaction
===========

.. txb:directive:: redirect

   :code:`redirect <location>`

   :code:`redirect [ <status>, <location> ]`

   .. code-block:: YAML

      redirect:
         to: <location>
         status: <status>
         reason: <reason phrase>
         body: <response body>

   This directive generates a redirect response to the user agent without an upstream request.

Utility
=======

.. txb:directive:: debug

   :code:`debug: <message>`

   :code:`debug: [ <tag>, <message> ]`

   Generate a plugin debug message. If *tag* is specified it is used as the debug tag, otherwise
   the plugin tag "txn_box" is used.


IPSpace
=======


Compatibility
=============

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
