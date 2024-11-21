#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <TFE_System/system.h>
#include "fileutil.h"
#include "filestream.h"
#ifdef __APPLE__
#include <mach-o/dyld.h>	// For macOS-specific executable path
#endif

// implement TFE FileUtil for Linux and compatibles.
namespace FileUtil
{
	bool existsNoCase(const char *filename);
	static char *findFileObjectNoCase(const char *filename, bool objisdir);

	void readDirectory(const char *dir, const char *ext, FileList& fileList)
	{
		char buf[PATH_MAX];
		struct dirent *de;
		struct stat st;
		int el, dl, ret;
		char *dn;
		DIR *d;

		d = opendir(dir);
		if (!d) {
			TFE_System::logWrite(LOG_ERROR, "readDirectory", "opendir(%s) failed with %d\n", dir, errno);
			return;
		}

		el = strlen(ext);
		while (NULL != (de = readdir(d))) {
			dn = de->d_name;
			dl = strlen(dn);
			memset(buf, 0, PATH_MAX);
			snprintf(buf, PATH_MAX - 1, "%s%s", dir, de->d_name);
			ret = stat(buf, &st);
			if (ret || !S_ISREG(st.st_mode))
				continue;
			
			// skip dotfiles, dotdirs, too short and files without extensions.
			if ((dl < (el + 2)) || (dn[0] == '.'))
				continue;
			if (dn[dl - el - 1] != '.')
				continue;
			// does the extension match (unix: case sensitive!)
			if (0 != strncasecmp(dn + dl - el, ext, el))
				continue;
			// we have a winner
			fileList.push_back(string(dn));
		}
		closedir(d);
	}

	void readSubdirectories(const char *dir, FileList& dirList)
	{
		char *dn, fp[PATH_MAX];
		struct dirent *de;
		struct stat st;
		int l1, l2, ret;
		DIR *d;

		d = opendir(dir);
		if (!d) {
			TFE_System::logWrite(LOG_ERROR, "readSubdirctories", "opendir(%s) failed with %d\n", dir, errno);
			return;
		}
		l1 = strlen(dir);
		while (NULL != (de = readdir(d))) {
			dn = de->d_name;
			memset(fp, 0, PATH_MAX);
			snprintf(fp, PATH_MAX - 2, "%s%s", dir, dn);
			ret = stat(fp, &st);
			if (ret || !S_ISDIR(st.st_mode))
				continue;
					
			if (!strcmp(dn, ".") || !strcmp(dn, ".."))
				continue;

			strcat(fp, "/");
			dirList.push_back(string(fp));
		}
		closedir(d);
	}

	bool makeDirectory(const char *dir)
	{
		if (!mkdir(dir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) || errno == EEXIST)
			return true;
		return false;
	}

	void getCurrentDirectory(char *dir)
	{
		char *err = getcwd(dir, TFE_MAX_PATH);
		/* should not happen, but just to be sure */
		if (err == NULL) {
			TFE_System::logWrite(LOG_ERROR, "getCurrentDirectory", "getcwd() error %d", errno);
			memset(dir, 0, TFE_MAX_PATH);
			/* substitute home */
			strcpy(dir, getenv("HOME"));
		}
	}

#ifdef __APPLE__

	void getExecutionDirectory(char *dir)
	{
		char exe[PATH_MAX];
		uint32_t size = PATH_MAX;

		if (_NSGetExecutablePath(exe, &size) != 0) {
			TFE_System::logWrite(LOG_CRITICAL, "getExecutionDirectory", "Failed to retrieve executable path");
			return;
		}

		// Resolve symbolic links, if any.
		realpath(exe, dir);

		// kill trailing slash
		char *c = strrchr(dir, '/');
		if (c)
			*c = '\0';
	}

#else 	// Linux

