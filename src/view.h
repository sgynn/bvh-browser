#ifndef _VIEW_
#define _VIEW_

#include "transform.h"
#include "bvh.h"

/** Single bvh view */
class View {
	public:
	enum State { EMPTY, LOADING, LOADED, INVALID };

	View(int x, int y, int w, int h);
	~View();

	bool loadFile(const char* file);
	void resize(int x, int y, int w, int h);

	void render() const;
	void update(float time);

	State getState() const;

	protected:
	int m_x, m_y, m_width, m_height;
	char m_title[128];
	bool m_paused;
	State m_state;
	BVH*  m_bvh;

	float m_projectionMatrix[16];
	float m_viewMatrix[16];
	float m_near, m_far;
	vec3  m_camera;
	vec3  m_target;

	protected:
	void updateCamera();
	void updateProjection(float fov=90);
	static void drawGrid();
	static void drawBone();

};


#endif

