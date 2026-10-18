// sstv_gui.cpp
//
// Version "ecran" de RPI_SSTV : decode et affiche les images SSTV
// directement sur l'ecran du Raspberry Pi, avec la meme technique que
// Langstone-V2 (Colin Durbridge G4EML) : acces direct au framebuffer Linux
// (mmap de /dev/fb0, dessin pixel par pixel, sans serveur X) et lecture
// directe d'un ecran tactile sur /dev/input/eventX (third_party/langstone_gui,
// licence GPLv3 -- voir ORIGIN.md et LICENSE).
//
// Contrairement a sstv_decode (qui attend la fin de l'image pour ecrire un
// fichier BMP), l'image est dessinee ligne par ligne au fur et a mesure du
// decodage, comme le fait un decodeur SSTV classique. Une copie BMP est
// aussi enregistree sur le disque, comme avec sstv_decode.
//
// Utilisation :
//   sstv_gui fichier <entree.wav> [dossier_sortie=images] [timeout_s=30] [options]
//   sstv_gui flux <frequence_hz> [dossier_sortie=images] [timeout_s=30] [options]
//
// Options :
//   --fb <peripherique>     Peripherique framebuffer (defaut /dev/fb0)
//   --touch <peripherique>  Peripherique ecran tactile, ex: /dev/input/event0
//                            (absent = pas de tactile, on quitte avec Ctrl+C)
//   --geom LxHxBPP           Impose la geometrie de l'ecran (tests uniquement,
//                            sans materiel Raspberry Pi -- voir Graphics.h)

#include "bmp_writer_stdio.h"
#include "sstv_sources.h"
#include "sstv_util.h"
#include "wav_reader.h"
#include "../third_party/sstv_library/sstv_decoder.h"
#include "../third_party/langstone_gui/Graphics.h"
#include "../third_party/langstone_gui/Touch.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <string>
#include <vector>
#include <unistd.h>

namespace
{
  const int HEADER_HEIGHT = 60;
  const int BUTTON_W = 100;
  const int BUTTON_H = 50;

  volatile sig_atomic_t g_quit = 0;
  bool g_touch_present = false;
  int g_quit_btn_x = 0;
  int g_quit_btn_y = 5;

  void on_signal(int) { g_quit = 1; }

  void rgb565_to_rgb(uint16_t v, uint8_t& r, uint8_t& g, uint8_t& b)
  {
    uint8_t r5 = (v >> 11) & 0x1F;
    uint8_t g6 = (v >> 5) & 0x3F;
    uint8_t b5 = v & 0x1F;
    r = (uint8_t)((r5 << 3) | (r5 >> 2));
    g = (uint8_t)((g6 << 2) | (g6 >> 4));
    b = (uint8_t)((b5 << 3) | (b5 >> 2));
  }

  void clear_rect(int x, int y, int w, int h, int r, int g, int b)
  {
    for (int yi = 0; yi < h; ++yi)
      for (int xi = 0; xi < w; ++xi)
        setPixel(x + xi, y + yi, r, g, b);
  }

  void draw_chrome()
  {
    setBackColour(0, 0, 0);
    setForeColour(255, 255, 255);
    clearScreen();
    g_quit_btn_x = screenXsize - BUTTON_W - 10;
    gotoXY(g_quit_btn_x, g_quit_btn_y);
    displayButton1x12((char*)"Quitter");
  }

  void set_status(const std::string& text)
  {
    clear_rect(0, 5, screenXsize - BUTTON_W - 20, 20, 0, 0, 0);
    setForeColour(0, 255, 0);
    setBackColour(0, 0, 0);
    textSize = 1;
    gotoXY(5, 5);
    displayStr((char*)text.c_str());
  }

  // Verifie l'interruption (Ctrl+C) et le bouton "Quitter" tactile. Appelee
  // regulierement (et non a chaque echantillon, trop couteux) pendant le
  // decodage comme lorsque le programme attend en fin d'image.
  void poll_ui()
  {
    if (g_touch_present) {
      int t = getTouch();
      if (t == 1) { // debut d'appui
        if (touchX >= g_quit_btn_x && touchX <= g_quit_btn_x + BUTTON_W &&
            touchY >= g_quit_btn_y && touchY <= g_quit_btn_y + BUTTON_H) {
          g_quit = 1;
        }
      }
    }
  }

