' Copyright 2020, Verizon Media
' SPDX-License-Identifier: Apache-2.0

.. include:: common.defs

.. highlight:: yaml
.. default-domain:: yaml

.. _expr:

Feature Expressions
*******************

A feature expression is an expression that specifies how to extract a feature. At run time, if the
feature is needed, the expression is applied and the result is the feature. Note the feature can be
a tuple or a list, which are considered the extracted feature. Expressions vary from simple to very
complex. Because |TxB| uses a `data flow <https://en.wikipedia.org/wiki/Dataflow_programming>`__
style, most feature manipulations are expected to be done by expressions. That is, a feature is
extracted and then successively modified until it is the desired data. Examples of this will be
shown later.

The fundamental mechanism for feature expressions is an :term:`extractor`. All feature expressions
consist of a sequence of extractors.

Basic Expressions
=================

|TxB| distinguishes between quoted and unquoted expressions. An unquoted expression is presumed to
be a single extractor. This is for convenience because a single extrator is by far the most common
form of a feature expression.

A quoted expression it is treated as a :term:`feature format` which is very similar to `Python
format strings <https://docs.python.org/3.4/library/string.html#format-string-syntax>`__ and can
contain a mixture of extractors and literal strings, although internally literal strings are really
extractors that always return that string. The underlying format structure is ::

   **{** [ *name* ] [ **:** [ *format* ] [ **:** *extension* ] ] **}**

:code:`name` specifies the extractor, :code:`format` makes it possible to control the output format
(such as width), and :code:`extension` is used to pass extra data to the extractor.

When a feature is extracted, it is placed in to a holding container, the "active feature".  The
active feature can be extracted with the extractor "...".

A key notation is that quoted strings are treated as features strings, which can be just literal
strings. Values without quotes are treated as extractors.

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

Scoped Extractors
=================

There are sets of extractors that extract data from a :term:`scope`. A scope becomes :term:`active`
at a specific point in a configuratoin and remains active for all nested configuration. Multiple
scopes of the same type can be active simultaneously. Any scope extractors extract data from the
most recently / innermost scope. Numeric indices can be used as extractors for this innermost
active scope.

Regular Expression Scope
------------------------

If a regular expression comparison is successful matched, or explicitly applied, this creates a
scope where the capture groups are accessible via index extractors.These are numbered in the
standard way, with ``0`` meaning the entire matched string, ``1`` the first capture group, ``2`` the
second capture group, and so on. It is an error to use an index that is larger than the available
capture groups, or outside an active scope. For example if a header named "mail-check" should be set
if the host contains the domain "mail", it could be done as ::

   with: creq-host
   select:
   - rxp: "^(?:(.*?)[.])?mail(?:[.](.*?))?$"
      do:
      - preq-field<mail-check>: "You've got mail from {2}!"

This is a case where a feature string must be used instead of an unquoted extractor. That is ::

   preq-field<mail-check>: 2

sets the field to the integer value 2, which fails. Similarly ::

   preq-field<mail-check>: "2"

sets the field to the string "2". The correct usage is ::

   preq-field<mail-check>: "{2}"

sets the field to the second capture group. This can be a bit troublesome and so the extractor
:code:`rxp` is available. Thus an alternative working directive is ::

   pref-field<mail-check>: rxp<2>

The :code:`rxp` extractor is primarily useful when there is another active scope that is not
a regular expression scope. :code:`rxp` enables reaching back to the innermost scope that is a
regular expression. THis is somewhat unusual to encounter in practive and normally using the index
in a feature format is sufficient.



Comparison Scope
----------------

======================================
| Name           | Feature           |
======================================
| ...            | Selection Feature |
======================================
