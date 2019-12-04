.. include:: common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _txn-box:

***************
Transaction Box
***************

Transaction Box, or "txn_box", is a plugin to manipulate :term:`transaction`\s in Apache Traffic
Server. The functionality is based on requests I have received over the years from users and
admnistrators for |TS|. Where possible I have designed to provide fewer generalized features that
can be used in a variety of combinations.

There are several purposes to this project

*  Provide a more generalized set of operations for working with transactions. This primarily involved
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
rewriting happens currently in |TS|, the difference being that for |TS| only a fixed subset is
available for comparison, and the set of actions is limited.

To aid further explanation, some terms need to be defined.

A :term:`feature` is data of interest for a transaction. :term:`Extraction` is collecting that
information. An :term:`extractor` provides data. A feature is the result of one *or more*
extractors. This is expressed syntactically as a :term:`feature string` which is very similar to
`Python format strings <https://docs.python.org/3.4/library/string.html#format-string-syntax>`__ and
can contain a mixture of extractors and literal strings [*]_. The result of applying the feature
string to a transaction is a feature. For example, a feature could be the path in the URL in the
user agent request. In the configuration the extractor :code:`creq-path` specifies that the URl path
should be extracted as a feature. Although most extractors get data from the transaction, others can
get data from the session, the environment, etc.. All of these are handled and used in the same way.

Feature extraction is the essence of |TxB|. All data that is used in any way initially comes from an
extractor. Each feature string yields a feature of a specific type and can be used any place data
of that type is needed.

Extraction is about getting data. A :term:`directive` is an action to be performed. Directives
generally need data to perform their action and that data is provided by extraction. For example,
setting the value of a field in an HTTP header is done by a directive, and the value of the field
is provided by extraction. For example, the directive :code:`prsp-field` sets a field value in the
proxy's response to the user agent. To set the field "X-SWOC" to "valid" the directive would be
:code:`prsp-field@X-SWOC: "valid"`.

A primary use of features is :term:`selection`. This is the conditional mechanism in |TxB|. The
basics are that a feature is extracted and then compared using various :term:`comparison` operators.
Each comparison can have an associated list of directives which are invoked if the comparison
matches. Because selection is done using the :code:`with` directive, selection can be nested to
an arbitrary depth, slicing the data of the transaction as seems best to the administrator.

It is a general rule that once selection has occurred, only directives associated with successful
comparison will be invoked. There is no backtracking from a selection. If no comparison in a
selection matches, the selection is skipped and the next directive invoked. This increases locality
such that to determine behavior of a piece of the configuration only direct parent selections need
be considered.

There is one more directive that merits special mention, the :code:`when` directive. This is used to
specify on which hook a list of directives should be invoked. The set of valid directives and
extractors depends on the hook. For instance, the directive mentioned above, :code:`prsp-field`
cannot be use in early hooks where the proxy response does not yet exist. This is checked during
configuration loading and reported.

Configuration
*************

|TxB| is configured using YAML. |TxB| can be used as a global or remap plugin and the configuration
is a bit different in the two cases.

For a global configuration, the top level directives must all be :code:`when` (except for an
optional single :code:`meta`) so that every directive is associated with a specific hook. For a
remap configuration, all directives are grouped in an implied :code:`when: remap` and therefore that
does not have to be specified.

Each directive and feature has an associated set of hooks in which it is valid, therefore some will
be available in a remap configuation and some will not. In particular there are several directives
which are specific to remap because they interact with the data passed to a remap plugin which is
not available in any other context.

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

Selection is the mechanism for conditional operation. This is done first by specifying the
extraction of a feature from the transaction then applying :term:`comparison`\s, each of which has
an associated list of directives. The :code:`with` directive is used for selection. The key
:code:`select` is used to anchor the list of comparison objects. If a comparison is successful,
those directives are executed. For example, to do a selection based on the host in the client
request URL ::

   with: creq-host
   select:
   -  match: "mail.example.one"
      do:
      -  preq-url: "https://example.com/mail"
   -  match: "search.example.one"
      do:
      -  preq-url: "https://engine.example.one"

Here :code:`creq-host` is an extractor that extracts the host of the URL in the client
request. The value of the :code:`select` key is a list of cases which consist of a
comparison and a list of directives as the value of the :code:`do` key.

The comparison :code:`match` is a comparison operator that does string comparisons between the
provided string and the feature. The directive :code:`preq-url` sets the URL in the proxy request.
What this configuration snippet does is rewrite the URL to use for the proxy request to the
upstream, changing requests for "mail.example.one" to a request to "example com/mail" and requests
for "search.example.on" to "engine.example.one".

