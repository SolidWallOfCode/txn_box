.. include:: common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _future:

************
Future Work
************

This is future intended work and so may change radically. The essence should remain similar.

Extractors
**********

Session
=======

inbound-remote-addr

inbound-local-addr

inbound-remote-port

inbound-local-port

Other
=====

rand
   A random integer from 1 to the value specified in the extension. E.g. :code:`{rand::10}` is a
   random integer in the range 1..10.

Features
********

Feature Types
=============

For better performance feature extraction which consists of only a single extractor is handled as a
special case. In particular the feature can have a non-string type in this case. Two such types are
currently supported.

Integer
   A integer value.

IP Addressß
   An IP address.

Feature Modifiers
=================

The extracted feature can be post processed using options in the :code:`with`. This is done by
having a pair where the first element is the feature extraction, and the second is a map of options.
Currently the only planned modifier is "hash".

hash
   "hash: <number>"

   Hash the feature and reduce it to the range 1 .. ::code:`number`. Something like ::

      with:
      -  "{creq.url}"
      -  hash: 4096

   This will get the client request URL, hash it, then (as evenly as possibl) reduce it to a number
   in the range 1 .. 4096.


Comparisons
***********

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
**********

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

Variables
*********

A set of variables is maintained for each transaction. These can be set with the "set-var" directive,
which takes a variable name and value. The value can be later retrieved with the extractor "var"
passing the name of the variable in the extension. E.g. to set the "thing" variable to the host in
the client request ::

   set-var: [ "thing" "{creq-field::host}" ]

Afterwards the value can be extracted with "{var::thing}" in any extraction string.

Inline Conditionals
*******************

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
**************

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

Suitable configuration could be

.. code-block:: YAML

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
Without a feature tuple, this could be done with

.. code-block:: YAML

   with: {inbound-remote-addr}
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
***************

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

