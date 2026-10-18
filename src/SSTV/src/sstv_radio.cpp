// sstv_radio.cpp
//
// Interface complete pour recevoir du SSTV directement depuis une cle
// RTL-SDR, un PlutoSDR ou un LimeSDR : reglage de la frequence et du mode
// (USB/LSB/FM), petit spectre local pour surveiller le signal, et la zone
// principale qui affiche l'image SSTV en cours de reception -- le tout sur
// le framebuffer, avec la meme technique que sstv_gui (Langstone-V2).
//
// La reception radio elle-meme (accord, demodulation, FFT) est faite par un
// sous-processus Python/GNU Radio (sdr/rx_rtlsdr.py, rx_pluto.py, rx_lime.py
// -- voir sdr/common_rx.py), choisi au demarrage (ligne de commande ou menu
// tactile). Ce programme :
//   - lance le sous-processus et lit l'audio demodule (PCM 15 kHz) sur son
//     entree standard, qu'il decode exactement comme sstv_gui ;
//   - lui envoie les commandes FREQ/MODE par UDP quand l'utilisateur touche
//     les boutons d'accord ;
//   - recoit les trames de spectre par UDP et les affiche.
//
// Utilisation :
//   sstv_radio --sdr rtlsdr|pluto|lime|test [--freq Hz] [--mode USB|LSB|FM]
//              [--device-args ...] [--gain dB] [--out dossier] [options ecran]
//
// Sans --sdr, un menu de selection s'affiche si un ecran tactile est
// disponible (--touch).

#include "bmp_writer_stdio.h"
#include "sstv_util.h"
#include "../third_party/sstv_library/sstv_decoder.h"
#include "../third_party/langstone_gui/Graphics.h"
#include "../third_party/langstone_gui/Touch.h"
#include "../third_party/portsdown_web/ffunc.h"

