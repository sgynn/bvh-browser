#ifndef _BVH_
#define _BVH_

#include "transform.h"

/** bvh mocap data */
class BVH {
	public:

	enum Channel { Xpos=1, Ypos, Zpos, Xrot, Yrot, Zrot };

	struct Part {
		Part*      parent;
		vec3       offset;
		vec3       end;
		float      length;
		char*      name;
		Transform* motion;
		int        channels;
	};

	public:
	BVH();
	~BVH();

	bool load(const char* data);

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

