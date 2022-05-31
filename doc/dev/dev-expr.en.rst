.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: txb

.. _dev-expr:

Feature Expression Evaluation
*****************************

Expressions are stored in the :txb:`Expr` class. The primary element is a specifier, which is
an instant of :txb:`Expr::Specifier` or a subclass. This contains formatting information along
with potentially a pointer to an extractor. String literals are represented by a special extractor
that returns the literal value.

NONE
   Invalid, no expression. Used for default constructed instances to indicate there's nothing. This
   will always evaluate to :code:`NIL`.
LITERAL
   The value is evaluated in the configuration loading logic and result stored. Evaluation consists
   of accessing the stored value.
IMMEDIATE
   A single specifier containing an extractor is stored. Evaluation means calling the extractor.
COMPOSITE
   A value that requires invoking multiple extractors.
LIST
   A list / tuple of other expressions.

Composite Evaluation
====================

Evaluating a composite expression is significantly more difficult than the other types. The primary
issue is use of the transient buffer. If the transient buffer is used for overall evaluation then
an extractor which also requires the transient buffer will corrupt the final expression value. To
handle some specifiers / extractors will need to be evaluated and the feature cached so the full
evaluation encounters what are effectively only literals.

Capture Groups
==============

Text manipulation is a key feature, particularly selecting sub-spans of text and using that sub-span.
The primary example of this is use of regular expressions which, after a successful match, can provide
a set of :term:`capture groups`. It is beneficial for performance to not allocate capture groups
buffers per application, but per transaction. This requires tracking the maximum number of groups
needed for a configuration. In addition, for configuration checking, it is useful to note when a
capture group is used but can't be available at run time (e.g. there is no match from which to get
the group).

Each expression tracks the maximum group index used by that expression. This can then be tracked
over all expressions in a configuration to compute the maximum group count required. The expression
constructors should update the maximum group index. Care must be taken if the embedded variants are
modified directly.

Evaluation
==========

Expressions are evaluated by the :txb:`Context` via :txb:`Context::extract`. This does some set up
and then calls :txb:`Expr::evaluator` as a visitor to the internal expression variant. Then the
modifiers are applied. The evaluator is passed a reference to the :txb:`Context` instance so there
is quite a bit of cross coordination between the context and the expression. One of the key bits
is :txb:`Context::ArgPack`. This is constructed from the context and provides the indexed arguments
to expression evaluation. This is opaque from the expression point of view - the evaluator constructs
it from the context and passes it to the :code:`BufferWriter` formatter.
