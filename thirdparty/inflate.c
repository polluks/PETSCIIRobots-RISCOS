#include "inflate.h"

#include <string.h>

#define MAXBITS 15
#define MAXLCODES 288   /* literals + length codes */
#define MAXDCODES 30    /* distance codes */

typedef struct {
    const uint8_t* in;
    uint32_t inSize;
    uint32_t inPos;

    uint8_t* out;
    uint32_t outSize;
    uint32_t outPos;

    uint32_t bitBuf;
    int bitCnt;
} Inflater;

static uint32_t crcTable[256];
static int tablesReady = 0;

static void buildTables(void)
{
    int k;
    uint32_t n, c;
    for (n = 0; n < 256; n++) {
        c = n;
        for (k = 0; k < 8; k++) {
            c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
        }
        crcTable[n] = c;
    }
    tablesReady = 1;
}

static int needBits(Inflater* z, int n)
{
    if (z->bitCnt < n) {
        while (z->bitCnt < n) {
            if (z->inPos >= z->inSize) return 0;
            z->bitBuf |= (uint32_t)z->in[z->inPos++] << z->bitCnt;
            z->bitCnt += 8;
        }
    }
    return 1;
}

static uint32_t takeBits(Inflater* z, int n)
{
    uint32_t v = z->bitBuf & ((1u << n) - 1);
    z->bitBuf >>= n;
    z->bitCnt -= n;
    return v;
}

/* Canonical Huffman table built from code lengths. */
typedef struct {
    const uint16_t* symbols;   /* symbols sorted by (length, symbol) */
    const uint16_t* count;     /* count[len], len = 1..15 */
} Huff;

