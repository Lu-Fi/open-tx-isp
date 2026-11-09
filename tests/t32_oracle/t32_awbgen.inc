static u32 rs = 0x6e0b43u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void p16(u8 *p, u32 off, u32 v) { p[off] = (u8)v; p[off + 1] = (u8)(v >> 8); }
static void p32(u8 *p, u32 off, u32 v) { p16(p, off, v); p16(p, off + 2, v >> 16); }
static u32 g32(const u8 *p, u32 off) { return p[off] | (u32)p[off + 1] << 8 | (u32)p[off + 2] << 16 | (u32)p[off + 3] << 24; }
static u32 g16(const u8 *p, u32 off) { return p[off] | (u32)p[off + 1] << 8; }

/* the pointer table of tisp_awb_long_par_update (0x3d158) */
static void ctx_build(u8 *ctx, u32 par, u32 st, u32 hw, u32 tbl)
{
	static const struct { u16 slot, off; } pp[] = {
		{ 0, 3322 }, { 20, 3184 }, { 24, 60 }, { 28, 120 }, { 32, 2164 }, { 36, 5714 }, { 40, 244 }, { 44, 44 },
		{ 52, 3306 }, { 56, 3448 }, { 60, 3438 }, { 64, 292 }, { 68, 3362 }, { 76, 324 }, { 88, 5126 }, { 92, 5166 },
		{ 96, 52 }, { 104, 5206 }, { 112, 5226 }, { 116, 5234 }, { 128, 5682 } };
	static const struct { u16 slot; u32 off; } ps[] = {
		{ 4, 28688 }, { 8, 0x9490 }, { 12, 0x94a0 }, { 48, 28764 }, { 72, 28708 }, { 80, 28718 }, { 84, 28738 },
		{ 100, 29800 }, { 120, 0x9468 }, { 124, 0x947c }, { 132, 0 }, { 136, 16384 }, { 140, 24576 },
		{ 144, 65536 + 13692 }, { 148, 65536 + 14476 }, { 152, 65536 + 15260 }, { 156, 65536 + 15652 },
		{ 160, 65536 + 15796 }, { 164, 65536 + 16516 }, { 168, 65536 + 17476 }, { 172, 65536 + 21572 },
		{ 176, 65536 + 25668 }, { 180, 65536 + 29764 }, { 184, 65536 + 14476 + 19384 }, { 188, 65536 + 14476 + 23480 } };
	unsigned int i;

	for (i = 0; i < sizeof(pp) / sizeof(pp[0]); i++)
		p32(ctx, pp[i].slot, par + pp[i].off);
	for (i = 0; i < sizeof(ps) / sizeof(ps[0]); i++)
		p32(ctx, ps[i].slot, st + ps[i].off);
	p32(ctx, 16, par + tbl);
	p32(ctx, 108, hw + 4246);
}

