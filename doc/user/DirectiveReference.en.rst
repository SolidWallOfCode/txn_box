.. Copyright 2020, Verizon Media
   SPDX-License-Identifier: Apache-2.0

.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _directive_reference:

Directive Reference
*******************

Fundamental
===========

.. directive:: when
   :value: hook
   :keys: do:Directive List

   Specify the hook for other directives. The ``do`` key has the list of directives to invoke for the specified :arg:`hook`. To set the field "Best-Band" to the correct value "Delain" on the proxy response (using :drtv:`proxy-rsp-field`) ::

      when: proxy-rsp
      do:
      - proxy-rsp-field<Best-Band>: "Delain"

   The top level directive(s) in a global configuration file must be this directive, which forces every directive to be associated with a specific hook. It can also be used as a normal directive to cause the invocation of directives on another hook (which must not be a prior hook). The action of the directive is to "schedule" its nested directives for later invocation on the specified hook. Therefore if the directive is not invoked its nested directives will not be either. This enables conditional invocation of those directives using data that may not be available in the later hook (e.g. the pre-remap request URL). Although any feature expressions in nested directives will be evaluated in the later hook, not at the time this directive is invoked, the decision on whether to invoke those directives can use current hook information.

.. directive:: with
   :value: expression
   :keys: select:Comparison list | do:Directive list | continue

   Conditional invoke a list of directives. The :arg:`expression` evaluates to a feature. This feature is then compared against the comparisons in ``select`` list of comparisons. Each element of that list is an object. Each object can have a :ref:`comparison<comparison_reference>` and a list of directives under the ``do`` key. The feature for :arg:`expression` is compared in order by the comparisons and the first successful comparison is selected and the directives for the comparison invoked.

   As a fundamental principle, once a comparison is selected and the directives invoked, this
   terminates the invocation of directives for the most recent :drtv:`when` or :drtv:`with`. That
   is, normally there is no return from inside a :drtv:`with` because an outer :drtv:`with` will
   normally also return immediately up to the top level :drtv:`when`. This can be overridden for a
   specific instance by adding the key `continue`. If present then subsequent directives will be
   invoked. This should be used sparingly as the point of this rule is to make it unambiguous which directives were invoked for a transaction. Note that if no comparison is successful subsequent directives are invoked and the :drtv:`with` has no effect.

   A comparison is not required to have ``do`` in which case if it is matched, nothing is done but it counts as a match for the purposes of no return.

   A comparison does not require an actual comparison but can consist only of ``do``. In this case it always matches and this form serves as a convenient "match anything" comparison. Obviously it should always be the last comparison if used.

   The ``do`` key can be used to invoke directives before any of the comparisons. This is useful primarily for access to the feature for :arg:`expression` via the :ex:`...` extractor which extracts that feature. If there is a nested :drtv:`with` that will terminate the list of ``do`` directives but will not prevent the comparisons and their associated directives.

User Agent Request
==================

.. directive:: ua-req-url
   :value: string

   Set the URL of the user agent request to :arg:`string`. This must be the full, parsable URL. To
   set more specific elements of the URL use the more specific directives.

.. directive:: ua-req-host
   :value: string

   Set the host for the user agent request to :arg:`value`. This updates both the URL and the
   ``Host`` field as needed. This has no effect on the request port.

.. directive:: ua-req-url-host
   :value: string

   Set the host in the URL for the user agent request to :arg:`value`. This has no effect on the
   port nor on the ``Host`` field.

   This can interact with with the |TS| core in odd ways on the ``remap`` hook. By default, after
   URL rewriting |TS| will itself update the ``Host`` header to match the host in the URL. This can
   be prevented by enabling `pristine host header
   <https://docs.trafficserver.apache.org/en/latest/admin-guide/files/records.config.en.html#proxy.config.url_remap.pristine_host_hdr>`__
   or overridden by using :drtv:`when` to revert the ``Host`` field on a later hook (such as
   ``post-remap``). The former  can be done globally as configuration, or locally using
   :drtv:`txn-conf`.

.. directive:: ua-req-port
   :value: integer

   Set the port for the request. This updates the URL and ``Host`` field as needed. This has no
   effect on the host.

