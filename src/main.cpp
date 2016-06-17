#include <SDL/SDL.h>
#include <GL/gl.h>
#include <cstdio>
#include <vector>
#include <string>

#include "view.h"
#include "directory.h"

struct App {
	int currentFile;
	View* mainView;
	View* activeView;
	std::vector<View*> views;
	std::vector< std::string > paths;
	std::vector< std::string > files;
} app;

// -------------------------------------------------------------------------------------- //

void addFile(const char* f) {
	app.files.push_back(f);
	printf("File: %s\n", f);
}
void addZip(const char*) {
}
void addDirectory(const char* dir, bool recursive) {
	printf("Path: %s\n", dir);
	for(size_t i=0; i<app.paths.size(); ++i) {
		if(app.paths[i] == dir) return;
	}

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

		} else {
			char file[1024];
			strcpy(file, argv[i]);
			int len = strlen(file);
			for(char* c=file+len; c>=file && *c!='/' && *c != '\\'; --c) *c = 0;
			if(file[0]==0) file[0] = '.';
			addDirectory(file, false);
			// Initial index
			for(size_t j=0; j<app.files.size(); ++j) {
				if(app.files[j] == argv[i]) {
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
		app.mainView->loadFile(app.files[0].c_str());
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
						printf("Load %d: %s\n", app.currentFile, app.files[app.currentFile].c_str());
						app.mainView->loadFile( app.files[app.currentFile].c_str() );
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

