.. include:: /common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _design:

************
Design Notes
************

Background
**********

The overall work here has gone through several iterations, including a previous design for |TxB| and
and YAML based overhaul of "remap.config".

At the Spring 2019 summit, it was decided this overhaul, changing only the configuration for
"remap.config", wasn't worth the trouble. The decision was made to instead authorize a complete
reworking of all of the URL rewriting machinery. This was based on the view that if the format was
to be changed, the change should take full advantage of YAML, disregarding any previous
configuration or specific functionality to avoid multiple configuaration changes. It was required
that anything that could be done currently could continue to be done, but without any requirement to
do it in a similar way. That is, any existing "remap.config" could be converted to the new YAML
configuartion with the same functionality. It is hoped that some of this can be automated, but that
is not required.

A primary goal of the work was also to be to minimize the number of times the request, and
particularly the URL, was matched against literals or especially regular expressions. The existing
configurations of various sorts all required independent matching resulting in the comparisons
being done repeatedly. This is true both between different configurations and inside "remap.config".

To avoid that, however, required the new URL rewrite configuration to be much more powerful and
general such that it could perform the functions of these other configurations, including

*  hosting.config
*  cache.config
*  parent.config
*  The ``header_rewrite`` plugin.
*  The ``regexr_remap`` plugin.
*  The ``cookie_remap`` plugin.
*  The ``conf_remap`` plugin.

This was not as large a task as it might originally seem, as just a few basic abilities would cover
most of the use cases. In particular these are

*  Set or remove fields in the header.

*  Set per transaction configuration variables.

*  Set the upstream destination.

*  Respond immediately with a specific status code. This covers redirect, access control, and
   similar cases.

Notes
*****

Extraction and Selection
========================

The previous work on URL rewriting cofiguration was based heavily on direct boolean expressions,
each of which would be evaluated in order to select a paricular rule. When that was ruled out I
decided it would be better to a more "slicing" style for the configuration, where early decisions
would preclude having to check other, no longer possible conditions. For instance, consider a
comparison using series of regular expression such as ".*[.]example[.]one", ".*[.]example[.]two",
etc. as the host. These must be compared, one by one, until a match is found. With |TxB| is possible
to first to a (fast) suffix check for "example.one". If there are multiple regular expressions for
hosts in that domain, then those can as a group be checked or not depending on the suffix check,
reducing the overal number of comparisons.

In current practice and expected practice, it is generally the case that some aspect ("feature") of
the transaction is considered and multiple comparisons are made to that feature to determine a
course of action. That is the model for this work.

Extractors
==========

The heart of the flexibility in this work is the concept of extractors in format strings. This
allows the configuration to reach in to the transaction and pull out data in easily combined ways.
This requires two things for extractors:

*  It must be (relatively) easy to make new extractors. This means the majority of the work should be
   getting the data, with a minor part being integrating it in to the extractor framework.

*  All extractors must have a text form of output.

Initially I debated the idea of feature types that wree not strings, but eventually decided that
there where a few types worth making exceptions in order to provide more powerful comparisons.
Requiring that text output be available is key to making the extractors useful in non-selectin
contexts, primarily as values during HTTP header manipulation. Having a single mechanism for
accessing data in the transaction was a key design goal, so that it need be written only once to be
available for both (and other) uses.

String Features
===============

Other feature types are scalars and therefore easily copied. String features (which are by far the
most common) are copied around as views but this doesn't alleviate the requirement to be careful
with the backing memory for those views.

The class :txb:`FeatureView` is used to track a view along with the source of the memory for the
view. This is needed only in the :txb:`Context` object which tracks transaction local data. For
the :txb:`Config` object is is sufficiently efficient to always copy any non-transient string to
local storage, as this is only once per configuration.

For a :code:`Context` the goal is to avoid copies of transient strings while providing safe access
to strings that are referenced later. When a string feature is used for selection, it is copied
to temporary arena memory. If the string is transient, which means it is referenced only in that
specific selection, then it can be abandonded to be overwritten by a future selection. This minimizes
allocation. In some cases the :code:`Context` can do even better and use a string in external (to
|TxB|) memory, such as the value of a field in a HTTP header, and not copy the feature even to
temporary memory. This also works if the string use is transient as described above. However, if
the feature is referenced later, it needs to be stabilized. :code:`FeatureView` tracks three cases