#include <algorithm>
#include <cmath>
#include <arpa/inet.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
  // ---- disposition ecran ----
  // Tous les boutons (accord + mode + quitter) tiennent sur une seule
  // ligne, pour laisser le plus de place possible a l'image SSTV en dessous.
  const int BTN_W = 70, BTN_H = 28, BTN_GAP = 4;
  const int HEADER_H = 22; // bandeau d'info (SDR/freq/mode), toujours affiche
  int ROW1_Y = HEADER_H + 2; // ligne de boutons d'accord/mode -- absente en mode kiosque
  // Trace spectrale (ligne, type Langstone) + cascade/waterfall en dessous,
  // meme principe que LangstoneGUI_RtlSdr.c (waterfall() dans portsdown4/src/langstone) :
  // la trace est le dernier relevé FFT brut (reactif, sans lissage ajoute),
  // la cascade accumule l'historique ligne par ligne, defilement vers le bas.
  const int LINE_H = 30, WATERFALL_ROWS = 70;
  const int SPECTRUM_H = LINE_H + WATERFALL_ROWS;
  // SPECTRUM_Y et IMAGE_TOP dependent du mode kiosque (pas de ligne de
  // boutons) : valeurs par defaut ici, recalculees par configure_layout()
  // une fois --kiosk connu, avant le premier dessin.
  int SPECTRUM_Y = ROW1_Y + BTN_H + 6;
  int IMAGE_TOP = SPECTRUM_Y + SPECTRUM_H + 4;
  const int FFT_SIZE = 512;
  const float SPECTRUM_MIN_DB = -90.0f, SPECTRUM_MAX_DB = -20.0f;
  // Reglages AFC (voir afc_update()) : fenetre de calcul du centroide
  // (+/- AFC_SEARCH_HZ autour de la frequence accordee) -- doit couvrir la
  // largeur occupee reelle du signal FM (deviation +/-5 kHz + modulation
  // jusqu'a 2.3 kHz, soit ~14.6 kHz par la regle de Carson), pas juste une
  // bande etroite autour de la porteuse, sinon le centroide est biaise par
  // le bord de la fenetre. Le reste : marge au-dessus du bruit ambiant
  // pour faire confiance a la mesure (AFC_THRESHOLD_DB), zone morte en
  // bins pour ne pas corriger un signal deja a peu pres centre
  // (AFC_DEADBAND_BINS), et fraction de l'ecart mesure appliquee a chaque
  // correction pour converger en douceur (AFC_DAMPING).
  const double AFC_SEARCH_HZ = 8000.0;
  const float AFC_THRESHOLD_DB = 8.0f;
  const int AFC_DEADBAND_BINS = 2;
  const double AFC_DAMPING = 0.5;
  const int AFC_PERIOD_FRAMES = 8; // ~1 correction/s a SPECTRUM_FPS=8 (common_rx.py)

  volatile sig_atomic_t g_quit = 0;
  bool g_touch_present = false;
  // Mode kiosque (--kiosk) : masque les boutons d'accord/mode (reglages
  // fixes en ligne de commande), ne garde qu'un petit bouton "Exit" --
  // utile sur une station laissee en reception fixe, pour eviter qu'un
  // appui accidentel sur l'ecran tactile ne change la frequence ou le mode.
  bool g_kiosk = false;

  // "Commande web" de Portsdown4 : nginx relaie les clics de la page
  // webroot/touchscreen/index.html (un <canvas> qui affiche screen.png,
  // capture periodique de l'ecran faite en externe par
  // scripts/screen_grab_for_web.sh) en POST FastCGI sur 127.0.0.1:2005,
  // que touchscreenClick() (voir plus bas) recoit via third_party/
  // portsdown_web/ffunc.{c,h} -- la meme mini-bibliotheque que toutes les
  // applications framebuffer de Portsdown4 (bandview, dmm, rydemon, ...).
  // Desactivee par defaut : n'est active que si portsdown_config.txt
  // contient "webcontrol=enabled", ou si --web est passe explicitement.
  bool g_webcontrol = false;
  char g_web_click_pending[7] = "no"; // "yes"/"no", mis a jour par touchscreenClick()
  int g_web_x = 0, g_web_y = 0;
  char g_program_name[255] = "sstv_radio";
  pthread_t g_webclick_thread;

  pid_t g_child_pid = -1;
  int g_audio_fd = -1;
  int g_control_fd = -1;
  int g_spectrum_fd = -1;
  long long g_freq = 144500000;
  // AFC (--afc) : recentre g_freq sur le pic du spectre local pour
  // compenser une derive (Doppler satellite, derive LO...) qui ferait
  // sortir le signal du filtre de demodulation. Desactivee par defaut (le
  // reglage manuel reste identique a avant). Voir afc_update().
  bool g_afc = false;
  std::string g_mode = "FM";
  std::string g_sdr_name;
  std::string g_sstv_mode = "-"; // mode SSTV detecte (PD120, Robot36, ...), affiche des la synchro
  // Certains SDR (Pluto en particulier, connexion IIO reseau) mettent
  // plusieurs secondes -- jusqu'a 15-20 s observees -- avant la toute
  // premiere trame de spectre, sans aucun retour visuel entre-temps : un
  // ecran statique peut alors sembler fige. g_sdr_connecting pilote
  // l'affichage d'un message "Connexion..." dans la zone image jusqu'a la
  // premiere trame recue (voir drain_spectrum()).
  bool g_sdr_connecting = true;
  // g_waterfall[0] = relevé FFT le plus recent (sert aussi a la trace du
  // haut) ; les lignes suivantes sont l'historique, decalees vers le bas a
  // chaque nouvelle trame (defilement).
  float g_waterfall[WATERFALL_ROWS][FFT_SIZE];

  struct s_button { int x, y, w, h; std::string label; };
  s_button g_freq_buttons[4] = {
    {0, ROW1_Y, BTN_W, BTN_H, "-1k"}, {0, ROW1_Y, BTN_W, BTN_H, "-100"},
    {0, ROW1_Y, BTN_W, BTN_H, "+100"}, {0, ROW1_Y, BTN_W, BTN_H, "+1k"} };
  const int g_freq_deltas[4] = { -1000, -100, 100, 1000 };
  s_button g_mode_buttons[3] = {
    {0, ROW1_Y, BTN_W, BTN_H, "USB"}, {0, ROW1_Y, BTN_W, BTN_H, "LSB"},
    {0, ROW1_Y, BTN_W, BTN_H, "FM"} };
  s_button g_quit_button;

  void on_signal(int) { g_quit = 1; }

  // Doit etre appelee une fois --kiosk connu, avant le premier dessin
  // (draw_chrome) : recalcule SPECTRUM_Y/IMAGE_TOP selon qu'une ligne de
  // boutons d'accord/mode est reservee ou non.
  void configure_layout()
  {
    SPECTRUM_Y = g_kiosk ? (HEADER_H + 4) : (ROW1_Y + BTN_H + 6);
    IMAGE_TOP = SPECTRUM_Y + SPECTRUM_H + 4;
  }

  void layout_buttons()
  {
    if (g_kiosk) {
      // Pas de boutons d'accord/mode : juste un petit "Exit" dans le
      // bandeau d'info, en haut a droite.
      g_quit_button = { screenXsize - 60 - 4, 2, 60, HEADER_H - 4, "Exit" };
      return;
    }

    // Accord a gauche, Quitter a droite, modes centres entre les deux.
    int x = 4;
    for (int i = 0; i < 4; ++i) { g_freq_buttons[i].x = x; x += BTN_W + BTN_GAP; }
    int freq_right = x - BTN_GAP;

    g_quit_button = { screenXsize - BTN_W - 4, ROW1_Y, BTN_W, BTN_H, "Quit" };

    int mode_total_w = 3 * BTN_W + 2 * BTN_GAP;
    int available_w = g_quit_button.x - freq_right;
    int mode_x = freq_right + (available_w - mode_total_w) / 2;
    for (int i = 0; i < 3; ++i) { g_mode_buttons[i].x = mode_x; mode_x += BTN_W + BTN_GAP; }
  }

  bool hit(const s_button& b, int tx, int ty)
  {
    return tx >= b.x && tx <= b.x + b.w && ty >= b.y && ty <= b.y + b.h;
  }

  // Groupe les chiffres par milliers avec des points, ex: 144500000 -> "144.500.000".
  std::string format_freq(long long hz)
  {
    std::string digits = std::to_string(hz);
    std::string out;
    int count = 0;
    for (int i = (int)digits.size() - 1; i >= 0; --i) {
      out.insert(out.begin(), digits[i]);
      if (++count % 3 == 0 && i != 0) out.insert(out.begin(), '.');
    }
    return out;
  }

  // Couleurs (theme sombre proche de Langstone : bandeaux bleu nuit, texte
  // cyan/blanc, accents par fonction).
  const int COL_BG[3]        = {  8,  10,  18 };
  const int COL_HEADER_BG[3] = { 10,  20,  46 };
  const int COL_BORDER[3]    = { 60, 130, 190 };
  const int COL_TEXT[3]      = {225, 235, 245 };
  const int COL_ACCENT[3]    = {  0, 200, 255 };
  const int COL_BTN_BG[3]    = { 22,  28,  42 };
  const int COL_BTN_ACTIVE[3]= {  0, 130, 110 };
  const int COL_QUIT_BG[3]   = { 60,  20,  20 };

  void fill_rect(int x, int y, int w, int h, int r, int g, int b)
  {
    for (int yi = 0; yi < h; ++yi)
      for (int xi = 0; xi < w; ++xi)
        setPixel(x + xi, y + yi, r, g, b);
  }

  void frame_rect(int x, int y, int w, int h, int r, int g, int b)
  {
    for (int i = 0; i < w; ++i) { setPixel(x + i, y, r, g, b); setPixel(x + i, y + h - 1, r, g, b); }
    for (int j = 0; j < h; ++j) { setPixel(x, y + j, r, g, b); setPixel(x + w - 1, y + j, r, g, b); }
  }

  // Ecrit d'un coup (une seule copie memoire par ligne) un rectangle RGB24
  // dans le framebuffer, au lieu d'un setPixel() par pixel. Une zone qui se
  // redessine souvent (le spectre, plusieurs fois par seconde) est ainsi
  // beaucoup moins longtemps "en cours d'ecriture" pendant le balayage de
  // l'ecran, ce qui supprime la plupart des micro-coupures visibles quand le
  // rafraichissement est fait pixel par pixel.
  void blit_rect(int x0, int y0, int w, int h, const std::vector<unsigned char>& rgb)
  {
    std::vector<unsigned char> row((size_t)w * (screenBpp / 8));
    for (int y = 0; y < h; ++y) {
      int fy = y0 + y;
      if (fy < 0 || fy >= screenYsize) continue;
      for (int x = 0; x < w; ++x) {
        int fx = x0 + x;
        size_t si = ((size_t)y * w + x) * 3;
        int r = rgb[si], g = rgb[si + 1], b = rgb[si + 2];
        size_t p = (size_t)x * (screenBpp / 8);
        if (fx < 0 || fx >= screenXsize) continue;
        if (screenBpp == 32) {
          row[p] = (unsigned char)b; row[p + 1] = (unsigned char)g;
          row[p + 2] = (unsigned char)r; row[p + 3] = 0x80;
        } else if (screenBpp == 24) {
          row[p] = (unsigned char)b; row[p + 1] = (unsigned char)g; row[p + 2] = (unsigned char)r;
        } else if (screenBpp == 16) {
          uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
          row[p] = (unsigned char)(v & 0xFF); row[p + 1] = (unsigned char)(v >> 8);
        }
      }
      long offset = (long)((x0 + (long)screenXsize * fy) * (screenBpp / 8));
      memcpy(fbp + offset, row.data(), row.size());
    }
  }

  // Degrade noir -> bleu -> vert -> jaune -> rouge, repris de la palette de
  // spectre/cascade de Langstone-V2 (gen_palette), pour un rendu plus
  // familier qu'un simple vert uni.
  void palette_colour(float v, int& r, int& g, int& b)
  {
    static const int stops[5][3] = {
      {0,0,0}, {0,0,255}, {0,255,0}, {255,255,0}, {255,0,0} };
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    float pos = v * 4.0f;
    int seg = (int)pos; if (seg > 3) seg = 3;
    float f = pos - seg;
    r = (int)(stops[seg][0] + (stops[seg + 1][0] - stops[seg][0]) * f);
    g = (int)(stops[seg][1] + (stops[seg + 1][1] - stops[seg][1]) * f);
    b = (int)(stops[seg][2] + (stops[seg + 1][2] - stops[seg][2]) * f);
  }

  void draw_button(const s_button& b, bool active, const int base_bg[3])
  {
    const int* bg = active ? COL_BTN_ACTIVE : base_bg;
    fill_rect(b.x, b.y, b.w, b.h, bg[0], bg[1], bg[2]);
    const int* border = active ? COL_ACCENT : COL_BORDER;
    frame_rect(b.x, b.y, b.w, b.h, border[0], border[1], border[2]);
    setForeColour(COL_TEXT[0], COL_TEXT[1], COL_TEXT[2]);
    setBackColour(bg[0], bg[1], bg[2]);
    textSize = 1;
    int tx = b.x + (b.w - (int)b.label.size() * 8) / 2;
    int ty = b.y + (b.h - 9) / 2;
    gotoXY(tx, ty);
    displayStr((char*)b.label.c_str());
  }

  void draw_controls(); // voir plus bas -- redessine aussi depuis draw_header()

  // Redessine tout le bandeau d'info (0..HEADER_H), PUIS les boutons
  // (draw_controls()) : en mode kiosque, le petit bouton "Exit" vit dans
  // cette meme bande (voir layout_buttons()), donc tout redessin du
  // bandeau sans ce deuxieme appel l'effacait durablement -- rencontre une
  // premiere fois avec la synchro d'image (image_write_line()), puis a
  // nouveau avec les corrections AFC (afc_update()) : les deux appellent
  // draw_header(), les fusionner ici evite que ce bug ne revienne a chaque
  // nouveau point d'appel futur.
  void draw_header()
  {
    fill_rect(0, 0, screenXsize, HEADER_H, COL_HEADER_BG[0], COL_HEADER_BG[1], COL_HEADER_BG[2]);
    for (int x = 0; x < screenXsize; ++x)
      setPixel(x, HEADER_H, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2]);
    setForeColour(COL_ACCENT[0], COL_ACCENT[1], COL_ACCENT[2]);
    setBackColour(COL_HEADER_BG[0], COL_HEADER_BG[1], COL_HEADER_BG[2]);
    textSize = 1;
    gotoXY(4, 4);
    char buf[128];
    snprintf(buf, sizeof(buf), "SDR: %s   Freq: %s Hz   Mode: %s%s",
             g_sdr_name.c_str(), format_freq(g_freq).c_str(), g_mode.c_str(),
             g_afc ? "   AFC" : "");
    displayStr(buf);

    char sstv_buf[64];
    snprintf(sstv_buf, sizeof(sstv_buf), "SSTV: %s", g_sstv_mode.c_str());
    // Centre (pas aligne a droite) : en mode kiosque, le petit bouton
    // "Exit" occupe le coin superieur droit du bandeau et masquait ce
    // texte. Le centrage pur peut retomber trop pres du texte de gauche
    // (SDR/Freq/Mode, de longueur variable) : on decale alors vers la
    // droite plutot que de simplement masquer le texte -- l'ancienne
    // version exigeait 20 px de marge par rapport au centrage calcule et
    // finissait par ne plus jamais afficher "SSTV: ..." des que le texte
    // de gauche depassait environ la moitie de l'ecran (cas courant,
    // ex: "SDR: rtlsdr   Freq: 437.000.000 Hz   Mode: FM" sur 800 px).
    int sstv_w = (int)strlen(sstv_buf) * 8;
    int left_w = (int)strlen(buf) * 8;
    int sstv_x = (screenXsize - sstv_w) / 2;
    if (sstv_x < left_w + 20) sstv_x = left_w + 20; // ne pas chevaucher le texte de gauche
    if (sstv_x + sstv_w <= screenXsize - 10) { // n'affiche que s'il reste la place avant le bord droit
      gotoXY(sstv_x, 4);
      displayStr(sstv_buf);
    }
    draw_controls();
  }

  void draw_controls()
  {
    if (!g_kiosk) {
      for (auto& b : g_freq_buttons) draw_button(b, false, COL_BTN_BG);
      for (auto& b : g_mode_buttons) draw_button(b, b.label == g_mode, COL_BTN_BG);
    }
    draw_button(g_quit_button, false, COL_QUIT_BG);
  }

  // bin ecran -> bin FFT, avec fftshift : logpwrfft ne recentre pas sa
  // sortie (bin 0 = porteuse/DC, puis frequences positives, puis negatives
  // repliees en fin de tableau) ; decaler d'un demi-tableau retrouve
  // l'allure classique, porteuse au centre.
  int shifted_bin(int x, int w)
  {
    int raw_bin = x * FFT_SIZE / w;
    return (raw_bin + FFT_SIZE / 2) % FFT_SIZE;
  }

  // Trace spectrale + cascade (waterfall), meme principe que la fonction
  // waterfall() de LangstoneGUI_RtlSdr.c (portsdown4/src/langstone) : la
  // trace du haut est le relevé FFT le plus recent, affiche brut (reactif,
  // sans lissage ajoute cote affichage) ; la cascade en dessous accumule
  // l'historique, degrade de couleur noir->bleu->vert->jaune->rouge comme
  // la palette de Langstone (gen_palette).
  void draw_spectrum()
  {
    // Composition entiere en memoire (RAM, pas d'acces au framebuffer),
    // puis une seule copie groupee vers l'ecran (blit_rect) : voir le
    // commentaire de blit_rect. C'est ce qui evite les micro-coupures d'un
    // rafraichissement pixel par pixel plusieurs fois par seconde.
    static std::vector<unsigned char> buf;
    int w = screenXsize, h = SPECTRUM_H;
    buf.assign((size_t)w * h * 3, 0);
    auto put = [&](int x, int y, int r, int g, int b) {
      if (x < 0 || x >= w || y < 0 || y >= h) return;
      size_t i = ((size_t)y * w + x) * 3;
      buf[i] = (unsigned char)r; buf[i + 1] = (unsigned char)g; buf[i + 2] = (unsigned char)b;
    };

    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        put(x, y, COL_BG[0], COL_BG[1], COL_BG[2]);

    // -- cascade : une ligne ecran par trame recue, la plus recente en haut --
    for (int row = 0; row < WATERFALL_ROWS; ++row) {
      for (int x = 0; x < w; ++x) {
        int bin = shifted_bin(x, w);
        float v = (g_waterfall[row][bin] - SPECTRUM_MIN_DB) / (SPECTRUM_MAX_DB - SPECTRUM_MIN_DB);
        if (v < 0) v = 0;
        if (v > 1) v = 1;
        int r, g, b;
        palette_colour(v, r, g, b);
        put(x, LINE_H + row, r, g, b);
      }
    }

    // -- trace du dernier relevé (ligne blanche, brute) --
    int prev_y = -1;
    for (int x = 0; x < w; ++x) {
      int bin = shifted_bin(x, w);
      float v = (g_waterfall[0][bin] - SPECTRUM_MIN_DB) / (SPECTRUM_MAX_DB - SPECTRUM_MIN_DB);
      if (v < 0) v = 0;
      if (v > 1) v = 1;
      int y = (LINE_H - 1) - (int)(v * (LINE_H - 1));
      if (prev_y >= 0) {
        int y0 = prev_y < y ? prev_y : y, y1 = prev_y < y ? y : prev_y;
        for (int yy = y0; yy <= y1; ++yy) put(x, yy, 255, 255, 255);
      } else {
        put(x, y, 255, 255, 255);
      }
      prev_y = y;
    }

    // repere de frequence centrale
    for (int y = 0; y < h; ++y)
      put(w / 2, y, 255, 140, 0);
    // bordure (redessinee a chaque image, comprise dans le meme blit)
    for (int x = 0; x < w; ++x) { put(x, 0, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2]);
                                   put(x, h - 1, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2]); }
    for (int y = 0; y < h; ++y) { put(0, y, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2]);
                                   put(w - 1, y, COL_BORDER[0], COL_BORDER[1], COL_BORDER[2]); }

    blit_rect(0, SPECTRUM_Y, w, h, buf);
  }

  void draw_chrome()
  {
    setBackColour(COL_BG[0], COL_BG[1], COL_BG[2]);
    setForeColour(COL_TEXT[0], COL_TEXT[1], COL_TEXT[2]);
    clearScreen();
    layout_buttons();
    draw_header(); // redessine aussi les boutons (voir son commentaire)
    for (int r = 0; r < WATERFALL_ROWS; ++r)
      for (int i = 0; i < FFT_SIZE; ++i) g_waterfall[r][i] = SPECTRUM_MIN_DB;
    draw_spectrum();
    frame_rect(0, IMAGE_TOP - 2, screenXsize, screenYsize - IMAGE_TOP + 2,
               COL_BORDER[0], COL_BORDER[1], COL_BORDER[2]);
  }

  // Ne garde que les max_images derniers fichiers "sstv_*.bmp" du dossier de
  // sortie, en supprimant les plus anciens (noms horodates, donc l'ordre
  // alphabetique est aussi l'ordre chronologique).
  void prune_old_images(const std::string& dir, int max_images)
  {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    std::vector<std::string> names;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
      std::string name = entry->d_name;
      if (name.size() > 9 && name.compare(0, 5, "sstv_") == 0 &&
          name.compare(name.size() - 4, 4, ".bmp") == 0) {
        names.push_back(name);
      }
    }
    closedir(d);

    std::sort(names.begin(), names.end());
    int excess = (int)names.size() - max_images;
    for (int i = 0; i < excess; ++i)
      sstv_remove_file(dir + "/" + names[i]);
  }

  void send_control(const char* fmt, ...)
  {
    if (g_control_fd < 0) return;
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    send(g_control_fd, buf, strlen(buf), 0);
  }

  void adjust_freq(int delta)
  {
    g_freq += delta;
    if (g_freq < 0) g_freq = 0;
    send_control("FREQ %lld", g_freq);
    draw_header();
  }

  void set_mode(const std::string& mode)
  {
    g_mode = mode;
    send_control("MODE %s", mode.c_str());
    draw_header();
    for (auto& b : g_mode_buttons) draw_button(b, b.label == g_mode, COL_BTN_BG);
  }

  // AFC : recentre g_freq sur le centroide (centre d'energie) du spectre
  // local (g_waterfall[0], derniere trame) dans une fenetre autour de la
  // frequence accordee. Un simple pic ne convient pas ici : en FM, le
  // signal occupe toute la largeur de deviation (+/-5 kHz, voir
  // common_rx.py) et l'energie instantanee se deplace avec le contenu de
  // l'image (zones claires/sombres) plutot que de rester concentree sur la
  // porteuse -- un "pic" observe suit alors le contenu de l'image, pas le
  // centre reel du signal. Le centroide pondere par l'energie (lineaire,
  // pas en dB) reste lui centre sur le signal independamment de cette
  // derive instantanee, tant que le spectre FM reste a peu pres
  // symetrique autour de la porteuse. Appelee a chaque trame recue
  // (drain_spectrum()) mais n'agit qu'une fois toutes les
  // AFC_PERIOD_FRAMES trames (~1 fois/s a 8 trames/s, cote common_rx.py)
  // et seulement d'une fraction (AFC_DAMPING) de l'ecart mesure, pour
  // converger en douceur plutot que de sur-corriger sur une mesure
  // bruitee.
  void afc_update()
  {
    static int counter = 0;
    if (++counter < AFC_PERIOD_FRAMES) return;
    counter = 0;

    // IF_RATE/FFT_SIZE cote common_rx.py (32000/512) : doit rester
    // coherent si l'un des deux change.
    const double HZ_PER_BIN = 32000.0 / FFT_SIZE;
    const int SEARCH_BINS = (int)(AFC_SEARCH_HZ / HZ_PER_BIN);
    const int CENTER = FFT_SIZE / 2;

    // Plancher de bruit estime (minimum de la fenetre) : seule l'energie
    // qui le depasse compte dans le centroide, pour ne pas laisser un
    // bruit de fond a peu pres uniforme (qui contribue alors a peu pres
    // symetriquement, donc sans biais) diluer le poids du signal reel.
    float floor_db = 1e9f;
    for (int s = CENTER - SEARCH_BINS; s <= CENTER + SEARCH_BINS; ++s) {
      float v = g_waterfall[0][shifted_bin(s, FFT_SIZE)];
      if (v < floor_db) floor_db = v;
    }

    double weight_sum = 0.0, weighted_offset_sum = 0.0;
    float peak_above_floor_db = 0.0f;
    for (int s = CENTER - SEARCH_BINS; s <= CENTER + SEARCH_BINS; ++s) {
      float v = g_waterfall[0][shifted_bin(s, FFT_SIZE)] - floor_db; // >= 0
      if (v > peak_above_floor_db) peak_above_floor_db = v;
      // dB -> lineaire : poids proportionnel a l'energie, pas a sa version
      // compressee en dB (sinon un signal fort et un bruit faible pesent
      // presque pareil dans le centroide).
      double p = pow(10.0, v / 10.0) - 1.0; // -1.0 : v=0 (plancher) pese 0
      if (p < 0.0) p = 0.0;
      weight_sum += p;
      weighted_offset_sum += p * (s - CENTER);
    }

    if (peak_above_floor_db < AFC_THRESHOLD_DB) return; // rien d'assez net pour s'y fier
    if (weight_sum <= 0.0) return;

    double centroid_offset_bins = weighted_offset_sum / weight_sum;
    if (fabs(centroid_offset_bins) <= AFC_DEADBAND_BINS) return; // deja raisonnablement centre

    long long correction = (long long)(centroid_offset_bins * HZ_PER_BIN * AFC_DAMPING);
    if (correction == 0) return;
    g_freq += correction;
    if (g_freq < 0) g_freq = 0;
    send_control("FREQ %lld", g_freq);
    draw_header();
  }

  void drain_spectrum()
  {
    if (g_spectrum_fd < 0) return;
    float frame[FFT_SIZE];
    bool got = false;
    // Meme technique que waterfall() de LangstoneGUI_RtlSdr.c : a chaque
    // nouvelle trame, l'historique defile d'une ligne et la trame recue est
    // stockee brute en ligne 0 (pas de lissage ajoute : la cascade elle-meme
    // donne la lecture stable, la trace du haut reste pleinement reactive).
    while (recv(g_spectrum_fd, frame, sizeof(frame), 0) == (ssize_t)sizeof(frame)) {
      for (int r = WATERFALL_ROWS - 1; r > 0; --r)
        memcpy(g_waterfall[r], g_waterfall[r - 1], sizeof(g_waterfall[r]));
      memcpy(g_waterfall[0], frame, sizeof(frame));
      got = true;
    }
    if (got) {
      draw_spectrum();
      if (g_sdr_connecting) {
        // Premiere trame recue : le SDR est bien connecte et produit des
        // donnees, efface le message "Connexion..." de la zone image.
        g_sdr_connecting = false;
        fill_rect(0, IMAGE_TOP, screenXsize, screenYsize - IMAGE_TOP, COL_BG[0], COL_BG[1], COL_BG[2]);
      }
      if (g_afc) afc_update();
    }
  }

  // Lit une valeur "cle=valeur" dans un fichier de configuration texte de
  // Portsdown4 (meme format que GetConfigParam() dans rydemon.c/bandview.c
  // etc.) : une ligne par parametre, "cle=valeur\n". Laisse value_out
  // inchange si la cle est absente ou le fichier introuvable (comportement
  // par defaut sans danger : hors d'une installation Portsdown4, ce
  // fichier n'existe simplement pas).
  void get_config_param(const char* path, const char* key, std::string& value_out)
  {
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[512];
    size_t key_len = strlen(key);
    while (fgets(line, sizeof(line), f)) {
      if (strncmp(line, key, key_len) == 0 && line[key_len] == '=') {
        std::string v = line + key_len + 1;
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r')) v.pop_back();
        value_out = v;
        break;
      }
    }
    fclose(f);
  }

  // Decoupe la querystring "x=...&y=..." du POST /touchscreen/click (voir
  // webroot/touchscreen/index.html : XMLHttpRequest POST "x="+clicked_x+
  // "&y="+clicked_y).
  void parse_click_querystring(char* query_string, int* x_out, int* y_out)
  {
    char* tokens = query_string;
    char* p;
    while ((p = strsep(&tokens, "&\n"))) {
      char* var = strtok(p, "=");
      char* val = var ? strtok(nullptr, "=") : nullptr;
      if (var && val) {
        if (strcmp(var, "x") == 0) *x_out = atoi(val);
        else if (strcmp(var, "y") == 0) *y_out = atoi(val);
      }
    }
  }

  // Thread dedie qui ecoute les clics web (port 2005, FastCGI) pendant
  // toute la duree du programme -- meme fonctionnement que WebClickListener
  // dans rydemon.c/bandview.c : ffunc_run() traite les requetes FastCGI une
  // a une et revient ici a chaque fois, d'ou la boucle.
  void* WebClickListener(void*)
  {
    while (g_webcontrol)
      ffunc_run(g_program_name);
    return nullptr;
  }

  // Teste un point (tactile physique ou clic web) contre les boutons
  // affiches, exactement comme un appui physique -- factorise pour etre
  // appele aussi bien par poll_ui() (tactile) que par le clic web relaye
  // par touchscreenClick() (voir plus bas).
  void handle_touch_point(int tx, int ty)
  {
    if (hit(g_quit_button, tx, ty)) { g_quit = 1; return; }
    if (g_kiosk) return; // pas de boutons d'accord/mode a tester
    for (int i = 0; i < 4; ++i)
      if (hit(g_freq_buttons[i], tx, ty)) { adjust_freq(g_freq_deltas[i]); return; }
    for (auto& b : g_mode_buttons)
      if (hit(b, tx, ty)) { set_mode(b.label); return; }
  }

  // Verifie l'interruption (Ctrl+C), les boutons tactiles (accord, mode,
  // quitter) et un eventuel clic recu par la "commande web" de Portsdown4
  // (voir touchscreenClick()). Appelee regulierement (pas a chaque
  // echantillon audio).
  void poll_ui()
  {
    drain_spectrum();

    if (strcmp(g_web_click_pending, "yes") == 0) {
      int wx = g_web_x, wy = g_web_y;
      strcpy(g_web_click_pending, "no");
      handle_touch_point(wx, wy);
    }

    if (!g_touch_present) return;
    int t = getTouch();
    if (t != 1) return; // debut d'appui uniquement
    handle_touch_point(touchX, touchY);
  }

  void rgb565_to_rgb(uint16_t v, uint8_t& r, uint8_t& g, uint8_t& b)
  {
    uint8_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
    r = (uint8_t)((r5 << 3) | (r5 >> 2));
    g = (uint8_t)((g6 << 2) | (g6 >> 4));
    b = (uint8_t)((b5 << 3) | (b5 >> 2));
  }

  // Source audio : lit le PCM 16 bits signe mono renvoye par le sous-processus SDR.
  // Historique glissant des echantillons audio lus, pour pouvoir rejouer les
  // dernieres secondes au decodeur : le verrouillage SSTV ne commence l'image
  // qu'apres deux synchros consecutives, donc les premieres lignes sont
  // perdues si on demarre le decodeur juste apres. Le rejeu permet de les
  // recuperer sans modifier le decodeur lui-meme.
  const size_t AUDIO_HISTORY_SAMPLES = 37500; // 2.5 s a 15 kHz
  const size_t REPLAY_REWIND_SAMPLES = 30000; // 2.0 s a 15 kHz

  class c_source_pipe
  {
    int m_fd;
    std::deque<int16_t> m_history;  // echantillons lus depuis le pipe, les plus recents en fin
    std::deque<int16_t> m_replay;   // echantillons a rejouer avant de relire le pipe
  public:
    explicit c_source_pipe(int fd) : m_fd(fd) {}

    int16_t read_sample()
    {
      if (!m_replay.empty()) {
        int16_t s = m_replay.front();
        m_replay.pop_front();
        return s;
      }
      int16_t s = read_pipe();
      m_history.push_back(s);
      if (m_history.size() > AUDIO_HISTORY_SAMPLES) m_history.pop_front();
      return s;
    }

    void set_replay(const std::deque<int16_t>& samples) { m_replay = samples; }

    std::deque<int16_t> history_tail(size_t n) const
    {
      size_t count = std::min(n, m_history.size());
      return std::deque<int16_t>(m_history.end() - count, m_history.end());
    }

  private:
    int16_t read_pipe()
    {
      int16_t s = 0;
      size_t got = 0;
      while (got < sizeof(s)) {
        ssize_t n = read(m_fd, ((char*)&s) + got, sizeof(s) - got);
        if (n <= 0) return 0;
        got += n;
      }
      return s;
    }
  };

  class c_sstv_decoder_radio : public c_sstv_decoder
  {
    c_source_pipe& m_source;
    c_bmp_writer_stdio* m_bmp = nullptr;
    uint32_t m_lines_written = 0;
    std::string m_mode_name;
    bool m_origin_set = false;
    int m_origin_x = 0, m_origin_y = 0;
    double m_scale = 1.0;
    bool m_allow_replay;
    bool m_replay_requested = false;

    int16_t get_audio_sample() override { return m_source.read_sample(); }

    void image_write_line(uint16_t line_rgb565[], uint16_t y, uint16_t width, uint16_t height,
                           const char* mode_name) override
    {
      // Premiere ligne decodee : le verrouillage a eu lieu une ligne plus tot,
      // et les lignes du debut ont ete manquees. On demande un rejeu depuis
      // l'historique audio plutot que de dessiner une image incomplete.
      if (!m_origin_set && m_allow_replay) {
        m_replay_requested = true;
        return;
      }

      m_bmp->change_width(width);
      m_bmp->change_height(height);
      m_bmp->write_row_rgb565(line_rgb565);

      if (!m_origin_set) {
        fill_rect(0, IMAGE_TOP, screenXsize, screenYsize - IMAGE_TOP, COL_BG[0], COL_BG[1], COL_BG[2]);
        // Si l'image ne tient pas dans la zone disponible (ex: PD120, 640 x
        // 496, plus haute que la hauteur dispo sur un ecran 800x480 une fois
        // bandeau+spectre retires), on la reduit (plus proche voisin, sans
        // interpolation) au lieu de la laisser deborder hors ecran -- le
        // debordement en hauteur ne peut pas etre recadre comme en largeur
        // (setPixel ignore les x/y hors ecran) car la zone au-dessus de
        // IMAGE_TOP est reservee (bandeau/spectre) et ne doit pas etre
        // ecrasee par l'image.
        int avail_h = screenYsize - IMAGE_TOP;
        m_scale = 1.0;
        if (width > screenXsize) m_scale = std::min(m_scale, (double)screenXsize / width);
        if (height > avail_h) m_scale = std::min(m_scale, (double)avail_h / height);
        int scaled_w = (int)(width * m_scale);
        int scaled_h = (int)(height * m_scale);
        // m_origin_x n'est pas borne a 0 : a l'echelle 1:1, une image plus
        // large que l'ecran doit rester centree, le debordement gauche et
        // droit etant alors coupe a parts egales plutot que de caler
        // l'image a gauche et ne perdre que le bord droit.
        m_origin_x = (screenXsize - scaled_w) / 2;
        m_origin_y = IMAGE_TOP + (avail_h - scaled_h) / 2;
        if (m_origin_y < IMAGE_TOP) m_origin_y = IMAGE_TOP;
        m_origin_set = true;
        g_sstv_mode = mode_name;
        draw_header(); // redessine aussi les boutons (voir son commentaire)
        // Synchro confirmee : on coupe l'ecoute (le "bruit" SSTV n'a pas
        // besoin d'etre entendu), sans affecter le decodage qui continue.
        send_control("MUTE 1");
      }

      // A l'echelle 1:1 (cas normal, la grande majorite des modes), ceci
      // reste un simple m_origin_x + x / m_origin_y + y : (int)(x*1.0)==x.
      // A echelle reduite, plusieurs lignes/colonnes source retombent sur le
      // meme pixel ecran (plus proche voisin) -- rendu moins fin, mais
      // l'image entiere reste visible au lieu d'etre tronquee.
      int py = m_origin_y + (int)(y * m_scale);
      for (uint16_t x = 0; x < width; ++x) {
        uint8_t r, g, b;
        rgb565_to_rgb(line_rgb565[x], r, g, b);
        setPixel(m_origin_x + (int)(x * m_scale), py, r, g, b);
      }

      m_lines_written++;
      m_mode_name = mode_name;
    }

  public:
    c_sstv_decoder_radio(float fs, c_source_pipe& source, bool allow_replay)
      : c_sstv_decoder(fs), m_source(source), m_allow_replay(allow_replay) {}

    bool replay_requested() const { return m_replay_requested; }

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
      while (!done && !g_quit && !m_replay_requested) {
        done = decode_image_non_blocking(timeout_s, true, image_in_progress);
        if ((++n & 0x3FF) == 0) poll_ui();
      }

      bmp.update_header();
      bmp.close();
      m_bmp = nullptr;
      mode_out = m_mode_name;
      if (m_replay_requested) return false;
      // Fin de cette tentative (image complete, perdue ou interrompue) :
      // on retablit l'ecoute pour la prochaine attente de signal.
      send_control("MUTE 0");
      return m_lines_written > 0;
    }
  };

  int make_udp_client(int port)
  {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(fd, (sockaddr*)&addr, sizeof(addr));
    return fd;
  }

  int make_udp_server(int port)
  {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    bind(fd, (sockaddr*)&addr, sizeof(addr));
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
  }

  // Lance le sous-processus Python (sdr/rx_<sdr>.py) et recupere son audio
  // (stdout) via un pipe. Retourne le descripteur de lecture, ou -1 en cas
  // d'echec.
  int spawn_backend(const std::string& sdr_dir, const std::string& sdr, long long freq,
                     const std::string& mode, const std::string& device_args, double gain,
                     const std::string& wav, int control_port, int spectrum_port,
                     const std::string& python_bin, const std::string& dc_offset)
  {
    std::string script = sdr_dir + "/rx_" + sdr + ".py";

    std::vector<std::string> args = { python_bin, script,
      "--freq", std::to_string(freq), "--mode", mode,
      "--control-port", std::to_string(control_port),
      "--spectrum-port", std::to_string(spectrum_port) };
    if (!device_args.empty()) { args.push_back("--device-args"); args.push_back(device_args); }
    if (gain > -1000.0) { args.push_back("--gain"); args.push_back(std::to_string(gain)); }
    if (sdr == "test") { args.push_back("--wav"); args.push_back(wav); }
    if (sdr == "rtlsdr") { args.push_back("--dc-offset"); args.push_back(dc_offset); }

    int pipefd[2];
    if (pipe(pipefd) != 0) return -1;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
      dup2(pipefd[1], STDOUT_FILENO);
      close(pipefd[0]);
      close(pipefd[1]);
      std::vector<char*> argv;
      for (auto& a : args) argv.push_back((char*)a.c_str());
      argv.push_back(nullptr);
      execvp(python_bin.c_str(), argv.data());
      _exit(127);
    }

    close(pipefd[1]);
    g_child_pid = pid;
    return pipefd[0];
  }

  // Detection automatique de l'ecran tactile, meme technique que
  // detectHw() dans LangstoneGUI_RtlSdr.c (portsdown4/src/langstone) :
  // cherche un controleur tactile connu (ecran officiel Raspberry Pi,
  // Hyperpixel4/Goodix, DFRobot MPI7003...) dans /proc/bus/input/devices,
  // puis son noeud /dev/input/eventN sur la ligne Handlers qui suit (celle
  // qui expose aussi une interface "mouse", comme le fait un touchscreen).
  // Evite d'avoir a deviner le bon /dev/input/eventX a la main avec --touch.
  std::string detect_touch_device()
  {
    FILE* f = fopen("/proc/bus/input/devices", "r");
    if (!f) return "";
    char line[512];
    bool is_touch = false;
    std::string found;
    while (fgets(line, sizeof(line), f)) {
      if (line[0] == 'N') {
        is_touch = strstr(line, "FT5406") || strstr(line, "pi-ts") ||
                   strstr(line, "MPI7003") || strstr(line, "Goodix");
      } else if (line[0] == 'H') {
        if (is_touch && strstr(line, "mouse")) {
          char* p = strstr(line, "event");
          if (p) {
            int n = atoi(p + 5);
            char buf[32];
            snprintf(buf, sizeof(buf), "/dev/input/event%d", n);
            found = buf;
          }
        }
        is_touch = false;
      }
    }
    fclose(f);
    return found;
  }

  struct s_menu_item { std::string key, label; };

  // Menu de selection du SDR (tactile uniquement). Bloque jusqu'a un choix
  // ou Ctrl+C.
  std::string sdr_selection_menu()
  {
    std::vector<s_menu_item> items = {
      {"rtlsdr", "RTL-SDR"}, {"pluto", "PlutoSDR"}, {"lime", "LimeSDR"}, {"test", "Test (sans materiel)"} };

    setBackColour(COL_BG[0], COL_BG[1], COL_BG[2]);
    clearScreen();
    setForeColour(COL_ACCENT[0], COL_ACCENT[1], COL_ACCENT[2]);
    gotoXY(10, 10);
    textSize = 2;
    displayStr((char*)"RPI_SSTV -- choisir le recepteur :");

    std::vector<s_button> buttons;
    int y = 60;
    for (auto& it : items) {
      buttons.push_back({40, y, 220, 50, it.label});
      y += 60;
    }
    for (auto& b : buttons) draw_button(b, false, COL_BTN_BG);

    while (!g_quit) {
      if (g_touch_present) {
        int t = getTouch();
        if (t == 1) {
          for (size_t i = 0; i < buttons.size(); ++i)
            if (hit(buttons[i], touchX, touchY)) return items[i].key;
        }
      }
      usleep(50000);
    }
    return "";
  }

  void usage(const char* argv0)
  {
    fprintf(stderr,
      "Usage: %s [--sdr rtlsdr|pluto|lime|test] [--freq Hz] [--mode USB|LSB|FM]\n"
      "          [--device-args ARGS] [--gain dB] [--dc-offset off|manual|automatic]\n"
      "          [--wav fichier.wav]\n"
      "          [--sdr-dir dossier] [--out dossier] [--timeout s] [--max-images N]\n"
      "          [--python interprete] [--control-port N] [--spectrum-port N]\n"
      "          [--fb peripherique] [--touch peripherique] [--geom LxHxBPP] [--kiosk] [--web] [--afc]\n"
      "Sans --sdr : un menu de selection tactile s'affiche (necessite --touch).\n"
      "--kiosk : masque les boutons d'accord/mode (reglages figes par la ligne de\n"
      "          commande), ne garde qu'un petit bouton \"Exit\".\n"
      "--web : force l'activation de la commande web Portsdown4 (port 2005),\n"
      "        meme sans portsdown_config.txt (webcontrol=enabled).\n"
      "--afc : recentre automatiquement la frequence sur le pic du spectre\n"
      "        local (compense une derive Doppler/LO), desactive par defaut.\n",
      argv0);
  }
}

