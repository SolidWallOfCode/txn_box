.. include:: common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _txn-box:

***************
Transaction Box
***************

Transaction Box, or "txn_box", is a plugin to manipulate :term:`transaction`\s in Apache Traffic
Server. It is intended to replace the ``header_rewrite`` and ``cookie_remap`` plugins with a more
extensive set of capabilities that are also more general and able to be combined more effectively.
This plugin is also `YAML <http://yaml.org>`__ based for increased ease of configuration.

|TxB| uses `libswoc <http://github.com/SolidWallOfCode/libswoc.git>`__ and `YAML CPP
<https://github.com/jbeder/yaml-cpp>`__.

This plugin should also be considered a prototype for upgrading the core URL rewriting engine. Doing
this first in a plugin will enable must faster iteration of the design without disrupting
production. Deployments can update the plugin and core TS independently which means changes can be
made to the plugin based on experience and feedback without blocking other core updates.

.. note::

   The code and this documentation is under rapid development, sections may already be outdated. In
   addition, much of this has yet to be implemented and represents future work. Time permitting,
   this will be made clearer.

Configuration
*************

|TxB| is configured using YAML. The configuration consists essentially of a list of
:term:`directive`\s. Each of these specifies a run time action to performa. A few of them have
special processing rules.

The work style of the configuration is based on :term:`selection` which means performing an
:term:`extraction` on a transaction to obtain a :term:`feature` which is subjected to a series of
:term:`comparison`\s. Each comparison has an associated sequence of directives and if the comparison
is successful, those directives and *only those directives* are invoked. This is the "no
backtracking" rule. Selection is done by a directive and so selections nest easily.

It is a key rule that once :term:`selection` has occurred, only directives associated with
successful comparison will be invoked. There is never any backtracking from a selection. If no
comparison in a selection matches, the selection skipped and the next directive invoked. This
increases locality such that to determine behavior of a piece of the configuration only direct
parent selections need be considered.

There is a wide variety of directives, with an emphasis on manipulating the HTTP header, such as
adding or changing fields, changing the URL, etc. There is also a variety of extractors to get
information from the transaction. While a few features can be compared as a more specialized type,
all* fatures are available in string format. Feature extraction is not just for selection - most
places that expect strings will also perform extraction. For example when setting the value of a
field the full set of extractors can be used to generate the string for the value.

