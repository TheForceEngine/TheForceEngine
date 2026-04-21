#pragma once
#include <TFE_System/types.h>
#include <vector>
#include <string>

using namespace std;
typedef vector<string> FileList;

namespace FileUtil
{
	void readDirectory(const char* dir, const char* ext, FileList& fileList);
	bool makeDirectory(const char* dir);
	void getCurrentDirectory(char* dir);
	void getExecutionDirectory(char* dir);
	void setCurrentDirectory(const char* dir);

	void readSubdirectories(const char* dir, FileList& dirList);

	void getFileNameFromPath(const char* path, char* name, bool includeExt=false);
	void getFilePath(const char* filename, char* path);
	void getFileExtension(const char* filename, char* extension);

	void copyFile(const char* srcFile, const char* dstFile);
	void deleteFile(const char* srcFile);

	bool exists(const char* path);
	bool directoryExists(const char* path, char* outPath = nullptr);
	u64  getModifiedTime(const char* path);

	void fixupPath(char* path);
	void convertToOSPath(const char* path, char* pathOS);

	void replaceExtension(const char* srcPath, const char* newExt, char* outPath);
	void stripExtension(const char* srcPath, char* outPath);
}
