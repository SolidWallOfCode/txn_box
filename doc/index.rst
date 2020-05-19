.. include:: common.defs

***************
Transaction Box
***************

Transaction Box, or "txn_box", is an Apache Traffic Server plugin to manipulate
:term:`transaction`\s. The functionality is based on requests I have received over the years from
users and admnistrators for |TS|. The primary points of interest are

*  YAML based configuration.
*  Interaction between global and remap hooks.
*  Consistent data handling for all uses.
*  Conditional actions based on a wide variety of transaction property.
*  A variety of comparison operations.
*  Transaction local variables.
*  Static file support.
*  IP address database support.

.. toctree::
   :maxdepth: 2

   txn_box.en
   building.en
   expr.en.rst
   directive.en
   selection.en
   guide.en
   examples.en
   arch.en
   user/ExtractorReference.en
   user/DirectiveReference.en
   user/ComparisonReference.en
   user/ModifierReference.en
   future.en
   misc.en
   dev/design.en

Reference
*********

.. toctree::
   :maxdepth: 1

   reference.en
