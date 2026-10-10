/*
 * T41 SwitchBin: request layout, request validation and bin validation
 * (host test, no kernel).  The real-bin case runs when the gc5603 bin of a
 * thingino build is given in T41_SAMPLE_BIN.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "../driver/t41/tx_isp_t41_switchbin.h"

#define BANK 0x1f008U

static const unsigned char VER[8] = "1.00";
static const unsigned char SOC[8] = "t41";

/* A well-formed bin: header, day bank, night bank, valid CRC. */
static unsigned char *make_bin(size_t *size_out, uint32_t night_off,
			       uint32_t declared)
{
	size_t size = 64 + declared;
	unsigned char *b = calloc(1, size);
	uint32_t crc, i;

	assert(b);
	memcpy(b, VER, 8);
	memcpy(b + 8, SOC, 8);
	for (i = 64; i < size; i++)
		b[i] = (unsigned char)(i * 31U + 7U);
	memcpy(b + T41_BIN_OFF_DECLARED, &declared, 4);
	memcpy(b + T41_BIN_OFF_NIGHT, &night_off, 4);
	crc = t41_bin_crc(b + 64, declared);
	memcpy(b + T41_BIN_OFF_CRC, &crc, 4);
	*size_out = size;
	return b;
}

static void fix_crc(unsigned char *b)
{
	uint32_t declared = t41_bin_rd32(b + T41_BIN_OFF_DECLARED);
	uint32_t crc = t41_bin_crc(b + 64, declared);

	memcpy(b + T41_BIN_OFF_CRC, &crc, 4);
}

