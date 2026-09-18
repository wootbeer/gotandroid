#ifndef GOT_RES_MAN_H_
#define GOT_RES_MAN_H_

// Minimal C port of GOTRES.DAT resource-archive reading: header-table parsing, the XOR decrypt,
// and (via lzss.h) LZSS decompression for compressed entries. The format is fully verified
// against a real GOTRES.DAT -- see got-android-port-notes_1.md sections 2, 2a, and 5 for the
// original Python verification this is a straight port of, not new reverse-engineering.
//
// RES_HEADER on disk is 23 bytes, tightly packed: char name[9]; long offset; long length;
// long original_size; int key (0 = stored uncompressed, nonzero = LZSS-compressed). 256 fixed
// slots make up the header table; empty slots have an all-zero/empty name and are skipped.

// Opens the archive at `path`, reads and decrypts its 256-entry header table into memory.
// Returns 0 on success, -1 if the file can't be opened or read (e.g. wrong path, or a truncated
// file). Call once before res_read(); safe to call again later to switch to a different archive
// (closes whatever was previously open first).
int res_open(const char *path);

// Closes the archive opened by res_open(). Safe to call even if res_open() was never called or
// already failed.
void res_close(void);

// Looks up `name` (case-sensitive, e.g. "BPICS1", up to 9 characters) in the archive, reads its
// raw bytes, and -- if the entry's header says it's LZSS-compressed -- decompresses them. On
// success returns the decoded byte count and sets *out_buf to a malloc()'d buffer the caller
// must free() (*out_buf is left untouched on any failure below). On failure returns a negative
// code identifying which step failed, added when a real on-device failure (see
// got-android-port-notes_1.md section 12) needed more than a single generic -1 to diagnose:
//   -1  res_open() hasn't succeeded yet (or wasn't called)
//   -2  `name` isn't present in the archive's header table
//   -3  malloc() of the entry's on-disk length failed
//   -4  fseek() to the entry's offset, or fread() of its length, failed or came up short
//   -5  the entry's header says it's LZSS-compressed, and lzss_decompress() rejected the bytes
long res_read(const char *name, unsigned char **out_buf);

#endif
