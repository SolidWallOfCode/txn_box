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
:term;`directive`'s. Each of these specifies a run time action to performa. A few of them have
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

::code:`name` specifies the extractor, ::code:`format` makes it possible to control the output format
(such as width), and ::code:`extension` is used to pass extra data to the extractor.

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


Feature Types
-------------

For better performance feature extraction which consists of only a single extractor is handled as a
special case. In particular the feature can have a non-string type in this case. Two such types are
currently supported.

Integer
   A integer value.

IP Address
   An IP address.

Feature Modifiers
-----------------

The extracted feature can be post processed using options in the :code:`with`. This is done by
having a pair where the first element is the feature extraction, and the second is a map of options.
Currently the only option supported is "hash".

hash
   "hash: <number>"

   Hash the feature and reduce it to the range 1 .. ::code:`number`. Something like ::

      with:
      -  "{creq.url}"
      -  hash: 4096

   This will get the client request URL, hash it, then (as evenly as possibl) reduce it to a number
   in the range 1 .. 4096.

Extractors
++++++++++

HTTP header
-----------

creq.url
   The URL in the client request.

creq.method
   The method in the client request.

creq.scheme
   The scheme in the client request.

creq-host
   The host of the client request URL.

creq.path
   The path in the client request URL.

creq.field
   The value of a field in the client request. The field name must be provided as the extension.
   E.g. the ``Connection`` field is extracted with "{creq.field::Connection}". If there is field
   with the specified name, the result is the empty string.

   If the field is multi-valued, a single value can be accessed by adding brackets and an index to
   the field name. E.g "{creq.field::Forward[1]}" to get the first value in the "Forward" field.

creq.location
   The URL path, anchor, and query.

creq.cookie
   The value of a cookie. The cookie name is specified in the extension.

creq.is_internal
   A true or false value which is true if the transaction is an internal transaction.

Session
-------

inbound.remove_addr
   The remote (client) address for the inbound (client) connection.

inbound.local_addr
   The local (proxy) address for the inbound (client) connection.

inbound.remote_port
   The remote (client) port for the inbound (client) connection.

inbound.local_port
   The local (proxy) port for the inbound (client) connection.

Other
-----

rand
   A random integer from 1 to the value specified in the extension. E.g. :code:`{rand::10}` is a
   random integer in the range 1..10.

Comparisons
+++++++++++

A note on regular expressions - these are not explicitly supported by comparisons. Instead, the
string should be marked as a regular expression with the YAML type "regex". For example, this
matches the literal string "ab[cd]e" ::

   match: "ab[cd]e"

This matches strings "abce" and "abde" ::

   match: !regex "ab[cd]e"

It is sufficiently frequent that regular expressions are used to match prefixes or suffixes that
these are provided explicitly, which allows supporting updating the current feature when matched.

Operators
---------

match
   Literal or regular expression matching. This requires a string or a list of strings, and matches
   if the current feature is the same as any of the strings.

suffix
   Match the trailing part of the feature. If matched, the matched suffix is removed from the
   current feature. This can have a string or a list of strings and is a match if any of the strings
   is a suffix.

prefix
   Match the leading part of the feature. If matched, the matched prefix is removed from the current
   feature. This can have a string or a list of strings and is a match if any of the strings is a
   suffix.

in
   "in: <min>-<max>"

   "in: <addr>/<cidr>"
   
   This matches if the current features in a member of the specified inclusive range. The feature
   must be an integer or an IP address. If the feature is an IP address, the "address/cidr" form can
   be used to specify the range. E.g. "10.0.0.0/15" is interpreted as "10.0.0.0-10.1.255.255".

   A list of ranges can be used and this comparison is match if the value is in any of the ranges.

lt,le,gt,ge,eq,ne
   Standard numeric comparisons. The comparison is "feature" "operator" "value". Therefore "lt: 10"
   checks if the feature is less than 10.

else
   Always match. As implied by the name this is useful only after other comparison operators, to
   serve as a "match all" or cleanup for transactions that did not match any of the previous cases.

anything
   A synonymn for :code:`else`. Always matches.

not
   This is not a direct comparison, it must have as its value another comparison. The overall result
   is the opposite of the contained comparison.

Directives
==========

