# txn_box

> Transaction Box - a transaction tool box plugin for Apache Traffic Server.

For more detail see the [documentation](http://docs.solidwallofcode.com/txn_box/).

### Background

Transaction Box, or "txn_box", grew from several sources. The primary goals were to

*  Provide a better, single plugin to replace a variety of inconsistent plugins such as header_rewrite, regex_remap,
   ssl_headers, cookie_remap, cache_key, conf_remap, and others.

*  Be a test bed for restructuring Traffic Server remapping to take advantage of YAML.

*  Drive development of [libswoc++](http://github.com/SolidWallOfCode/libswoc). Nothing improves code like the author
   having to use it.
