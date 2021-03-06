
union V3 {
	struct {
		T	x, y, z;
	};
	T		arr[3];
	
	T& operator[] (u32 i) {					return arr[i]; }
	constexpr T operator[] (u32 i) const {	return arr[i]; }
	
	INL V3 () {}
	INL constexpr V3 (T all):				x{all},	y{all},	z{all} {}
	INL constexpr V3 (T x, T y, T z):		x{x},	y{y},	z{z} {}
	INL constexpr V3 (V2 v, T z):			x{v.x},	y{v.y},	z{z} {}
	
	INL constexpr V2 xy () const {			return V2(x,y); };
	
#if !BOOLVEC
	V3& operator+= (V3 r) {					return *this = V3(x +r.x, y +r.y, z +r.z); }
	V3& operator-= (V3 r) {					return *this = V3(x -r.x, y -r.y, z -r.z); }
	V3& operator*= (V3 r) {					return *this = V3(x * r.x, y * r.y, z * r.z); }
	V3& operator/= (V3 r) {					return *this = V3(x / r.x, y / r.y, z / r.z); }

	#if FLTVEC
	operator u8v3() const;
	operator s64v3() const;
	operator s32v3() const;
	#endif
	#if INTVEC
	operator fv3() const {					return fv3((f32)x, (f32)y, (f32)z); }
	#endif
#endif
};

#if BOOLVEC
	VEC_STATIC constexpr bool all (V3 b) {				return b.x && b.y && b.z; }
	VEC_STATIC constexpr bool any (V3 b) {				return b.x || b.y || b.z; }
	
	VEC_STATIC constexpr V3 operator! (V3 b) {			return V3(!b.x,			!b.y,		!b.z); }
	VEC_STATIC constexpr V3 operator&& (V3 l, V3 r) {	return V3(l.x && r.x,	l.y && r.y,	l.z && r.z); }
	VEC_STATIC constexpr V3 operator|| (V3 l, V3 r) {	return V3(l.x || r.x,	l.y || r.y,	l.z || r.z); }
	VEC_STATIC constexpr V3 XOR (V3 l, V3 r) {			return V3(BOOL_XOR(l.x, r.x),	BOOL_XOR(l.y, r.y),	BOOL_XOR(l.z, r.z)); }
	
