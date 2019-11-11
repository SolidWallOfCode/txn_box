.. include:: common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _future:

************
Future Work
************

This is future intended work and so may change radically. The essence should remain similar.

Session
=======

inbound-remote-addr

inbound-local-addr

inbound-remote-port

inbound-local-port


Features
********

ts-uuid
   Process UUID for Traffic Server.

Feature Modifiers
=================

The extracted feature can be post processed using options in the :code:`with`. This is done by
having a pair where the first element is the feature extraction, and the second is a map of options.
Currently the only planned modifier is "hash".

hash
   "hash: <number>"

   Hash the feature and reduce it to the range 1 .. ::code:`number`. Something like ::

      with:
      - "{creq.url}"
      - hash: 4096

   This will get the client request URL, hash it, then (as evenly as possibl) reduce it to a number
   in the range 1 .. 4096.


slice
   Extract elements of a list.

Comparisons
***********

in
   "in: <min>-<max>"

   "in: <addr>/<cidr>"

   This matches if the current features in a member of the specified inclusive range. The feature
   must be an integer or an IP address. If the feature is an IP address, the "address/cidr" form can
   be used to specify the range. E.g. "10.0.0.0/15" is interpreted as "10.0.0.0-10.1.255.255".

   A list of ranges can be used and this comparison is match if the value is in any of the ranges.

whatever
   Always match. As implied by the name this is useful only after other comparison operators, to
   serve as a "match all" or cleanup for transactions that did not match any of the previous cases.

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
Still, there are some cases where selecting on more than one feature in parallel is useful. This is
the case with features that are naturally lists, such as multi-valued headers or query strings. It
may also be useful to be able to hand specify tuples by passing a list to :code:`with`. The syntax
here may be a bit tricky but it should be possible to distinguish tuples from modified features.

.. note::

   One parsing rule would be

   *  A scalar is an extractor.

   * A list is a modified feature or a tuple. Such a list must be one of the form

     *  The first element is a scalar or list and the second and all subsequent elements are objects.
        This is a modified feature.

     *  No elements are objects. This is a list of features.

     *  For error reporting, if the second element is an object then it is treated as a modified feature.
        Otherwise, it is treated as a tuple. If the first element is an object, it's a malformed value.

With a list feature, the matching is done across elements of the list. This can be done in an iterative
style where a comparison is made against each element in the list, or tuple style where there is a
different comparison for each element in the list.

The list style matching operators require a value that is another comparison.

:code:`for-all`
    The match succeeds if the base comparison succeeds for every element.

:code:`for-any`
    The match succeeds if the base comparison succeeds for any element.

:code:`for-none`
    The match succeeds if the base comparison fails for all elements.

The tuple style match is :code:`tuple`. It requires a list of comparisons and applies the
comparisons against the list in the same order. It matches if all of the comparisons match. Elements
that do not have a comparison do not match. This means by default if the feature list is a different
length that the comparison list, the match will fail. This is the common case. For less common cases
there are other options.

Tuple elements can be skipped with the :code:`whatever` comparison which accepts any feature type
and always matches.

Trailing elements can be matched with any of the list comparisons. This must always be the last
comparison in a tuple and applies to all elements that do not have an explicit comparison.

These can be combined to ignore all elements past a fixed initial set by using a list comparison
after the last significant comparison. ::

   -  for-all:
      -  whatever:

This is useful if there are different comparisons in the same selection. Otherwise it might be
better to use modifiers to shape the list. E.g., if only the first two elements are relevant then
the :code:`slice` modifier can be used to reduce the list to the first two elements. Using
modifiers is faster and more compact but has the cost of limiting all of the comparisons in the
selection.

Matching can be done in a more explicitly iterative style by use of :code:`...` and modifiers. This
can be used to process successively smaller subsequences of the list.

For examples of all this, consider working with the `Via` header. This is a multi-valued field.
Suppose it was required to check for having been through the local instance of Traffic Server by
looking for the process UUID in the fields. If the first element is the current instance, that's
a direct loop and an error. Otherwise, if the UUID is any other element that is an error unless
the `k8-routing` field is present, indicating that there is active routing that sent it back.

