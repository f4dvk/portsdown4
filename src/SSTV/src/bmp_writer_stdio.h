// Ecriture BMP sur disque, modele fourni par PicoSSTV (decoder_test/test.cpp),
// partage entre sstv_decode et sstv_gui.

#ifndef __BMP_WRITER_STDIO_H__
#define __BMP_WRITER_STDIO_H__

#include "../third_party/sstv_library/bmp_lib.h"
#include <cstdio>
#include <unistd.h>

class c_bmp_writer_stdio : public c_bmp_writer
{
  FILE* f = nullptr;

  bool file_open(const char* filename) override
  {
    f = fopen(filename, "wb");
    return f != nullptr;
  }
  void file_close() override { fclose(f); }
  void file_seek(uint32_t offset) override { fseek(f, offset, SEEK_SET); }
  void file_write(const void* data, uint32_t element_size, uint32_t num_elements) override
  {
    fwrite(data, element_size, num_elements, f);
  }
  void file_truncate(uint32_t size) override
  {
    fflush(f);
    if (ftruncate(fileno(f), size) != 0) { /* fichier laisse tel quel : en-tete quand meme a jour */ }
  }
};

#endif
