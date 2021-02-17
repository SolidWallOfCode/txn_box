# SPDX-License-Identifier: Apache-2.0
# Bash script to generate certificates for tls_client_verify3 Au test using the openssl command.

# the openssl.cnf file requires two environment variables
# PREFIX - the certificate prexifx. This is used to decorate the distinguished name.
# DN - this must be "CA" for root certs and "Signed" for signed certs.

let N=1 # signed certificate serial number.
for PREFIX in alpha bravo charlie delta
do
  # Generate the root cert key.
  rm -f ${PREFIX}-ca.key
  openssl genrsa -passout pass:12345678 -des3 -out ${PREFIX}-ca.key 2048

  # Create the root cert.
  rm -f ${PREFIX}-ca.pem
  PREFIX=${PREFIX} DN=CA openssl req -passin pass:12345678 -config ./openssl.cnf -x509 -new -nodes \
    -key ${PREFIX}-ca.key -sha256 -days 36500 -batch -out ${PREFIX}-ca.pem

  # Generate key for the signed cert.
  rm -f ${PREFIX}-signed.key
  PREFIX=${PREFIX} DN=Signed openssl genrsa -passout pass:12345678 -out ${PREFIX}-signed.key 2048

  # Generate the CSR for the signed cert.
  rm -f tmp.csr
  PREFIX=${PREFIX} DN=Signed openssl req -passin pass:12345678 -config ./openssl.cnf -new \
    -key ${PREFIX}-signed.key -batch -out tmp.csr

  # Generate the signed certificate.
  rm -f ${PREFIX}-signed.pem
  PREFIX=${PREFIX} DN=Signed openssl x509 -passin pass:12345678 -req -in tmp.csr -CA ${PREFIX}-ca.pem -CAkey ${PREFIX}-ca.key \
    -set_serial 0$N -out ${PREFIX}-signed.pem -days 36500 -sha256

  # cleanup.
  rm -f tmp.csr ${PREFIX}-ca.srl

  let N=N+1
done
