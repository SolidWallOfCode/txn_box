.. include:: common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _directive:

Directives
**********

Directives are implemented by subclass :txb:`Directive`. The general convention is to prefix
the directive name with "Do\_" to form a class name.

Example
=======

set-preq-host
+++++++++++++

Consider a directive to set the host for the proxy request. This will update the header, not just
the URL. A name for this directive would be "set-preq-host". The standard class name would be
"Do_set_preq_host". Here is the full class declaration

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: class Do_set_preq_host
   :end-at: };

Each directive has a key that is the name of the directive, it is best to declare this as a
:code:`const` :code:`std::string`.

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: class Do_set_preq_host
   :lines: 4-6
   :emphasize-lines: 2

Because it's a static and not :code:`constexpr` it must be initialized out of line, after the class
declaration.

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: "set-preq-host"
   :lines: 1

The directive must also specify the set of hooks for which it is valid. For this directive, it
is not usable before the proxy request is created, and useless after the proxy request has been
sent upstream. The class :txb:`HookMask` is used to hold a bit mask of the valid hooks. A static
instance is declared in the class

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: class Do_set_preq_host
   :lines: 4-6
   :emphasize-lines: 3

and externally it is defined to contain the appropriate hook bits.

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: Do_set_preq_host::HOOKS
   :lines: 1

To make specifying the bit mask easier, the :txb:`MaskFor` function is overloaded to compute the
mask from the :txb:`hook enumeration values <Hook>`. One overload takes a single enumeration for cases where only
one hook is valid. The other overload takes a list of enumerations and sets the bit for each one.
In this case, several hooks need to be enabled and so the latter style is used to enable this
directive for the proxy request, pre-remap, and post-remap hooks.

The first requirement of the directive implementation is to handling loading the directive instance
from a YAML node. The framework, when expecting a directive, checks if the node is an object. If so,
the keys in the object are checked for being a directive by matching the key name with a table of
directive names. If there is a match, the corresponding load functor is invoked. Five arguments
are passed to the functor.

:code:`Config& cfg`
    A reference to the configuration object, which contains the configuration loading context. See :txb:`Config`.

:code:`YAML::Node const& drtv_node`
    The directive object. This is the object that contains the directive key.

:code:`TextView const& name`
    The name of the directive. This is the same as the value in the directive table. In most cases it
    is irrelevant. In the cases of a group of similar directives, a single load functor could load
    all of them, distinguishing the exact case with this value.

:code:`TextView const& arg`
    A directive key can have an argument, which is additional text attached to the directive name
    with a period separator. Although implementationally arbitrary, the convention is the argument
    (if any) should be used to select the target of the directive if the value can't be known at
    compile time. Data used to perform the directive should be in the value of the directive key.

:code:`YAML::Node const& key_value`
    The value of the directive key. Note this can be any type of YAML data, including nested objects.

For this directive, the directive would look like

.. code-block:: YAML
   :emphasize-lines: 2

   do:
   - set-preq-host: example.one
   - set-proxy-req-field.x-host-valid: true

The "do" key contains a list of directives, each of which is an object. The first such object is the
"set-preq-host" object. It will be invoked with :code:`drtv_node` being the object in the list for
"do", while :code:`name` will be "set-preq-host" and :code:`key_value` the value for the
"set-preq-host" key, that is "example.one". For illustrative purposes, a "set-proxy-req-field" directive
follows, which is handled in the same way. For that, :code:`drtv_node` is the second object in the
list, :code:`name` is "set-proxy-req-field", :code:`arg` is "x-host-valid", and :code:`key_value` is "true".

A more complex example would be

.. code-block:: YAML

   do:
   - redirect:
       location: "http://example.one"
       status: 302
       body: "Redirecting..."

In this case, :code:`key-value` is an object, with keys "location", "status", and "body", which must
be handled by the directive implementation.

Getting back to "set-preq-host", the easiest way to provide a functor is to create a static method
in the class and pass that as the functor.

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: class Do_set_preq_host
   :lines: 21-32

The functor is required to hand back a handle to the new instance and an :code:`Errata` instance,
packaged together as an instance of :code:`Rv<Directive::Handle>`. In practice, this pair will
contain either a valid handle and an empty (default constructed) :code:`Errata`, or a
:code:`nullptr` (default constructed) handle and an :code:`Errata` with error messages.

In addition, the value for the host should support standard feature extraction. This is the
responsibility of the directive implementation, for the rare cases where special handling is needed.
In this case, because that is not required, standard support mechanisms can be used. The
:txb:`Config` instance can be used to perform the required parsing of the feature string.

This is the implementation of the :code:`load` static method.

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: Do_set_preq_host::load
   :lines: 1-9
   :emphasize-lines: 2,7

In the first highlighted line, the configuration context is used to parse the feature format, which
is passed back as an :code:`Rv<Extractor::Expr>` pair. This type provides tuple support which, for
C++17, allows the return to picked apart easily into those two components. If there's a problem, an
additional note is added to the :code:`Errata` and then that is passed back. Otherwise the feature
format is marked to force a string output in the second highlighted line. E.g. if the extractor is
just an IP address, the string version of that is needed, not the raw IP address.

Note the configuration feature format parser will handle any extraction modifiers and embed them, as
needed, in the return feature format.

If successful, a new instance is created with the feature format and passed back, along with an
empty (successful) :code:`Errata`. The constructor implementation is trivial

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: Do_set_preq_host::Do_set_preq_host
   :lines: 1

At run time, if the directive is invoked, the :txb:`Do_set_preq_host::invoke` method is called. This
is passed a transaction context, a reference to an instance of :txb:`Context`. All run time
information that is available is accessible from this object. In particular, the actual Traffic
Server API transaction object is accessible.

As with configuration loading, feature extraction is the responsibility of the directive
implementation. And also as before, the context provides mechanisms for the common case. The
implementation for this method is

.. literalinclude:: ../plugin/src/Machinery.cc
   :start-at: Do_set_preq_host::invoke
   :lines: 1-10
   :emphasize-lines: 2

The highlighted line does the feature extraction using the context. This handles doing the binding
between extractors and the context, yielding the extracted feature. The string variant of this
can be safely extracted because it was forced to a string during configuration load.

Once the string is extracted, it is a simple matter of using the Traffic Server API wrappers to set
the host in the URL and also in the "Host" field, if it already exists. There aren't any error
conditions and so the method simply returns a default constructed / empty / successful
:code:`Errata`.

This constitutes the entirety of the implementation. For simple directives, there is not much work,
although some complex directives require a bit more intracite implementations.
