#ifndef BEEROS_BLOB_LOADER_H
#define BEEROS_BLOB_LOADER_H

/* Run every form of an embedded .beer source (an `xxd -i` array) with
 * beerlang's core_load_source_ns / core_load_source: in home_ns if given,
 * else in the current namespace; the current namespace is restored
 * afterwards. Errors are reported on the UART; returns false on one. */
bool beer_load_blob(const unsigned char* data, unsigned int len,
                    const char* name, const char* home_ns);

/* An embedded library: load it in ns and record ns in *loaded-libs*, so
 * (require 'ns) doesn't look for a file */
bool beer_load_lib(const unsigned char* data, unsigned int len,
                   const char* name, const char* ns);

#endif