	void getExecutionDirectory(char *dir)
	{
		char exe[PATH_MAX], *c;
		ssize_t r;

		memset(dir, 0, TFE_MAX_PATH);
		memset(exe, 0, PATH_MAX);
		sprintf(exe, "/proc/%d/exe", getpid());
		r = readlink(exe, dir, PATH_MAX);
		if (r < 0) {
			TFE_System::logWrite(LOG_CRITICAL, "getExecutionDirectory", "readlink %s failed with %d", exe, errno);
			return;
		}

		// kill trailing slash
		c = strrchr(dir, '/');
		if (c)
			*c = 0;
	}
#endif

	void setCurrentDirectory(const char *dir)
	{
		int ret = chdir(dir);
		if (ret) {
			TFE_System::logWrite(LOG_ERROR, "chdir(%s) failed with %d", dir, errno);
		}
	}

	void getFilePath(const char *fn, char *path)
	{
		char *tmpbuf, *dn;
		int l;

		memset(path, 0, TFE_MAX_PATH);
		if (!fn)
			return;
		l = strlen(fn);
		if (!l)
			return;

		// dirname() messes with the input buffer - make a copy.
		tmpbuf = (char *)malloc(l + 1);
		if (!tmpbuf)
			return;
		strcpy(tmpbuf, fn);
		dn = dirname(tmpbuf);
		sprintf(path, "%s/", dn);
		free(tmpbuf);
	}

	void getFileExtension(const char *filename, char *extension)
	{
		const char *c;

		c = strrchr(filename, '.');
		if (c) {
			strcpy(extension, c + 1);
		}
	}

	void getFileNameFromPath(const char *path, char *name, bool ext)
	{
		char *tmpbuf, *fn, *c;

		// basename() messes with the input buffer - make a copy.
		memset(name, 0, TFE_MAX_PATH);
		tmpbuf = (char *)malloc(TFE_MAX_PATH + 1);
		if (!tmpbuf)
			return;
		strcpy(tmpbuf, path);
		fn = basename(tmpbuf);
		strcpy(name, fn);
		if (!ext) {
			// find the last dot and null it.
			c = strrchr(name, '.');
			if (c)
				*c = 0;
		}
		free(tmpbuf);
	}

	void copyFile(const char *src, const char *dst)
	{
		ssize_t rd, wr;
		char buf[1024], *fnd;
		int s, d;

		// assume the source is case-insensitive
		fnd = findFileObjectNoCase(src, false);
		if (!fnd)
			return;

		s = open(fnd, O_RDONLY);
		free(fnd);
		if (!s)
			return;
		d = open(dst, O_WRONLY | O_CREAT, 00644);
		if (!d) {
			close(s);
			return;
		}

		do {
			rd = read(s, buf, 1024);
			if (rd <= 0)
				break;
			wr = write(d, buf, rd);
			if (wr < 0 || wr != rd)
				break;
		} while (rd > 0);
		close(d);
		close(s);
	}

	void deleteFile(const char *fn)
	{
		int ret = unlink(fn);
		if (ret) {
			TFE_System::logWrite(LOG_WARNING, "deleteFile", "unlink(%s) failed with %d\n", fn, errno);
		}
	}

	bool directoryExits(const char *path, char *outPath)
	{
		char *ret;

		ret = findFileObjectNoCase(path, true);
		if (ret == NULL)
			return false;

		// replace input with found path
		if (outPath)
			snprintf(outPath, TFE_MAX_PATH, "%s/", ret);
		free(ret);
		return true;
	}

	bool exists(const char *path)
	{
		return existsNoCase(path);
	}

