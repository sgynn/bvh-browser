#include <SDL2/SDL.h>
#include <GL/gl.h>
#include <cstdio>
#include <vector>
#include <string>
#include <set>

#include "view.h"
#include "directory.h"
#include "thread.h"

#include "miniz.c"

using namespace base;

struct FileEntry {
	std::string directory;	// File directory or zip file
	std::string name;		// File name
	std::string archive;	// Directory is a zip file
	int         zipIndex;	// Index of file in archive
};

struct LoadRequest {
	FileEntry file;		// File to load
	View*     view;		// Target view
};

enum AppMode { VIEW_SINGLE, VIEW_TILES };

struct App {
	SDL_Window* window;					// app window
	int         currentFile;			// current file of main view
	View*       mainView;				// main view
	View*       activeView;				// view accepting input
	AppMode     mode;					// current mode
	int         scrollOffset;			// Scroll offset in tile view
	std::vector<View*> views;			// all views
	std::set< std::string > paths;		// directorys - to avoid duplication
	std::vector< FileEntry > files;		// all bvh files found
	int width, height;					// window size
	int tileSize;						// tile size for tiled view

	base::Thread loadThread;				// loading thread
	base::Mutex  loadMutex;					// Loading mutex
	std::vector<LoadRequest> loadQueue;		// Queue of views to be loaded
} app;

// -------------------------------------------------------------------------------------- //

inline bool endsWith(const char* s, const char* end) {
	int sl = strlen(s);
	int el = strlen(end);
	return sl >= el && strcmp(s+sl-el, end) == 0;
}
inline const char* getName(const char* path) {
	const char* c = strrchr(path, '/');
	if(!c) c = strrchr(path, '\\');
	return c? c + 1: path;
}
inline std::string getDirectory(const char* path) {
	const char* c = strrchr(path, '/');
	if(!c) c = strrchr(path, '\\');
	return c? std::string(path, c-path): std::string(".");
}

// -------------------------------------------------------------------------------------- //

void addFile(const char* f) {
	FileEntry file;
	file.name = getName(f);
	file.directory = getDirectory(f);
	app.files.push_back(file);
	printf("File: %s\n", f);
}
int addZip(const char* f) {
	mz_zip_archive zipFile;
	memset(&zipFile, 0, sizeof(zipFile));
	mz_bool status = mz_zip_reader_init_file(&zipFile, f, 0);
	if(!status) {
		printf("Failed to open zip file %s\n", f);
		return -1;
	}
	// read directory info
	int files = mz_zip_reader_get_num_files(&zipFile);
	for(int i=0; i<files; ++i) {
		mz_zip_archive_file_stat stat;
		if(mz_zip_reader_file_stat(&zipFile, i, &stat)) {
			if( endsWith(stat.m_filename, ".bvh") ) {
				printf("File %s\n", stat.m_filename);
				FileEntry file;
				file.directory = getDirectory(stat.m_filename);
				file.name = getName(stat.m_filename);
				file.archive = f;
				file.zipIndex = i;
				app.files.push_back(file);
			}
		} else {
			printf("Failed to get file info from archive %s\n", f);
			mz_zip_reader_end(&zipFile);
			return -1;
		}
	}

	mz_zip_reader_end(&zipFile);
	return 0;
}
void addDirectory(const char* dir, bool recursive) {
	printf("Path: %s\n", dir);
	if(app.paths.find(dir) != app.paths.end()) return;

	char buffer[2048];
	Directory d( dir );
	for(Directory::iterator i=d.begin(); i!=d.end(); ++i) {
		if(i->type == Directory::DIRECTORY && recursive && i->name[0]!='.') {
			snprintf(buffer, 2048, "%s%s", dir, i->name);
			addDirectory(buffer, true);
		}
		else if(strcmp(i->name + i->ext, "bvh")==0) {
			snprintf(buffer, 2048, "%s/%s", dir, i->name);
			addFile(buffer);
		}
	}
}


// -------------------------------------------------------------------------------------- //