#else
	VEC_STATIC constexpr BV3 operator < (V3 l, V3 r) {	return BV3(l.x  < r.x,	l.y  < r.y,	l.z  < r.z); }
	VEC_STATIC constexpr BV3 operator<= (V3 l, V3 r) {	return BV3(l.x <= r.x,	l.y <= r.y,	l.z <= r.z); }
	VEC_STATIC constexpr BV3 operator > (V3 l, V3 r) {	return BV3(l.x  > r.x,	l.y  > r.y,	l.z  > r.z); }
	VEC_STATIC constexpr BV3 operator>= (V3 l, V3 r) {	return BV3(l.x >= r.x,	l.y >= r.y,	l.z >= r.z); }
	VEC_STATIC constexpr BV3 operator== (V3 l, V3 r) {	return BV3(l.x == r.x,	l.y == r.y,	l.z == r.z); }
	VEC_STATIC constexpr BV3 operator!= (V3 l, V3 r) {	return BV3(l.x != r.x,	l.y != r.y,	l.z != r.z); }
	VEC_STATIC constexpr V3 select (BV3 c, V3 l, V3 r) {
		return V3(	c.x ? l.x : r.x,	c.y ? l.y : r.y,	c.z ? l.z : r.z );
	}
	
	VEC_STATIC constexpr bool equal (V3 l, V3 r) {		return l.x == r.x && l.y == r.y && l.z == r.z; }
	
	VEC_STATIC constexpr V3 operator+ (V3 v) {			return v; }
	VEC_STATIC constexpr V3 operator- (V3 v) {			return V3(-v.x, -v.y, -v.z); }

	VEC_STATIC constexpr V3 operator+ (V3 l, V3 r) {	return V3(l.x +r.x, l.y +r.y, l.z +r.z); }
	VEC_STATIC constexpr V3 operator- (V3 l, V3 r) {	return V3(l.x -r.x, l.y -r.y, l.z -r.z); }
	VEC_STATIC constexpr V3 operator* (V3 l, V3 r) {	return V3(l.x * r.x, l.y * r.y, l.z * r.z); }
	VEC_STATIC constexpr V3 operator/ (V3 l, V3 r) {	return V3(l.x / r.x, l.y / r.y, l.z / r.z); }
	#if INTVEC
	VEC_STATIC constexpr V3 operator% (V3 l, V3 r) {	return V3(l.x % r.x, l.y % r.y, l.z % r.z); }
	#endif
	
	VEC_STATIC constexpr T dot(V3 l, V3 r) { return l.x*r.x + l.y*r.y + l.z*r.z; }

	VEC_STATIC constexpr V3 cross(V3 l, V3 r) {
		return V3(l.y*r.z - l.z*r.y, l.z*r.x - l.x*r.z, l.x*r.y - l.y*r.x);
	}

	VEC_STATIC V3 abs(V3 v) { return V3(abs(v.x), abs(v.y), abs(v.z)); }
	VEC_STATIC T max_component(V3 v) { return max(max(v.x, v.y), v.z); }

	VEC_STATIC constexpr V3 min(V3 l, V3 r) { return V3(min(l.x, r.x), min(l.y, r.y), min(l.z, r.z)); }
	VEC_STATIC constexpr V3 max(V3 l, V3 r) { return V3(max(l.x, r.x), max(l.y, r.y), max(l.z, r.z)); }
	
	VEC_STATIC constexpr V3 clamp (V3 val, V3 l, V3 h) {return min( max(val,l), h ); }
	
#if FLTVEC
	VEC_STATIC constexpr V3 lerp (V3 a, V3 b, V3 t) {	return (a * (V3(1) -t)) +(b * t); }
	VEC_STATIC constexpr V3 map (V3 x, V3 in_a, V3 in_b) {	return (x -in_a)/(in_b -in_a); }
	VEC_STATIC constexpr V3 map (V3 x, V3 in_a, V3 in_b, V3 out_a, V3 out_b) {
													return lerp(out_a, out_b, map(x, in_a, in_b)); }
	
	VEC_STATIC T length (V3 v) {						return sqrt(v.x*v.x +v.y*v.y +v.z*v.z); }
	VEC_STATIC T length_sqr (V3 v) {					return v.x*v.x +v.y*v.y +v.z*v.z; }
	VEC_STATIC T distance (V3 a, V3 b) {				return length(b -a); }
	VEC_STATIC V3 normalize (V3 v) {					return v / V3(length(v)); }
	VEC_STATIC V3 normalize_or_zero (V3 v) { // TODO: epsilon?
		T len = length(v);
		if (len != 0) {
			 v /= len;
		}
		return v;
	}
	
	VEC_STATIC constexpr V3 to_deg (V3 v) {				return v * RAD_TO_DEG; }
	VEC_STATIC constexpr V3 to_rad (V3 v) {				return v * DEG_TO_RAD; }

	VEC_STATIC V3 mymod (V3 val, V3 range) {
		return V3(	mymod(val.x, range.x),
					mymod(val.y, range.y),
					mymod(val.z, range.z) );
	}
	
	VEC_STATIC V3 floor (V3 v) {						return V3(floor(v.x),	floor(v.y),	floor(v.z)); }
	VEC_STATIC V3 ceil (V3 v) {							return V3(ceil(v.x),	ceil(v.y),	ceil(v.z)); }

	VEC_STATIC V3 pow (V3 v, V3 e) {					return V3(pow(v.x,e.x),	pow(v.y,e.y),	pow(v.z,e.z)); }
	#endif
#endif