.. directive:: ua-url-port
   :value: integer

   Set the port in the user agent request to :arg:`value`. This has no effect on the host in the
   URL nor the ``Host`` field.

.. directive:: ua-req-path
   :value: string

   Set the path in the user agent request to :arg:`value`. A leading slash is ignored.

.. directive:: ua-req-query
   :value: string

   Sets the query string for the user agent request.

.. directive:: ua-req-field
   :arg: name
   :value: string, tuple of strings

   Set the field named :arg:`name` to the :arg:`value`. If :arg:`value` is a tuple of strings,
   create a field for every element of the tuple and set the value for that field to the tuple
   element.

   Set the field names "X-Swoc" to "Potzrebie" ::

      ua-req-field<X-Swoc>: "Potzrebie"

Proxy Request
=============

.. directive:: proxy-req-url
   :value: string

   Set the URL of the proxy request to :arg:`string`. This must be the full, parsable URL. To set
   more specific elements of the URL use the more specific directives.

.. directive:: proxy-req-host
   :value: string

   Set the host for the proxy request to :arg:`value`. This updates both the URL and the ``Host``
   field as needed. This has no effect on the request port.

.. directive:: proxy-req-url-host
   :value: string

   Set the host in the URL for the proxy request to :arg:`value`. This has no effect on the
   port nor on the ``Host`` field.

.. directive:: proxy-req-query
   :value: string

   Sets the query string for the proxy request.

.. directive:: proxy-req-field
   :arg: name
   :value: string, tuple of strings.

   Set the field named :arg:`name` to the :arg:`value`. If :arg:`value` is a tuple of strings,
   create a field for every element of the tuple and set the value for that field to the tuple
   element.

   To set the field named "Best-Band" to the correct value "Delain" - ::

      proxy-req-field<Best-Band>: "Delain"


Upstream Response
=================

.. directive:: upstream-rsp-field
   :arg: name
   :value: string, tuple of strings.

   Set the field named :arg:`name` to the :arg:`value`. If :arg:`value` is a tuple of strings,
   create a field for every element of the tuple and set the value for that field to the tuple
   element.

.. directive:: upstream-rsp-status
   :value: integer, tuple of integer, string

   Set the upstream response status. If the value is an integer, the response status is set to
   that value. If the value is a list, it must two elements, an integer and a string. The status
   is set to the integer value and the reason is set to the string.

   Set the status to 403 ::

      upstream-rsp-status: 403

   Set the status to 404 with the reason "Desk too messy" ::

      upstream-rsp-status: [ 404, "Desk too messy" ]

.. directive:: upstream-rsp-body
   :value: string

   Replace the upstream response body with the value of this directive.

Proxy Response
==============

.. directive:: proxy-rsp-field
   :arg: name
   :value: string, tuple of strings.

   Set the field named :arg:`name` to the :arg:`value`. If :arg:`value` is a tuple of strings,
   create a field for every element of the tuple and set the value for that field to the tuple
   element.

   To set the field named "Best-Band" to the correct value "Delain" - ::

      proxy-rsp-field<Best-Band>: "Delain"

.. directive:: proxy-rsp-status
   :value: integer, tuple of integer, string

   Set the proxy response status. If the value is an integer, the response status is set to
   that value. If the value is a list, it must two elements, an integer and a string. The status
   is set to the integer value and the reason is set to the string.

   Set the status to 403 ::

      proxy-rsp-status: 403

   Set the status to 404 with the reason "Desk too messy" ::

      proxy-rsp-status: [ 404, "Desk too messy" ]

Transaction
===========

.. txb:directive:: var
   :arg: *name*
   :value: *any*

   Set the transaction local variable :arg:`name` to :arg:`value`. :arg:`value` is evaluated in
   the directive context then stored in the variable for the current transaction. This has no effect
   on the value in any other transaction. The value persists until changed or the transaction ends.

   Example - set the variable "Best-Band" to the value "Delain" ::

      var<Best-Band>: "Delain"

   Example - stash the user agent request host and path before remap in the variable "save". ::

      var<save>: "{ua-req-host}/{ua-req-path}"