The :code:`with` / :code:`select` mechanism is a directive and so selection can be nested to an
arbitrary depth. Each selection can be on a different feature. As result the relative importance of
features is determined by the particular configuration, it is not built in to |TxB|. Selection is a
one way descent, however - once a selection has been made, only the directives selected will be
performed.

Features
++++++++

Feature extraction from the transaction is done using strings that contain literal text and
extractors. This formatting style is similar to Python formatted strings, where "{}" marks
extractors that will be replaced by the extracted data. `More detail is provided later
<Extractors>`_, with the basics being inside the braces are three (optional) elements, separated by
colons.

   **{** [ *name* ] [ **:** [ *format* ] [ **:** *extension* ] ] **}**

:code:`name` specifies the extractor, :code:`format` makes it possible to control the output format
(such as width), and :code:`extension` is used to pass extra data to the extractor.

When a feature is extracted, it is placed in to a holding container, the "current feature".  The
current feature can be extracted with the name "...". This is useful because some comparison
operators update the current feature. For example, the :code:`suffix` comparison operator matches a
suffix and if it matches the suffix is removed from the current feature. This makes walking the
components of a path or host name much easier ::

   with: creq-host
   select:
   -  suffix: ".org"
      do:
      -  with: ...
         select:
         -  match: "apache"
            do: # stuff for the exact domain "apache.org"
         -  suffix: ".apache"
            do:
            -  with: ...
               select:
               -  match: "mail"
                  do: # handle mail.apache.org
               -  match: "id"
                  do: # handle id.apache.org
               # more comparisons

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
:code:`is-internal`. This returns a true or false value, which is in the C style mapped to 1
and 0. However, it can be changed to "true" and "false" by specifying the output as a string. ::

   preq-field@Carp-Internal: "{is-internal:s}"

Formatting is most commonly useful when setting values, such as field values. The extracted strings
can be justified, limited in width, and in particular IP addresses can be formatted in a variety of
ways.

Hook Control
============

The directive key :code:`when` can be used to specify on which hook directives should be performed.
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

For a global plugin, the top level directives must be :code:`when` directives so that every
directive is associated with a specific hook. To set the HTTP header field ``directive`` to
``invoked`` immediately after the client request has been read, it would be ::

   txn_box:
      when: creq
      do:
      -  creq-field@directive: "invoked"

For use a remap plugin, the directives are wrapped in a notional ``when: remap`` directive and are
therefore all performed during remap.

Issues
******

#. What happens to the remap requirement? What counts as a remap match? Currently adding a
   comparison "always" which always matches, so that can count as a match.

#. There are some use cases that want to check just for an extract string to be empty or non-empty.
   Should there be a forcing to the ``bool`` type for these, which are then ``true`` for non-empty
   and ``false`` for empty? Specific match operators for empty and non-empty (although this can be
   done with regular expressions)?

#. How optional should ``do`` nodes be? For instance, for selection cases is it OK to omit the
   node entirely if there's nothing to actually do, it's just a match for some other reason (e.g.
   to prevent requests that match from matching subsequent cases)?

Examples
********

For request URLs of the form "A.apache.org/path", change to "apache.org/A/path". This example has
the full configuration to demonstrate that. Later examples have just the relevant configuration and
assume this outer wrapper is present. ::

   txn_box:
   - when: creq
     do:
     - with: creq-host
       select:
       -  suffix: ".apache.org"
          do:
          -  rewrite-url: "{creq-scheme}://apache.org/{...}{creq-path}"
       -  match: "apache.org"
          # Explicitly do nothing with the base host name to count as "matched".

Access Control
==============

Access to upstream resources can be controlled on a fine grained level by doing a selection and
using the :code:`deny` directive. For instance, if access to ``id.example.one`` should be restricted
to the ``GET``, ``POST``, and ``HEAD`` methods, this could be done wth ::

   with: creq-url
   select:
   -  match: "id.example.one"
      do:
      -  with: creq-method
         select:
         -  none-of:
               match: [ "GET", "POST", "HEAD" ]
            do:
               deny:
      -  preq-field@Access: "Allowed" # mark OK and let the request go through.

If the method is not one of the allowed ones, the :code:`select` matches resulting in a denial.
Otherwise, because there is no match, further directives in the outside :code:`select` continue
to be performed and the transaction proceeds.

This could be done in another way ::

   with: "{creq.url}"
   select:
   -  match: "id.example.one"
      do:
      -  with: "{creq.method}"
         select:
         -  match: [ "GET", "POST", "HEAD" ]
            do:
               set-field: [ "Access", "Allowed" ] # mark OK and let the request go through.
      -  deny:

The overall flow is the same - if the method doesn't match an allowed, the :code:`with` is passed
over and the :code:`deny` is performed. Which of these is better depends on how much additional
processing is to be done.

