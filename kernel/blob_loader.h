#ifndef BEEROS_BLOB_LOADER_H
#define BEEROS_BLOB_LOADER_H

/* Read, compile and run every form of an embedded .beer source (an
 * `xxd -i` array). If home_ns is non-NULL it is made current for the
 * load (so `def`s land there); the caller's current namespace is
 * restored afterwards either way, since a file's own (ns ...) form also
 * switches it. Errors are reported on the UART prefixed with `name`. */
void beer_load_blob(const unsigned char* data, unsigned int len,
                    const char* name, const char* home_ns);

#endif
