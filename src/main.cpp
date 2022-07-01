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
	int         activeIndex;			// current view index in single mode
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
		else {
			printf("Error loading %s\n", filename.c_str());
			delete bvh;
		}
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
	v->setText( file.name.c_str() );
	v->setState( View::QUEUED );

}
void cancelLoad(View* v) {
	MutexLock lock(app.loadMutex);
	for(size_t i=0; i<app.loadQueue.size(); ++i) {
		if(app.loadQueue[i].view == v) {
			app.loadQueue.erase( app.loadQueue.begin()+i);
			v->setState( View::EMPTY );
			break;
		}
	}
}
void cancelAll() {
	MutexLock lock(app.loadMutex);
	for(size_t i=0; i<app.loadQueue.size(); ++i) {
		app.loadQueue[i].view->setState( View::EMPTY );
	}
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

bool exportFile(const FileEntry& file) {
	printf("Exporting %s\n", file.name.c_str());
	const char* outFile = file.name.c_str();
	if(file.archive.empty()) {
		std::string filename = file.directory + "/" + file.name;
		FILE* fp = fopen(filename.c_str(), "r");
		if(!fp) { printf("Failed\n"); return false; }
		fseek(fp, 0, SEEK_END);
		size_t len = ftell(fp);
		rewind(fp);
		char* content = new char[len];
		fread(content, 1, len, fp);
		fclose(fp);
		
		fp = fopen(outFile, "w");
		if(fp) fwrite(content, 1, len, fp);
		delete [] content;
		return fp;
	}
	else {
		mz_zip_archive zipFile;
		memset(&zipFile, 0, sizeof(zipFile));
		mz_bool status = mz_zip_reader_init_file(&zipFile, file.archive.c_str(), 0);
		if(!status) return false;
		mz_zip_reader_extract_to_file(&zipFile, file.zipIndex, outFile, MZ_ZIP_FLAG_IGNORE_PATH);
		mz_zip_reader_end(&zipFile);
		return true;
	}
}

// -------------------------------------------------------------------------------------- //

void mainLoop();
void createViews();
void setupTiles(bool smooth);
void setLayout(AppMode layout);

int main(int argc, char* argv[]) {
	printf(
		"bvh-browser (c) Sam Gynn\n"
		"http://sam.draknek.org/projects/bvh-browser\n"
		"Distributed under GPL\n");
	
	app.activeIndex = -1;
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
					app.activeIndex = j;
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

	// Load font
	View::setFont("/usr/share/fonts/truetype/DejaVuSans.ttf", 16);	// ick - seems there is no search.

	// Set up views
	createViews();

	// Initial single mode
	if(app.activeIndex >= 0) {
		app.activeView = app.views[app.activeIndex];
		app.activeView->resize(0, 0, app.width, app.height, false);
		app.activeView->setVisible(true);
		// Load files
		for(int i=0; i<4; ++i) {
			int k = (app.activeIndex + i) % app.views.size();
			if(app.views[k]->getState() == View::EMPTY) {
				requestLoad(app.files[k], app.views[k]);
			}
		}
	} else {
		setLayout(VIEW_TILES);
		setupTiles(false);
	}


	mainLoop();

	return 0;

}

void createViews() {
	for(size_t i=app.views.size(); i<app.files.size(); ++i) {
		app.views.push_back( new View(0,0,1,1) );
	}
}

void setupTiles(bool smooth) {
	int columns = app.width / app.tileSize;
	for(size_t i=0; i<app.views.size(); ++i) {
		View* view = app.views[i];
		int x = i % columns * app.tileSize;
		int y = app.height - app.tileSize - i / columns * app.tileSize - app.scrollOffset;
		view->resize(x, y, app.tileSize, app.tileSize, smooth);
		view->setVisible(true);
	}
}

void setLayout(AppMode layout) {
	switch(layout) {
	case VIEW_SINGLE:	// Single view
		for(size_t i=0; i<app.views.size(); ++i) {
			app.views[i]->setVisible(false);
		}
		app.activeView->setVisible(true);
		app.activeView->resize(0,0,app.width,app.height, true);
		break;

	case VIEW_TILES: // Tile view
		setupTiles(true);
		break;
	}
	app.mode = layout;
}

int getViewAt(int mx, int my) {
	if(app.mode == VIEW_TILES) {
		my = app.height - my;
		for(size_t i=0; i<app.views.size(); ++i) {
			if(app.views[i]->contains(mx, my)) {
				return i;
			}
		}
	}
	else return app.activeIndex;
	return -1;
}

void selectView(int index) {
	if(index >= 0 && index < (int)app.views.size()) {
		app.activeIndex = index;
		app.activeView = app.views[index];
	}
}


void mainLoop() {
	bool running = true;
	SDL_Event event;
	uint ticks, lticks;
	ticks = lticks = SDL_GetTicks();
	bool rotate = false;
	bool moved = false;
	int keyMask = 0;
	int index = 0;

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
					if(app.tileSize > app.width) app.tileSize = app.width;
					if(app.mode == VIEW_SINGLE) {
						app.activeView->resize(0,0,app.width,app.height,false);
						app.activeView->autoZoom();
					}
					else {
						setupTiles(false);
					}
					break;

				}
				break;

			case SDL_DROPFILE:
				if(endsWith(event.drop.file, ".bvh")) {
					addFile(event.drop.file);
					createViews();
					selectView( app.views.size() - 1 );
					app.activeView = app.views.back();
					setLayout(VIEW_SINGLE);
				}
				SDL_free(event.drop.file);
				break;

			case SDL_MOUSEWHEEL:
				moved = true;
				if(app.mode == VIEW_TILES && (keyMask&3)) {
					app.tileSize *= 1.0 + event.wheel.y * 0.1;
					if(app.tileSize < 32) app.tileSize = 32;
					if(app.tileSize > app.width) app.tileSize = app.width;
					setupTiles(false);
				}
				else if(app.mode == VIEW_TILES && !rotate) {
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
				index = getViewAt(event.button.x, event.button.y);
				selectView(index);
				break;

			case SDL_MOUSEBUTTONUP:
				rotate = false;
				if(!moved && app.activeView) {
					if(app.mode == VIEW_TILES) {
						setLayout(VIEW_SINGLE);
					}
					else {
						setLayout(VIEW_TILES);
					}
				}
				break;

			case SDL_KEYDOWN:
				if(event.key.keysym.sym == SDLK_z) app.activeView->autoZoom();
				if(event.key.keysym.sym == SDLK_SPACE) app.activeView->togglePause();

				// Mask
				if(event.key.keysym.sym == SDLK_LCTRL)  keyMask |= 0x01;
				if(event.key.keysym.sym == SDLK_RCTRL)  keyMask |= 0x02;
				if(event.key.keysym.sym == SDLK_LSHIFT) keyMask |= 0x04;
				if(event.key.keysym.sym == SDLK_RSHIFT) keyMask |= 0x08;
				if(event.key.keysym.sym == SDLK_LALT)   keyMask |= 0x10;
				if(event.key.keysym.sym == SDLK_RALT)   keyMask |= 0x20;

				// Navigation
				if(app.files.size() > 1 && app.mode == VIEW_SINGLE) {
					int m = 0;
					if(event.key.keysym.sym == SDLK_LEFT) m = -1;
					if(event.key.keysym.sym == SDLK_RIGHT) m = 1;
					if(m != 0) {
						int count = app.views.size();
						index = (app.activeIndex + m + count) % count;
						app.activeView->setVisible(false);
						selectView(index);
						app.activeView->setVisible(true);
						app.activeView->resize(0,0,app.width,app.height, false);
						// Load files (with look ahead)
						for(int i=0; i<4; ++i) {
							int k = (index + i) % count;
							if(app.views[k]->getState() == View::EMPTY) {
								requestLoad(app.files[k], app.views[k]);
							}
						}
					}
				}

				// Escape
				if(event.key.keysym.sym == SDLK_ESCAPE) {
					if(app.mode == VIEW_SINGLE) setLayout(VIEW_TILES);
					else running = false;
				}

				// Export test
				if(event.key.keysym.sym == SDLK_s) {
					exportFile(app.files[app.activeIndex]);
				}

				break;

			case SDL_KEYUP:
				if(event.key.keysym.sym == SDLK_LCTRL)  keyMask &= ~0x01;
				if(event.key.keysym.sym == SDLK_RCTRL)  keyMask &= ~0x02;
				if(event.key.keysym.sym == SDLK_LSHIFT) keyMask &= ~0x04;
				if(event.key.keysym.sym == SDLK_RSHIFT) keyMask &= ~0x08;
				if(event.key.keysym.sym == SDLK_LALT)   keyMask &= ~0x10;
				if(event.key.keysym.sym == SDLK_RALT)   keyMask &= ~0x20;
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
				if(app.activeView) app.activeView->rotateView(-mx*0.01, my*0.01);
				moved |= mx || my;
			}
			

			
			// Update all views
			lticks = ticks;
			ticks = SDL_GetTicks();
			float time = (ticks - lticks) * 0.001; // ticks in miliseconds


			int count = 0;
			switch(app.mode) {
			case VIEW_SINGLE:
				if(app.activeView) {
					glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
					app.activeView->update(time);
					app.activeView->render();
				}
				break;
			case VIEW_TILES:
				// Update all visible views
				for(size_t i=0; i<app.views.size(); ++i) {
					View* view = app.views[i];
					if(view->top() > app.height) continue;
					if(view->bottom() <= 0) break;
					if(view->getState() == View::EMPTY) {
						requestLoad(app.files[i], view);
					}
					view->update(time);
				}

				// Render everything
				glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
				if(app.mode == VIEW_TILES) {
					for(size_t i=0; i<app.views.size(); ++i) {
						if(app.views[i]->top() > app.height) continue;
						if(app.views[i]->bottom() <= 0) break;
						if(app.views[i] != app.activeView) app.views[i]->render();
						++count;
					}
				}
				if(app.activeView) app.activeView->render();
				break;
			}

			// Limit to 60fps?
			uint t = SDL_GetTicks() - ticks;
			if(t < 10) SDL_Delay(10 - t);
			else SDL_Delay(1);

			static char buffer[128];
			sprintf(buffer, "%d %x\n", t, keyMask);
			SDL_SetWindowTitle(app.window, buffer);

			SDL_GL_SwapWindow(app.window);
		}
	}
}

