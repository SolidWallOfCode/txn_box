.. include:: /common.defs

.. highlight:: yaml
.. default-domain:: cpp

.. _guide:

***********
Usage Guide
***********

This section focuses on tasks rather than mechanism, to illustrate how to use the mechanisms.

Working with HTTP fields
************************

A main use of |TxB| is to manipulate the fields in the HTTP header. This done using sets of four
directives and extractors, one for each of the transaction headers - client request, proxy request,
upstream response, and proxy response. If a particular directive or extractor is not allowed on a
hook, that indicates it's not useful. For instance, there is no use in changing anything in the
client request during the "send proxy response" hook, as it would have no observable effect.
Conversely the proxy response can't be changed during the "read client request" hook because the
proxy response doesn't exist.

To set the field "Best-Band" to the string "Delain" in the proxy request ::

   proxy-req-field<Best-Band>: "Delain"

The value is a feature expression, but most yield a string or a tuple of string. For instance, to
set the field "TLS-Source" to the SNI name and the client IP address ::

   proxy-req-field<TLS-Source>: "{inbound-sni}@{inbound-addr-remote}"

For a connection that had an SNI of "delain.nl" from the address 10.12.97.156, the proxy request
would have "TLS-Source: delain.nl@10.12.97.256".

Consider the case where various requests get remapped to the same upstream host name, but the upstream
needs the value of the "Host" field from the original request. This could be done as ::

   proxy-req-field<Org-Host>: ua-req-field<Host>

If this was intended for debugging and more human readable, it could be done as ::

   proxy-req-field<Org-Host>: "Original host was {ua-req-field<Host>}"

Another common use case is to have a default value. For instance, set the field "Accept-Encoding" to
"identity" if not already set. ::

   proxy-req-field<Accept-Encoding>: [ proxy-req-field<Accept-Encoding> , { else: "identity" } ]

This assigns to "Accept-Encoding" as before, but the modifier :txb:mod:`else` is applied after
retrieving the current value of that field. This modifier keeps the original value unless it's
empty, in which case it uses its own value.

Because the input is YAML, the previous example could also be written as ::

   proxy-req-field<Accept-Encoding>:
   - proxy-req-field<Accept-Encoding>
   - else: "identity"

From the |TxB| point of view, these are indistinguishable. In both cases the feature expresion is
a list of an unquoted string and an object, the first treated as an extactor and the second as
modifier. Further note the extractor being the same field as the directive is happenstance - it
could be any field, or any extractor or feature expression. This is how values can be easily copied
between fields.

A field can also be removed by assigning it the :code:`NULL` value. To remove the "X-Forwarded-For"
field from the client request ::

   ua-req-field<X-Forwarded-For>: NULL

Note this is distinct from assigning the string "NULL" ::

   ua-req-field<X-Forwarded-For>: "NULL"

and not the same as assigning the empty string, such that the field is present but without a value ::

   ua-req-field<X-Forwarded-For>: ""

This can be done even more elaborately by trying to use the client request "Host" field value and
if that's not present, using the host from the client request URL. ::

   proxy-req-field<Org-Host>: [ croxy-req-field<Host> , { else: creq-host } ]

Finally, if for some reason it was needed to set the "Old-Path" field in the proxy request to the
value for "Forwarded", and if that is not set, to the value for "X-Forwarded-For", and if that's not
set either, to the value of "Client-IP", and if none of those, to "INVALID", one could do ::

   proxy-req-field<Old-Path>:
   - ua-req-field<Forwarded>
   - { else: ua-req-field<X-Forwarded-For> }
   - { else: ua-req-field<Client-IP> }
   - { else: "INVALID" }

Rewriting URLs
**************

There are a number of ways to rewrite URLs in a client request. It can be done by specifying the
entire replacement URL or by changing it piecewise.