.. directive:: txn-conf
   :arg: *configuration variable name*
   :value: *any*

   This sets a transaction overridable configuration variable. The argument is the full name of the
   configuration variable. The value should be the type appropriate for that configuration variable.

   Example - disable caching for this transaction ::

      txn-conf<proxy.config.http.cache.http>: disable

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

.. directive:: debug

  :code:`debug: <message>`

  :code:`debug: [ <tag>, <message> ]`

  Generate a plugin debug message. If *tag* is specified it is used as the debug tag, otherwise
  the plugin tag "txn_box" is used.

.. directive:: text-block-define

   Define a text block. This is valid only in the ``post-load`` hook. A text block can be read
   from a file or config based text, or both. If a file is specified it can be set to be checked
   for modifications and reloaded if changed. The keys are

   name
      Name of the text block. This is used to reference the contents via :ex:`text-block`. This is
      required.

   path
      Path to a file with content for the text block.

   text
      Explicit text content for the text block.

   duration
      An optional value that specifies how often to check the file for changes.

   One of ``path`` and ``text`` must be present. If both are present ``path`` takes precedence. The
   file contents are used if the file can be read, otherwise the value in ``text`` is used. If
   only ``path`` is present it is a configuration error if the file specified by ``path`` cannot
   be read.

.. directive:: stat-define

   Define a plugin statistic, which can be externally accessed. Currently |TS| limits plugin
   statistics to integers.

   name
      Name of the statistic.

   value
      Initial value. This is optional. If not present the value zero is used.

   persistent
      Whether the statistic is persistent, the value must be a boolean. This is optional. If not
      present the statistic is not persistent.

.. directive:: stat-update
   :arg: *name*
   :value: integer

   Change the value of the plugin statistic :arg:`name`. If the value is present it must be an
   integer, which is added to the value of the statistic. If not present the statistic is
   incremented by 1.

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
   versions would not apply the rule URL rewriting until after the first remap plugin had been
   called and this would also be dependent on the return value from that call. Starting with ATS 9,
   the URL rewrite is always applied before any remap plugin is called. This directive enables
   simulating the ATS 9 behavior in earlier versions by making this the first directive when |TxB|
   is the first remap plugin. Unfortunately correct use requires knowing this, but it's the best
   that can be done.

   Things to note -

   *  If only a portion of the URL is to be changed,  this needs to be used to prevent ATS from
      wiping out that change, while still getting the effect of updating the post-rewrite URL.

   *  When matching on the client request, this (and the pre-ATS 9 URL rewriting) changes the client
      request URL and therefore changes what will be matched. See :ref:`pre-remap extractors
      <ex-pre-remap>` for additional information.

   *  It is possible to execute other directives first but this requires much care and isn't
      portable across ATS versions because it is impossible to replicate as of ATS 9.

.. directive:: did-remap

   Valid only in the ``REMAP`` hook. This sets whether the |TS| core will consider |TxB| to have done
   the URL rewriting for a rule. If so, then the core will *not* do the rule URL rewriting. If no
   value is provide, this will mark the plugin as having done the URL rewrite. If there is a value
   if it is equivalent to ``true`` then the plugin will be marked as having done the rewrite, and if
   if the equivalent of ``false`` will be marked as *not* having done the rewrite. The last instance
   invoked in a hook determines the effective value. This makes it easier to, for instance, mark as
   rewriting and then unmark if not actually done. ::

      -  did-remap: true # assume something will match and rewrite.
      -  with: ua-req-path
         select:
         -  prefix: "v1/video/search"
            do:
            - ua-req-host: "staging.app.ex"
         -  prefix: "v1/video/api"
            do:
            -  ua-req-host: "staging.api.app.ex"
      -  did-remap: false # only get here if nothing matched in the with

   This is useful for versions of |TS| before 9. Starting with |TS| 9 the URL rewriting is done
   before any plugin is called and therefore cannot be prevented by a plugin. In contrast with
   earlier versions if |TxB| is the first plugin, the rewrite done by |TxB| can be wiped out by the
   core rewrite. This directive can be necessary to prevent that. This is nicely compatible because
   for version 9 and later it has no effect and can therefore be included in configurations used in
   different versions.
