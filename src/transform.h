#ifndef _TRANSFORM_
#define _TRANSFORM_

#include <cmath>

/** Super simple math stuff */
class vec3 {
	public:
	float x, y, z;
	public:
	vec3(float v=0) : x(v), y(v), z(v) {}
	vec3(float x, float y, float z) : x(x), y(y), z(z) {}
	const vec3& operator=(const vec3& v) { x=v.x; y=v.y; z=v.z; return *this; }
	const vec3& operator*=(float s)      { x*=s; y*=s; z*=s; return *this; }
	vec3 operator+(const vec3& v) const  { return vec3(x+v.x, y+v.y, z+v.z); }
	vec3 operator-(const vec3& v) const  { return vec3(x-v.x, y-v.y, z-v.z); }
	vec3 operator*(float s) const        { return vec3(x*s, y*s, z*s); }
	vec3  cross(const vec3& v) const     { return vec3(y*v.z-z*v.y, z*v.x-x*v.z, x*v.y-y*v.x); }
	float dot(const vec3& v) const       { return x*v.x + y*v.y + z*v.z; }
	vec3& normalise()                    { float l=sqrt(dot(*this)); if(l!=0) *this*=1/l; return *this; }
};

// --------------------------------------------------------------------------------- //

class Quaternion {
	public:
	float x, y, z, w;

	public:
	Quaternion() : x(0), y(0), z(0), w(1) {}
	Quaternion(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
	Quaternion(const vec3& axis, float angle) {
		float hAngle = angle*0.5f;
		float sine = sin(hAngle);
		w = cos(hAngle);
		x = sine * axis.x;
		y = sine * axis.y;
		z = sine * axis.z;
	}
	Quaternion operator*(const Quaternion& q) const {
		return Quaternion(	w*q.x + x*q.w + y*q.z - z*q.y,
							w*q.y + y*q.w + z*q.x - x*q.z,
							w*q.z + z*q.w + x*q.y - y*q.x,
							w*q.w - x*q.x - y*q.y - z*q.z);
	}
	vec3 operator*(const vec3& v) const {
		vec3 uv, uuv;
		vec3 qvec(x, y, z);
		uv = qvec.cross(v);
		uuv = qvec.cross(uv);
		uv *= 2.0 * w;
		uuv *= 2.0;
		return v + uv + uuv;
	}
};

// --------------------------------------------------------------------------------- //

class Transform {
	public:
	vec3       offset;
	Quaternion rotation;
};

// --------------------------------------------------------------------------------- //


inline vec3 lerp(const vec3& a, const vec3& b, float t) {
	return a + (b-a) * t;
}
inline Quaternion slerp(const Quaternion& a, const Quaternion& b, float t) {
	float c = a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
	c = c<-1? -1: c>1? 1: c;	// clamp -1,1
	float theta = acos(c);
	if(theta != 0.f) {
		float d = 1.f / sin(theta);
		float u = sin((1.f - t) * theta) * d;
		float v = sin(t * theta) * d;
		if(c < 0) c = -c;
		return Quaternion(a.x*u + b.x*v, a.y*u + b.y*v, a.z*u + b.z*v, a.w*u + b.w*v);
	}
	return a;
}

#endif

