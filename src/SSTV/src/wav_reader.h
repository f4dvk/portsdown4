// wav_reader.h
// Lecture minimale d'un fichier WAV PCM (mono ou stéréo, 8/16/24/32 bits).
// Les échantillons sont restitués en mono 16 bits signé, downmixés si besoin.

#ifndef __WAV_READER_H__
#define __WAV_READER_H__

#include <cstdint>
#include <cstdio>
#include <string>

class c_wav_reader
{
public:
  // Ouvre le fichier et lit l'entête RIFF/WAVE. Retourne false en cas d'erreur
  // (fichier introuvable, format non reconnu ou non PCM).
  bool open(const std::string& filename, std::string& error);

  void close();

  // Fréquence d'échantillonnage déclarée dans le fichier.
  uint32_t sample_rate() const { return m_sample_rate; }

  // Lit un échantillon mono 16 bits signé. Retourne 0 après la fin du fichier
  // (comportement volontairement silencieux : le décodeur SSTV sait ignorer
  // du bruit/silence en fin de flux).
  int16_t read_sample();

private:
  FILE* m_file = nullptr;
  uint32_t m_sample_rate = 0;
  uint16_t m_num_channels = 1;
  uint16_t m_bits_per_sample = 16;
  uint32_t m_data_start = 0;
  uint32_t m_data_size = 0;
};

#endif