The top level configuration is grouped under the :code:`txn_box` key. The value of this key must be
a single instance or list of :code:`when` `directives <when>`_. This structure enables |TxB|
configuration to be mixed with other configuration in the same file [#config]_. The requirement to
start with :code:`when` is done to be clear about the hook on which the configuration is active.

Selection
=========

Selection is the root of configuration. This is done first by specifying the extraction of a feature
from the transaction then applying :term:`comparison`\s, each of which has an associated
list of directives. If the comparison is successful, those directives are executed. For example, to
do a selection based on the host in the client request URL ::

   with: "{creq-host}"
   select:
   -  match: "mail.example.one"
      do:
      -  set-preq-url: "https://example.com/mail"
   -  match: "search.example.one"
      do:
      -  set-preq-url: "https://engine.example.one"

Here :code:`{creq-host}` is an extractor that extracts the host of the URL in the client
request. The value of the :code:`select` key is a list of cases which consist of a
:comparison and a list of directives as the value of the :code:`do` key.

The specific key :code:`match` is a comparison operator that does string comparisons between the
provided string and the feature. :code:`set-preq-url` is an directive that sets the URL in the proxy
request. What this configuration snippet does is rewrite the URL to use for the proxy request to the
upstream, changing requests for "mail.example.one" to a request to "example com/mail" and requests
for "search.example.on" to "engine.example.one".

The :code:`with` / :code:`select` mechanism is a directive and so selection can be nested to an
arbitrary depth. Each selection can be on a different feature. As result the relative importance of
features is determined by the particular configuration, it is not built in to |TxB|.

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

   with: "{creq-host}"
   select:
   -  suffix: ".org"
      do:
      -  with: "{...}"
         select:
         -  match: "apache"
            do: # stuff for the exact domain "apache.org"
         -  suffix: ".apache"
            do:
            -  with: "{...}"
               select:
               -  match: "mail"
                  do: # handle mail.apache.org
               -  match: "id"
                  do: # handle id.apache.org
               # ....


Extractors
++++++++++

HTTP header
-----------

creq-url
   The URL in the client request.

creq-url-method
   The method in the client request.

creq-url-scheme
   The scheme in the client request.

creq-url-host
   The host of the client request URL.

creq-url-path
   The path in the client request URL.

creq-field
   The value of a field in the client request. The field name must be provided as the extension.
   E.g. the ``Connection`` field is extracted with "{creq.field::Connection}". If there is field
   with the specified name, the result is the empty string.

   If the field is multi-valued, a single value can be accessed by adding brackets and an index to
   the field name. E.g "{creq.field::Forward[1]}" to get the first value in the "Forward" field.

Regular Expression
------------------

If a regular expression has successful matched in a comparison, or explicitly applied, it and its
capture groups become *active*. This makes the capture groups available as features to be extracted.
These are numbered in the standard way, with ``0`` meaning the entire matched string, ``1`` the
first capture group, ``2`` the second capture group, and so on. It is an error to use an index that
is larger than the available capture groups, or when no regular expression is active. For example if
a header named "mail-check" should be set if the host contains the domain "mail", it could be done
as ::

    with: "{creq-host}"
    select:
    - regex: "^(?:(.*?)[.])?mail[.](.*?)$"
      do:
      - set-preq-field: [ mail-check, "You've got mail from {2}!" ]

Session
-------

Other
-----

is-internal
   Boolean feature, this is ``true`` if the transaction is an internal transaction, ``false`` if not.

Comparisons
+++++++++++

A comparison compares a feature against fixed data, specified in the configuration. A comparison
either matches or it doesn't. A comparison that matches may have side effects but a comparison
that does not match is irrelevant. For example, a regular expression comparison can set the
regular expression capture groups, but this happens only if the comparison matches.

match
   Literal string matching. The comparison matches if the feature is exactly the specified string.

match-nocase
   Literal string matching, case insensitive. The comparison matches if the feature is exactly the
   specified string, ignoring case.

suffix
   Match the suffix only as a literal string.

suffix-nocase
   Match the suffix only as a literal string, case insensitive.

regex
   Regular expression matching. The value should be the regular expression to apply to the feature.
   If there are capture groups and the regular expression matches the feature, these groups will
   become available via extractor which are numbers, 0 for the entire matched text, and 1,2,etc.
   explicit capture groups. Regular expression are not anchored, this must be done explicitly.

regex-nocase
   Regular expression matching that is case insensitive. Otherwise this is identical to :code:`regex`.

true
   For boolean features, this matches if the boolean value is true.

false
   For boolean features, this matchs if the boolean value is false.


Directives
==========

with
   See `Selection`_.

when
   See `Hook Control`_.

set-preq-field
   :code:`set-preq-field: [ "name", "value" ]`

   Set the value of a field in the proxy request to the upstream. The value should be a list of
   two elements, a field name and a field value. Any existing value for the field is overwritten
   and the field created if it does not exist.

debug
   :code:`debug: "message"`

   :code:`debug: [ "tag", "message" ]`

   Generate a plugin debug message. If *tag* is specified it is used as the debug tag, otherwise
   the plugin tag "txn_box" is used.
   
Formatting
==========

The second part of an extractor supports controlling the format of the output. This is not generally
requried, but in some cases it is quite useful. A good example is the extractor
:code:`is-internal`. This returns a true or false value, which is in the C style mapped to 1
and 0. However, it can be changed to "true" and "false" by specifying the output as a string. ::

   set-preq-field: [ Carp-Internal, "{is-internal:s}" ]

Formatting is most commonly useful when setting values, such as field values. The extracted strings
can be justified, limited in width, and in particular IP addresses can be formatted in a variety of
ways.

Hook Control
============

The directive key :code:`when` can be used to specify on which hook directives should be performed.
The "when" must also have a :code:`do` key which contains the directives. The value of :code:`when`
is the hook name, which must be one of

================== ====
Hook               when
================== ====
Client Request     creq
Proxy Request      preq
Upstream Response  ursp
Proxy Response     prsp
================== ====

The top level directives, those in the :code:`txn_box` key, must be :code:`when` directives so that
every directive is associated with a specific hook. To set the HTTP header field ``directive`` to
``invoked`` immediately after the client request has been read, it would be ::

   txn_box:
      when: creq
      do:
      -  set-creq-field: [ directive, invoked ]

Issues
******

#  What happens to the remap requirement? What counts as a remap match? Currently adding a
   comparison "always" which always matches, so that can count as a match.

#  There are some use cases that want to check just for an extract string to be empty or non-empty.
   Should there be a forcing to the ``bool`` type for these, which are then ``true`` for non-empty
   and ``false`` for empty? Specific match operators for empty and non-empty (although this can be
   done with regular expressions)?

#  How optional should ``do`` nodes be? For instance, for selection cases is it OK to omit the
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
     - with: "{creq-host}"
       select:
       -  suffix: ".apache.org"
          do:
          -  rewrite-url: "{creq.scheme}://apache.org/{...}{creq.path}"
       -  match: "apache.org"
          # Explicitly do nothing with the base host name to count as "matched".

Access Control
==============

Access to upstream resources can be controlled on a fine grained level by doing a selection and
using the :code:`deny` directive. For instance, if access to ``id.example.one`` should be restricted
to the ``GET``, ``POST``, and ``HEAD`` methods, this could be done wth ::

   with: "{creq.url}"
   select:
   -  match: "id.example.one"
      do:
      -  with: "{creq.method}"
         select:
         -  not:
               match: [ "GET", "POST", "HEAD" ]
            do:
               deny:
      -  set-field: [ "Access", "Allowed" ] # mark OK and let the request go through.

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

.. [#config]

   Presuming none of the other configuration uses the top level key :code:`txn_box` which seems a
   reasonable requirement.
