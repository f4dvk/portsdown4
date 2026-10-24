// sstv_decode.cpp
//
// Enrobage Linux (Raspberry Pi / PC) autour de la bibliotheque de decodage
// SSTV de dawsonjon/PicoSSTV (third_party/sstv_library, licence MIT).
//
// Version "sans ecran" : decode et enregistre les images au format BMP,
// sans affichage. Pour un affichage direct sur l'ecran du Raspberry Pi
// (framebuffer, comme Langstone-V2), voir sstv_gui.cpp.
//
// Deux modes d'utilisation, sur le meme modele que le decodeur Sarsat
// "406" de Portsdown4 (fichier audio ou flux stdin en continu) :
//
//   sstv_decode fichier <entree.wav> <dossier_sortie> [timeout_s]
//     Decode une seule image a partir d'un fichier .wav existant.
//
//   sstv_decode flux <frequence_hz> <dossier_sortie> [timeout_s]
//     Lit du PCM 16 bits signe mono sur l'entree standard (ex: sortie de
//     sox ou d'un futur script RTL-SDR) et decode les images en continu,
//     tant que le programme n'est pas interrompu.
//
// A chaque image effectivement decodee (au moins une ligne recue), le
// programme affiche sur stdout une ligne "MODE=<mode> FICHIER=<chemin.bmp>"
// que les scripts shell (decode_file.sh / decode_live.sh) utilisent pour
// declencher l'affichage via fbi, comme le fait deja Portsdown4 pour ses
// propres images.

#include "bmp_writer_stdio.h"
#include "sstv_sources.h"
#include "sstv_util.h"
#include "wav_reader.h"
#include "../third_party/sstv_library/sstv_decoder.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
  // Decodeur SSTV pilote par un objet quelconque fournissant read_sample().
  // Les deux modes (fichier WAV, flux stdin) se contentent d'implementer
  // une source differente.
  template <typename Source>
  class c_sstv_decoder_impl : public c_sstv_decoder
  {
    Source& m_source;
    c_bmp_writer_stdio* m_bmp = nullptr;
    uint32_t m_lines_written = 0;
    std::string m_mode_name;

    int16_t get_audio_sample() override { return m_source.read_sample(); }

    void image_write_line(uint16_t line_rgb565[], uint16_t y, uint16_t width, uint16_t height,
                           const char* mode_name) override
    {
      m_bmp->change_width(width);
      m_bmp->change_height(height);
      m_bmp->write_row_rgb565(line_rgb565);
      m_lines_written++;
      m_mode_name = mode_name;
    }

  public:
    c_sstv_decoder_impl(float fs, Source& source) : c_sstv_decoder(fs), m_source(source) {}

    // Decode une image dans le fichier bmp indique. Retourne true si au
    // moins une ligne a ete recue (donc qu'un signal SSTV a bien ete
    // synchronise), false si rien n'a ete detecte avant expiration du
    // timeout.
    bool decode_one_image(const std::string& bmp_path, uint8_t timeout_s, std::string& mode_out)
    {
      c_bmp_writer_stdio bmp;
      bmp.open(bmp_path.c_str(), 10, 10); // dimensions reelles fixees pendant le decodage
      m_bmp = &bmp;
      m_lines_written = 0;
      m_mode_name.clear();

      decode_image(timeout_s, true);

      // Lignes decodees apres le dernier top de synchro valide (fin
      // d'emission, signal perdu) : du bruit, retire de l'image (voir
      // get_valid_rows()).
      const uint32_t kept = m_lines_written < get_valid_rows() ? m_lines_written : get_valid_rows();
      bmp.truncate_rows(kept);
      bmp.update_header();
      bmp.close();
      m_bmp = nullptr;

      mode_out = m_mode_name;
      return kept > 0;
    }
  };

  void usage(const char* argv0)
  {
    fprintf(stderr,
      "Usage:\n"
      "  %s fichier <entree.wav> <dossier_sortie> [timeout_s=30]\n"
      "  %s flux <frequence_hz> <dossier_sortie> [timeout_s=30]\n",
      argv0, argv0);
  }
}

int main(int argc, char** argv)
{
  if (argc < 4) {
    usage(argv[0]);
    return 1;
  }

  std::string mode = argv[1];

  if (mode == "fichier") {
    std::string wav_path = argv[2];
    std::string out_dir = argv[3];
    uint8_t timeout_s = argc > 4 ? (uint8_t)atoi(argv[4]) : 30;

    c_wav_reader wav;
    std::string error;
    if (!wav.open(wav_path, error)) {
      fprintf(stderr, "erreur : %s\n", error.c_str());
      return 1;
    }

    c_source_wav source(wav);
    c_sstv_decoder_impl<c_source_wav> decoder((float)wav.sample_rate(), source);

    std::string bmp_path = out_dir + "/sstv_" + sstv_timestamp() + ".bmp";
    std::string mode_name;
    bool ok = decoder.decode_one_image(bmp_path, timeout_s, mode_name);
    wav.close();

    if (ok) {
      printf("MODE=%s FICHIER=%s\n", mode_name.c_str(), bmp_path.c_str());
      return 0;
    } else {
      fprintf(stderr, "aucun signal SSTV synchronise dans %s\n", wav_path.c_str());
      sstv_remove_file(bmp_path);
      return 2;
    }
  }

  if (mode == "flux") {
    float fs = (float)atof(argv[2]);
    std::string out_dir = argv[3];
    uint8_t timeout_s = argc > 4 ? (uint8_t)atoi(argv[4]) : 30;

    c_source_stdin source;
    c_sstv_decoder_impl<c_source_stdin> decoder(fs, source);

    while (true) {
      std::string bmp_path = out_dir + "/sstv_" + sstv_timestamp() + ".bmp";
      std::string mode_name;
      bool ok = decoder.decode_one_image(bmp_path, timeout_s, mode_name);
      if (ok) {
        printf("MODE=%s FICHIER=%s\n", mode_name.c_str(), bmp_path.c_str());
        fflush(stdout);
      } else {
        sstv_remove_file(bmp_path);
      }
      if (feof(stdin)) break;
    }
    return 0;
  }

  usage(argv[0]);
  return 1;
}
