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
	void setCamera(float yaw, float pitch, float zoom);
	void rotateView(float yaw, float pitch);
	void zoomView(float mult);
	void autoZoom();

	void render() const;
	void update(float time);
	void togglePause();

	State getState() const;

	protected:
	int m_x, m_y, m_width, m_height;
	char m_title[128];
	bool m_paused;
	State m_state;

	BVH*       m_bvh;
	Transform* m_final;
	float      m_frame;

	float m_projectionMatrix[16];
	float m_viewMatrix[16];
	float m_near, m_far;
	vec3  m_camera;
	vec3  m_target;

	protected:
	void updateBones(float frame);
	void updateCamera();
	void updateProjection(float fov=90);
	float zoomToFit(const vec3& point, const vec3& dir, const vec3* n, float* d);
	static void drawGrid();
	static void drawBone();

};


#endif

