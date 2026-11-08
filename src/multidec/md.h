// md.h : decodeur multiple de Portsdown 4 (multidec), declarations communes
#ifndef MD_H_INCLUDED
#define MD_H_INCLUDED

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#define MD_W 800
#define MD_H 480

#define MD_SHM      "/dev/shm/multidec"                         // fichiers vivants (RAM)
#define MD_CONFIG   "/home/pi/rpidatv/scripts/multidec_config.txt"
#define MD_PCONFIG  "/home/pi/rpidatv/scripts/portsdown_config.txt"
#define MD_LOCATORS "/home/pi/rpidatv/scripts/portsdown_locators.txt"
#define MD_RUN      "/home/pi/rpidatv/src/multidec/run_decoder.sh"
#define MD_LOGDIR   "/home/pi/multidec_logs"

// ---------------------------------------------------------------- dessin
typedef struct { int r, g, b; } md_rgb;

extern const md_rgb C_WHITE, C_HEAD, C_GREEN, C_AMBER, C_GRID, C_RED, C_PANEL, C_DIM, C_SLATE;

enum { F_R16, F_R18, F_R20, F_R22, F_R24, F_C16, F_C18, F_C20, F_B16, F_B18, F_B20, F_B22, F_B24, F_COUNT };

int  draw_init(void);
void draw_present(void);
void draw_clear(void);
void draw_title(const char *s);
void draw_button(int x, int y, int w, int h, const char *colour, const char *label, int big);
void draw_text(int x, int base, int font, md_rgb c, int align, const char *s);
int  text_width(int font, const char *s);
int  text_fit(int font, const char *s, int maxw);
void draw_rect(int x, int y, int w, int h, md_rgb c);
void draw_line(float x0, float y0, float x1, float y1, md_rgb c);
void draw_circle(float cx, float cy, float r, md_rgb c, int fill);
void draw_plane(float cx, float cy, float r, float hdg, md_rgb c);
void draw_sonde_icon(float x, float y);
void menu_rect(int pos, int *x, int *y, int *w, int *h);

// icones APRS (md_aprs_sym.c, genere par make_aprs_symbols.py)
#define MD_SYM 24
extern const uint32_t md_sym_px[3][94][MD_SYM * MD_SYM];
bool draw_aprs_symbol(int cx, int cy, const char *sym);

// icone de radiosonde (md_sonde_icon.c, genere par make_sonde_icon.py)
extern const int md_sonde_w, md_sonde_h;
extern const float md_sonde_ax, md_sonde_ay;
extern const uint32_t md_sonde_px[];

// ---------------------------------------------------------------- tactile
int  touch_init(bool *web);      // descripteur a surveiller : evenements (x, y) en int[2]

// ---------------------------------------------------------------- configuration
const char *conf_get(const char *key, const char *def);
void conf_set(const char *key, const char *val);
void conf_load(void);
void pconf_get(const char *file, const char *key, char *out, int size);

// ---------------------------------------------------------------- JSON (lecture simple)
bool json_str(const char *js, const char *path, char *out, int size);
bool json_num(const char *js, const char *path, double *v);
const char *json_find(const char *js, const char *path, int *len);
const char *json_skip(const char *p);

// ---------------------------------------------------------------- decodeurs
enum { V_LIST, V_OBJ, V_SONDE, V_STREAM, V_SENSOR, V_RADIO };
enum { P_MULTIMON, P_APRS, P_MINIMODEM, P_CW, P_SONDE, P_ADSB, P_RTL433, P_AIS, P_ACARS, P_VDL2,
       P_DSD, P_FT8, P_WSPR, P_SELCALL, P_RDS, P_DAB };
enum { O_NONE, O_AIRCRAFT, O_SHIP, O_APRS, O_SONDE, O_STATION, O_SENSOR, O_SERVICE };

#define MAX_OPTS 4
#define MAX_VALS 5

typedef struct {
  const char *key;               // cle de configuration <decodeur>_<key> ("freq" : frequence)
  const char *label;             // libelle de la touche
  const char *vals[MAX_VALS];    // valeurs (NULL : fin)
  const char *shown[MAX_VALS];   // libelle affiche (NULL : la valeur)
  const char *def;               // valeur par defaut
} md_opt;

typedef struct {
  const char *key;               // nom court (configuration, script, journal)
  const char *button;            // libelle de la touche du menu (^ : deux lignes)
  const char *title;             // nom dans le bandeau
  const char *freq;              // frequence par defaut (MHz)
  bool fixed;                    // frequence imposee (ADS-B, AIS...)
  int view, parser, objtype;
  bool audio;                    // chaine audio rtl_fm (barre de niveau)
  bool fm;                       // audio FM (niveau = silence du bruit)
  md_opt opt[MAX_OPTS];
  const char *preset[2][2];      // touches de preselection : libelle, "cle=valeur,..."
} md_decoder;