/* Returns the next symbol, or -1 on corrupt input. */
static int huffDecode(Inflater* z, const Huff* h)
{
    int len;
    int count;
    int code = 0;
    int first = 0;
    int index = 0;

    for (len = 1; len <= MAXBITS; len++) {
        if (!needBits(z, 1)) return -1;
        code |= takeBits(z, 1);

        count = h->count[len];
        if (code - first < count) {
            return h->symbols[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* Validate code lengths (Koshy condition) and build count[] + symbol[] sort. */
static int buildHuff(const uint8_t* lengths, int num,
                     Huff* h, uint16_t* count, uint16_t* symbols)
{
    int i, len;
    int left;
    uint16_t offsets[MAXBITS + 1];
    int next[MAXBITS + 1];

    for (i = 0; i <= MAXBITS; i++) count[i] = 0;

    for (i = 0; i < num; i++) {
        if (lengths[i] > MAXBITS) return 0;
        count[lengths[i]]++;
    }
    if (count[0] == num) { /* empty code: leave as-is */
        h->symbols = symbols;
        h->count = count;
        return 1;
    }

    /* oversubscription check */
    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= count[len];
        if (left < 0) return 0;
    }

    /* build sorted symbol list */
    offsets[1] = 0;
    for (len = 1; len < MAXBITS; len++) offsets[1 + len] = offsets[len] + count[len];
    for (len = 1; len <= MAXBITS; len++) next[len] = offsets[len];

    for (i = 0; i < num; i++) {
        int len = lengths[i];
        if (len != 0) symbols[next[len]++] = (uint16_t)i;
    }

    /* complete (incomplete codes filled with zeros are not needed to decode) */
    h->symbols = symbols;
    h->count = count;
    return 1;
}

static int runLengths(Inflater* z, uint8_t* lengths, int n, const Huff* codeHuff)
{
    int i = 0;
    int rep;
    uint8_t val;
    while (i < n) {
        int sym = huffDecode(z, codeHuff);
        if (sym < 0) return 0;

        if (sym < 16) {
            lengths[i++] = (uint8_t)sym;
        } else if (sym == 16) {
            if (i == 0) return 0;
            if (!needBits(z, 2)) return 0;
            rep = 3 + takeBits(z, 2);
            val = lengths[i - 1];
            while (rep-- > 0 && i < n) lengths[i++] = val;
        } else if (sym == 17) {
            if (!needBits(z, 3)) return 0;
            rep = 3 + takeBits(z, 3);
            while (rep-- > 0 && i < n) lengths[i++] = 0;
        } else { /* 18 */
            if (!needBits(z, 7)) return 0;
            rep = 11 + takeBits(z, 7);
            while (rep-- > 0 && i < n) lengths[i++] = 0;
        }
    }
    return 1;
}

static const uint8_t lenExtra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t lenBase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t distExtra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static const uint16_t distBase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193,
    12289, 16385, 24577
};

static void fixedLengths(uint8_t* litLens, uint8_t* distLens)
{
    int i;
    for (i = 0; i < 144; i++) litLens[i] = 8;
    for (i = 144; i < 256; i++) litLens[i] = 9;
    for (i = 256; i < 280; i++) litLens[i] = 7;
    for (i = 280; i < 288 + 0; i++) if (i < 288) litLens[i] = 8;
    for (i = 0; i < 30; i++) distLens[i] = 5;
}

static int inflateBlock(Inflater* z, Huff* litHuff, Huff* distHuff)
{ int k;
    for (;;) {
        int sym = huffDecode(z, litHuff);
        if (sym < 0) return 0;

        if (sym < 256) {
            if (z->outPos >= z->outSize) return 0;
            z->out[z->outPos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 1;
        } else {
            int li;
            int len;
            int extraBits;
            int dsym;
            int dist;
            uint32_t src;

            li = sym - 257;
            if (li >= 29) return 0;

            len = lenBase[li];
            extraBits = lenExtra[li];
            if (!needBits(z, extraBits)) return 0;
            len += takeBits(z, extraBits);

            dsym = huffDecode(z, distHuff);
            if (dsym < 0 || dsym >= 30) return 0;

            dist = distBase[dsym];
            extraBits = distExtra[dsym];
            if (!needBits(z, extraBits)) return 0;
            dist += takeBits(z, extraBits);

            if (dist > (int)z->outPos) return 0;

            if (z->outPos + (uint32_t)len > z->outSize) return 0;
            src = z->outPos - dist;
            for (k = 0; k < len; k++) {
                z->out[z->outPos++] = z->out[src++];
            }
        }
    }
}

static int inflateRaw(Inflater* z)
{
    int i;
    for (;;) {
        int bfinal, btype;
        if (!needBits(z, 3)) return 0;
        bfinal = takeBits(z, 1);
        btype = takeBits(z, 2);

        if (btype == 0) {
            /* stored block: byte-aligned */
            uint16_t clen, nlen;
            z->bitBuf = 0;
            z->bitCnt = 0;
            if (z->inPos + 4 > z->inSize) return 0;
            clen = (uint16_t)(z->in[z->inPos] | (z->in[z->inPos + 1] << 8));
            nlen = (uint16_t)(z->in[z->inPos + 2] | (z->in[z->inPos + 3] << 8));
            z->inPos += 4;
            if ((clen ^ 0xffff) != nlen) return 0;
            if (z->outPos + clen > z->outSize) return 0;
            if (z->inPos + clen > z->inSize) return 0;
            memcpy(z->out + z->outPos, z->in + z->inPos, clen);
            z->outPos += clen;
            z->inPos += clen;
        } else {
            uint8_t litLens[MAXLCODES];
            uint8_t distLens[MAXDCODES];
            uint16_t litSymbols[MAXLCODES];
            uint16_t distSymbols[MAXDCODES];
            Huff litHuff, distHuff;
            uint16_t count[2 * (MAXBITS + 1)];   /* [0..15] lit, [16..31] dist */

            if (btype == 1) {
                fixedLengths(litLens, distLens);
            } else if (btype == 2) {
                int hlit, hdist, hclen;
                uint8_t codeLens[19];
                Huff codeHuff;
                uint16_t codeSymbols[19];
                uint16_t codeCount[MAXBITS + 1];
                static const uint8_t order[19] = {
                    16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                    11, 4, 12, 3, 13, 2, 14, 1, 15
                };

                if (!needBits(z, 14)) return 0;
                hlit = takeBits(z, 5) + 257;
                hdist = takeBits(z, 5) + 1;
                hclen = takeBits(z, 4) + 4;
                if (hlit > MAXLCODES || hdist > MAXDCODES) return 0;
                for (i = 0; i < 19; i++) codeLens[i] = 0;
                for (i = 0; i < hclen; i++) {
                    if (!needBits(z, 3)) return 0;
                    codeLens[order[i]] = takeBits(z, 3);
                }

                if (!buildHuff(codeLens, 19, &codeHuff, codeCount, codeSymbols)) return 0;

                if (!runLengths(z, litLens, hlit, &codeHuff)) return 0;
                if (!runLengths(z, distLens, hdist, &codeHuff)) return 0;
                for (i = hlit; i < MAXLCODES; i++) litLens[i] = 0;
                for (i = hdist; i < MAXDCODES; i++) distLens[i] = 0;
            } else {
                return 0;   /* reserved block type */
            }

            if (!buildHuff(litLens, 288, &litHuff, count + 0, litSymbols)) return 0;
            if (!buildHuff(distLens, 30, &distHuff, count + 16, distSymbols)) return 0;

            if (litHuff.count[0] == 288 && distHuff.count[0] == 30) {
                /* empty block: nothing emitted */
            } else {
                if (!inflateBlock(z, &litHuff, &distHuff)) return 0;
            }
        }

        if (bfinal) return 1;
    }
}

int inflate_mem(const uint8_t* in, uint32_t inSize, uint8_t* out,
                uint32_t outSize, uint32_t* bytesOut, int raw)
{
    const uint8_t* streamStart;
    Inflater z;
    uint8_t flg;
    uint32_t hdrLen;
    uint16_t xlen;

    if (!tablesReady) buildTables();

    streamStart = in;

    memset(&z, 0, sizeof(z));
    z.in = in;
    z.inSize = inSize;
    z.out = out;
    z.outSize = outSize;

    if (!raw) {
        if (inSize < 18) return 0;
        if (in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return 0;  /* gzip, deflate */
        flg = in[3];
        hdrLen = 10;
        if (flg & 0x04) {                       /* FEXTRA */
            if (hdrLen + 2 > inSize) return 0;
            xlen = (uint16_t)(in[hdrLen] | (in[hdrLen + 1] << 8));
            hdrLen += 2 + xlen;
        }
        if (flg & 0x08) {                       /* FNAME */
            while (hdrLen < inSize && in[hdrLen] != 0) hdrLen++;
            hdrLen++;
        }
        if (flg & 0x10) {                       /* FCOMMENT */
            while (hdrLen < inSize && in[hdrLen] != 0) hdrLen++;
            hdrLen++;
        }
        if (flg & 0x02) hdrLen += 2;            /* FHCRC */
        if (hdrLen > inSize) return 0;
        streamStart = in + hdrLen;
        z.in = streamStart;
        z.inSize = inSize - hdrLen;
        z.inPos = 0;
    }

    if (!inflateRaw(&z)) return 0;

    if (bytesOut) *bytesOut = z.outPos;

    if (!raw) {
        /* The 8-byte gzip trailer (CRC32 + ISIZE) sits at the end of the input. */
        uint32_t wantCrc, wantSize;
        uint32_t crc;
        uint32_t i;
        const uint8_t* tail;

        tail = in + inSize - 8;
        wantCrc = (uint32_t)tail[0] | ((uint32_t)tail[1] << 8) |
                           ((uint32_t)tail[2] << 16) | ((uint32_t)tail[3] << 24);
        wantSize = (uint32_t)tail[4] | ((uint32_t)tail[5] << 8) |
                            ((uint32_t)tail[6] << 16) | ((uint32_t)tail[7] << 24);

        /* The decoded stream must end exactly at the trailer. */
        if (streamStart + (size_t)z.inPos != in + inSize - 8) return 0;
        if (wantSize != z.outPos) return 0;      /* ISIZE must match output */

        crc = 0xffffffffu;
        for (i = 0; i < z.outPos; i++) {
            crc = crcTable[(crc ^ out[i]) & 0xffu] ^ (crc >> 8);
        }
        crc ^= 0xffffffffu;
        if (crc != wantCrc) return 0;
    }

    return 1;
}