apply
   "apply: [ <regex>, <string> ]"

   Apply the regular expression ::code:`regex` to ::code:`string`. This updates the extraction argument
   list such that capture groups in the regular expression can be extracted via numbered extractors.
   E.g. "{2}" is replaced by the second capture group. Groups that do not exist or were not part of
   the regular expression match yield the empty string.

deny
   "deny:"

   "deny: <reason>"

    Deny the request. The status will be 403 and the reason "Access Denied" unless overidden. This
    is really an alias for :code:`respond: [ 403 <reason> ]` for convenience and if more control
    is needed use :code:`respond` directly.

respond
   "respond: [ <status, <reason> ]"

    Respond immediaately to the user agent with ::code:`status` and ::code:`reason` without connecting
    upstream.

rewrite-url
   "rewrite-url: <string>"

   Rewrite the URL in the proxy request to be "string".

rewrite-url-host
    "rewrite-url-host: <string>"

    Rewrite the host of the URL in the proxy request to be ::code:`string`.

rewrite-url-path
    "rewrite-url-path: <string>"

    Rewrite the path of the URL to be ::code:`string`.

redirect
   "redirect: <string>"

   "redirect: [ <status>, <string> ]"

   Send a redirect response to the URL ::code:`string` without connecting to the upstream. By default
   the status is 302.

call
   "call: <plugin>"

   "call: [ <plugin>, <args>, ... ]"

   Invoke a callback provided by a plugin.

   .. note:: Implementation

      Should the entry point be specifiable in the directive? That could be very nice.

when
   Specify hook for directives. This requires a "do" value which contains the list of directives.
   
Formatting
==========

The second part of an extractor supports controlling the format of the output. This is not generally
requried, but in some cases it is quite useful. A good example is the extractor
:code:`creq.is_internal`. This returns a true or false value, which is in the C style mapped to 1
and 0. However, it can be changed to "true" and "false" by specifying the output as a string. E.g.
::

   with: "{creq.is_internal:s}"
   select:
   -  match: "true"
      do: # stuff
   -  match: "false"
      do: # things

Formatting is most commonly useful when setting values, such as field values. The extracted strings
can be justified, limited in width, and in particular IP addresses can be formatted in a variety of
ways.

Experimental
============

These capabilities are experimental or tentative.

.. _when:

Hook Control
++++++++++++

.. note::

   This is no longer experimental, the design has been changed to make it fundamental.
   This documentation needs to be moved elsewhere.

The directive key :code:`when` can be used to specify when a hook on which to perform directives.
The "when" must also have a :code:`do` key with those directives. The value of :code:`when` is the
hook name, which must be one of

*  read-request
*  pre-remap
*  post-remap
*  send-request
*  read-response
*  send-response

For example, to select on the upstream response in the "read-response" hook ::

   with: "{creq.field::X-Amzn-Id}"
   select:
   -  prefix: "cc1d-"
      do:
      -  when: send-response
         do:
         -  set-field: [ "Source", "external" ]

This will set the field named "source" to the value "external" on the proxy response during the
Send Response hook if the field "X-Amzn-Id" starts with "cc1d-".

If not specified, |TxB| operates during the Read Request hook.

Variables
+++++++++

A set of variables is maintained for each transaction. These can be set with the "set-var" directive,
which takes a variable name and value. The value can be later retrieved with the extractor "var"
passing the name of the variable in the extension. E.g. to set the "thing" variable to the host in
the client request ::

   set-var: [ "thing" "{creq-field::host}" ]

Afterwards the value can be extracted with "{var::thing}" in any extraction string.

Inline Conditionals
+++++++++++++++++++

One thing that could be useful but is difficult in this design is the ability to perform a simple
operation such as setting a header conditionally, inline. While possible, the no backtrack rule
means it can require either being careful to do it at the end of processing, or duplicate
significant chunks of configuration. One approach to work around this is to allow the abuse of
the :code:`when` operator. This was originally put in to enable actions on future callbacks
conditionally. However, it would be a relatively small change to allow it on the *same* callback,
just "later". Then a conditional (such as setting a header based on whether the transaction is
internal) could be done as ::

  when: read-response
  do:
     with: "{creq-is-internal}"
     select:
        eq: true
        do:
           set-header "x-Is-Internal" "true"

presuming "read-response" is the current hook. If this is allowed, it might be reasonable to have
a special value such as "after" which means the current hook to make this less error prone.