  // Decodeur SSTV qui dessine chaque ligne directement sur le framebuffer au
  // fur et a mesure, en plus d'enregistrer l'image BMP sur le disque.
  template <typename Source>
  class c_sstv_decoder_gui : public c_sstv_decoder
  {
    Source& m_source;
    c_bmp_writer_stdio* m_bmp = nullptr;
    uint32_t m_lines_written = 0;
    std::string m_mode_name;
    bool m_origin_set = false;
    int m_origin_x = 0;
    int m_origin_y = 0;

    int16_t get_audio_sample() override { return m_source.read_sample(); }

    void image_write_line(uint16_t line_rgb565[], uint16_t y, uint16_t width, uint16_t height,
                           const char* mode_name) override
    {
      m_bmp->change_width(width);
      m_bmp->change_height(height);
      m_bmp->write_row_rgb565(line_rgb565);

      if (!m_origin_set) {
        // Une nouvelle image commence : on efface la precedente (qui reste
        // affichee jusque-la, le temps qu'un nouveau signal soit synchronise)
        // avant de dessiner la premiere ligne.
        clear_rect(0, HEADER_HEIGHT, screenXsize, screenYsize - HEADER_HEIGHT, 0, 0, 0);
        // m_origin_x n'est pas borne a 0 : une image plus large que l'ecran
        // (ex: PD120, 640 px) doit rester centree, le debordement gauche et
        // droit etant alors coupe a parts egales (setPixel ignore les x hors
        // ecran) plutot que de caler l'image a gauche et ne perdre que le
        // bord droit.
        m_origin_x = (screenXsize - width) / 2;
        m_origin_y = HEADER_HEIGHT + (screenYsize - HEADER_HEIGHT - height) / 2;
        if (m_origin_y < HEADER_HEIGHT) m_origin_y = HEADER_HEIGHT;
        m_origin_set = true;
        set_status(std::string("Decodage : ") + mode_name);
      }

      for (uint16_t x = 0; x < width; ++x) {
        uint8_t r, g, b;
        rgb565_to_rgb(line_rgb565[x], r, g, b);
        setPixel(m_origin_x + x, m_origin_y + y, r, g, b);
      }

      m_lines_written++;
      m_mode_name = mode_name;
    }

  public:
    c_sstv_decoder_gui(float fs, Source& source) : c_sstv_decoder(fs), m_source(source) {}

    bool decode_one_image(const std::string& bmp_path, uint8_t timeout_s, std::string& mode_out)
    {
      c_bmp_writer_stdio bmp;
      bmp.open(bmp_path.c_str(), 10, 10);
      m_bmp = &bmp;
      m_lines_written = 0;
      m_mode_name.clear();
      m_origin_set = false;

      bool image_in_progress = false;
      bool done = false;
      uint32_t n = 0;
      while (!done && !g_quit) {
        done = decode_image_non_blocking(timeout_s, true, image_in_progress);
        if ((++n & 0x3FF) == 0) poll_ui();
      }

      bmp.update_header();
      bmp.close();
      m_bmp = nullptr;

      mode_out = m_mode_name;
      return m_lines_written > 0;
    }
  };

  struct s_options
  {
    std::string fb_device = "/dev/fb0";
    std::string touch_device;
    int geom_w = 0, geom_h = 0, geom_bpp = 0;
    std::vector<std::string> positional;
  };

