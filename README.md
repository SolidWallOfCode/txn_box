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

## Tests

### Automated End to End

The automated end to end tests are written using the
[AuTest](https://bitbucket.org/autestsuite/reusable-gold-testing-system/src/master/)
test framework. To run the tests, build the following required dependencies:

1. Build the `txn_box` Sconstruct target.
1. Build [traffic_server](https://github.com/apache/trafficserver)
1. Build [Proxy Verifier](https://github.com/yahoo/proxy-verifier)

Now, run the tests like so:

```
cd test/autest/
./autest.sh \
  --autest-site <path/to/trafficserver>/tests/gold_tests/autest-site \
  gold_tests/autest-site/ \
  --ats-bin <path/to/built/trafficserver/bin> \
  --verifier-bin <path/to/built/proxy-verifier/bin>
```

After the first invocation of this, the pipenv shell is created. The tests can
be re-run more quickly from within the shell like so:

```
# Still within test/autest/
autest \
  -D gold_tests/
  --autest-site <path/to/trafficserver>/tests/gold_tests/autest-site \
  gold_tests/autest-site/ \
  --ats-bin <path/to/built/trafficserver>/bin \
  --verifier-bin <path/to/built/proxy-verifier>/bin
```
