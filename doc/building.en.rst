.. include:: common.defs

.. highlight:: text
.. default-domain:: cpp

.. _building:

***************
Buiding
***************

|TxB| building is based on `SCons <https://scons.org>`__ and `Parts
<https://pypi.org/project/scons-parts/>`__. As a plugin, |TxB| also requires an instance of `Traffic
Server <https://trafficserver.apache.org>`__, or at least the plugin API header files.

To build the plugin, first build or install Traffic Server. Then use the command ::

   scons txn_box --with-trafficserver=<ts_path>

where ``<ts_path`` is the path to a Traffic Server install. In general this wil be the same
path as used for the ``prefix`` configuration option in Traffic Server.
