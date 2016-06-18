#include "view.h"
#include <SDL_opengl.h>
#include <cstdio>
#include <cstdlib>

View::View(int x, int y, int w, int h) : m_x(x), m_y(y), m_width(w), m_height(h), m_visible(true), m_paused(false), m_state(EMPTY), m_bvh(0) {
	m_near = 0.1f;
	m_far = 1000.f;
	m_frame = 0;
	updateProjection();

	m_camera = vec3(60, 60, 60);
	updateCamera();
}

View::~View() {
}


bool View::loadFile(const char* file) {
	FILE* fp = fopen(file, "r");
	if(!fp) { printf("Failed\n"); return false; }
	fseek(fp, 0, SEEK_END);
	int len = ftell(fp);
	rewind(fp);
	char* content = new char[len+1];
	fread(content, 1, len, fp);
	content[len] = 0;
	fclose(fp);
	// Read bvh
	BVH* bvh = new BVH();
	int r = bvh->load(content);
	if(r) {
		setBVH(bvh);
	} else {
		setBVH(0);
		delete bvh;
	}
	return r;
}
void View::setBVH(BVH* bvh) {
	if(m_bvh) {
		delete m_bvh;
		delete [] m_final;
	}
	m_bvh = bvh;
	m_frame = 0;
	if(bvh) {
		m_final = new Transform[ m_bvh->getPartCount() ];
		updateBones(0);
	}
}

void View::setVisible(bool v) {
	m_visible = v;
}

void View::resize(int x, int y, int w, int h) {
	m_x = x;
	m_y = y;
	m_width = w;
	m_height = h;
	updateProjection();
}

void View::setCamera(float yaw, float pitch, float zoom) {
	vec3 d;
	float cp = cos(pitch);
	d.x = sin(yaw) * cp;
	d.y = sin(pitch);
	d.z = cos(yaw) * cp;
	m_camera = m_target + d * zoom;
	updateCamera();
}
void View::rotateView(float yaw, float pitch) {
	vec3 d = m_camera - m_target;
	float zoom = d.length();
	float oldYaw = atan2(d.x, d.z);
	float oldPitch = atan2(d.y, sqrt(d.x*d.x+d.z*d.z));
	setCamera(oldYaw+yaw, oldPitch+pitch, zoom);
}
void View::zoomView(float mult) {
	vec3 d = m_camera - m_target;
	m_camera = m_target + d * mult;
	updateCamera();
}

inline float View::zoomToFit(const vec3& point, const vec3& dir, const vec3* n, float* d) {
	float shift = 0;
	for(int i=0; i<4; ++i) {
		// Distance to plane in dir
		float denom = n[i].dot(dir);
		float t = d[i] - n[i].dot(point) / denom;
		if(t>shift) shift = t;
	}
	// update frustum
	if(shift > 0) {
		m_camera = m_camera - dir * shift;
		for(int i=0; i<4; ++i) d[i] = n[i].dot(m_camera);
	}
	return shift;
}

void View::autoZoom() {
	if(!m_bvh) return;

	vec3 dir = m_target - m_camera;
	dir.normalise();
	m_camera = m_target;

	vec3 n[4];
	float d[4];
	float m[16];
	// Get frustum planes
	multMatrix(m_projectionMatrix, m_viewMatrix, m);
	n[0] = vec3(m[3]+m[0], m[7]+m[4], m[11]+m[8]);
	n[1] = vec3(m[3]-m[0], m[7]-m[4], m[11]-m[8]);
	n[2] = vec3(m[3]+m[1], m[7]+m[5], m[11]+m[9]);
	n[3] = vec3(m[3]-m[1], m[7]-m[5], m[11]-m[9]);
	for(int i=0; i<4; ++i) d[i] = n[i].dot(m_camera);

	// Zoom out to fit points
	float shift = 0;
	for(int i=0; i<m_bvh->getPartCount(); ++i) {
		shift += zoomToFit( m_final[i].offset, dir, n, d);
	}
	for(int i=0; i<m_bvh->getFrames(); ++i) {
		shift += zoomToFit(m_bvh->getPart(0)->motion[i].offset, dir, n, d);
	}
	if(shift == 0) m_camera = m_target - dir;
	updateCamera();
}

void View::togglePause() {
	m_paused = !m_paused;
}


void View::update(float time) {
	
	if(m_bvh && !m_paused && m_visible) {
		m_frame += time / m_bvh->getFrameTime();
		if(m_frame > m_bvh->getFrames()) m_frame = 0;
		updateBones(m_frame);
	}
}

