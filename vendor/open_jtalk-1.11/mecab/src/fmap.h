// MeCab -- Yet Another Part-of-Speech and Morphological Analyzer
//
//
//  Copyright(C) 2001-2006 Taku Kudo <taku@chasen.org>
//  Copyright(C) 2004-2006 Nippon Telegraph and Telephone Corporation
#ifndef MECAB_FMAP_H
#define MECAB_FMAP_H

#include <errno.h>
#include <string>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

extern "C" {

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#define HAVE_FCNTL_H
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#if defined(_WIN32_OFF) && !defined(__CYGWIN__)
#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif
#else

#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <io.h>
#pragma warning(disable : 4996)
#endif
#endif
}

#include "common.h"
#include "utils.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

namespace MeCab {

template <class T> class Fmap {
 private:
  off_t        begin_;
  off_t        end_;
  std::string  fileName;
  whatlog what_;
  int    fd;
  int    flag;
  T *buf;

 public:
  T operator[](size_t n) const { T ret; read(&ret, begin_ + n * sizeof(T), sizeof(T)); return ret; }
  int get_fd() { return fd; }
  T* get_buf() { return buf; }
  off_t begin()         const { return begin_; }
  off_t end()           const { return end_; }
  size_t size()         const { return (end_ - begin_)/sizeof(T); }
  const char *what()          { return what_.str(); }
  const char *file_name()     { return fileName.c_str(); }
  size_t file_size()          { return end_ - begin_; }
  bool empty()                { return begin_ == end_; }

  bool open(const char *filename, const char *mode = "r") {
    this->close();
    struct stat st;
    fileName = std::string(filename);

    if      (std::strcmp(mode, "r") == 0)
      flag = O_RDONLY;
    else if (std::strcmp(mode, "r+") == 0)
      flag = O_RDWR;
    else
      CHECK_FALSE(false) << "unknown open mode: " << filename;

    CHECK_FALSE((fd = ::open(filename, flag | O_BINARY)) >= 0)
        << "open failed: " << filename;

    CHECK_FALSE(::fstat(fd, &st) >= 0)
        << "failed to get file size: " << filename;

    begin_ = 0;
    end_ = st.st_size;
    CHECK_FALSE(::lseek(fd, begin_, SEEK_SET) >= 0)
        << "seek failed: " << filename;

    return true;
  }

  void close() {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
    delete[] buf;
  }

  size_t read(void *dst, off_t offset, size_t size) const {
      if (buf != 0) {
          memcpy(dst, &buf[offset / sizeof(T)], size);
          return size;
      }
      else {
          ::lseek(fd, offset, SEEK_SET);
          return ::read(fd, dst, (int)size);
      }
  }

  Fmap(): fd(-1), buf(0), begin_(0), end_(0) {}

  Fmap(int fd, int begin, int end) : fd(fd), buf(0), begin_(begin), end_(end) {}

  Fmap(T *buf, int count) : fd(-1), buf(buf), begin_(0), end_(count * sizeof(T)) {}

  virtual ~Fmap() { this->close(); }
};
}
#endif
