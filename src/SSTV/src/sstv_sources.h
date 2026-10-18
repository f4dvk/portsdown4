// Sources audio pour le decodeur SSTV, partagees entre sstv_decode et
// sstv_gui : lecture d'un fichier WAV reel, ou de PCM 16 bits signe mono
// sur l'entree standard (capture en continu).

#ifndef __SSTV_SOURCES_H__
#define __SSTV_SOURCES_H__

#include "wav_reader.h"
#include <cstdio>
#include <cstdint>

class c_source_wav
{
  c_wav_reader& m_wav;
public:
  explicit c_source_wav(c_wav_reader& wav) : m_wav(wav) {}
  int16_t read_sample() { return m_wav.read_sample(); }
};

class c_source_stdin
{
public:
  int16_t read_sample()
  {
    int16_t sample = 0;
    if (fread(&sample, sizeof(sample), 1, stdin) != 1) return 0;
    return sample;
  }
};

#endif
