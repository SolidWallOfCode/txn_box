' Copyright 2020, Verizon Media
' SPDX-License-Identifier: Apache-2.0

.. include:: common.defs

.. highlight:: yaml
.. default-domain:: yaml

.. _txn-box:

***************
Transaction Box
***************

Transaction Box, or "txn_box", is a plugin to manipulate :term:`transaction`\s in Apache Traffic
Server. The functionality is based on requests I have received over the years from users and
admnistrators for |TS|. Where possible I have designed to provide fewer generalized features that
can be used in a variety of combinations.

There are several purposes to this project.

*  Providing a more generalized set of operations for working with transactions. This primarily
   involved separating data access from data use - that is, any data can be used in any context in a
   consistent style. In essence, instead of being a specialized plugin for a single task, it is
   a tool box with which solutions can be constructed.

*  Consolidating transaction manipulation so that rather than a constellation of distinct plugins
   with different syntax and restrictions, |TxB| provides a single toolbox sufficient for
   most needs. A consequence of this is that |TxB| will replace a number of current plugins.

*  Being extensible, so that it is easier to add new mechanisms to |TxB| than to create a
   new plugin for some task.

*  Provide YAML configuration for consistency, standardization, and easy of use by automation tools.

|TxB| uses `libswoc <http://github.com/SolidWallOfCode/libswoc.git>`__ and `YAML CPP
<https://github.com/jbeder/yaml-cpp>`__.

.. note::

   The code and this documentation is under rapid development, sections may already be outdated. In
   addition, much of this has yet to be implemented and represents future work. Time permitting,
   this will be made clearer.

Concepts
********

|TxB| is based on the idea that for a given transaction and hook in |TS|, the administrator wants to
consider some subset of the information in the transaction and, based on that, perform specific
actions. There is a further presumption that the common use will be multiple comparisons against the
same data, each comparison associated with different actions. This is a generalization of how URL
rewriting happens currently in |TS| via "remap.config", the difference being that for |TS| only a
fixed subset is available for comparison, the set of actions is limited, and there is only one
decision point.

To aid further explanation, some terms need to be defined.

|TxB| has two phases of operation, :term:`load time` and :term:`run time`. Load time is the time
during which the configuration is being loaded. |TxB| also has :term:`hook`\s which are essentially
the same as a hook in |TS|. In addition to the hooks in |TS|, |TxB| has additional hooks specific to
|TxB|.

