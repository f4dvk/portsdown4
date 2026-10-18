#include "wav_reader.h"
#include <cstring>

namespace
{
  uint32_t read_u32le(FILE* f)
  {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  }

  uint16_t read_u16le(FILE* f)
  {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
  }
}

bool c_wav_reader::open(const std::string& filename, std::string& error)
{
  m_file = fopen(filename.c_str(), "rb");
  if (!m_file) {
    error = "impossible d'ouvrir le fichier " + filename;
    return false;
  }

  char riff_id[4], wave_id[4];
  if (fread(riff_id, 1, 4, m_file) != 4 || strncmp(riff_id, "RIFF", 4) != 0) {
    error = filename + " : entete RIFF absente (pas un fichier WAV)";
    return false;
  }
  read_u32le(m_file); // taille totale du fichier, non utilisee
  if (fread(wave_id, 1, 4, m_file) != 4 || strncmp(wave_id, "WAVE", 4) != 0) {
    error = filename + " : identifiant WAVE absent";
    return false;
  }

  bool have_fmt = false;
  while (true) {
    char chunk_id[4];
    if (fread(chunk_id, 1, 4, m_file) != 4) break;
    uint32_t chunk_size = read_u32le(m_file);
    long chunk_data_pos = ftell(m_file);

    if (strncmp(chunk_id, "fmt ", 4) == 0) {
      uint16_t audio_format = read_u16le(m_file);
      m_num_channels = read_u16le(m_file);
      m_sample_rate = read_u32le(m_file);
      read_u32le(m_file); // octets/seconde
      read_u16le(m_file); // alignement de bloc
      m_bits_per_sample = read_u16le(m_file);
      // 1 = PCM entier, 0xFFFE = WAVE_FORMAT_EXTENSIBLE (souvent aussi du PCM)
      if (audio_format != 1 && audio_format != 0xFFFE) {
        error = filename + " : format audio non PCM non supporte";
        return false;
      }
      have_fmt = true;
    } else if (strncmp(chunk_id, "data", 4) == 0) {
      m_data_start = (uint32_t)chunk_data_pos;
      m_data_size = chunk_size;
    }

    // Les chunks sont alignes sur 2 octets.
    long next = chunk_data_pos + chunk_size + (chunk_size & 1);
    if (fseek(m_file, next, SEEK_SET) != 0) break;
    if (m_data_start && have_fmt) break;
  }

  if (!have_fmt || !m_data_start) {
    error = filename + " : chunks fmt/data introuvables";
    return false;
  }
  if (m_num_channels < 1 || m_num_channels > 2) {
    error = filename + " : nombre de voies non supporte (1 ou 2 attendu)";
    return false;
  }
  if (m_bits_per_sample != 8 && m_bits_per_sample != 16 &&
      m_bits_per_sample != 24 && m_bits_per_sample != 32) {
    error = filename + " : profondeur d'echantillon non supportee";
    return false;
  }

  fseek(m_file, m_data_start, SEEK_SET);
  return true;
}

void c_wav_reader::close()
{
  if (m_file) fclose(m_file);
  m_file = nullptr;
}

int16_t c_wav_reader::read_sample()
{
  int32_t channel_values[2] = {0, 0};
  int bytes_per_sample = m_bits_per_sample / 8;

  for (int c = 0; c < m_num_channels; ++c) {
    uint8_t raw[4] = {0, 0, 0, 0};
    if (fread(raw, 1, bytes_per_sample, m_file) != (size_t)bytes_per_sample) {
      return 0; // fin de fichier : silence
    }

    int32_t value = 0;
    switch (m_bits_per_sample) {
      case 8:
        // WAV 8 bits est non signe, centre sur 128.
        value = ((int32_t)raw[0] - 128) << 8;
        break;
      case 16:
        value = (int16_t)((uint16_t)raw[0] | ((uint16_t)raw[1] << 8));
        break;
      case 24: {
        int32_t v = (int32_t)raw[0] | ((int32_t)raw[1] << 8) | ((int32_t)raw[2] << 16);
        if (v & 0x800000) v |= ~0xFFFFFF; // extension de signe
        value = v >> 8;
        break;
      }
      case 32: {
        int32_t v = (int32_t)((uint32_t)raw[0] | ((uint32_t)raw[1] << 8) |
                               ((uint32_t)raw[2] << 16) | ((uint32_t)raw[3] << 24));
        value = v >> 16;
        break;
      }
    }
    channel_values[c] = value;
  }

  if (m_num_channels == 1) return (int16_t)channel_values[0];
  return (int16_t)((channel_values[0] + channel_values[1]) / 2);
}
