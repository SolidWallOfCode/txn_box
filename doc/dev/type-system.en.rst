.. Copyright 2022, LinkedIn
   SPDX-License-Identifier: Apache-2.0

.. include:: /common.defs

.. highlight:: cpp
.. default-domain:: txb

.. _common_types:

Types
*****

|TxB| supports a basic set of types. These are encapsulated in the :txb:`Feature`. This is a variant and therefor carries internal type information. There is an enum, :txb:`ValueType`, which is used to indicate a value type outside of a :code:`Feature` instance. This is converted to a :code:`Feature` index by the function :txb:`IndexFor`. This is a compile type computation.

The currently supported types are

:code:`NO_VALUE`
   Uninitialized value. This indicates a :code:`Feature` that has not be constructed / initialized. Any function or method that returns :code:`Feature` must *never* return an instance with this index.

:code:`NIL`
   Externally defined no value. This is used for configuration and other externally visible values. Functions or methods that return :code:`Feature` can use this to indicate failure or absence. For instance, if a type conversion such as string to integer fails, this should be returned. The YAML keyword "NULL" is rendered as a :code:`nil` :code`Feature`.

:code:`BOOLEAN`
   A boolean value, modeled by `bool`.

:code:`INTEGER`
   A signed integer value, modeled by :code:`intmax_t`, that is the largest native integer.

:code:`STRING`
   A string of ASCII characters. Modeled as :code:`FeatureView`. The handling of strings is complex and will be more fully described later.

:code:`FLOAT`
   A floating point value, modeled by :code:`double`.

:code:`IP_ADDR`
   An IP address, IPv4 or IPv6. Modeled as :code:`swoc::IPAddr`.

:code:`DURATION`
   Time duration, modeled as :code:`std::chrono::nanoseconds`.

:code:`TIMEPOINT`
   Time point, modeled as :code:`std::chrono::time_point`.

:code:`CONS`
   A "cons" cell, which contains two instances of :code:`Feature`. Modeled as :code:`Cons *`.

:code:`TUPLE`
   A sequence of :code:`Feature` instances. Modeled as :code:`swoc::MemSpan<Feature>`.

:code:`GENERIC`
   Escape clause, a type used for future extensions. Modeled as :code:`Generic *`.

FeatureView
===========

Strings are modeled as instances of :txb:`FeatureView`. This contains a :code:`swoc::TextView` and additional metadata. The most challenging aspect of strings is memory management. For most other feature types the entire value can be embedded directly in the :code:`Feature`. This is not possible for strings. There are three possible locations for string memory.

transient
   Stack or other memory that has a short lifetime. Such a view cannot be trusted outside of the function directly manipulating it.

literal
   Stored in process static, configuration or transaction memory.

direct
   Externallly controlled memory which is somewhat stable. The most common example is the value of an HTTP field in a |TS| HTTP header. Immediate strings are expected to be stable across a callback, but not further.

A view can never be both literal and direct. In general views should be transient transiently, either not used outside a function scope (but reasonable to pass to nested functions). If a view needs to persist outside the scope of the function that created it, it needs to be literal or direct (and almost always literal). Both :txb:`Config` and :txn:`Context` support varioius "localize" methods. These convert a transient view into a literal view in (respectively) configuration or transaction context storage. A view can be created as a literal which is correct when creating from process static data such as a literal string. It is reasonable to localize a literal string - the localization will be ignored. This makes various string handling logics easier to implement because there is no need to check the view before localizing.


Obscure Details
===============

Much of the work of supporting the feature types is done with meta-programming. These are brief
descriptions of those support metatypes and uses. This is deep infrastructure and isn't needed to
actually use the type system.

:code:`FeatureTypeList`
   A list of of the types in :code:`Feature`. This is used to construct the :code:`Feature` type and other support metafunctions. This is the true definition of what types are supported. Any changes to supported types starts here and then other data structures are adjusted to match. This must always exactly match :code:`ValueType`.
