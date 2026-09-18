// See lzss.h for the format and how it was verified.

#include "lzss.h"

#include <stdlib.h>

long lzss_decompress(const unsigned char *src, long compressed_len, unsigned char **out_buf) {
	long decompressed_size;
	long pos;
	unsigned char *out;
	long written;

	if (compressed_len < 4) {
		return -1;
	}

	// [2 bytes LE: decompressed_size][2 bytes LE: unused, always 0x0001]
	decompressed_size = (long) src[0] | ((long) src[1] << 8);
	pos = 4;

	out = (unsigned char *) malloc((size_t) decompressed_size);
	if (!out) {
		return -1;
	}
	written = 0;

	while (written < decompressed_size && pos < compressed_len) {
		unsigned char control_byte = src[pos++];
		int bit;
		for (bit = 0; bit < 8; ++bit) {
			if (written >= decompressed_size || pos >= compressed_len) {
				break;
			}
			if ((control_byte >> bit) & 1) {
				// bit=1 -> copy next literal byte from src to dst
				out[written++] = src[pos++];
			} else {
				// bit=0 -> next 2 bytes LE are `control`: count = (control>>12)+2 (range
				// 2-17), offset = control & 0xFFF (back-distance in dst). Copy `count` bytes
				// one at a time -- self-overlapping runs are allowed and required: each newly
				// written byte can become the source for a later copy in the very same run.
				unsigned int control;
				int count;
				unsigned int offset;
				int i;

				if (pos + 1 >= compressed_len) {
					free(out);
					return -1;
				}
				control = (unsigned int) src[pos] | ((unsigned int) src[pos + 1] << 8);
				pos += 2;
				count = (int) (control >> 12) + 2;
				offset = control & 0xFFFu;

				if (offset == 0 || (long) offset > written) {
					free(out);
					return -1;
				}
				for (i = 0; i < count && written < decompressed_size; ++i) {
					out[written] = out[written - (long) offset];
					++written;
				}
			}
		}
	}

	*out_buf = out;
	return written;
}