int main(void)
{
	struct t41_switch_bin_req req;
	struct t41_bin_info info;
	const char *path;
	char dflt[132] = "/etc/sensor/gc5603-t41.bin";
	unsigned char *b, *c;
	size_t size;
	uint32_t v;
	int n = 0;

	/* ---- wire layout the stock kernel copies (132 bytes) ---- */
	assert(sizeof(req) == T41_SWITCH_BIN_REQ_BYTES);
	assert(sizeof(req) == 132);
	assert(offsetof(struct t41_switch_bin_req, path) == 4);
	assert(T41_SWITCH_BIN_CID == 0x080000a5U);

	/* ---- request validation ---- */
	memset(&req, 0, sizeof(req));
	req.enable = 1;
	strcpy(req.path, "/tmp/night-t41.bin");
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), &path) == 0);
	assert(path == req.path);
	n++;
	/* enable 0: back to the sensor's start path, request path ignored */
	req.enable = 0;
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), &path) == 0);
	assert(path == dflt);
	n++;
	/* no default known */
	assert(t41_switch_bin_req_check(&req, "", 1, &path) == -ENOENT);
	assert(t41_switch_bin_req_check(&req, NULL, 0, &path) == -ENOENT);
	{
		char unterminated[8];

		memset(unterminated, 'a', sizeof(unterminated));
		assert(t41_switch_bin_req_check(&req, unterminated,
						sizeof(unterminated), &path) == -ENOENT);
	}
	/* enable above 1 */
	req.enable = 2;
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), &path) == -EINVAL);
	req.enable = 0xffffffffU;
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), &path) == -EINVAL);
	/* empty and unterminated path */
	req.enable = 1;
	req.path[0] = '\0';
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), &path) == -EINVAL);
	memset(req.path, 'x', sizeof(req.path));
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), &path) == -ENAMETOOLONG);
	req.path[127] = '\0';                      /* 127 characters: fits */
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), &path) == 0);
	assert(t41_switch_bin_req_check(NULL, dflt, sizeof(dflt), &path) == -EINVAL);
	assert(t41_switch_bin_req_check(&req, dflt, sizeof(dflt), NULL) == -EINVAL);

	/* ---- CRC: stock word-xor with the 8-entry table ---- */
	{
		unsigned char z[16] = { 0 };
		unsigned char w[8] = { 1, 0, 0, 0, 2, 0, 0, 0 };

		assert(t41_bin_crc(z, 16) == 0);
		/* crc=1 ^ t[1]=0x77073096 -> 0x77073097; ^2 -> 0x77073095,
		 * ^ t[5]=0x706af48f -> 0x076dc41a */
		assert(t41_bin_crc(w, 8) == 0x076dc41aU);
		/* a partial trailing word is ignored */
		assert(t41_bin_crc(w, 7) == 0x77073097U);
	}

	/* ---- bin validation ---- */
	b = make_bin(&size, BANK, 2 * BANK);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == 0);
	assert(info.declared == 2 * BANK && info.night_off == BANK);
	n++;
	/* the loaded bin's version is accepted when the file carries it */
	memcpy(b, "2.00", 5);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EPROTO);
	assert(t41_bin_validate(b, size, VER, (const unsigned char *)"2.00\0\0\0\0",
				SOC, &info) == 0);
	assert(t41_bin_validate(b, size, VER, (const unsigned char *)"3.00\0\0\0\0",
				SOC, &info) == -EPROTO);
	memcpy(b, VER, 8);
	/* SoC tag */
	memcpy(b + 8, "t31", 4);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EPROTO);
	memcpy(b + 8, SOC, 8);
	/* one flipped payload bit: CRC mismatch */
	b[64 + 1000] ^= 0x10;
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EPROTO);
	b[64 + 1000] ^= 0x10;
	/* a flipped bit in the CRC word itself */
	b[T41_BIN_OFF_CRC] ^= 1;
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EPROTO);
	b[T41_BIN_OFF_CRC] ^= 1;
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == 0);
	/* too short */
	assert(t41_bin_validate(b, 63, VER, NULL, SOC, &info) == -EINVAL);
	assert(t41_bin_validate(b, 0, VER, NULL, SOC, &info) == -EINVAL);
	assert(t41_bin_validate(NULL, size, VER, NULL, SOC, &info) == -EINVAL);
	/* declared beyond the file, night offset beyond declared */
	v = 2 * BANK + 4;
	memcpy(b + T41_BIN_OFF_DECLARED, &v, 4);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EINVAL);
	v = 2 * BANK;
	memcpy(b + T41_BIN_OFF_DECLARED, &v, 4);
	v = 2 * BANK + 4;
	memcpy(b + T41_BIN_OFF_NIGHT, &v, 4);
	fix_crc(b);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EINVAL);
	/* a truncated file: header says 2 banks, only one is there */
	v = BANK;
	memcpy(b + T41_BIN_OFF_NIGHT, &v, 4);
	fix_crc(b);
	assert(t41_bin_validate(b, 64 + BANK, VER, NULL, SOC, &info) == -EINVAL);
	free(b);
	n++;

	/* banks the consumers would read past: the stock loader accepted these */
	b = make_bin(&size, 0x1000, 2 * BANK);          /* day bank too small */
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EINVAL);
	free(b);
	b = make_bin(&size, BANK, BANK + 0x1000);       /* night bank too small */
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EINVAL);
	free(b);
	b = make_bin(&size, 0, 2 * BANK);               /* night_off 0 */
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EINVAL);
	free(b);
	/* the minimum is exactly the highest fixed offset the driver reads */
	b = make_bin(&size, T41_BIN_BANK_MIN, 2 * T41_BIN_BANK_MIN);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == 0);
	free(b);
	b = make_bin(&size, T41_BIN_BANK_MIN - 4, 2 * T41_BIN_BANK_MIN);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == -EINVAL);
	free(b);
	/* declared words not a multiple of 4 still follow the stock CRC */
	b = make_bin(&size, BANK, 2 * BANK + 2);
	assert(t41_bin_validate(b, size, VER, NULL, SOC, &info) == 0);
	free(b);
	/* trailing bytes after the declared payload are ignored */
	b = make_bin(&size, BANK, 2 * BANK);
	c = realloc(b, size + 100);
	assert(c);
	assert(t41_bin_validate(c, size + 100, VER, NULL, SOC, &info) == 0);
	free(c);
	n += 3;

	/* ---- a real bin, when one is at hand ---- */
	{
		const char *real = getenv("T41_SAMPLE_BIN");
		FILE *f = real ? fopen(real, "rb") : NULL;

		if (f) {
			unsigned char *buf = malloc(1 << 20);
			size_t got = fread(buf, 1, 1 << 20, f);

			fclose(f);
			assert(t41_bin_validate(buf, got, VER, NULL, SOC, &info) == 0);
			assert(info.night_off >= T41_BIN_BANK_MIN);
			printf("real bin %s: %zu bytes, night at %u, dnw %#x\n",
			       real, got, info.night_off, info.dnw);
			buf[got / 2] ^= 1;
			assert(t41_bin_validate(buf, got, VER, NULL, SOC, &info) == -EPROTO);
			free(buf);
			n++;
		}
	}

	printf("t41 switchbin: %d groups passed\n", n);
	return 0;
}