BVH* loadFile(const FileEntry& file) {
	printf("Load %s\n", file.name.c_str());
	if(file.archive.empty()) {
		std::string filename = file.directory + "/" + file.name;
		FILE* fp = fopen(filename.c_str(), "r");
		if(!fp) { printf("Failed\n"); return 0; }
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
		if(r) return bvh;
		else delete bvh;
		return 0;
	}
	else {
		bool result = false;
		mz_zip_archive zipFile;
		memset(&zipFile, 0, sizeof(zipFile));
		mz_bool status = mz_zip_reader_init_file(&zipFile, file.archive.c_str(), 0);
		if(!status) return 0;
		BVH* bvh = 0;
		size_t size;
		void* p = mz_zip_reader_extract_to_heap(&zipFile, file.zipIndex, &size, 0);
		if(p) {
			((char*)p)[size-1] = 0;
			bvh = new BVH();
			result = bvh->load((const char*)p);
			if(!result) { delete bvh; bvh = 0; }
			mz_free(p);
		}
		mz_zip_reader_end(&zipFile);
		return bvh;
	}
}

void requestLoad(const FileEntry& file, View* v) {
	MutexLock lock(app.loadMutex);
	LoadRequest r;
	r.file = file;
	r.view = v;
	app.loadQueue.push_back(r);

}
void cancelLoad(View* v) {
	MutexLock lock(app.loadMutex);
	for(size_t i=0; i<app.loadQueue.size(); ++i) {
		if(app.loadQueue[i].view == v) {
			app.loadQueue.erase( app.loadQueue.begin()+i);
			break;
		}
	}
}
void cancelAll() {
	MutexLock lock(app.loadMutex);
	app.loadQueue.clear();
}
void loadThreadFunc(bool* running) {
	printf("Load thread started\n");
	while(*running) {
		LoadRequest next;
		next.view = 0;
		{
			MutexLock lock(app.loadMutex);
			if(!app.loadQueue.empty()) {
				next = app.loadQueue.front();
				app.loadQueue.erase(app.loadQueue.begin());
			}
		}
		if(next.view) {
			next.view->setState(View::LOADING);
			BVH* bvh = loadFile(next.file);
			next.view->setBVH(bvh, next.file.name.c_str());
			next.view->autoZoom();
			next.view->setState(bvh? View::LOADED: View::INVALID);
		}
		Thread::sleep(10);
	}
	printf("Load thread ended\n");
}


// -------------------------------------------------------------------------------------- //

void mainLoop();

int main(int argc, char* argv[]) {
	printf(
		"bvh-browser (c) Sam Gynn\n"
		"http://sam.draknek.org/projects/bvh-browser\n"
		"Distributed under GPL\n");
	
	app.currentFile = 0;
	app.mode = VIEW_SINGLE;
	app.scrollOffset = 0;
	
	// Parse arguments
	for(int i=1; i<argc; ++i) {
		// valid: bvh, zip, directory
		if(isDirectory(argv[i])) {
			addDirectory( argv[i], true );

		} else if(endsWith(argv[i], ".zip")) {
			addZip(argv[i]);

		} else {
			std::string dir = getDirectory(argv[i]);
			addDirectory(dir.c_str(), false);

			// Initial index
			const char* name = getName(argv[i]);
			for(size_t j=0; j<app.files.size(); ++j) {
				if(app.files[j].name == name) {
					app.currentFile=j;
					break;
				}
			}
		}
	}


	// setup SDL window
	app.width = 1280;
	app.height = 1024;
	app.tileSize = 256;
	const char* title = "bvh-browser";
	int flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;

	int r = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	if(r<0) {
		fprintf(stderr, "Unable to initialise SDL: %s\n", SDL_GetError());
		return 1;
	}

	atexit(SDL_Quit);
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1);
	app.window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, app.width, app.height, flags);
	if(!app.window) {
		fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
		SDL_Quit();
		return 2;
	}

	SDL_GL_CreateContext(app.window);

	glEnable(GL_DEPTH_TEST);

	// Set up views
	app.mainView = new View(0,0,app.width,app.height);
	app.activeView = app.mainView;
	app.views.push_back(app.mainView);

	if(!app.files.empty()) {
		requestLoad(app.files[app.currentFile], app.mainView);
	}

	mainLoop();

	return 0;

}

void setupTiles() {
	int columns = app.width / app.tileSize;
	for(size_t i=0; i<app.files.size(); ++i) {
		// create view
		View* view;
		if(i < app.views.size()) view = app.views[i];
		else {
			view = new View(0,0,1,1);
			app.views.push_back(view);
		} 

		// Position view
		int x = i % columns * app.tileSize;
		int y = app.height - app.tileSize - i / columns * app.tileSize - app.scrollOffset;
		view->resize(x, y, app.tileSize, app.tileSize, true);
		view->setVisible(true);

		// Load data
		if(view->getState() == View::EMPTY) {
			requestLoad(app.files[i], view);
		}
	}
}