*  Transient - the string exists only in the context local temporary memory.
*  Literal - the string  is in the :code:`Config` local memory.
*  Direct - the string is in external memory which is unstable.

These are handled in different ways. ``Literal`` is the easiest - nothing needs to be done as the
string (from the point of view of the transaction processing) is persistent. A ``direct`` string
needs to be copied if referenced later because the plugin cannot make assumptions about the
durability of external memory, but on the other hand if it is transient it does not need to be
copied to temporary memory either. Strings that are neither of these exist in the temporary memory
and don't need to be copied again, it is only necessary to "allocate" the temporary memory in which
the string resides to prevent overwritting.

No Backtracking
===============

This is modeled somewhat on the current "remap.config" where once a comparison for a rule is matched,
the rule is selected and no other rules are considered, but if the rule doesn't match it is skipped
entirely. The behavior of :code:`with` matches this, in that one of the comparisons matches and no
other rule at that level is considered, or none match in which case the :code:`with` is entirely
skipped and the next directive at that level invoked.

This rule should also (I hope) make maintenance and the associated debugging easier, since a comparsion
match limits future actions to that specific subtree, results from other previous subtrees cannot
"leak" into the matched one. If a particular directive is invoked, then the path to that invocation
is unambiguous and moreoeve that state of things and previous directives is likewise.


Boolean Expressions
===================

With the addition of the :code:`not` comparison and suport for implicit "or" in other comparison
operators such as :code:`match`, it is possible to implement the "NOR" operator, which in turn is
sufficient to represent any boolean computation. Although this seems to make that somewhat obscure,
in practice use of complex booleans expressions doesn't occur due to its difficulty in comprehension
of humans writing and debugging the configuraion. This structure makes the common tasks and
expressions simpler, at the acceptable expense of somewhat more complex general expressions which
will be rarely (if ever) used.

For example, suppose the goal was to allow requests from loopback, the 10.1.2.0/24 network, and
the 172.17.0.0/16 network, but not other non-routable networks. This would be ::

  in(127.0.0.0/8) OR in(::1) OR in(10.1.2.0/24) OR in(172.17.0.0/16)
  OR (NOT(in(10.0.0/8) OR in(192.168.0.0/16) AND NOT(in(172.16.0.0/12))))

There is no AND, but a bit of `DeMorgan magic <http://mathworld.wolfram.com/deMorgansLaws.html>`__
can transform this to a conjunctive form, expressed in configuration as ::

   or:
   -  in:
      -  "10.1.2.8/24"
      -  "172.17.0.0/16"
   -  not:
          in:
          -  "10.0.0.8/8"
          -  "172.16.0.0/12"
          -  "192.168.0.0/16"

Note the loopback addresses can be elided because they are subsumed by the "not in the
non-routables" clause.


History
*******

This work is the conjuction of a number of efforts. The original inspiration was the
``header_rewrite`` plugin and the base work in |libswoc| was done with the intention of upgrading
``header_rewrite``. The purpose was to provide much more generalized and expandable string
manipulation for working with transaction headers. At the time, ``header_rewrite`` did not support
string concatenation at all. When the Buffer Writer formatting became available in the Traffic
Server core, it seemed clear this would be an excellent way to provide such facilities to
``header_rewrite``.

For a long time work effort was put in to improving |libswoc| to provide the anticipated
requirements of the upgrade. However, there was much resistance to moving these improvements back
in to the Traffic Server core and therefore upgrading ``header_rewrite`` was no longer feasible.
The decision to proceed with a fully separate plugin was catalyzed by work on the ``cookie_remap``
plugin. It seemed similar enough to ``header_rewrite`` that having two different plugins was
sub-optimal. Plans were made to do this work.

At the same time work was proceeding on upgrading the configuration for the core remap
functionality, to make it YAML. As noted, at the Spring 2019 summit this work was rejected in favor
of doing a complete overhaul of core remap, not just updating the configuration. I decided at that
point that making a plugin to demonstrate my proposed configuration and functionality was the best
way to move forward. Going directly to a rewrite of the core code was such a big step that it was
likely to get bogged down. A plugin, on the other hand, would be completely separate and not subject
to the friction of work on the core. In the long run, if this is successful it is expected the
plugin functionality will be moved in to core Traffic Server.