A :term:`feature` is data of interest for a transaction. :term:`Extraction` is collecting that
information. An :term:`extractor` retrieves the data. Extractors are used in :term:`feature
expression"\s which can combine extractors and literals, or simply be a single extractor. During run
time, as needed, |TxB| will do extraction, which in more detail means to :term:`apply` a
feature expression (or "expression") resulting in a feature.

Feature extraction is the essence of |TxB|. All data that is used in any way initially comes from an
extractor. Each extration yields a feature of a specific type, although an expression may return
different feature types depending on the data. For instance, extracting an HTTP message field may
yield a string if the field is present, or the :code:`NIL` value if not.

A :term:`directive` is an action to be performed. Some directives can have an :term:`argument` which
provides additional control of the directive's action. Arguments are enclosed in brackets after the
name of the directive. Directives generally need data to perform their action and that data is
provided by extraction. For example, setting the value of a field in an HTTP message header is done
by a directive. The new value of the field is provided by extraction. For example, the directive
:code:`prsp-field` sets a field value in the proxy's response to the user agent. To set the field
"X-SWOC" to "valid" the directive would be :code:`prsp-field<X-SWOC>: "valid"`. Here the directive
has the argumnet "X-SWOC" to specify the exact field to change, and the expression :code:`"valid"`
which is a :term:`literal extractor` which always yields the string "valid".

Basing actions on a feature is done by :term:`selection`. This is the conditional mechanism in
|TxB|. The basics are that a feature is extracted and then compared using various
:term:`comparison`\s. Each comparison can have an associated list of directives which are invoked if
the comparison matches. Because selection is done with a directive, selection can be nested to an
arbitrary depth, slicing the data of the transaction as seems best to the administrator.

It is a general rule that once selection has occurred, only directives associated with successful
comparison will be invoked. There is no backtracking from a selection. If no comparison in a
selection matches, the selection is skipped and the next directive invoked. This increases locality
such that to determine behavior of a piece of the configuration only direct parent selections need
be considered.

There is one more directive that merits special mention, the :txb:drtv:`when` directive. This is
used to specify on which hook a list of directives should be invoked. The set of valid directives
and extractors depends on the hook. For instance, the directive :txb:drtv:`prsp-field` which sets a
field in the proxy response cannot be use in early hooks because the proxy response does not yet
exist. This is checked during configuration loading and reported.

Configuration
*************

|TxB| is configured using YAML. |TxB| can be used as a global or remap plugin and the configuration
is a bit different in the two cases.

For a global configuration, the top level directives must all be :txb:drtv:`when` so that every
directive is associated with a specific hook. For a remap configuration, all directives are grouped
in an implied :code:`when: remap` and therefore that does not have to be specified.

Each directive and extractor has an associated set of hooks in which it is valid, therefore some
will be available in a remap configuration and some will not. In particular there are several
directives which are specific to remap because they interact with the data passed to a remap plugin
which is not available in any other context.

For both global and remap plugins a file containing YAML must be specified. A specific key in the
file is used as the base of the configuration. For global configuration this is by default the
:code:`txn_box` key at the top level. For remap it is the top level node in the file (generally the
entire file). This can be overridden by a second parameter, which is a path to the root
configuration node. This must be a sequence of keys in maps, starting from the top. The path is
specified by a dot separated list of these keys. For example, consider a file with this at the top
node level. ::

   txn_box: # path - "txn_box"
      example-1: # path - "txn_box.example-1"
         inner-1: # path - "txn_box.examle-1.inner-1"
      example-2: # path "txn_box.example-2"

If "example-1" was to be the root, the path would be "txn_box.example-1". The global default,
"txn_box", would select "txn_box"" as the root node. The path could also be
"txn_box.example-1.inner-1" to select the inner most node. As a special case, the path "." means
"the unnamed top level node". Note this is problematic in the case of keys that contains ".", which
should be avoided.

Selection
=========

Selection is the mechanism for conditional operation. This is done by specifying the extraction of a
feature then applying :term:`comparison`\s. Each cmparison has an associated list of directives
which is invoked if the comparison is successful. The :txb:drtv:`with` directive is used for
selection. The key :code:`select` is used to anchor the list of comparisons.  ::

   with: creq-host
   select:
   -  match: "mail.example.one"
      do:
      -  preq-url: "https://example.com/mail"
   -  match: "search.example.one"
      do:
      -  preq-url: "https://engine.example.one"

Here :txb:xf:`creq-host` is an extractor that extracts the host of the URL in the client request.
The value of the :code:`select` key is a list of objects which consist of a comparison and a list of
directives as the value of the :code:`do` key.

The comparison :txb:cmp:`match` is a comparison operator that does string comparisons between its
value and the active feature. The directive :txb:drtv:`upstream` sets upstream destination in the
proxy request. What this configuration snippet does is change requests for "mail.example.one" to
requests to "example com/mail" and requests for "search.example.on" to "engine.example.one".

The :code:`with` / :code:`select` mechanism is a directive and so selection can be nested to an
arbitrary depth. Each selection can be on a different feature. As result the relative importance of
features is determined by the particular configuration. This means decisions can be made in a
hierarchial style rather than a single linear set of rules, which enables a large reduction in "cross
talk" between rules.

Hooks
============

The directive key :txb:drtv:`when` can be used to specify on which hook directives should be performed.
The "when" must also have a :code:`do` key which contains the directives. The value of :code:`when`
is the hook name, which must be one of

================== =============  ============ ========================
Hook               when           Abbreviation Plugin API Name
================== =============  ============ ========================
Client Request     read-request   creq         READ_REQUEST_HDR_HOOK
Proxy Request      send-request   preq         SEND_REQUEST_HDR_HOOK
Upstream Response  read-response  ursp         READ_RESPONSE_HDR_HOOK
Proxy Response     send-response  prsp         SEND_RESPONSE_HDR_HOOK
Pre remap          pre-remap                   PRE_REMAP_HOOK
Post remap         post-remap                  POST_REMAP_HOOK
Load time
================== =============  ============ ========================

The abbreviations are primarily for consistency between hook tags, extractors, and directives.

For a global plugin, the top level directives must be :txb:drtv:`when` directives so that every
directive is associated with a specific hook. To set the HTTP header field ``directive`` to
``invoked`` immediately after the client request has been read, it would be ::

   txn_box:
      when: creq
      do:
      -  creq-field<directive>: "invoked"

For use a remap plugin, the directives are wrapped in a notional code:`when: remap` directive and are
therefore all performed during remap.