.. note:: This is a real life example.

The design here is to split the `Via` header and then work with the list. The :code:`ts-uuid`
extractor gets the UUID for the Traffic Server process which is used in the `Via` header.

   with: [ creq-field@Via , { split: "," } ] # split in to list
   select:
   -  tuple:
      -  contains: ts-uuid # only check first
      -  for-all:
         - whatever:
      do:
      -  txn-status: [ 400 , "Loop detected" ]
   -  tuple:
      -  whatever: # skip first element.
      -  for-any: # otherwise, see if it's any other element.
         -  contains: ts-uuid
      do: # found it, fail if there's no routing flag.
      -  with: creq-field@k8-routing::present
         select:
         -   eq: false
             do:
                txn-status: [ 400 , "Loop detected" ]

Issues

*  Matching on just the first value is annoyingly verbose. This would be noticeably better if there
   was an "apply" directive which loaded the :code:`with` context, e.g. regular expression groups
   and :code:`...` without even trying to do matches.e43se

*  With support for :code:`do` in each comparison, this may be of more limited utility. But that
   would be verbose to (for instance) do something for every tuple with a specific first element
   if there are multiple cases that match with that element.

IP Address Maps
***************

For fast lookups on large IP address data sets there is support for "IP Address Space". This is
a mapping from IP addresses to feature tuples. An IPSpace is defined via a comma separated value (CSV)
file. The first column must contain an IP address range. Subsequent columns must be of a supported
type. These are

*  Enumeration - the value is one of a small set of strings.

*  Flags - the value is a list that is a subset of a set of strings.

*  Integer - the value is an integer.

*  Boolean - the value is :code:`true` or :code:`false` (or equivalent).

*  String - the value is a string.

The definition of an IPSpace is done with the :code:`ipspace-define` directive. It is a structured
directive that has the following keys

name
   Name of the IPSpace for later reference.

path
   Path to the CSV file continaing hte IPSpace data.

columns
   A list of column definitions, in order.

Each column definition is an object with the keys

name
   Name of the column.

type
   Type of data in nthe column. This is an enumeration with the values.

   string
      Each value is a string.

   integer
      Each value is an integer.

   enum
      Each value is one of a set of strings.

   flags
      Each value is a list of strings. Each string is one of a set of strings.

tags
   If ``type`` is ``enum`` or ``flags`` this contains the list of valid strings.

Using an IPSpace is done via a :term:`modifier`. For an example use case, the goal is to label
IP addresses with whether the address is a corporate network address, a production address, or
an edge address. The CSV fila has the address ranges in the first column and an ``enum`` of
``corp``, ``prod``, and ``edge``. Addresses not marked are external.

The IPSpace is defined as ::

   ip-space-define:
      name: "label"
      path: "networks/label.csv"
      columns:
      -  name: "net"
         type: "enum"
         tags: [ "corp", "prod", "edge" ]

Use is via selection and modifiers. ::

   with: [ cssn-remote-addr , { ip-space: [ "label", "net" ] } ]
   select:
   -  match: "corp"
      do: # ....
   -  match: "prod"
      do: # ...
   -  match: "edge"
      do: # ...
   -  whatever:
      do: #  external / foreign network.

This example gets the remote (source) address of the inbound connection and looks it up in the "label"
IPSpace, retriving the value for the "net" column. This is then compared to the various enumeration
strings to determine the appropriate action.

Alternatively, if the goal were simply to mark the connection for upstreams, this could be done as ::

   preq-field@X-Net-Type: [ cssn-remote-addr , { ip-space: [ "label", "net" ] } ]

If the address is not in the IPSpace, the value of the field "X-Net-Type" in the upstream request
 will be the nil value and the field cleared. Otherwise it will be the string from the CSV file.

For the modifier ``ip-space``, the column name can be omitted. In this case the entire defined tuple
for the address is returned.

.. note:: Should a list of names be supported, to generate an arbitrary tuple from the data?