In either case, the :code:`set-field` directive isn't required, it is present for illustrative
purposes. This form of the innter :code:`select` works as well.  ::

   with: "{creq.method}"
   select:
   -  not:
         match: [ "GET", "POST", "HEAD" ]
      do:
         deny:

More complex situations can be handled. Suppose all methods were to be allowed from the loopback
address. That could be done (considering just the access control :code:`select`) with ::

   with: "{inbound.remove_addr}"
   select:
   -  in: [ "127.0.0.0/8", "::1" ]
      do: # nothing
   with: "{creq.method}"
   select:
   -  not:
         match: [ "GET", "HEAD", "POST" ]
      do:
         deny:


Reverse Mapping
===============

If the URL "example.one" is rewritten to "some.place", then it useful to rewrite the ``Location``
header from "some.place" to "example.one". This could be done as ::

   with: "{creq-host}"
   select:
   -  match: "example.one"
      do:
      -  rewrite-url-host: "some.place"
      -  when: "send-response"
         do:
         -  with: "{prsp.field::Location}"
            select:
            -  prefix: "http://some.place"
               do:
               -  set-field: [ "Location", "http://example.com{...}" ]

Referer Remapping
=================

While there is no direct support for the old style referer remapping, it is straight forward to
achieve the same functionality using the field "Referer" extractor and selection. ::

   with: "{creq.field::Referer}"
   select:
   -  match: "" # no referrer, equivalent to the "-" notation
      do:
         deny: # must have referer
   -  suffix: [ ".example.one", ".friends.one" ]
      do:
         rewrite-url-host: "example.one"
   -  else:
      do:
         rewrite-url: "http://example.com/denied"

RECV_PORT
=========

As with referer remapping, this is easily done by extracting and selecting on the local (proxy) port.
To restrict the URL "example.one" to connections to the local port 8180 ::


   with: "{creq-host}"
   select:
   -  match: "example.one"
      do:
         with: "{inbound.local_port}"
         select:
         -  ne: 8180
            do:
               deny:

Note because of the no backtrack rule, the request will pass through unmodified if allowed. If it
needed to be changed to "special.example.one" that would be ::

   with: "{creq-host}"
   select:
   -  match: "example.one"
      do:
         with: "{inbound.local_port}"
         select:
         -  eq: 8180
            do:
               set-url-host: "special.example.on"
         -  else:
            do:
               deny:

A/B Testing
===========

The random extractor and the hash modifier are useful for doing bucket testing, where some
proportion of requests should be routed to a staging system instead of the main production upstream.
This can be done randomly per request with the random extractor, or more deterministically using the
has modifier.

Presuming the production destination is "example.one" and the test destination is "stage.example.one"
then 1% of traffic can be sent to the staging system with ::

   with: "{creq-host}"
   select:
   -  match: "example.com"
      do:
      -  with: "{rand::100}"
         select:
         -  eq: 1
            do:
            -  rewrite-url: "http://stage.example.com"
         -  always: # match to allow unmodified request to continue.

To be more deterministic, the bucket could be based on the client IP address. ::

   with: "{creq-host}"
   select:
   -  match: "example.com"
      do:
      -  with: [ "{inbound.remove_addr}", { hash: 100} ]
         select:
         -  eq: 1
            do:
            -  rewrite-url: "http://stage.example.com"
         -  always: # match to allow unmodified request to continue.

As another alternative, this could be done with a cookie in the client request. If the cookie was
"example" with the test bucket indicated by the value "test", then it would be. ::

   with: "{creq-host}"
   select:
   -  match: "example.com"
      do:
      -  with: "{creq.cookie::example}"
         select:
         -  match: "test"
            do:
            -  rewrite-url: "http://stage.example.com"
         -  always: # match to allow unmodified request to continue.

Real Life
=========

The following examples are not intended to be illustrative, but are based on actual production
requests from internal sources or on the mailing list. These are here to serve as a guide to
implementation.

Legacy Appliances
+++++++++++++++++

Support for legacy appliances is required. The problem is these have very old TLS stacks and among
other things do not provide SNI data. However, it is unacceptable to accept such connections in
general. The requirement is therefore to allow such connections only to specific upstream
destinations and fail the connection otherwise.

Query Only
++++++++++

Rewrite the URL iff there is a non-empty query string.

Cache TTL
+++++++++

Set a cache TTL if

*  From a specific domain.
*  There are no cache TTL control fields.

This is a bit tricky because multiple fields are involved. Could this be done via an extraction
vector, or better by doing a single extraction of all fields and checking if that is empty?

Mutual TLS
++++++++++

Control remapping based on whether the user agent provided a TLS certificate, whether the
certificate was verified, and whether the SNI name is in a whitelist.

.. rubric:: Footnotes

.. [*]

   Literals are treated internally as extractors that "extract" the literal string. In practice every
   feature string is an array of extractors.
