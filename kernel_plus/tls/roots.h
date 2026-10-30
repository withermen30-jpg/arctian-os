#ifndef ARCTIAN_TLS_ROOTS_H
#define ARCTIAN_TLS_ROOTS_H

/* Mozilla CA bundle (PEM) - tools/mk_roots.py ile uretilir.
 * mbedtls_x509_crt_parse() bu tamponu tek cagrida yukler. */
extern const unsigned char ca_bundle_pem[];
extern const unsigned int  ca_bundle_pem_len;

#endif