	// Linux filesystems are case-sensitive; try to open the
	// base directory and find a file/directory that has the same name
	// with different case.
	// Caller MUST free the pointer returned by this function!
	// Input: full path to file or dir: /abs/path/to/object
	// This function will try to find "object" with differently cased name
	// in path "/abs/path/to/".
	static char *findFileObjectNoCase(const char *filename, bool objisdir)
	{
		char *fncopy, *dn, *fn;
		char *result = NULL;
		char buf[PATH_MAX];
		int ol, dl, fl, nfl, ret;
		struct stat st;
		struct dirent *de;
		DIR *dir;

		// dirname() and basename() screw with the input buffer,
		// hence we need to make 2 copies.
		ol = strlen(filename);
		fncopy = (char *)malloc(ol * 2 + 2);
		if (!fncopy)
			return NULL;
		memset(fncopy, 0, ol + ol + 2);
		strncpy(fncopy, filename, ol);
		strncpy(fncopy + ol + 1, filename, ol);

		dn = dirname(fncopy);
		fn = basename(fncopy + ol +1);

		dl = strlen(dn);
		fl = strlen(fn);
		if ((dl < 1) || (fl < 1)) {
			free(fncopy);
			return NULL;
		}

		dir = opendir(dn);
		if (!dir) {
			free(fncopy);
			return NULL;
		}

		while (NULL != (de = readdir(dir)))
		{
			memset(buf, 0, PATH_MAX);
			snprintf(buf, PATH_MAX - 2, "%s/%s", dn, de->d_name);
			ret = stat(buf, &st);
			if (ret != 0)
				continue;
			if (objisdir) {
				if (!S_ISDIR(st.st_mode))
					continue;
			} else {
				if (!S_ISREG(st.st_mode))
					continue;
			}

			nfl = strlen(de->d_name);
			if (nfl != fl)
				continue;

			if (0 != strncasecmp(de->d_name, fn, fl))
				continue;

			// found a match
			result = (char *)malloc(dl + 1 + nfl + 1);
			if (!result)
				break;
			memset(result, 0, dl + 1 + nfl + 1);
			sprintf(result, "%s/%s", dn, de->d_name);
			break;	// we're done.
		}
		free(fncopy);
		closedir(dir);
		return result;
	}

	char *findFileNoCase(const char *filename)
	{
		return findFileObjectNoCase(filename, false);
	}

	char *findDirNoCase(const char *dn)
	{
		return findFileObjectNoCase(dn, true);
	}

	bool existsNoCase(const char *filename)
	{
		char *fn2 = findFileNoCase(filename);
		if (fn2 != NULL) {
			free(fn2);
			return true;
		}

		return false;
	}

	u64 getModifiedTime(const char *path)
	{
		struct stat st;
		u64 mtim;

		int ret = stat(path, &st);
		if (ret) {
			TFE_System::logWrite(LOG_WARNING, "getModifiedTime", "stat(%s) failed with %d\n", path, errno);
			return (u64)-1;  // revisit
		}

#ifdef __APPLE__
		mtim = (u64)st.st_mtimespec.tv_sec * 10000 + (u64)((double)st.st_mtimespec.tv_nsec / 100.0);
#else
		mtim = (u64)st.st_mtim.tv_sec * 10000 + (u64)((double)st.st_mtim.tv_nsec / 100.0);
#endif

		return mtim;
	}

	void fixupPath(char *path)
	{
		char *c = path;
		while (NULL != (c = strchr(c, '\\'))) {
			*c = '/';
		}
	}

	void convertToOSPath(const char *in, char *out)
	{
		memset(out, 0, TFE_MAX_PATH);
		strncpy(out, in, TFE_MAX_PATH - 1);
		fixupPath(out);
	}

	void replaceExtension(const char* srcPath, const char* newExt, char* outPath)
	{
		// Find the last '.' in the name.
		strcpy(outPath, srcPath);
		size_t len = strlen(srcPath);
		s32 lastDot = -1;
		for (size_t i = 0; i < len; i++)
		{
			if (srcPath[i] == '.') { lastDot = (s32)i; }
		}
		if (lastDot >= 0)
		{
			strcpy(&outPath[lastDot + 1], newExt);
		}
		else
		{
			strcat(outPath, ".");
			strcat(outPath, newExt);
		}
	}

	void stripExtension(const char* srcPath, char* outPath)
	{
		// Find the last '.' in the name.
		size_t len = strlen(srcPath);
		s32 lastDot = -1;
		for (size_t i = 0; i < len; i++)
		{
			if (srcPath[i] == '.') { lastDot = (s32)i; }
		}
		if (lastDot >= 0)
		{
			outPath[0] = 0;
			strncpy(outPath, srcPath, lastDot);
		}
		else
		{
			strcpy(outPath, srcPath);
		}
	}
}