void View::render() const {
	if(!m_visible) return;
	glViewport(m_x, m_y, m_width, m_height);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m_projectionMatrix);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(m_viewMatrix);
	glTranslatef(-m_camera.x, -m_camera.y, -m_camera.z);


	glEnableClientState(GL_VERTEX_ARRAY);
	glPushMatrix();
	glRotatef(90, 1,0,0);
	glScalef(10,10,10);
	drawGrid();
	glPopMatrix();

	// Draw skeleton
	if(m_bvh) {
		glEnable(GL_POLYGON_OFFSET_LINE);
		glPolygonOffset(-1,-1);
		float matrix[16];
		for(int i=0; i<m_bvh->getPartCount(); ++i) {
			m_final[i].toMatrix(matrix);

			/*
			const BVH::Part* part = m_bvh->getPart(i);
			glPushMatrix();
			glMultMatrixf(matrix);
			glBegin(GL_LINES);
			glVertex3f(0,0,0);
			glVertex3fv(&part->end.x);
			glEnd();
			glPopMatrix();
			*/


			glPushMatrix();
			glMultMatrixf(matrix);

			// Rotate to use z axis mesh
			const vec3 zAxis(0,0,1);
			vec3 dir = m_bvh->getPart(i)->end;
			float length = dir.length();
			dir *= 1.0 / length;
			if(dir.z < 0.999) {
				vec3 n = dir.cross(zAxis);
				float d = dir.dot(zAxis);
				glRotatef( -acos(d) * 180/3.141592653592, n.x, n.y, n.z );
			}
			glScalef(length, length, length);

			// Draw bone mesh
			glPolygonMode(GL_FRONT, GL_LINE);
			glColor4f(0.2, 0, 0.5, 1);
			drawBone();
			glPolygonMode(GL_FRONT, GL_FILL);
			glColor4f(0.5, 0, 1, 1);
			drawBone();
			glPopMatrix();
		}
	}

	// Border?
	glLoadIdentity();
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glColor4f(0.3, 0.3, 0.3, 1);
	static const float border[] = { -1,-1, 1,-1, 1,1, -1,1, -1,-1 };
	glVertexPointer(2, GL_FLOAT, 0, border);
	glDrawArrays(GL_LINE_STRIP, 0, 5);


	glDisableClientState(GL_VERTEX_ARRAY);

}

// ------------------------------------------------- //

void View::updateBones(float frame) {
	int f = floor(frame);
	float t = frame - f;

	if(f >= m_bvh->getFrames()-1) {
		f = m_bvh->getFrames()-1;
		t = 0.f;
	}

	Transform local;
	for(int i=0; i<m_bvh->getPartCount(); ++i) {
		const BVH::Part* part = m_bvh->getPart(i);

		if(t > 0) {
			local.offset = lerp(part->motion[f].offset, part->motion[f+1].offset, t);
			local.rotation = slerp(part->motion[f].rotation, part->motion[f+1].rotation, t);
		} else {
			local = part->motion[f];
		}

		if(part->parent>=0) {
			local.offset = part->offset; // ?
			const Transform& parent = m_final[part->parent];
			m_final[i].offset   = parent.offset + parent.rotation * local.offset;
			m_final[i].rotation = parent.rotation * local.rotation;
		} else {
			m_final[i] = local;
		}
	}
}


// ------------------------------------------------- //

void View::updateCamera() {
	const vec3 up(0,1,0);
	vec3 z = m_camera - m_target;
	z.normalise();
	vec3 x = up.cross(z);
	x.normalise();
	vec3 y = z.cross(x);
	y.normalise();
	
	float* m = m_viewMatrix;
	m[3] = m[7] = m[11] = 0;
	m[0] = x.x; m[4] = x.y; m[8]  = x.z;
	m[1] = y.x; m[5] = y.y; m[9]  = y.z;
	m[2] = z.x; m[6] = z.y; m[10] = z.z;
	m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 1;
}

void View::updateProjection(float fov) {
	fov *= 3.141592653592f / 180.f;
	float aspect = (float) m_width / m_height;
	float f = 1.0f / tan(fov / 2.f);
	float* m = m_projectionMatrix;
	m[1]=m[2]=m[3]=m[4]=m[6]=m[7]=m[8]=m[9]=m[12]=m[13]=m[15] = 0.f;
	m[0] = f / aspect;
	m[5] = f;
	m[10] = (m_far + m_near) / (m_near - m_far);
	m[11] = -1.f;
	m[14] = (2.f * m_far * m_near) / (m_near - m_far);
}

void View::drawGrid() {
	int lines = 15;
	struct GridVertex { float x, y; int c; };
	static GridVertex* data = 0;

	if(!data) {
		data = new GridVertex[lines * 4];

		int k = 0;
		float s = lines / 2;
		float t = -s;
		for(int i=0; i<lines; ++i) {
			int c = i==lines/2? 0x008000: 0x808080;
			data[k].x = s;
			data[k].y = t;
			data[k].c = c;

			data[k+1].x = -s;
			data[k+1].y = t;
			data[k+1].c = c;

			k += 2;
			t += 1;
		}

		t = -s;
		for(int i=0; i<lines; ++i) {
			int c = i==lines/2? 0x00080: 0x808080;
			data[k].x = t;
			data[k].y = s;
			data[k].c = c;

			data[k+1].x = t;
			data[k+1].y = -s;
			data[k+1].c = c;
			k += 2;
			t += 1;
		}
	}

	// Draw it
	glEnableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, sizeof(GridVertex), data);
	glColorPointer(3, GL_UNSIGNED_BYTE, sizeof(GridVertex), &data[0].c);
	glDrawArrays(GL_LINES, 0, lines*4);
	glDisableClientState(GL_COLOR_ARRAY);
}

void View::drawBone() {
	static float vx[18] = { 0,0,0,  .06,.06,.1,  .06,-.06,0.1,  -.06,-.06,.1, -.06,.06,.1,  0,0,1 };
	static unsigned char ix[24] = { 0,1,2, 0,2,3, 0,3,4, 0,4,1,  1,5,2, 2,5,3, 3,5,4, 4,5,1 };
	glVertexPointer(3, GL_FLOAT, 0, vx);
	glDrawElements(GL_TRIANGLES, 24, GL_UNSIGNED_BYTE, ix);
}