// Handler FastCGI de la "commande web" de Portsdown4 (voir ffunc_run() dans
// third_party/portsdown_web/ffunc.c, qui le resout par son nom exact via
// dlsym -- d'ou le "extern C", indispensable pour eviter le "mangling" du
// nom par g++). nginx y relaie chaque POST /touchscreen/click avec le corps
// "x=<...>&y=<...>" envoye par webroot/touchscreen/index.html.
extern "C" FFUNC touchscreenClick(ffunc_session_t* session)
{
  ffunc_str_t payload;
  if (!g_webcontrol || !ffunc_read_body(session, &payload)) {
    if (g_webcontrol) {
      ffunc_write_out(session, "Status: 400 Bad Request\r\n");
      ffunc_write_out(session, "Content-Type: text/plain\r\n\r\n");
      ffunc_write_out(session, "%s\n", "payload not found.");
    }
    return;
  }

  ffunc_write_out(session, "Status: 200 OK\r\n");
  ffunc_write_out(session, "Content-Type: text/plain\r\n\r\n");
  ffunc_write_out(session, "%s\n", "click received.");

  int x = -1, y = -1;
  parse_click_querystring(payload.data, &x, &y);
  if (x >= 0 && y >= 0) {
    g_web_x = x;
    g_web_y = y;
    strcpy(g_web_click_pending, "yes");
  }
}

