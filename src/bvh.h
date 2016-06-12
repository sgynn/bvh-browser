#ifndef _BVH_
#define _BVH_

#include "transform.h"

/** bvh mocap data */
class BVH {
	public:

	enum Channel { Xpos=1, Ypos, Zpos, Xrot, Yrot, Zrot };

	struct Part {
		int        parent;
		vec3       offset;
		vec3       end;
		char*      name;
		Transform* motion;
		int        channels;
	};

	public:
	BVH();
	~BVH();

	bool load(const char* data);

	int         getPartCount() const		{ return m_partCount; }
	const Part* getPart(int index) const    { return m_parts[index]; }
	int         getFrames() const           { return m_frames; }
	float       getFrameTime() const        { return m_frameTime; }


	private:
	Part* readHeirachy(const char*& data);

	protected:
	Part*  m_root;
	Part** m_parts;
	int    m_partCount;
	int    m_frames;
	float  m_frameTime;


};



#endif