Feature Tuples
++++++++++++++

The basic configuration requires selections to be done on a single extracted feature of the
transaction. This should be adequate for almost all uses, and very much in the style of the existing
"remap.config". In addition, the presence of the :code:`not` comparison means many cases that are
naturally a combination of two feature compares can be changed to :code:`not` of alternatives.
Still, there are some rare edge cases where selecting on more than one feature in parallel is
useful. In this case a tuple of features can be created and then matched against.

The tuple is created by passing :code:`with` a list of extractors. Each extractor generates a
feature which is stored in a "feature tuple". When matching a "combination" key must be used to
provide a list of comparison operators. These keys are

:code:`all-of`
    The match succeeds if every comparison succeeds.

:code:`any-of`
    The match succeeds if any comparison succeeds.

:code:`none-of`
    The match succeeds if no comparsion succeeds (all must fail).

:code:`else`
    The match succeeds. This combination is not allowed to have any comparison operators.

The list of comparisons for the combination (except :code:`else`) must be exactly the same length as the feature
list. Each comparison in the list is applied to the corresponding feature tuple element.

The simplest example is doing access control based on both method and source address. In this
example, the goal is

*  Loopback should be able to use all methods.
*  Non-routables should be able to use "GET", "POST", "HEAD", and "DELETE".
*  Other addresses should be restricted to "GET", "POST", "HEAD".

Suitable configuration could be ::

   with: [ "{creq-method}", "{inbound-remote-addr}" ]
   select:
   -  any-of:
      -  match: [ "GET", "POST", "HEAD ]
      -  in: [ "127.0.0.0/8", "::1" ]
      do: # nothing - transaction allowed
   -  all-of:
      -  match: "DELETE"
      -  in: [ "10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16" ]
      do: # nothing - transaction allowed
   -  else:
      do:
         deny: # not allowed

This could have be done without a feature tuple at the cost of some repetition. The actual use
cases that require this mechanism are rare and difficult to reduce to a sufficiently simple example.
Without a feature tuple, this could be done with ::

   with: {inbound-remoye-addr}
   select:
   -  in: [ "127.0.0.0/8", "::1" ]
      do: # nothing - transaction allowed.
   -  in: [ "10.0.0.8/8", "172.16.0.0/12", "192.168.0.0/16" ]
      do:
         with: "{creq-method}"
         select:
         -  not:
               match: [ "GET", "POST", "HEAD", "DELETE" ]
            do:
               deny:
   -  else:
      do:
         with: "{creq-method}"
         select:
         -  match: [ "GET", "POST", "HEAD" ]
            do: # nothing - transaction allowed
         -  else:
            do:
               deny:

Because of no backtracking, once an address is selected it is only necessary to block invalid
methods. This can be done either with :code:`not` to match invalid methods, or by matching
the valid methods and using :code:`else` to do the :code:`deny`.

This is also easier because of YAML support for `anchors and references
<https://yaml.org/spec/1.2/spec.html#id2784064>`__ means that even if the configuration must be
repeated that can be done syntatically rather than actually copying the configuration. This wouldn't
be any help in this example, but if the configuration in the alternates were non-trivial it can of
great benefit.

IP Address Maps
===============

For fast lookups on large IP address data sets there is support for "IP Address Space". This is
a mapping from IP addresses to property sets, which are the equivalent of YAML objects where
the values are all scalars. The style of use would be to define the IP space, and then use it
as a feature modifier on an IP address feature. The selection feature would then be either the
property set for the address, or a particular property of that set. The selection would then be
done on that value.

For example, the IP space could map to a property that describes what type of network the address
is in, such as "data-center", "production", "corporate", etc. The client request address could then
be extracted and modified by the space, transforming it in to one of those values, which would then
be used for the selection. Suppose the IP space was named "networks" and the network type was encoded
in the "role" property in the property sets. Then this might be something like ::

   with:
   -  "{inbound.remove_addr}"
   -  ip-space: [ "networks", "role" ]
   select:
   -  match: [ "corporate", "data-center" ]
      do:
         set-header "X-Access" "internal"
   -  match: "production"
      do:
         set-header "X-Access" "prod"
   -  else:
      do:
         deny: "External access is not allowed."

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
   - when: read-request
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
