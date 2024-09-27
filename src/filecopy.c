/* File manipulation routines */
/* Author of Win32-specific bits: Nick Gammon */

#include "copyrite.h"

#include <stdlib.h>
#include <stdio.h>

#ifdef WIN32
#include <windows.h>
#include <process.h>
#include <direct.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include "conf.h"
#include "externs.h"
#include "log.h"
#include "match.h"
#include "mushdb.h"
#include "mymalloc.h"

extern char confname[BUFFER_LEN]; /* From bsd.c */

#ifdef WIN32

/* This is bad, but only for WIN32, which is bad anyway... */
#define EMBEDDED_MKINDX
static char buff[1024];
BOOL
ConcatenateFiles(const char *path, const char *outputfile)
{
  HANDLE filscan;
  WIN32_FIND_DATA fildata;
  BOOL filflag;
  DWORD status;
  FILE *fo = NULL;
  FILE *f = NULL;
  size_t bytes_in, bytes_out;
  long total_bytes = 0;
  int total_files = 0;
  char directory[MAX_PATH];
  char fullname[MAX_PATH];
  char *p;

  /* If outputfile is an empty string, forget it. */
  if (!outputfile || !*outputfile)
    return FALSE;

  /* extract the directory from the path name */
  strcpy(directory, path);
  p = strrchr(directory, '\\');
  if (p)
    p[1] = 0;

  else {
    p = strrchr(directory, '/');
    if (p)
      p[1] = 0;
  }

  /* Open output file */
  fo = fopen(outputfile, "wb");
  if (!fo) {
    do_rawlog(LT_ERR, "Unable to open file: %s", outputfile);
    return FALSE;
  }
  do_rawlog(LT_ERR, "Creating file: %s", outputfile);

  /* Find first file matching the wildcard */
  filscan = FindFirstFile(path, &fildata);
  if (filscan == INVALID_HANDLE_VALUE) {
    status = GetLastError();
    fclose(fo);
    do_rawlog(LT_ERR, "**** No files matching: \"%s\" found.", path);
    if (status == ERROR_NO_MORE_FILES)
      return TRUE;

    else
      return FALSE;
  }

  /*
     Now enter the concatenation loop.
   */

  do {
    if (!(fildata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      do_rawlog(LT_ERR, "%s: %s, %ld %s", "    Copying file", fildata.cFileName,
                fildata.nFileSizeLow,
                fildata.nFileSizeLow == 1 ? "byte" : "bytes");
      strcpy(fullname, directory);
      strcat(fullname, fildata.cFileName);

      /* Open the input file */
      f = fopen(fullname, "rb");
      if (!f)
        do_rawlog(LT_ERR, "    ** Unable to open file: %s", fullname);

      else {
        total_files++;

        /* do the copy loop */
        while (!feof(f)) {
          bytes_in = fread(buff, 1, sizeof(buff), f);
          if (bytes_in <= 0)
            break;
          bytes_out = fwrite(buff, 1, bytes_in, fo);
          total_bytes += bytes_out;
          if (bytes_in != bytes_out) {
            do_rawlog(LT_ERR, "Unable to write to file: %s", outputfile);
            fclose(f);
            break;
          }
        } /* end of copy loop */
        fclose(f);
      } /* end of being able to open file */
    }

    /* end of not being a directory */
    /* get next file matching the wildcard */
    filflag = FindNextFile(filscan, &fildata);
  } while (filflag);
  status = GetLastError();
  FindClose(filscan);
  fclose(fo);
  do_rawlog(LT_ERR, "Copied %i %s, %ld %s", total_files,
            total_files == 1 ? "file" : "files", total_bytes,
            total_bytes == 1 ? "byte" : "bytes");
  if (status == ERROR_NO_MORE_FILES)
    return TRUE;

  else
    return FALSE;
}

int
CheckDatabase(const char *path, FILETIME *modified, long *filesize)
{
  HANDLE filscan;
  WIN32_FIND_DATA fildata;
  SYSTEMTIME st;
  static const char *const months[] = {">!<", "Jan", "Feb", "Mar", "Apr",
                                       "May", "Jun", "Jul", "Aug", "Sep",
                                       "Oct", "Nov", "Dec"};
  FILE *f;
  size_t bytes;
  filscan = FindFirstFile(path, &fildata);
  if (filscan == INVALID_HANDLE_VALUE) {
    do_rawlog(LT_ERR, "File \"%s\" not found.", path);
    return FALSE;
  }
  *modified = fildata.ftLastWriteTime;
  *filesize = fildata.nFileSizeLow;
  FindClose(filscan);
  FileTimeToSystemTime(&fildata.ftLastWriteTime, &st);
  if (st.wMonth < 1 || st.wMonth > 12)
    st.wMonth = 0;
  do_rawlog(
    LT_ERR,
    "File \"%s\" found, size %ld %s, modified on %02d %s %04d %02d:%02d:%02d",
    path, fildata.nFileSizeLow, fildata.nFileSizeLow == 1 ? "byte" : "bytes",
    st.wDay, months[st.wMonth], st.wYear, st.wHour, st.wMinute, st.wSecond);
  if (fildata.nFileSizeHigh == 0 && fildata.nFileSizeLow < 80) {
    do_rawlog(LT_ERR, "File is too small to be a MUSH database.");
    return FALSE;
  }

  /* check file for validity */
  f = fopen(path, "rb");
  if (!f) {
    do_rawlog(LT_ERR, "Unable to open file %s", path);
    return FALSE;
  }
  if (fseek(f, -80, SEEK_END) != 0) {
    do_rawlog(LT_ERR, "Unable to check file %s", path);
    fclose(f);
    return FALSE;
  }
  bytes = fread(buff, 1, 80, f);
  fclose(f);
  if (bytes != 80) {
    do_rawlog(LT_ERR, "Unable to read last part of file %s", path);
    return FALSE;
  }
  if (strstr(buff, "***END OF DUMP***") == 0) {
    do_rawlog(LT_ERR, "Database not terminated correctly, file %s", path);
    return FALSE;
  }
  return TRUE;
} /* end of  CheckDatabase */

void
Win32MUSH_setup(void)
{
  int indb_OK, outdb_OK, panicdb_OK;
  FILETIME indb_time, outdb_time, panicdb_time;
  long indb_size, outdb_size, panicdb_size;

#ifndef _DEBUG
  char FileName[256];
  if (GetModuleFileName(NULL, FileName, 256) != 0) {
    if (!strcasecmp(strrchr(FileName, '\\') + 1, "pennmush.exe")) {
      if (CopyFile("pennmush.exe", "pennmush_run.exe", FALSE)) {
        do_rawlog(LT_ERR, "Successfully copied executable, starting copy.");
#ifdef WIN32SERVICES
        execl("pennmush_run.exe", "pennmush_run.exe", "/run", NULL);
#else
        execl("pennmush_run.exe", "pennmush_run.exe", confname, NULL);
#endif
      }
    }
  }
#endif /*  */
  ConcatenateFiles("txt\\hlp\\*.hlp", "txt\\help.txt");
  ConcatenateFiles("txt\\nws\\*.nws", "txt\\news.txt");
  ConcatenateFiles("txt\\evt\\*.evt", "txt\\events.txt");
  ConcatenateFiles("txt\\rul\\*.rul", "txt\\rules.txt");
  ConcatenateFiles("txt\\idx\\*.idx", "txt\\index.txt");
  indb_OK = CheckDatabase(options.input_db, &indb_time, &indb_size);
  outdb_OK = CheckDatabase(options.output_db, &outdb_time, &outdb_size);
  panicdb_OK = CheckDatabase(options.crash_db, &panicdb_time, &panicdb_size);
  if (indb_OK) {        /* Look at outdb */
    if (outdb_OK) {     /* Look at panicdb */
      if (panicdb_OK) { /* outdb or panicdb or indb */
        if (CompareFileTime(&panicdb_time, &outdb_time) >
            0) { /* panicdb or indb */
          if (CompareFileTime(&panicdb_time, &indb_time) > 0) { /* panicdb */
            ConcatenateFiles(options.crash_db, options.input_db);
          } else { /* indb */
          }
        } else { /* outdb or indb */
          if (CompareFileTime(&outdb_time, &indb_time) > 0) { /* outdb */
            ConcatenateFiles(options.output_db, options.input_db);
          } else { /* indb */
          }
        }
      } else {                                              /* outdb or indb */
        if (CompareFileTime(&outdb_time, &indb_time) > 0) { /* outdb */
          ConcatenateFiles(options.output_db, options.input_db);
        } else { /* indb */
        }
      }
    } else {            /* outdb not OK */
      if (panicdb_OK) { /* panicdb or indb */
        if (CompareFileTime(&panicdb_time, &indb_time) > 0) { /* panicdb */
          ConcatenateFiles(options.crash_db, options.input_db);
        } else { /* indb */
        }
      } else { /* indb */
      }
    }
  } else {              /* indb not OK */
    if (outdb_OK) {     /* look at panicdb */
      if (panicdb_OK) { /* out or panic */
        if (CompareFileTime(&panicdb_time, &outdb_time) > 0) { /* panicdb */
          ConcatenateFiles(options.crash_db, options.input_db);
        } else { /* outdb */
          ConcatenateFiles(options.output_db, options.input_db);
        }
      } else { /* outdb */
        ConcatenateFiles(options.output_db, options.input_db);
      }
    } else {            /* outdb not OK */
      if (panicdb_OK) { /* panicdb */
        ConcatenateFiles(options.crash_db, options.input_db);
      } else { /* NOTHING */
        return;
      }
    }
  }

  /* Final failsafe - input database SHOULD still be OK. */
  do_rawlog(LT_ERR, "Verifying selected database.");
  if (!CheckDatabase(options.input_db, &indb_time, &indb_size)) {
    do_rawlog(LT_ERR, "File corrupted during selection process.");
    exit(-1);
  } else {
    do_rawlog(LT_ERR, "Input database verified. Proceeding to analysis.");
  }
}

#endif /* WIN32 */

/** Portably renames a file
 * \param origname the orignal name of the file
 * \param newname the new name of the file
 * \return 0 on success, negative on failure
 */
int
rename_file(const char *origname, const char *newname)
{
#ifdef WIN32
  if (ReplaceFile(newname, origname, NULL, 0, NULL, NULL)) {
    return 0;
  } else if (GetLastError() == ERROR_FILE_NOT_FOUND) {
    /* ReplaceFile() won't create a new file, MoveFile() won't overwrite an
     * existing one. */
    if (MoveFile(origname, newname)) {
      return 0;
    } else {
      return -1;
    }
  } else {
    return -1;
  }
#else
  return rename(origname, newname);
#endif
}

/** Truncate a file to 0 bytes without deleting it.
 * Because info_slave and ssl_slave also write to the main error
 * log file, we don't want to just delete the files and open new
 * ones with the same name.
 *
 * \param f the FILE object to rewind.
 * \return 0 on success, -1 on failure.
 */
int
trunc_file(FILE *f)
{
  if (fseek(f, 0, SEEK_SET) < 0)
    return -1;
  if (ftruncate(fileno(f), 0) < 0)
    return -1;
  return 0;
}

/** Block copy a file.
 *
 * Possible future direction: Use sendfile(2) on linux to reduce
 * the amount of copying of data.
 *
 * \param f FILE object that must be open for reading.
 * \param newname the name of the destination file.
 * \param reset true to seek to the start of the file.
 * \return 0 on success, -1 on failure.
 */
int
copy_file(FILE *f, const char *newname, bool reset)
{
  FILE *copy = fopen(newname, "w");
  if (copy) {
    char buf[BUFSIZ];
    size_t len;
    if (reset) {
      if (fseek(f, 0, SEEK_SET) < 0) {
        fclose(copy);
        return -1;
      }
    }
    while ((len = fread(buf, 1, BUFSIZ, f)) > 0) {
      if (fwrite(buf, 1, len, copy) != len) {
        fclose(copy);
        return -1;
      }
    }
    fclose(copy);
    return feof(f) ? 0 : -1;
  } else
    return -1;
}

/** Copies the contents of a given file to an open FILE object.
 *
 * Possible future direction: Use sendfile(2) on linux to reduce
 * the amount of copying of data.
 *
 * \param name the name of the file to copy
 * \param to the destination FILE.
 * \return 0 on success, -1 on failure.
 */
int
copy_to_file(const char *name, FILE *to)
{
  FILE *from;
  char buf[BUFSIZ];
  size_t len;

  from = fopen(name, "r");
  if (!from)
    return -1;

  while ((len = fread(buf, 1, BUFSIZ, from)) > 0) {
    if (fwrite(buf, 1, len, to) != len) {
      fclose(from);
      return -1;
    }
  }
  fclose(from);

  return 0;
}

/** Test to see if a filename exists.
 *
 * Normal caveat about a race condition involving the file being
 * created or deleted by something else after this function is used.
 *
 * \param name the name of the file
 * \return true if it exists, false if it doesn't.
 */
bool
file_exists(const char *name)
{
  struct stat s;
  return stat(name, &s) == 0;
}
