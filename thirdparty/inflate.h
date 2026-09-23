#ifndef INFLATE_H_INCLUDED
#define INFLATE_H_INCLUDED

#include <stdint.h>

/*
 * Minimal gzip (RFC 1952) / raw DEFLATE (RFC 1951) decompressor.
 * Handles stored, fixed-Huffman and dynamic-Huffman blocks, LZ77 matches.
 *
 * Returns 1 on success, 0 on failure. On success *bytesOut is set to the
 * number of bytes written to `out`.
 *
 * Pass raw = 1 to skip the gzip header/trailer (data is a bare DEFLATE
 * stream). Otherwise a full gzip member (header + deflate + CRC32 + ISIZE)
 * is expected; the CRC is verified only if outputSize matches ISIZE.
 */
int inflate_mem(const uint8_t* in, uint32_t inSize, uint8_t* out,
                uint32_t outSize, uint32_t* bytesOut, int raw);

#endif