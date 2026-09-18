// See res_man.h for the format and how it was verified.

#include "res_man.h"
#include "lzss.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RES_HEADER_COUNT 256
#define RES_ENTRY_SIZE 23
#define RES_NAME_LEN 9

typedef struct {
	char name[RES_NAME_LEN + 1]; // +1 for a null terminator, for easy strcmp
	long offset;
	long length;         // on-disk (possibly compressed) size
	long original_size;  // decompressed size
	int key;              // 0 = stored uncompressed, nonzero = LZSS-compressed
	int used;              // 1 if this slot has a non-empty name
} res_entry_t;

static FILE *s_res_file = NULL;
static res_entry_t s_entries[RES_HEADER_COUNT];

static long read_le32(const unsigned char *p) {
	return (long) ((unsigned long) p[0] | ((unsigned long) p[1] << 8)
			| ((unsigned long) p[2] << 16) | ((unsigned long) p[3] << 24));
}

static int read_le16(const unsigned char *p) {
	return (int) ((unsigned int) p[0] | ((unsigned int) p[1] << 8));
}

void res_close(void) {
	if (s_res_file) {
		fclose(s_res_file);
		s_res_file = NULL;
	}
}

int res_open(const char *path) {
	unsigned char header_block[RES_HEADER_COUNT * RES_ENTRY_SIZE];
	unsigned char key;
	size_t i;

	res_close();

	s_res_file = fopen(path, "rb");
	if (!s_res_file) {
		return -1;
	}

	if (fread(header_block, 1, sizeof(header_block), s_res_file) != sizeof(header_block)) {
		res_close();
		return -1;
	}

	// XOR-decrypt: running key starting at 128, +1 per byte, wrapping mod 256 (free, since
	// `key` is an unsigned char), applied as one continuous stream across the whole header
	// block -- not reset per entry. Verified in got-android-port-notes_1.md section 2.
	key = 128;
	for (i = 0; i < sizeof(header_block); ++i) {
		header_block[i] = (unsigned char) (header_block[i] ^ key);
		++key;
	}

	for (i = 0; i < RES_HEADER_COUNT; ++i) {
		const unsigned char *entry = header_block + i * RES_ENTRY_SIZE;
		res_entry_t *dst = &s_entries[i];
		memcpy(dst->name, entry, RES_NAME_LEN);
		dst->name[RES_NAME_LEN] = '\0';
		dst->offset = read_le32(entry + 9);
		dst->length = read_le32(entry + 13);
		dst->original_size = read_le32(entry + 17);
		dst->key = read_le16(entry + 21);
		dst->used = dst->name[0] != '\0';
	}

	return 0;
}

// Debugging note: this used to collapse every failure into a single -1, which was enough to prove
// *that* ACTOR44/ACTOR14 were failing (got-android-port-notes_1.md section 10 onward) but not
// *why* -- an offline replay of this exact function against a byte-identical, fully-verified-
// complete copy of the same GOTRES.DAT succeeded cleanly for both names (len=5200, valid
// dimensions), yet the on-device app still logs "read failed (-1)" for exactly those two, every
// run, with the file's own copy now independently proven complete (see notes section 12). That
// rules out a truncated/incomplete file and a res_man.c logic bug (this exact code already reads
// them fine offline) -- so the remaining question is *which* of this function's four distinct
// failure modes is actually happening on-device, which the old single -1 return couldn't
// distinguish. Now each mode returns its own code; got_load_sprite()'s existing
// `LOGI("... read failed (%ld)", res_name, len)` call needs no changes to surface whichever one
// fires next time.
long res_read(const char *name, unsigned char **out_buf) {
	int i;
	const res_entry_t *found = NULL;
	unsigned char *raw;
	unsigned char *decoded;
	long decoded_len;

	if (!s_res_file) {
		return -1; // res_open() never succeeded (or wasn't called) before this res_read()
	}

	for (i = 0; i < RES_HEADER_COUNT; ++i) {
		if (s_entries[i].used && strcmp(s_entries[i].name, name) == 0) {
			found = &s_entries[i];
			break;
		}
	}
	if (!found) {
		return -2; // name not present in the 256-entry header table this res_open() decoded
	}

	raw = (unsigned char *) malloc((size_t) found->length);
	if (!raw) {
		return -3; // malloc(found->length) failed
	}
	if (fseek(s_res_file, found->offset, SEEK_SET) != 0
			|| fread(raw, 1, (size_t) found->length, s_res_file) != (size_t) found->length) {
		free(raw);
		return -4; // fseek() to found->offset, or fread() of found->length bytes, failed/short-read
	}

	if (found->key == 0) {
		// Stored uncompressed -- length == original_size, the raw bytes are the answer.
		*out_buf = raw;
		return found->length;
	}

	decoded_len = lzss_decompress(raw, found->length, &decoded);
	free(raw);
	if (decoded_len < 0) {
		return -5; // lzss_decompress() rejected the compressed bytes just read
	}
	*out_buf = decoded;
	return decoded_len;
}