  s_options parse_args(int argc, char** argv)
  {
    s_options opt;
    for (int i = 1; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--fb" && i + 1 < argc) {
        opt.fb_device = argv[++i];
      } else if (a == "--touch" && i + 1 < argc) {
        opt.touch_device = argv[++i];
      } else if (a == "--geom" && i + 1 < argc) {
        sscanf(argv[++i], "%dx%dx%d", &opt.geom_w, &opt.geom_h, &opt.geom_bpp);
      } else {
        opt.positional.push_back(a);
      }
    }
    return opt;
  }

  void usage(const char* argv0)
  {
    fprintf(stderr,
      "Usage:\n"
      "  %s fichier <entree.wav> [dossier_sortie=images] [timeout_s=30] [options]\n"
      "  %s flux <frequence_hz> [dossier_sortie=images] [timeout_s=30] [options]\n"
      "Options:\n"
      "  --fb <peripherique>      defaut /dev/fb0\n"
      "  --touch <peripherique>   ex: /dev/input/event0 (absent = pas de tactile)\n"
      "  --geom LxHxBPP           geometrie imposee (tests hors Raspberry Pi)\n",
      argv0, argv0);
  }

  // Attend que l'utilisateur quitte (tactile ou Ctrl+C) une fois l'image
  // affichee, pour laisser le temps de la regarder.
  void wait_for_quit()
  {
    while (!g_quit) {
      poll_ui();
      usleep(100000);
    }
  }
}

int main(int argc, char** argv)
{
  s_options opt = parse_args(argc, argv);

  if (opt.positional.size() < 2) {
    usage(argv[0]);
    return 1;
  }

  std::string mode = opt.positional[0];
  std::string out_dir = opt.positional.size() > 2 ? opt.positional[2] : "images";
  uint8_t timeout_s = opt.positional.size() > 3 ? (uint8_t)atoi(opt.positional[3].c_str()) : 30;

  if (!initScreenAt(opt.fb_device.c_str(), opt.geom_w, opt.geom_h, opt.geom_bpp)) {
    fprintf(stderr, "erreur : impossible d'initialiser l'ecran (%s)\n", opt.fb_device.c_str());
    return 1;
  }

  if (!opt.touch_device.empty()) {
    g_touch_present = (initTouch((char*)opt.touch_device.c_str()) == 0);
    if (!g_touch_present) {
      fprintf(stderr, "avertissement : ecran tactile %s indisponible, Ctrl+C pour quitter\n",
              opt.touch_device.c_str());
    }
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  draw_chrome();

  if (mode == "fichier") {
    std::string wav_path = opt.positional[1];

    c_wav_reader wav;
    std::string error;
    if (!wav.open(wav_path, error)) {
      fprintf(stderr, "erreur : %s\n", error.c_str());
      closeScreen();
      return 1;
    }

    set_status("Decodage en cours...");

    c_source_wav source(wav);
    c_sstv_decoder_gui<c_source_wav> decoder((float)wav.sample_rate(), source);

    std::string bmp_path = out_dir + "/sstv_" + sstv_timestamp() + ".bmp";
    std::string mode_name;
    bool ok = decoder.decode_one_image(bmp_path, timeout_s, mode_name);
    wav.close();

    if (ok) {
      printf("MODE=%s FICHIER=%s\n", mode_name.c_str(), bmp_path.c_str());
      set_status(std::string("Image enregistree : ") + bmp_path);
    } else {
      fprintf(stderr, "aucun signal SSTV synchronise dans %s\n", wav_path.c_str());
      set_status("Aucun signal SSTV synchronise");
      sstv_remove_file(bmp_path);
    }

    wait_for_quit();
  } else if (mode == "flux") {
    float fs = (float)atof(opt.positional[1].c_str());

    c_source_stdin source;
    c_sstv_decoder_gui<c_source_stdin> decoder(fs, source);

    set_status("En attente d'un signal SSTV...");

    while (!g_quit) {
      std::string bmp_path = out_dir + "/sstv_" + sstv_timestamp() + ".bmp";
      std::string mode_name;
      bool ok = decoder.decode_one_image(bmp_path, timeout_s, mode_name);

      if (ok) {
        printf("MODE=%s FICHIER=%s\n", mode_name.c_str(), bmp_path.c_str());
        fflush(stdout);
        set_status(std::string("Image enregistree : ") + bmp_path);
      } else {
        sstv_remove_file(bmp_path);
      }
    }
  } else {
    usage(argv[0]);
    closeScreen();
    return 1;
  }

  closeScreen();
  return 0;
}