int main(int argc, char** argv)
{
  std::string sdr, mode = "FM", device_args, wav = "test_wav/test_pd120.wav";
  std::string sdr_dir = "sdr", out_dir = "images", fb_device = "/dev/fb0", touch_device;
  // sdr/common_rx.py cible GNU Radio 3.7, qui sur les installations
  // Raspberry Pi OS existantes (Portsdown4/Langstone-V2) n'est disponible
  // que pour python2. --python permet de forcer un autre interprete si un
  // systeme plus recent (GNU Radio 3.9/3.10) est utilise.
  std::string python_bin = "python2";
  // 144.500 MHz FM : SSTV ISS, recevable par une cle RTL-SDR standard
  // (contrairement au 20 m HF, hors de portee d'un tuner R820T sans mod).
  long long freq = 144500000;
  double gain = -1000.0; // marqueur "non precise" (AGC materiel)
  std::string dc_offset = "automatic"; // RTL-SDR uniquement ; "off" pour comparer si suspecte
  uint8_t timeout_s = 30;
  int max_images = 10; // nombre max d'images conservees dans --out ; les plus anciennes sont effacees
  int control_port = 7355, spectrum_port = 7356;
  int geom_w = 0, geom_h = 0, geom_bpp = 0;
  bool force_web = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() { return std::string(argv[++i]); };
    if (a == "--sdr") sdr = next();
    else if (a == "--freq") freq = (long long)atof(next().c_str());
    else if (a == "--mode") mode = next();
    else if (a == "--device-args") device_args = next();
    else if (a == "--gain") gain = atof(next().c_str());
    else if (a == "--dc-offset") dc_offset = next();
    else if (a == "--wav") wav = next();
    else if (a == "--python") python_bin = next();
    else if (a == "--sdr-dir") sdr_dir = next();
    else if (a == "--out") out_dir = next();
    else if (a == "--timeout") timeout_s = (uint8_t)atoi(next().c_str());
    else if (a == "--max-images") max_images = atoi(next().c_str());
    else if (a == "--control-port") control_port = atoi(next().c_str());
    else if (a == "--spectrum-port") spectrum_port = atoi(next().c_str());
    else if (a == "--fb") fb_device = next();
    else if (a == "--touch") touch_device = next();
    else if (a == "--geom") sscanf(next().c_str(), "%dx%dx%d", &geom_w, &geom_h, &geom_bpp);
    else if (a == "--kiosk") g_kiosk = true;
    else if (a == "--web") force_web = true;
    else if (a == "--afc") g_afc = true;
    else { usage(argv[0]); return 1; }
  }

  for (auto& c : mode) c = toupper(c);
  g_mode = mode;
  g_freq = freq;
  configure_layout();

  if (!initScreenAt(fb_device.c_str(), geom_w, geom_h, geom_bpp)) {
    fprintf(stderr, "erreur : impossible d'initialiser l'ecran (%s)\n", fb_device.c_str());
    return 1;
  }
  if (touch_device.empty()) touch_device = detect_touch_device();
  if (!touch_device.empty()) {
    g_touch_present = (initTouch((char*)touch_device.c_str()) == 0);
    fprintf(stderr, "ecran tactile : %s (%s)\n", touch_device.c_str(),
            g_touch_present ? "ouvert" : "echec d'ouverture");
  } else {
    fprintf(stderr, "ecran tactile : aucun detecte (voir --touch pour forcer un peripherique)\n");
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  // "Commande web" de Portsdown4 (voir touchscreenClick() plus haut) :
  // desactivee par defaut, activee par portsdown_config.txt (installation
  // Portsdown4 avec le controle web active) ou --web (tests hors
  // Portsdown4). screen.png est capture en externe par
  // scripts/screen_grab_for_web.sh, pas par ce programme.
  std::string webcontrol_cfg;
  get_config_param("/home/pi/rpidatv/scripts/portsdown_config.txt", "webcontrol", webcontrol_cfg);
  g_webcontrol = force_web || webcontrol_cfg == "enabled";
  if (g_webcontrol) {
    strncpy(g_program_name, argv[0], sizeof(g_program_name) - 1);
    pthread_create(&g_webclick_thread, nullptr, &WebClickListener, nullptr);
    fprintf(stderr, "commande web : active (port 2005)\n");
  }

  if (sdr.empty()) {
    if (!g_touch_present) {
      usage(argv[0]);
      closeScreen();
      return 1;
    }
    sdr = sdr_selection_menu();
    if (sdr.empty() || g_quit) { closeScreen(); return 0; }
  }
  g_sdr_name = sdr;

  if (system(("mkdir -p '" + out_dir + "'").c_str()) != 0) {
    fprintf(stderr, "erreur : impossible de creer le dossier %s\n", out_dir.c_str());
    closeScreen();
    return 1;
  }

  g_audio_fd = spawn_backend(sdr_dir, sdr, g_freq, g_mode, device_args, gain, wav,
                              control_port, spectrum_port, python_bin, dc_offset);
  if (g_audio_fd < 0) {
    fprintf(stderr, "erreur : impossible de lancer %s/rx_%s.py\n", sdr_dir.c_str(), sdr.c_str());
    closeScreen();
    return 1;
  }
  g_control_fd = make_udp_client(control_port);
  g_spectrum_fd = make_udp_server(spectrum_port);

  draw_chrome();

  // Message temporaire tant qu'aucune trame de spectre n'est arrivee (voir
  // g_sdr_connecting / drain_spectrum()) : sans lui, l'ecran reste figement
  // statique pendant toute la connexion au SDR (observe jusqu'a 15-20 s sur
  // Pluto, connexion IIO reseau), ce qui peut sembler plante.
  {
    const char* msg = "Connexion au SDR en cours...";
    int msg_w = (int)strlen(msg) * 8;
    int msg_x = (screenXsize - msg_w) / 2;
    int msg_y = IMAGE_TOP + (screenYsize - IMAGE_TOP) / 2 - 4;
    if (msg_x < 0) msg_x = 0;
    setForeColour(COL_TEXT[0], COL_TEXT[1], COL_TEXT[2]);
    setBackColour(COL_BG[0], COL_BG[1], COL_BG[2]);
    textSize = 1;
    gotoXY(msg_x, msg_y);
    displayStr((char*)msg);
  }

  c_source_pipe source(g_audio_fd);
  bool allow_replay = true;

  while (!g_quit) {
    std::string bmp_path = out_dir + "/sstv_" + sstv_timestamp() + ".bmp";
    std::string mode_name;
    c_sstv_decoder_radio decoder(15000.0f, source, allow_replay);
    bool ok = decoder.decode_one_image(bmp_path, timeout_s, mode_name);
    if (decoder.replay_requested()) {
      // Rejeu : une nouvelle tentative repart des dernieres secondes d'audio
      // (qui contiennent la synchro manquee) avant de reprendre le flux direct.
      sstv_remove_file(bmp_path);
      source.set_replay(source.history_tail(REPLAY_REWIND_SAMPLES));
      allow_replay = false;
      continue;
    }
    allow_replay = true;
    if (ok) {
      printf("MODE=%s FICHIER=%s\n", mode_name.c_str(), bmp_path.c_str());
      fflush(stdout);
      prune_old_images(out_dir, max_images);
    } else {
      sstv_remove_file(bmp_path);
    }
  }

  if (g_child_pid > 0) {
    kill(g_child_pid, SIGTERM);
    waitpid(g_child_pid, nullptr, 0);
  }
  closeScreen();
  return 0;
}
