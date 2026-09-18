#ifndef GOT_LZSS_H_
#define GOT_LZSS_H_

// Decompressor for GOTRES.DAT's LZSS variant. NOT the classic Okumura-style LZSS -- see
// got-android-port-notes_1.md section 5 for the format and how it was derived/verified (an
// independent Python implementation matched a known-good decompressed SDAT1 at 99.9%, with the
// small remainder explained by the two source files being slightly different game releases, not
// a decode bug). This is a straight C port of that already-working logic -- decompression only,
// matching the original source's own lzss.c (its lzss_compress() was an unimplemented stub too;
// this port only ever reads the user's existing GOTRES.DAT, never writes one).

// Decompresses `compressed_len` bytes at `src` into a newly malloc()'d buffer written to
// *out_buf, which the caller must free(). On success returns the decompressed byte count
// (matching the size the compressed stream's own 2-byte header declares). Returns -1 on any
// malformed input (truncated stream, a back-reference pointing before the start of output) and
// leaves *out_buf untouched.
long lzss_decompress(const unsigned char *src, long compressed_len, unsigned char **out_buf);

#endif
