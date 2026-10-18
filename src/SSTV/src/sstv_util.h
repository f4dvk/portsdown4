// Utilitaires partages entre sstv_decode et sstv_gui.

#ifndef __SSTV_UTIL_H__
#define __SSTV_UTIL_H__

#include <cstdio>
#include <ctime>
#include <string>

// Horodatage au 1/100e de seconde : deux images SSTV courtes (ex: Robot 36,
// ~36 s) peuvent se terminer dans la meme seconde en mode flux continu, une
// resolution "secondes" provoquerait alors un ecrasement de fichier.
inline std::string sstv_timestamp()
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  struct tm tm_now;
  localtime_r(&ts.tv_sec, &tm_now);
  char buf[32];
  size_t len = strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm_now);
  snprintf(buf + len, sizeof(buf) - len, "_%02ld", ts.tv_nsec / 10000000);
  return buf;
}

inline void sstv_remove_file(const std::string& path)
{
  std::remove(path.c_str());
}

#endif
