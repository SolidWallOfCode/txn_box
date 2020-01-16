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

*  Providing a more generalized set of operations for working with transactions. This primarily involved
   separating data access from data use - that is, any data can be used in any context.

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

A :term:`feature` is data of interest for a transaction. :term:`Extraction` is collecting that
information. An :term:`extractor` retrives the data. A feature is the result of one *or more*
extractors. The result of applying the feature string to a transaction is a feature. For example, a
feature could be the path in the URL in the user agent request. In the configuration the extractor
:txb:xf:`creq-path` specifies that the URL path should be extracted. Although most extractors get
data from the transaction, others can get data from the session, the environment, and other sources.
All of these are handled and used in the same way.

Feature extraction is the essence of |TxB|. All data that is used in any way initially comes from an
extractor. Each feature string yields a feature of a specific type and can be used any place data
of that type is needed.

Extraction is about getting data. A :term:`directive` is an action to be performed. Directives
generally need data to perform their action and that data is provided by extraction. For example,
setting the value of a field in an HTTP header is done by a directive. The new value of the field is
provided by extraction. For example, the directive :code:`prsp-field` sets a field value in the
proxy's response to the user agent. To set the field "X-SWOC" to "valid" the directive would be
:code:`prsp-field<X-SWOC>: "valid"`.

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
      -  upstream: "https://example.com/mail"
   -  match: "search.example.one"
      do:
      -  upstream: "https://engine.example.one"

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
hierachial style rather than a single linear set of rules, which enables a large reduction in "cross
talk" between rules.

Features
++++++++

Unquoted text is presumed to be an extractor, which can include the full format specifier. If text
is quoted it is treated as a :term:`feature string` which are very similar to `Python format strings
<https://docs.python.org/3.4/library/string.html#format-string-syntax>`__ and can contain a mixture
of extractors and literal strings [*]_.

   **{** [ *name* ] [ **:** [ *format* ] [ **:** *extension* ] ] **}**

:code:`name` specifies the extractor, :code:`format` makes it possible to control the output format
(such as width), and :code:`extension` is used to pass extra data to the extractor.

When a feature is extracted, it is placed in to a holding container, the "active feature".  The
active feature can be extracted with the name "...". This is useful because some comparisons
update the active feature. For example, the :txb:cmp:`suffix` comparison operator matches a
suffix and if it matches the suffix is removed from the current feature. This makes walking the
components of a path or host name much easier ::

A key notation is that quoted strings are treated as features strings, which can be just literal
strings. Values without quotes are treated as extractors.

Regular Expression
------------------

If a regular expression has successful matched in a comparison, or explicitly applied, its capture
groups become *active*. This makes the capture groups available as features to be extracted. These
are numbered in the standard way, with ``0`` meaning the entire matched string, ``1`` the first
capture group, ``2`` the second capture group, and so on. It is an error to use an index that is
larger than the available capture groups, or when no regular expression is active. For example if a
header named "mail-check" should be set if the host contains the domain "mail", it could be done as
::

   with: creq-host
   select:
   - regex: "^(?:(.*?)[.])?mail[.](.*?)$"
      do:
      - preq-field@mail-check: "You've got mail from {2}!"

This is a case where the format string is required to use the extractor, otherwise it will be treated
as an integer value. That is, "2" is the integer 2, while "{2}" is the second active capture group.

Session
-------

Other
-----

is-internal [boolean]
   ``true`` if the transaction is an internal transaction, ``false`` if not.

Formatting
==========

The second part of an extractor supports controlling the format of the output. This is not generally
requried, but in some cases it is quite useful. A good example is the extractor
:txb:xf:`is-internal`. This returns a true or false value, which is in the C style mapped to 1
and 0. However, it can be changed to "true" and "false" by specifying the output as a string. ::

   preq-field<Carp-Internal>: is-internal:s

Formatting is most commonly useful when setting values, such as field values. The extracted strings
can be justified, limited in width, and in particular IP addresses can be formatted in a variety of
ways.

Hook Control
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
