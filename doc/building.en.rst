.. include:: common.defs

.. highlight:: text
.. default-domain:: cpp

.. _building:

***************
Building
***************

|TxB| building is based on `SCons <https://scons.org>`__ and `Parts
<https://pypi.org/project/scons-parts/>`__. As a plugin, |TxB| also requires an instance of `Traffic
Server <https://trafficserver.apache.org>`__, or at least the plugin API header files.

Other dependencies

*  `pcre2 <https://www.pcre.org>`__

   Generally ``sudo dnf install pcre2-devel`` or ``sudo yum install pcre2-devel``.

*  Scons/Parts

   As mentioned above, |TxB| is built using the Scons build tool. This is distributed as a Python
   package. A root level ``Pipenv`` is provided with which a Python virtual environment containing
   the required Scons dependencies can be created. Simply run ``pipenv install`` to create the
   environment followed by ``pipenv shell`` to enter it. Alternatively you can install the required
   scons-parts package into your PATH's Python 3 environment via ::

      python3 -m pip install --user scons-parts

   It is important to use `Python 3 <https://www.python.org/download/releases/3.0/>`__ - no attempt
   has been made for Python 2 compabitility. It may be necessary to edit the "scons" script for this
   reason. This should be "~/.local/bin/scons". If scons or parts doesn't seem to be found, make
   sure the first line in that file has "python3" and not just "python".

To build |TxB|, first build and install Traffic Server. When configuring Traffic Server, specify
that its YAML headers should be exported via the ``--enable-yaml-headers`` configuration option.
Once Traffic Server is built, build |TxB| using the following command ::

   scons txn_box --with-trafficserver=<ts_install_path>

where ``<ts_install_path>`` is the path to a Traffic Server install. In general this will be the
same path as used for the ``prefix`` configuration option in Traffic Server.

By default the system OpenSSL library will be used. This can be overridden by using another option ::

   scons txn_box --with-trafficserver=<ts_install_path> --with-ssl=<openssl_install_path>

This is intended to work identically to the ``--with-ssl`` for Traffic Server so the option can by cut and
pasted from that build.