The primary directive for this is the :txb:drtv:`remap` directive. This always applies to
the proxy request, and takes a full URL as its value. The proxy request is updated to be to that
URL. If the existing URL is a full URL, it is changed to the URL in the value. Otherwise only
the path is copied over. If the value URL scheme is different, the request is modified to use
that scheme (e.g., if the value URL has "https://" then the proxy request will use TLS). The
"Host field is also updated to contain the host from the value URL.

For instance, to send the request to the upstream "app.txnbox" ::

   proxy-req-host: "app.txnbox"

This will change the host in the URL if already present and set the "Host" field. This could also
be done as ::

   proxy-req-url-host: "app.txnbox"
   proxy-req-field<Host>: "app-txnbox"

The difference is this will cause the host to be in the URL regardless if it was already present.

Using Variables
***************

For each transaction, |TxB| supports a set of named variables. The names can be arbitrary strings
and the value any feature. A variable is set using the :txb:directive:`var` directive with an
argument of the variable name and the value a feature. To set the variable "Best-Band" to "Delain" ::

   var<Best-Band>: "Delain"

To later set the field "X-Best-Band" to the value of that variable ::

   proxy-req-field<X-Best-Band>: var<Best-Band>

Note variables are not fields in the HTTP transaction, they are entirely an internal feature of
|TxB|. In the preceeding example, there is only a relationship between the variable "Best-Band" and
the proxy request field "X-Best-Band" because of the explicit assignment. If either is changed
later, the other is not [#]_. Each transaction starts with no variables set, variables do not carry
over from one transaction to any other.

One common use case for variables is to cache a value in an early hook for use in a later hook. Note
there is only one transaction name space for variables and variables set in global hooks are
available in remap and vice versa. This is handy if some remap behavior should depend on the
original client request URL or host, and not on the post-remap one. This can be done, in a limited
way, with the "proxy.config.http.pristine_host_header" configuration, but that has other potential
side effects and may not be usable because of other constraints. In contrast, caching the original
host name in a variable is easy ::

   when: ua-req
   do:
      var<pristine-host>: uareq-host

A specific use case for this is handling cross site scripting fields, where these should be set
unless the original request was to the static image server at "images.txnbox", which may have been
remapped to a different upstream shard, changing the host in the client request. This could be done
by selecting on the "pristine-host" variable and setting the cross site fields if that is not
"image.txnbox" or a subdomain of it. ::

   when: proxy-sp
   do:
      with: var<pristine-host>
      select:
      -  none-of:
         -  tld: "image.txnbox" # This domain or any subdomain
         do:
         -  proxy-rsp-field<Expect-CT>: "max-age=31536000, report-uri=\"http://csp.txnbox\""
         -  proxy-rsp-field<X-XSS-Protection>: "1; mode=block"

Variables can be used to simplify configurations, if there is a complex configuration needed in
multiple places, the results can be placed in a variable and then that variable's value used later,
avoiding much of the complexity. For instance, remap rules could set a variable as a flag to
indicate which remap rule triggered.

Diverting Traffic
*****************

Requests can be routed to one of a set of upstreams in a deterministic or random fashion. This can
be used for A/B testing or for gradually ramping traffic from one set of upstreams to another.

The random mechanism uses the :txb:extractor:`random` extractor. This generates a random integer
in a range which is used to select the specific upstream. This enables sending a specific fraction
of traffic to each upstream. A common case is ramping up to transition from one service to another.
Suppose the current service was at "v1.app.txbox" and the new version at "v2.app.txnbox". To divert
5% of the traffic to the new upstream ::

   with: random # default is 0..99
   select:
   -  lt 5: # selected 5% of the time.
      do:
         proxy-req-host: "v2.app.txnbox"
   -  whatever:
      do:
         proxy-req-host: "v1.app.txnbox"


.. rubric:: Footnotes

.. [#]

   These are similar to the "@" headers for core |TS|, but don't have any name restriction and are
   not related to any specific header. They are stored entirely inside the |TxB| plugin.
