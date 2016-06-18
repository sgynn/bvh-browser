#include <SDL/SDL.h>
#include <GL/gl.h>
#include <cstdio>
#include <vector>
#include <string>
#include <set>

#include "view.h"
#include "directory.h"

#include "miniz.c"

struct FileEntry {
	std::string directory;	// File directory or zip file
	std::string name;		// File name
	std::string archive;	// Directory is a zip file
	int         zipIndex;	// Index of file in archive
};

struct App {
	int currentFile;
	View* mainView;
	View* activeView;
	std::vector<View*> views;
	std::set< std::string > paths;
	std::vector< FileEntry > files;
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

bool loadFile(const FileEntry& file, View* view) {
	printf("Load %s\n", file.name.c_str());
	if(file.archive.empty()) {
		return view->loadFile( (file.directory + "/" + file.name).c_str() );

	} else {
		bool result = false;
		mz_zip_archive zipFile;
		memset(&zipFile, 0, sizeof(zipFile));
		mz_bool status = mz_zip_reader_init_file(&zipFile, file.archive.c_str(), 0);
		if(!status) return false;
		size_t size;
		void* p = mz_zip_reader_extract_to_heap(&zipFile, file.zipIndex, &size, 0);
		if(p) {
			((char*)p)[size-1] = 0;
			BVH* bvh = new BVH();
			result = bvh->load((const char*)p);
			view->setBVH(result? bvh: 0);
			if(!result) delete bvh;
			mz_free(p);
		}
		mz_zip_reader_end(&zipFile);
		return result;
	}
}


// -------------------------------------------------------------------------------------- //

void mainLoop();

int main(int argc, char* argv[]) {
	printf(
		"bvh-browser (c) Sam Gynn\n"
		"http://sam.draknek.org/projects/bvh-browser\n"
		"Distributed under GPL\n");
	
	app.currentFile = 0;
	
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
	int width = 1280;
	int height = 1024;

	int r = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
	if(r<0) {
		fprintf(stderr, "Unable to initialise SDL: %s\n", SDL_GetError());
		return 1;
	}

	atexit(SDL_Quit);
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1);
	if(!SDL_SetVideoMode( width, height, 32, SDL_OPENGL | SDL_RESIZABLE)) {
		fprintf(stderr, "Unable to create window: %s\n", SDL_GetError());
		SDL_Quit();
		return 2;
	}

	SDL_WM_SetCaption("bvh-viewer", 0);

	glEnable(GL_DEPTH_TEST);

	// Set up views
	app.mainView = new View(0,0,width,height);
	app.activeView = app.mainView;
	app.views.push_back(app.mainView);

	if(!app.files.empty()) {
		loadFile(app.files[app.currentFile], app.mainView);
		app.mainView->autoZoom();
	}

	mainLoop();

	return 0;

}


void mainLoop() {
	bool running = true;
	SDL_Event event;
	uint ticks, lticks;
	ticks = lticks = SDL_GetTicks();
	bool rotate = false;

	while(running) {
		if(SDL_PollEvent(&event)) {
			switch(event.type) {
			case SDL_QUIT:
				running = false;
				break;
			case SDL_VIDEORESIZE:
				SDL_SetVideoMode( event.resize.w, event.resize.h, 32, SDL_OPENGL | SDL_RESIZABLE);
				app.mainView->resize(0, 0, event.resize.w, event.resize.h);
				app.mainView->autoZoom();
				break;

			case SDL_ACTIVEEVENT:
				break;

			case SDL_MOUSEBUTTONDOWN:
				if(event.button.button == SDL_BUTTON_WHEELUP) app.activeView->zoomView(0.9);
				if(event.button.button == SDL_BUTTON_WHEELDOWN) app.activeView->zoomView(1.1);
				rotate = true;
				break;

			case SDL_MOUSEBUTTONUP:
				rotate = false;
				break;

			case SDL_KEYDOWN:
				if(event.key.keysym.sym == SDLK_z) app.activeView->autoZoom();
				if(event.key.keysym.sym == SDLK_SPACE) app.activeView->togglePause();

				if(app.files.size() > 1) {
					int m = 0;
					if(event.key.keysym.sym == SDLK_LEFT) m = -1;
					if(event.key.keysym.sym == SDLK_RIGHT) m = 1;
					if(m!=0) {
						app.currentFile = (app.currentFile + m + app.files.size()) % app.files.size();
						loadFile(app.files[ app.currentFile], app.mainView);
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
			}
			

			
			// Update all views
			lticks = ticks;
			ticks = SDL_GetTicks();
			float time = (ticks - lticks) * 0.001; // ticks in miliseconds

			for(size_t i=0; i<app.views.size(); ++i) {
				app.views[i]->update(time);
			}

			// Render everything
			for(size_t i=0; i<app.views.size(); ++i) {
				app.views[i]->render();
			}

			// Limit to 60fps?
			uint t = ticks - lticks;
			if(t < 10) SDL_Delay(10 - t);

			SDL_GL_SwapBuffers();
		}
	}
}