extern const md_decoder DECODERS[];
extern const int NDECODERS;
const md_decoder *dec_find(const char *key);
const char *dec_opt(const md_decoder *d, int i);
const char *dec_freq(const md_decoder *d);
const char *dec_gain(const md_decoder *d);

// ---------------------------------------------------------------- donnees decodees
#define MSG_MAX 400
#define OBJ_MAX 400
#define TRAIL   32
#define TRACK_MAX 1200

typedef struct {
  time_t t;
  md_rgb col;
  char head[100];
  char body[600];
} md_msg;

typedef struct {
  int type;
  char id[24];
  char name[32];
  char sym[4];                   // symbole APRS : table (ou overlay) puis code, "/-"
  char pty[24];                  // radio (RDS, DAB) : type de programme
  char kind[16];                 // DAB : "DAB+ 88k" ; RDS : PI
  float mhz;                     // radio : frequence (stations du balayage FM), 0 sinon
  char info[96];                 // valeurs principales (capteurs) ou commentaire
  char extra[96];
  double lat, lon;
  bool pos;
  float alt;                     // m
  float spd;                     // noeuds
  float crs, vz;                 // deg, m/s
  float temp, hum, batt, snr;
  bool hum_approx;               // radiosonde : humidite approchee (M10, calcul sans etalonnage) : "~"
  int frame, burst;              // radiosondes
  long count;
  time_t first, seen, lastmsg;
  float trail[TRAIL][2];
  int ntrail;
} md_obj;

typedef struct { float lat, lon, alt; time_t t; } md_track;

extern md_msg  g_msg[MSG_MAX];
extern int     g_nmsg, g_msghead;          // anneau : g_msghead = prochain index
extern md_obj  g_obj[OBJ_MAX];
extern int     g_nobj;
extern md_track g_track[TRACK_MAX];
extern int     g_ntrack;
extern char    g_track_id[24];
extern char    g_status[128];               // etat propre au decodeur (porteuse...)
extern double  g_slat, g_slon;              // station

// Radio (RDS, DAB+) : station ou multiplex en cours de reception, et l'heure
// qu'il diffuse (a la minute), comparee a l'horloge du Pi
typedef struct {
  char name[40];                 // RDS : nom (PS) ; DAB : multiplex
  char id[16];                   // PI ; identifiant du multiplex
  char pty[24];
  char text[96];                 // RDS : radiotexte
  char extra[160];               // RDS : frequences alternatives ; DAB : canal, SNR
  bool tp, ta;
  time_t utc;                    // derniere heure recue (UTC)
  int lto;                       // decalage local diffuse (minutes)
  double diff;                   // horloge du Pi - heure recue (s), a la reception
  time_t got;                    // reception de cette heure (horloge du Pi)
} md_radio;
extern md_radio g_radio;
void radio_time(time_t utc, int lto, double latency);
const char *dab_channel(const char *freq);
// delais entre le debut de la minute diffusee et sa lecture (s), mesures sur
// le Pi synchronise par NTP. RDS : 1,13 et 1,19 s (RMC 106,7, chaine rtl_fm,
// md_level, redsea), emetteur compris ; DAB : 0,64 et 0,30 s (8D, welle-cli,
// mux.json lu toutes les 0,25 s)
#define RDS_LATENCY 1.15
#define DAB_LATENCY 0.45
extern bool    g_dirty;                     // a redessiner
extern long    g_adsb_msgs;
extern float   g_adsb_rate;

void ascii_fold(char *s);
void msg_add(md_rgb col, const char *head, const char *body);
md_obj *obj_get(int type, const char *id);
void obj_pos(md_obj *o, double lat, double lon);
void track_add(const char *id, double lat, double lon, double alt);
void stream_put(const char *s, int n);
void stream_clear(void);
void data_clear(void);
void dist_az(double lat, double lon, double *km, double *az);
bool locator_to_ll(const char *loc, double *lat, double *lon);

extern char g_stream[60][80];
extern int  g_stream_lines, g_stream_col;

void parse_line(const md_decoder *d, char *line);
void parse_bytes(const md_decoder *d, const char *buf, int n);
void parse_poll(const md_decoder *d);       // appele toutes les 250 ms
void log_line(const md_decoder *d, const char *head, const char *body);

#endif