static void gen(u8 *par, u8 *stt, const u8 *sec, u32 tbl)
{
	u32 i, x, y, os, fp, rows, cols, mode = rnd() % 16;

	if (sec)
		memcpy(par, sec, PARB);
	else
		for (i = 0; i < PARB; i++)
			par[i] = (u8)rnd();
	x = rnd() % 100; y = rnd() % 100;
	for (i = 0; i < 15; i++) {
		x += 1 + rnd() % 200;
		y += 1 + rnd() % 200;
		p32(par, 60 + 4 * i, x);
		p32(par, 120 + 4 * i, y);
	}
	fp = 8 + rnd() % 6;
	os = rnd() % 12 ? 6 + rnd() % 6 : rnd() % 40;
	p16(par, 3306, fp);
	p16(par, 3308, os);
	rows = rnd() % 8 ? 1 + rnd() % 32 : rnd() % 33;
	cols = rnd() % 8 ? 1 + rnd() % 32 : rnd() % 33;
	p16(par, 3186, rows);
	p16(par, 3190, cols);
	p16(par, 3448, rnd() % 3 ? 1 : rnd() % 3);
	p16(par, 3450, mode == 0 ? 2 : rnd() % 6 ? 1 : rnd() % 3);
	p16(par, 3452, rnd() % 3 ? 1 : rnd() % 3);
	p16(par, 3454, mode == 1 ? 1 : rnd() % 5 == 0 ? 2 : 0);
	for (i = 0; i < 20; i++)
		if (i % 2 == 0 && rnd() % 5 == 0)
			p16(par, 5126 + 2 * i, 0);
	if (rnd() % 4 == 0) {	/* all ten non-zero */
		for (i = 0; i < 10; i++)
			p16(par, 5126 + 4 * i, 1 + rnd() % 60000);
	}
	for (i = 0; i < 1024; i++)
		par[5714 + i] = rnd() % 4;
	for (i = 0; i < STB; i++)
		stt[i] = (u8)rnd();
	/* raw zone values: small, large, zero, equal */
	for (i = 0; i < 1024; i++) {
		u32 v = rnd() % 4 == 0 ? rnd() : rnd() % 5 == 0 ? 0 : rnd() % 100000;

		if (mode == 2)
			v = 0;
		else if (mode == 3)
			v = 77;
		p32(stt, 16384 + 4 * i, v);
	}
	/* statistics around the clamp bounds */
	{
		u32 lr = (u32)(par[60] | par[61] << 8) << (os & 31), hr = (u32)(par[60 + 56] | par[61 + 56] << 8) << (os & 31);

		for (i = 0; i < 4096; i++) {
			u32 v;

			switch (rnd() % 6) {
			case 0: v = lr - 1 + rnd() % 3; break;
			case 1: v = hr - 1 + rnd() % 3; break;
			case 2: v = rnd() % (hr + 1000); break;
			default: v = rnd();
			}
			p32(stt, 4 * i, v);
		}
	}

	/* phase 1 inputs: parameter table, ct grid, thresholds, weight kind, stats mostly inside the axes */
	{
		u32 th[4], j, lo_r = (par[60] | par[61] << 8) << (os & 31), hi_r = (par[60 + 56] | par[61 + 56] << 8) << (os & 31);
		u32 lo_b = (par[120] | par[121] << 8) << (os & 31), hi_b = (par[120 + 56] | par[121 + 56] << 8) << (os & 31);

		for (j = 0; j < 225; j++) {
			p32(par, 2164 + 4 * j, rnd() % 40 == 0 ? 0 : 200 + rnd() % 6000);
			p32(par, tbl + 4 * j, rnd() % 40 == 0 ? rnd() : rnd() % 3 ? (1 + rnd() % 256) << (os & 31) : 200 + rnd() % 6000);
		}
		for (j = 0; j < 4; j++)
			th[j] = 100 + rnd() % 8000;
		for (j = 0; j < 4; j++) {
			u32 k, t;

			for (k = j + 1; k < 4; k++)
				if (th[k] < th[j]) { t = th[j]; th[j] = th[k]; th[k] = t; }
			p32(stt, 38032 + 4 * j, th[j]);
		}
		p32(stt, 38048, rnd() % 4 ? 1 : rnd() % 4);
		p32(stt, 38052, rnd() % 4 ? rnd() % ((1u << fp) + 2) : rnd());
		if (rnd() % 30 == 0) {		/* axes that do not rise: refused */
			p32(par, 60, (par[60 + 56] | par[61 + 56] << 8) + 5);
		}
		if (mode < 10 && hi_r > lo_r && hi_b > lo_b)
			for (j = 0; j < 4096; j++) {
				u32 lo = (j & 1024) ? lo_b : lo_r, hi = (j & 1024) ? hi_b : hi_r;

				if (rnd() % 8 == 0)
					continue;
				p32(stt, 4 * j, rnd() % 16 == 0 ? hi : rnd() % 16 == 0 ? lo : lo + rnd() % (hi - lo + 1));
			}
	}

	/* phase 2 inputs: reference points near some zone statistics, counts mostly <= 10 */
	{
		u32 j, nz = (par[3186] | par[3187] << 8) * 32;
		u32 lo_r = (par[60] | par[61] << 8) << (os & 31), hi_r = (par[60 + 56] | par[61 + 56] << 8) << (os & 31);
		u32 lo_b = (par[120] | par[121] << 8) << (os & 31), hi_b = (par[120 + 56] | par[121 + 56] << 8) << (os & 31);

		for (j = 0; j < 20; j++) {
			u32 z = nz ? rnd() % nz : 0, sr = g32(stt, 4 * z), sb = g32(stt, 4096 + 4 * z), r2 = sr, b2 = sb;

			if (r2 < lo_r) r2 = lo_r;
			if (r2 > hi_r) r2 = hi_r;
			if (b2 < lo_b) b2 = lo_b;
			if (b2 > hi_b) b2 = hi_b;
			p16(par, 3322 + 4 * (j % 10) + (j / 10) * 40, rnd() % 6 ? ((r2 + (1u << (os & 31)) / 2) >> (os & 31)) + rnd() % 15 - 7 : rnd());
			p16(par, 3324 + 4 * (j % 10) + (j / 10) * 40, rnd() % 6 ? ((b2 + (1u << (os & 31)) / 2) >> (os & 31)) + rnd() % 15 - 7 : rnd());
		}

		for (j = 0; j < 6; j++) {	/* points at squared distances next to the class thresholds (sums of two squares only) from zone 0 */
			static const u32 th[] = { 512, 514, 520, 538, 541, 544, 569, 576, 613, 617, 625, 677, 680, 725, 810, 818, 820, 521 };
			u32 t = th[rnd() % 18], dr = 0, db = 0, tries, k = rnd() % 20, sr = g32(stt, 0), sb = g32(stt, 4096), found = 0, rv, bv;

			if (sr < lo_r) sr = lo_r;
			if (sr > hi_r) sr = hi_r;
			if (sb < lo_b) sb = lo_b;
			if (sb > hi_b) sb = hi_b;
			rv = (sr + (1u << (os & 31)) / 2) >> (os & 31);
			bv = (sb + (1u << (os & 31)) / 2) >> (os & 31);
			for (tries = 0; tries < 400 && !found; tries++) {
				dr = rnd() % 30;
				db = rnd() % 30;
				found = dr * dr + db * db == t;
			}
			if (!found)
				continue;
			p16(par, 3322 + 4 * (k % 10) + (k / 10) * 40, rv + dr);
			p16(par, 3324 + 4 * (k % 10) + (k / 10) * 40, bv + db);
		}
		stt[28688] = rnd() % 16 ? rnd() % 11 : rnd();
		stt[28689] = rnd() % 16 ? rnd() % 11 : rnd();
		for (j = 0; j < 514; j++)
			p16(stt, 28764 + 2 * j, rnd() % 40);
	}

	/* phase 3 inputs: sat_mode 1/2 mostly, zt2 values around the fixed point unit */
	{
		u32 j;

		p16(par, 3438, rnd() % 5 ? 1 + rnd() % 2 : rnd() % 4);
		p16(par, 3306, rnd() % 12 ? 8 + rnd() % 6 : rnd() % 36);
		for (j = 0; j < 1024; j++) {
			u32 v = rnd() % 6 == 0 ? rnd() : rnd() % 6 == 0 ? rnd() % 3 : (1u << 8) + rnd() % (1u << 14);

			p32(stt, 24576 + 4 * j, rnd() % 40 == 0 ? 0 : v);
			if (rnd() % 3 == 0)
				p32(stt, 87108 + 4 * j, rnd() % 4 ? (1u << 8) + rnd() % 5000 : rnd());
		}
		if (rnd() % 12 == 0)
			for (j = 0; j < 1024; j++)
				p32(stt, 24576 + 4 * j, rnd() % 3);
	}

	/* phase 4 inputs: candidate mode 1 mostly, cell counts sparse, kept slots small */
	{
		u32 j;

		p32(par, 292, rnd() % 8 ? 1 : rnd() % 3);
		if (rnd() % 6 == 0) { p32(par, 60, 0); p32(par, 120, 0); if (rnd() % 2) { p32(par, 64, 0); p32(par, 124, 0); } }
		for (j = 0; j < 196; j++)
			p16(stt, 80796 + 2 * j, rnd() % 3 == 0 ? rnd() % 40 : rnd() % 2 ? 0 : rnd() % 3);
		for (j = 0; j < 12; j++) {
			p32(stt, 81188 + 4 * j, rnd() % 14);
			p32(stt, 81236 + 4 * j, rnd() % 14);
			p32(stt, 81284 + 4 * j, rnd() % 3 ? 0 : rnd() % 45);
		}
		if (rnd() % 3 == 0)	/* equal axes points make equal candidates */
			for (j = 1; j < 15; j++)
				if (rnd() % 3 == 0) {
					p32(par, 60 + 4 * j, g32(par, 60 + 4 * j - 4));
					p32(par, 120 + 4 * j, g32(par, 120 + 4 * j - 4));
				}
	}
	/* phase 5 inputs: radius, tolerance and pass limit; zone statistics mostly inside the axes (above) */
	{
		p32(par, 296, rnd() % 5 == 0 ? rnd() : rnd() % 3 ? 50 + rnd() % 30000 : rnd() % 100);
		p32(par, 308, rnd() % 6 == 0 ? rnd() : rnd() % 3 ? rnd() % 20 : rnd() % 2);
		p32(par, 312, rnd() % 8 == 0 ? 60000 + rnd() % 10000 : rnd() % 8);
	}
	/* phase 10 inputs: spec samples near some zone statistics, control values around the squared distances */
	{
		u32 j, nz = (par[3186] | par[3187] << 8) * 32;
		u32 lo_r = (par[60] | par[61] << 8) << (os & 31), hi_r = (par[60 + 56] | par[61 + 56] << 8) << (os & 31);
		u32 lo_b = (par[120] | par[121] << 8) << (os & 31), hi_b = (par[120 + 56] | par[121 + 56] << 8) << (os & 31);

		for (j = 0; j < 10; j++) {
			u32 z = nz ? rnd() % nz : 0, sr = g32(stt, 4 * z), sb = g32(stt, 4096 + 4 * z);

			if (sr < lo_r) sr = lo_r;
			if (sr > hi_r) sr = hi_r;
			if (sb < lo_b) sb = lo_b;
			if (sb > hi_b) sb = hi_b;
			stt[28708 + j] = rnd() % 3 ? 2 : rnd() % 4;
			p16(stt, 28718 + 2 * j, rnd() % 8 ? ((sr + (1u << (os & 31)) / 2) >> (os & 31)) + rnd() % 51 - 25 : rnd());
			p16(stt, 28738 + 2 * j, rnd() % 8 ? ((sb + (1u << (os & 31)) / 2) >> (os & 31)) + rnd() % 51 - 25 : rnd());
			p32(par, 324 + 4 * j, rnd() % 5 == 0 ? rnd() : rnd() % 30);
		}
		for (j = 0; j < 4; j++) {	/* samples at squared distances next to the class thresholds from zone 0 (sums of two squares only) */
			static const u32 th[] = { 512, 513, 514, 515, 520, 538, 544, 569, 570, 577, 612, 613, 616, 617, 625, 8, 100 };
			u32 t = th[rnd() % 17], dr = 0, db = 0, tries, k = rnd() % 10, sr = g32(stt, 0), sb = g32(stt, 4096), found = 0;

			if (sr < lo_r) sr = lo_r;
			if (sr > hi_r) sr = hi_r;
			if (sb < lo_b) sb = lo_b;
			if (sb > hi_b) sb = hi_b;
			for (tries = 0; tries < 400 && !found; tries++) {
				dr = rnd() % 30;
				db = rnd() % 30;
				found = dr * dr + db * db == t;
			}
			if (!found)
				continue;
			stt[28708 + k] = 2;
			p16(stt, 28718 + 2 * k, ((sr + (1u << (os & 31)) / 2) >> (os & 31)) + dr);
			p16(stt, 28738 + 2 * k, ((sb + (1u << (os & 31)) / 2) >> (os & 31)) + db);
			if (rnd() % 2)
				p32(par, 324 + 4 * k, rnd() % 2 ? 3 : t < 20 ? 3 : 20 + rnd() % 8);
		}
		if (rnd() % 3 == 0)
			for (j = 0; j < 514; j++)
				p16(stt, 28764 + 2 * j, rnd() % 5 ? 1 + rnd() % 256 : rnd());
	}
	/* phase 6 inputs: merge radius, slots kept from the last frame (mostly empty) */
	{
		u32 j;

		p16(par, 5686, rnd() % 3 ? rnd() % 257 : rnd());
		p32(par, 304, rnd() % 6 == 0 ? rnd() : rnd() % 4 ? 20 + rnd() % 20000 : rnd() % 50);
		p32(par, 300, rnd() % 6 == 0 ? rnd() : rnd() % 4 ? 20 + rnd() % 20000 : rnd() % 50);
		for (j = 0; j < 60; j++) {
			p32(stt, 82052 + 4 * j, rnd() % 3 ? 0 : rnd() % 3000);
			p32(stt, 82292 + 4 * j, rnd() % 3 ? 0 : rnd() % 3000);
			p32(stt, 82532 + 4 * j, rnd() % 3 ? 0 : rnd() % 200 ? rnd() % 40 : rnd());
		}
	}
	/* phase 12 inputs: outlier pass on mostly, limits around the normalised distances (about 1 << fp) / 64 */
	{
		u32 lo = rnd() % 3 ? rnd() % 100 : rnd() % 5;

		par[244] = rnd() % 8 ? 1 : rnd() % 3;
		p32(par, 252, lo);
		p32(par, 248, rnd() % 10 == 0 ? rnd() % 40 : rnd() % 12 == 0 ? lo : lo + rnd() % 150);
	}
	/* phase 13 inputs: rectangles [lim1[2k], lim1[2k+1]) x [lim2[2k+1], lim2[2k]) in par+5126 / par+5166, zone statistics (st+29800, r then b) in and around them */
	{
		u32 j, k, n, nz = 1024;

		par[316] = rnd() % 8 ? 1 : rnd() % 3;
		par[317] = rnd() % 16 == 0 ? rnd() % 2 : 0;
		n = rnd() % 11;
		for (k = 0; k < 10; k++) {
			u32 lo = 1 + rnd() % 300, hi = lo + (rnd() % 10 == 0 ? 0 : rnd() % 2 ? rnd() % 60 : rnd() % 400), bl = 1 + rnd() % 300, bh = bl + (rnd() % 10 == 0 ? 0 : rnd() % 2 ? rnd() % 60 : rnd() % 400);

			if (rnd() % 3 == 0 && k) {		/* touching / equal neighbours */
				lo = g16(par, 5126 + 4 * (k - 1) + 2);
				bh = g16(par, 5166 + 4 * (k - 1) + 2);
				if (!lo) lo = 1;
			}
			if (rnd() % 20 == 0) { lo = 65535 - rnd() % 3; hi = 65535; }
			p16(par, 5126 + 4 * k, k < n ? lo : n < 10 && k == n ? 0 : lo);
			p16(par, 5126 + 4 * k + 2, hi);
			p16(par, 5166 + 4 * k, bh);
			p16(par, 5166 + 4 * k + 2, bl);
		}
		(void)nz;
		for (j = 0; j < 1024; j++) {
			u32 q = rnd() % 10, r, b;

			k = rnd() % 10;
			r = g16(par, 5126 + 4 * k) + (rnd() % 4 == 0 ? rnd() % 3 : 0);
			b = g16(par, 5166 + 4 * k + 2) + (rnd() % 4 == 0 ? rnd() % 3 : 0);
			switch (q) {
			case 0: r = g16(par, 5126 + 4 * k) - 1 + rnd() % 3; break;
			case 1: r = g16(par, 5126 + 4 * k + 2) - 1 + rnd() % 3; break;
			case 2: b = g16(par, 5166 + 4 * k) - 1 + rnd() % 3; break;
			case 3: b = g16(par, 5166 + 4 * k + 2) - 1 + rnd() % 3; break;
			case 4: r = rnd() % 700; b = rnd() % 700; break;
			case 5: r = 0; break;
			case 6: b = 0; break;
			case 7: r = rnd(); break;
			default: r = (g16(par, 5126 + 4 * k) + g16(par, 5126 + 4 * k + 2)) / 2; b = (g16(par, 5166 + 4 * k) + g16(par, 5166 + 4 * k + 2)) / 2; break;
			}
			p32(stt, 29800 + 4 * j, r);
			p32(stt, 33896 + 4 * j, b);
		}
	}
	/* phase 14 / 15 inputs: kept zlim small, ramp tables (par+5226 falling x, par+5234 y; st lim_a / lim_b 3..6 rising x, y) */
	{
		u32 j, x;

		for (j = 0; j < 1024; j++)
			p32(stt, 103492 + 4 * j, rnd() % 40 == 0 ? rnd() : rnd() % 12);
		x = 110 + rnd() % 40;
		for (j = 0; j < 4; j++) {
			p16(par, 5226 + 2 * j, rnd() % 25 == 0 ? rnd() % 300 : x);
			p16(par, 5234 + 2 * j, rnd() % 4 ? rnd() % 1100 : rnd());
			x -= rnd() % 5 ? 10 + rnd() % 40 : 0;
		}
		x = rnd() % 30;
		for (j = 3; j < 7; j++) {
			p16(stt, 37992 + 2 * j, rnd() % 25 == 0 ? rnd() % 100 : x);
			p16(stt, 38012 + 2 * j, rnd() % 4 ? rnd() % 1100 : rnd());
			x += rnd() % 5 ? 5 + rnd() % 30 : 0;
		}
	}
	/* wide ramps with large y: the 32 bit product becomes negative (the 16 bit mask of the result) */
	if (rnd() % 10 == 0) {
		static const u16 dx[4] = { 65000, 50000, 40000, 3 }, dy[4] = { 50000, 60000, 65000, 40000 };
		static const u16 ux[4] = { 3, 50000, 55000, 60000 }, uy[4] = { 60000, 65000, 30000, 10 };
		u32 j;

		for (j = 0; j < 4; j++) {
			p16(par, 5226 + 2 * j, dx[j]);
			p16(par, 5234 + 2 * j, dy[j]);
			p16(stt, 37992 + 2 * (3 + j), ux[j]);
			p16(stt, 38012 + 2 * (3 + j), uy[j]);
		}
	}
	/* the percentages leave 16 bits: one rectangle (nlim 1: r 100 .. 300, b 100 .. 300) with zone 0 in it as the only zone
	 * with both values, the grid 26 x 26 (m 0) or 20 x 20 (m 1): m 0: 656 zones with r over the limit (ct_lim[-2] = 5000) and b 0, pr = 65600
	 * = 64 as 16 bit, below the ramp (x 200 ..); m 1: b over the limit and r 0, pb = nb * 100 as signed 16 bit (negative) */
	if (rnd() % 4 == 0) {
		u32 j, m = rnd() % 2, cnt = 0, row, col;

		p16(par, 3186, m ? 20 : 26);
		p16(par, 3190, m ? 20 : 26);
		p16(par, 5122, 5000);
		p16(par, 5126, 100);
		p16(par, 5128, 300);
		p16(par, 5130, 0);
		p16(par, 5166, 300);
		p16(par, 5168, 100);
		p16(par, 5172, 50);
		for (j = 0; j < 1024; j++) {
			p32(stt, 29800 + 4 * j, 0);
			p32(stt, 33896 + 4 * j, m ? 60000 + rnd() % 5000 : 0);
		}
		if (!m) {
			for (row = 0; row < 26; row++)
				for (col = 0; col < 26; col++)
					if (cnt++ < 657 && row + col)
						p32(stt, 29800 + 4 * (row * 32 + col), 60000 + rnd() % 5000);
			for (j = 0; j < 4; j++) {
				p16(stt, 37992 + 2 * (3 + j), 200 + 100 * j);
				p16(stt, 38012 + 2 * (3 + j), 10 * (j + 1));
			}
		}
		p32(stt, 29800, 150);
		p32(stt, 33896, 150);
	}
	/* phase 16 inputs: mode 0 / 1 mostly, percentage, the first three lim_a / lim_b entries around the 1024 bounds */
	{
		u32 j, m = rnd() % 8;

		p16(par, 5694, m < 3 ? 0 : m < 6 ? 1 : m == 6 ? 2 : rnd());
		p16(par, 5700, rnd() % 3 ? 1 : rnd() % 3);
		p16(par, 5682, rnd() % 8 == 0 ? rnd() : rnd() % 6);
		p16(par, 5696, rnd() % 4 ? rnd() % 2000 : 0);
		p16(par, 5698, rnd() % 4 ? rnd() % 2000 : 0);
		p32(par, 52, rnd() % 5 ? 1u << 4 | (rnd() % 256) : rnd());
		p32(par, 56, rnd() % 5 ? 1u << 4 | (rnd() % 256) : rnd());
		p16(par, 5684, rnd());
		p16(par, 5692, rnd() % 4 == 0 ? rnd() : rnd() % 3 == 0 ? 50 + rnd() % 51 : rnd() % 101);
		for (j = 0; j < 3; j++) {
			p16(stt, 37992 + 2 * j, rnd() % 4 == 0 ? rnd() : rnd() % 8 == 0 ? 1022 + rnd() % 5 : rnd() % 1500);
			p16(stt, 38012 + 2 * j, rnd() % 4 == 0 ? rnd() : rnd() % 8 == 0 ? 1022 + rnd() % 5 : rnd() % 1500);
		}
		if (rnd() % 2 == 0) {	/* many full rectangles side by side: n rectangles r in [100 + 50 k, 140 + 50 k), b in (100, 300), zones spread over them */
			u32 n = 2 + rnd() % 9, k;

			par[316] = 1;
			par[317] = 0;
			for (k = 0; k < 10; k++) {
				p16(par, 5126 + 4 * k, k < n ? 100 + 50 * k : k == n ? 0 : 100 + 50 * k);
				p16(par, 5126 + 4 * k + 2, 140 + 50 * k);
				p16(par, 5166 + 4 * k, 300);
				p16(par, 5166 + 4 * k + 2, 100);
			}
			for (j = 0; j < 1024; j++) {
				u32 a1 = rnd() % n, b1 = rnd() % n, rr;

				k = rnd() % 4 == 0 ? a1 : a1 < b1 ? a1 : b1;
				rr = 100 + 50 * k + rnd() % 40;
				if (rnd() % 40 == 0)
					rr = rnd() % 700;
				p32(stt, 29800 + 4 * j, rr);
				p32(stt, 33896 + 4 * j, rnd() % 30 == 0 ? rnd() % 400 : 101 + rnd() % 199);
			}
			p16(par, 5682, rnd() % 10 == 0 ? rnd() : 1 + rnd() % 4);
			if (rnd() % 4 != 0) {	/* no candidate pass (cand_mode != 1): the zone weights stay at their table values */
				p32(par, 292, 2);
				p32(par, 324, 0);
				for (j = 0; j < 10; j++)
					stt[28708 + j] = 0;
			}
			if (rnd() % 4 != 0)	/* zone weights and flags that survive the shifts of the sums */
				for (j = 0; j < 1024; j++) {
					p32(stt, 16384 + 4 * j, rnd() % 10 == 0 ? rnd() % 3000 : 2000 + rnd() % 1000);
					par[5714 + j] = rnd() % 5 ? 1 + rnd() % 3 : 0;
				}
			if (rnd() % 2) {	/* few zones only: small totals make the rounded threshold of mode 1 hit the running sums */
				u32 few = 2 + rnd() % 24, rows = g16(par, 3186), cols = g16(par, 3190);

				for (j = 0; j < 1024; j++)
					p32(stt, 29800 + 4 * j, 0);
				if (rows && cols && rows <= 32 && cols <= 32)
					for (j = 0; j < few; j++) {
						u32 z = (rnd() % rows) * 32 + rnd() % cols, kk = rnd() % 3 ? rnd() % 3 : rnd() % n;

						if (kk >= n)
							kk = 0;
						p32(stt, 29800 + 4 * z, 100 + 50 * kk + rnd() % 40);
						p32(stt, 33896 + 4 * z, 101 + rnd() % 199);
					}
			}
		}
	}
	if (mode >= 12)
		for (i = 0; i < 4096; i++)
			p32(stt, 4 * i, (par[(i & 1024) ? 120 : 60] | par[(i & 1024) ? 121 : 61] << 8) << (os & 31));
}

