#include "bvh.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>


BVH::BVH() : m_root(0), m_parts(0), m_partCount(0) {
}

BVH::~BVH() {
	for(int i=0; i<m_partCount; ++i) {
		delete [] m_parts[i]->name;
		delete [] m_parts[i]->motion;
		delete m_parts[i];
	}
	delete [] m_parts;
}

// -------------------------------------------------------------------------- //

inline void whitespace(const char*& s) {
	while(*s==' ' || *s=='\t' || *s=='\n' || *s=='\r') ++s;
}
inline void nextLine(const char*& s) {
	while(*s && *s != '\n' && *s != '\r') ++s;
	whitespace(s);
}
inline bool word(const char*& s, const char* key, int len) {
	if(strncmp(s, key, len) != 0) return false;
	s += len;
	return true;
}
inline bool readFloat(const char*& data, float& out) {
	char* end;
	out = strtod(data, &end);
	if(end > data) {
		data += end - data;
		return true;
	}
	else return false;
}
inline bool readInt(const char*& data, int& out) {
	char* end;
	out = strtol(data, &end, 10);
	if(end > data) {
		data += end - data;
		return true;
	}
	else return false;
}


// -------------------------------------------------------------------------- //

BVH::Part* BVH::readHeirachy(const char*& data) {
	whitespace(data);

	// parse name
	int len = 0;
	const char* name = data;
	while(data[len]>='*' && data[len]<='z') ++len;
	data += len;
	whitespace(data);

	// block start
	if(!word(data, "{", 1)) return 0;

	// Create part
	Part* part = new Part;
	part->parent = -1;
	part->name = 0;
	part->channels = 0;
	part->motion = 0;

	if(len>0) {
		part->name = new char[len+1];
		memcpy(part->name, name, len);
		part->name[len] = 0;
	}

	// Add part to flat list
	if(~m_partCount & 0x7) {
		Part** list = new Part*[ (m_partCount&0xff8) + 0x8 ];
		if(m_partCount) memcpy(list, m_parts, m_partCount * sizeof(Part*));
		delete [] m_parts;
		m_parts = list;
	}
	int index = m_partCount;
	m_parts[ m_partCount ] = part;
	++m_partCount;
	int channelCount = 0;
	int childCount = 0;

	// Part data
	while(*data) {
		whitespace(data);

		// Reat joint offset
		if(word(data, "OFFSET", 6)) {
			readFloat(data, part->offset.x);
			readFloat(data, part->offset.y);
			readFloat(data, part->offset.z);
		}

		// Read active channels
		else if(word(data, "CHANNELS", 8)) {
			readInt(data, channelCount);
			for(int i=0; i<channelCount; ++i) {
				whitespace(data);
				if(     word(data, "Xposition", 9)) part->channels |= Xpos << (i*3);
				else if(word(data, "Yposition", 9)) part->channels |= Ypos << (i*3);
				else if(word(data, "Zposition", 9)) part->channels |= Zpos << (i*3);
				else if(word(data, "Xrotation", 9)) part->channels |= Xrot << (i*3);
				else if(word(data, "Yrotation", 9)) part->channels |= Yrot << (i*3);
				else if(word(data, "Zrotation", 9)) part->channels |= Zrot << (i*3);
				else { printf("Error: invalid channel %.10s\n", data); break; }
			}
		}

		// Read child part
		else if(word(data, "JOINT", 5)) {
			Part* child = readHeirachy(data);
			child->parent = index;
			part->end = part->end + child->offset;
			++childCount;
		}

		// End point
		else if(word(data, "End Site", 8)) {
			// get length of end bone
			whitespace(data);
			word(data, "{", 1);
			while(*data) {
				whitespace(data);
				if(word(data, "}", 1)) break;
				if(word(data, "OFFSET", 6)) {
					readFloat(data, part->end.x);
					readFloat(data, part->end.y);
					readFloat(data, part->end.z);
				}
			}
		}

		// End block
		else if(word(data, "}", 1)) {
			if(childCount>0) part->end *= 1.0 / childCount;
			return part;
		}

		// Error?
		else {
			nextLine(data);
		}
	}
	delete [] part->name;
	delete part;
	return 0;
}

bool BVH::load(const char* data) {
	while(*data) {
		whitespace(data);

		// Load bone heirachy
		if(word(data, "HIERARCHY", 9)) {
			nextLine(data);
			if(word(data, "ROOT", 4)) {
				m_root = readHeirachy(data);
			}
		}

		// Load motion data
		else if(word(data, "MOTION", 6)) {
			whitespace(data);
			if(word(data, "Frames:", 7)) {
				readInt(data, m_frames);
				whitespace(data);
			}

			if(word(data, "Frame Time:", 11)) {
				readFloat(data, m_frameTime);
				whitespace(data);
			}

			// Initialise memory
			for(int i=0; i<m_partCount; ++i) {
				m_parts[i]->motion = new Transform[m_frames];
			}

			int partIndex;
			int channel;
			Part* part;
			float value;
			vec3 pos, rot;
			const vec3 xAxis(1,0,0);
			const vec3 yAxis(0,1,0);
			const vec3 zAxis(0,0,1);
			const float toRad = 3.141592653592f / 180;

			// Read frames
			for(int frame=0; frame<m_frames; ++frame) {
				partIndex = 0;
				part = m_parts[0];
				channel = part->channels;
				pos = rot = vec3(0);
				
				while(*data) {
					readFloat(data, value);
					switch(channel&0x7) {
					case Xpos: pos.x = value; break;
					case Ypos: pos.y = value; break;
					case Zpos: pos.z = value; break;
					case Xrot: rot.x = value; break;
					case Yrot: rot.y = value; break;
					case Zrot: rot.z = value; break;
					}
					channel >>= 3;

					// Read all values for this part - process!
					if(channel == 0) {
						Quaternion qX(xAxis, rot.x * toRad);
						Quaternion qY(yAxis, rot.y * toRad);
						Quaternion qZ(zAxis, rot.z * toRad);

						part->motion[frame].rotation = qZ * qX * qY;
						part->motion[frame].offset = pos;

						// Extract matrix for comparison
						float mat[16];
						Transform tmp = part->motion[frame];
						tmp.toMatrix(mat);

						// Next part
						++partIndex;
						if(partIndex == m_partCount) break;
						part = m_parts[partIndex];
						channel = part->channels;
					}
				}

				nextLine(data);
			}
		}
		else {
			printf("Read error\n");
			return false;
		}
	}
	return m_root && m_frames;
}

