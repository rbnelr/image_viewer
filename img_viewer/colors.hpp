#pragma once

#include "vector.hpp"

typedef u8v4	rgba8;
typedef fv4		frgb8;
typedef fv3		frgba8;

template <typename T> T to_linear (T srgb) {
	return select(srgb <= T(0.0404482362771082),
		srgb * T(1/12.92),
		pow( (srgb +T(0.055)) * T(1/1.055), T(2.4) )
	);
}
template <typename T> T to_srgb (T linear) {
	return select(linear <= T(0.00313066844250063),
		linear * T(12.92),
		( T(1.055) * pow(linear, T(1/2.4)) ) -T(0.055)
	);
}

fv3 hsl_to_rgb (fv3 hsl) {
	#if 0
	// modified from http://www.easyrgb.com/en/math.php
	f32 H = hsl.x;
	f32 S = hsl.y;
	f32 L = hsl.z;

	auto Hue_2_RGB = [] (f32 a, f32 b, f32 vH) {
		if (vH < 0) vH += 1;
		if (vH > 1) vH -= 1;
		if ((6 * vH) < 1) return a +(b -a) * 6 * vH;
		if ((2 * vH) < 1) return b;
		if ((3 * vH) < 2) return a +(b -a) * ((2.0f/3) -vH) * 6;
		return a;
	};

	fv3 rgb;
	if (S == 0) {
		rgb = fv3(L);
	} else {
		f32 a, b;

		if (L < 0.5f)	b = L * (1 +S);
		else			b = (L +S) -(S * L);

		a = 2 * L -b;

		rgb = fv3(	Hue_2_RGB(a, b, H +(1.0f / 3)),
				  Hue_2_RGB(a, b, H),
				  Hue_2_RGB(a, b, H -(1.0f / 3)) );
	}

	return to_linear(rgb);
	#else
	f32 hue = hsl.x;
	f32 sat = hsl.y;
	f32 lht = hsl.z;

	f32 hue6 = hue*6.0f;

	f32 c = sat*(1.0f -abs(2.0f*lht -1.0f));
	f32 x = c * (1.0f -abs(mod(hue6, 2.0f) -1.0f));
	f32 m = lht -(c/2.0f);

	fv3 rgb;
	if (		hue6 < 1.0f )	rgb = fv3(c,x,0);
	else if (	hue6 < 2.0f )	rgb = fv3(x,c,0);
	else if (	hue6 < 3.0f )	rgb = fv3(0,c,x);
	else if (	hue6 < 4.0f )	rgb = fv3(0,x,c);
	else if (	hue6 < 5.0f )	rgb = fv3(x,0,c);
	else						rgb = fv3(c,0,x);
	rgb += m;

	return to_linear(rgb);
	#endif
}
