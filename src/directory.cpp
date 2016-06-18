
#include "directory.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>


#ifdef WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#endif

// File list sorting functor //
struct SortFiles {
	Directory::File* s;
	SortFiles(Directory::File* s) : s(s) {}
	bool operator()(const Directory::File& a, const Directory::File& b) const {
		if(a.type!=b.type) return a.type>b.type; // List folders at the top?

		// Case insensitive matching
		//int r = strncmp(a.name, b.name, 8);
		const char* u=a.name;
		const char* v=a.name;
		while(*u && (*u == *v || *u+32==*v || *u==*v+32)) ++u, ++v;
		return *u - *v;
	}
};


Directory::Directory(const char* path) {
	strncpy(m_path, path, 2048);
	m_path[2047] = 0;
}
Directory::~Directory() {
}

/** Scan directory for files */
int Directory::scan() {
	m_files.clear();

	//----------------------------- WINDOWS ---------------------------- //
	
	#ifdef WIN32

	int tLen = strlen(m_path);
	#ifdef UNICODE // Using unicode?
	wchar_t *dir = new wchar_t[tLen+3];
	mbstowcs(dir, m_path, tLen+1);
	#else
	char *dir = new char[tLen+3];
	strcpy(dir, m_path);
	#endif
	//this string must be a search pattern, so add /* to path
	dir[tLen+0] = '/';	
	dir[tLen+1] = '*';
	dir[tLen+2] = 0;
	// Search
	WIN32_FIND_DATA findFileData;
	HANDLE hFind = FindFirstFile(dir, &findFileData);	
	if(hFind  != INVALID_HANDLE_VALUE) {
		do {
			File file;
			// Convert to char* from wchar_t
			TCHAR* wName = findFileData.cFileName;
			for(int i=0; i<128 && (!i||wName[i-1]); i++) file.name[i] = (char) wName[i];

			// Is it a directory?
			if(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) file.type=DIRECTORY;
			else file.type = Directory::FILE;

			//extract extension
			for(file.ext=0; file.name[file.ext]; ++file.ext) {
				if(file.ext && file.name[file.ext-1]=='.') break;
			}
			m_files.push_back(file);
		} while(FindNextFile(hFind, &findFileData));
		FindClose(hFind);
	}
	delete [] dir;

	// ---------------------------- LINUX -------------------------------- //
	
	#else

	DIR* dp;
	struct stat st;
	struct dirent *dirp;
	char buffer[1024];
	if((dp = opendir(m_path))) {
		while((dirp = readdir(dp))) {
			File file;
			strcpy(file.name, dirp->d_name);

			//is it a file or directory
			snprintf(buffer, 128, "%s/%s", m_path, dirp->d_name);
			stat(buffer, &st);
			if(S_ISDIR(st.st_mode)) file.type = DIRECTORY;
			else file.type = Directory::FILE;

			//extract extension
			for(file.ext=0; file.name[file.ext]; ++file.ext) {
				if(file.ext && file.name[file.ext-1]=='.') break;
			}
			m_files.push_back(file);
		}
	}
	#endif

	std::sort(m_files.begin(), m_files.end(), SortFiles(&m_files[0]));
	return m_files.size();
}


bool Directory::contains( const char* file ) {
	if(m_files.empty()) scan();
	for(iterator i=m_files.begin(); i!=m_files.end(); i++) {
		if(strcmp(i->name, file)==0) return true;
	}
	return false;
}

bool isDirectory(const char* path) {
	#ifdef WIN32
	return GetFileAttributes(path) & FILE_ATTRIBUTE_DIRECTORY;
	#else
	struct stat st;
	stat(path, &st);
	return S_ISDIR(st.st_mode);
	#endif
}