void setLayout(AppMode layout) {
	switch(layout) {
	case VIEW_SINGLE:	// Single view
		for(size_t i=0; i<app.views.size(); ++i) {
			if(app.views[i] != app.activeView) app.views[i]->setVisible(false);
		}
		app.activeView->setVisible(true);
		app.activeView->resize(0,0,app.width,app.height, true);
		break;

	case VIEW_TILES: // Tile view
		setupTiles();
		break;
	}
	app.mode = layout;
}


void mainLoop() {
	bool running = true;
	SDL_Event event;
	uint ticks, lticks;
	ticks = lticks = SDL_GetTicks();
	bool rotate = false;
	bool moved = false;

	// start load thread
	app.loadThread.begin(&loadThreadFunc, &running);

	while(running) {
		if(SDL_PollEvent(&event)) {
			switch(event.type) {
			case SDL_QUIT:
				running = false;
				break;

			case SDL_WINDOWEVENT:
				switch(event.window.event) {
				case SDL_WINDOWEVENT_RESIZED:
				case SDL_WINDOWEVENT_SIZE_CHANGED:
					app.width = event.window.data1;
					app.height = event.window.data2;
					app.mainView->resize(0, 0, app.width, app.height);
					app.mainView->autoZoom();
					break;

				}
				break;

			case SDL_DROPFILE:
				if(endsWith(event.drop.file, ".bvh")) {
					addFile(event.drop.file);
				}
				SDL_free(event.drop.file);
				break;

			case SDL_MOUSEWHEEL:
				if(app.mode == VIEW_TILES) {
					int offset = event.wheel.y * 48;
					if(offset>0 && app.scrollOffset >=0) break;

					app.scrollOffset += offset;
					for(size_t i=0; i<app.views.size(); ++i) {
						app.views[i]->move(0, -offset);
					}
				}
				else {
					app.activeView->zoomView( 1.0 - event.wheel.y * 0.1);
				}
				break;

			case SDL_MOUSEBUTTONDOWN:
				moved = false;
				rotate = true;
				break;

			case SDL_MOUSEBUTTONUP:
				rotate = false;
				if(!moved) {
					if(app.mode == VIEW_TILES) {
						int mx = event.button.x;
						int my = app.height - event.button.y;
						for(size_t i=0; i<app.views.size(); ++i) {
							if(app.views[i]->contains(mx, my)) {
								app.activeView = app.views[i];
								setLayout(VIEW_SINGLE);
								break;
							}
						}
					}
					else {
						setLayout(VIEW_TILES);
					}
				}
				break;

			case SDL_KEYDOWN:
				if(event.key.keysym.sym == SDLK_z) app.activeView->autoZoom();
				if(event.key.keysym.sym == SDLK_SPACE) app.activeView->togglePause();
				if(event.key.keysym.sym == SDLK_t) setLayout(VIEW_TILES);

				if(app.files.size() > 1) {
					int m = 0;
					if(event.key.keysym.sym == SDLK_LEFT) m = -1;
					if(event.key.keysym.sym == SDLK_RIGHT) m = 1;
					if(m!=0) {
						app.currentFile = (app.currentFile + m + app.files.size()) % app.files.size();
						requestLoad(app.files[ app.currentFile], app.mainView);
					}
				}

			case SDL_KEYUP:
				if(event.key.keysym.sym == SDLK_ESCAPE) running = false;
				break;

			default:
				break;
			}
		}

		else {
			// Rotation
			int mx, my;
			SDL_GetRelativeMouseState(&mx, &my);
			if(rotate) {
				app.activeView->rotateView(-mx*0.01, my*0.01);
				moved |= mx || my;
			}
			

			
			// Update all views
			lticks = ticks;
			ticks = SDL_GetTicks();
			float time = (ticks - lticks) * 0.001; // ticks in miliseconds

			for(size_t i=0; i<app.views.size(); ++i) {
				app.views[i]->update(time);
			}

			// Render everything
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
			for(size_t i=0; i<app.views.size(); ++i) {
				if(app.views[i] != app.activeView) app.views[i]->render();
			}
			if(app.activeView) app.activeView->render();

			// Limit to 60fps?
			uint t = ticks - lticks;
			if(t < 10) SDL_Delay(10 - t);

			SDL_GL_SwapWindow(app.window);
		}
	}
}

