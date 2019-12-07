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
client request during the "send proxy response", as it would have no observable effect. Conversely
the proxy response can't be changed during the "read client request" hook because the proxy response
doesn't exist.

This is the basic use of the directives. To set the field "Best-Band" to the string "Delain" in the
proxy request ::

   preq-field<Best-Band>: "Delain"

The value can be a feature string. For instance, to set the field "TLS-Source" to the SNI name and
the client IP address ::

   preq-field<TLS-Source>: "{cssn-sni}@{cssn-remote-addr}"

For a connection that had an SNI of "delain.nl" from the address 10.12.97.156, the proxy request
would have "TLS-Source: delain.nl@10.12.97.256".

Consider the case where various requests get remapped to the same upstream host name, but the upstream
needs the value of the "Host" field from the original request. This could be done as ::

   preq-field<Org-Host>: creq-field<Host>

If this was intended for debugging and more human readable, it could be done as ::

   preq-field<Org-Host>: "Original host was {creq-field<Host>}"

Another common use case is to have a default value. For instance, set the field "Accept-Encoding" to
"identity" if not already set. ::

   preq-field<Accept-Encoding>: [ preq-field<Accept-Encoding> , { else: "identity" } ]

This assigns to "Accept-Encoding" as before, but the modifier :code:`else` is applied after
retrieving the current value of that field. This modifier keeps the original value unless it's
empty, in which case it uses its own value.

A field can also be removed by assigning it the :code:`NULL` value. To remove the "X-Forwarded-For"
field from the client request ::

   creq-field<X-Forwarded-For>: NULL

Note this is distinct from assigning the string "NULL" ::

   creq-field<X-Forwarded-For>: "NULL"

and not the same as assigning the empty string, such that the field is present but without a value ::

   creq-field<X-Forwarded-For>: ""

or ::

   creq-field<X-Forwarded-For>:

This can be done even more elaborately by trying to use the client request "Host" field value and
if that's not present, using the host from the client request URL. ::

   preq-field<Org-Host>: [ creq-field<Host> , { else: creq-host } ]

Finally, if for some reason it was needed to set the "Old-Path" field in the proxy request to the
value for "Forwarded", and if that is not set, to the value for "X-Forwarded-For", and if that's not
set either, to the value of "Client-IP", and if none of those, to "INVALID", one could do ::

   preq-field<Old-Path>:
   - creq-field<Forwarded>
   - { else: creq-field<X-Forwarded-For> }
   - { else: creq-field<Client-IP> }
   - { else: "INVALID" }

Using Variables
***************

For each transaction, |TxB| supports a set of named variables. The names can be arbitrary strings
and the value any feature. A variable is set using the :txb:directive:`var` directive with an
argument of the variable name and the value a feature. To set the variable "Best-Band" to "Delain" ::

   var<Best-Band>: "Delain"

To later set the field "X-Best-Band" to the value of that variable ::

   preq-field<X-Best-Band>: var<Best-Band>

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

   when: creq
   do:
      var<pristine-host>: creq-host

A specific use case for this is handling cross site scripting fields, where these should be set
unless the original request was to the static image server at "images.txnbox", which may have been
remapped to a different upstream shard, changing the host in the client request. This could be done
by selecting on the "pristine-host" variable and setting the cross site fields if that is not
"image.txnbox" or a subdomain of it. ::

   when: prsp
   do:
      with: var<pristine-host>
      select:
      -  none-of:
         -  match: "image.txnbox" # Exactly this domain
         -  suffix: ".image.txnbox" # any subdomain
         do:
         -  prsp-field<Expect-CT>: "max-age=31536000, report-uri=\"http://csp.txnbox\""
         -  prsp-field<X-XSS-Protection>: "1; mode=block"

Variables can be used to simplify configurations, if there is a complex configuration needed in
multiple places, the results can be placed in a variable and then that variable's value used later,
avoiding much of the complexity. For instance, remap rules could set a variable as a flag to
indicate which remap rule triggered.

Diverting Traffic
*****************

These examples are about sending inbound traffic to different upstream destination.

.. rubric:: Footnotes

.. [#]

   These are similar to the "@" headers for core |TS|, but don't have any name restriction and are
   not related to any specific header. They are stored entirely inside the |TxB| plugin.
