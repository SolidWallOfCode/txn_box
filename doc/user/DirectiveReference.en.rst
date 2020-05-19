.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _directive_reference:

Directive Reference
*******************

Fundamental
===========

.. txb:directive:: when
   :value: literal string

   Specify the hook for other directives.

.. txb:directive:: with

   Do selection of directives.

User Agent Request
==================

.. directive:: ua-url
   :value: string

   Set the URL of the client request to ;arg:`value`. This must be the full, parsable URL. To set
   more specific elements of the URL use the more specific directives.

.. directive:: ua-req-host
   :value: string

   Set the host for the client request to :arg:`value`. This updates both the URL and the ``Host``
   field as needed. This has no effect on the request port.

.. directive:: ua-url-host
   :value: string

   Set the host in the URL for the client request to :arg:`value`. This has no effect on the
   port.

.. directive:: ua-req-port
   :value: integer

   Set the port for the request. This updates the URL and ``Host`` field as needed. This has no
   effect on the host.

.. directive:: ua-url-port
   :value: integer

   Set the port in the user agent request to :arg:`value`. This has no effect on the host in the
   URL.

.. directive:: ua-req-path
   :value: string

   Set the path in the client request to :arg:`value`. A leading slash is ignored.

.. txb:directive:: ua-req-field
   :arg: *name*
   :value: string, tuple of strings

   Set the field named :arg:`name` to the :arg:`value`. If :arg:`value` is a tuple of strings,
   create a field for every element of the tuple and set the value for that field to the tuple
   element.

   Set the field names "X-Swoc" to "Potzrebie" ::

      ua-req-field<X-Swoc>: "Potzrebie"

Proxy Request
=============

.. txb:directive:: proxy-req-field
   :arg: *name*
   :value: string, tuple of strings.

   Set the field named :arg:`name` to the :arg:`value`. If :arg:`value` is a tuple of strings,
   create a field for every element of the tuple and set the value for that field to the tuple
   element. ::

      ua-req-field<X-Swoc>: "Potzrebie"

Transaction
===========

.. txb:directive:: var
   :arg: *name*
   :value: *any*

   Set the  transaction local variable :arg:`name` to :arg:`value`. :arg:`value` is evaluated in
   the directive context and then stored in the variable.

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

.. directive:: ip-space-define

   This must be used on the ``post-load`` hook. It defines and loads an IP space. This is a mapping
   from IP addresses to an array of data items, called a *row*. Each element of the array is a
   *column* in the space, so that every address in the space maps to a row containing the same
   columns. The input data is a CSV file where the first element is an IP address range, network,
   or singleton. The other elements must correspond to the columns defined in this directive.

   It has the following keys.

   name
      Name of the IP space.

   path
      Path to the IP space data file.

   columns
      Defines a data column. This has the keys

      name
         Name of the column. This is optional.

      type
         Type of the data in the column. This must be one of the strings

         ``string``
            A string value.

         ``integer``
            An integer value.

         ``enum``
            One of a set of specific string keys.

         ``flags``
            A subset of a set of specific string keys.

      ``keys``
         The strings that are the keys for ``enum`` or ``flags``. This is required for a ``flags``
         column. If present for an ``enum`` column, the input values are checked agains this list.
         If not, any key is valid. It is ignored for ``string`` or ``integer``.

   Columns can be accessed by name or index. The indices start at 1, column 0 is predefined to be
   the matched IP address.

   See the modifier :mod:`ip-space` and extractor :ex:`ip-col` for how to access the data once defined.


Compatibility
=============

.. txb:directive:: apply-remap-rule

   Valid only in the ``REMAP`` hook, this applies the URL rewriting of the remap rule that matched.
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
