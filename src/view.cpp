#include "view.h"
#include <SDL_opengl.h>
#include <cstdio>
#include <cstdlib>

View::View(int x, int y, int w, int h) : m_x(x), m_y(y), m_width(w), m_height(h), m_paused(false), m_state(EMPTY) {
	m_near = 0.1f;
	m_far = 1000.f;
	updateProjection();

	m_camera = vec3(10, 10, 10);
	updateCamera();
}

View::~View() {
}


bool View::loadFile(const char* file) {
	FILE* fp = fopen(file, "r");
	if(!fp) return false;
	fseek(fp, 0, SEEK_END);
	int len = ftell(fp);
	rewind(fp);
	char* content = new char[len+1];
	fread(content, 1, len, fp);
	content[len] = 0;
	fclose(fp);
	// Read bvh
	m_bvh = new BVH();
	bool r = m_bvh->load(content);
	return r;
}

void View::resize(int x, int y, int w, int h) {
	m_x = x;
	m_y = y;
	m_width = w;
	m_height = h;
	updateProjection();
}

void View::update(float time) {
	
}

void View::render() const {
	glViewport(m_x, m_y, m_width, m_height);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf(m_projectionMatrix);
	glMatrixMode(GL_MODELVIEW);
	glLoadMatrixf(m_viewMatrix);
	glTranslatef(-m_camera.x, -m_camera.y, -m_camera.z);


	glEnableClientState(GL_VERTEX_ARRAY);
	glRotatef(90, 1,0,0);
	drawGrid();
	glDisableClientState(GL_VERTEX_ARRAY);
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

