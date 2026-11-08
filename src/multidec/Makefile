# Decodeur multiple (multidec) et ses deux utilitaires.
# Reutilise le dessin de la GUI : ../gui/Graphics.h, button3d.h, polices, ffunc.

GUI = ../gui
CFLAGS = -O2 -Wall -fcommon -I$(GUI) -I$(GUI)/font
FONTS = $(GUI)/font/font.c $(GUI)/font/dejavu_sans_16.c $(GUI)/font/dejavu_sans_18.c \
	$(GUI)/font/dejavu_sans_20.c $(GUI)/font/dejavu_sans_22.c $(GUI)/font/dejavu_sans_24.c \
	$(GUI)/font/dejavu_sans_26.c $(GUI)/font/dejavu_sans_28.c $(GUI)/font/dejavu_sans_30.c \
	$(GUI)/font/dejavu_sans_32.c $(GUI)/font/dejavu_sans_36.c $(GUI)/font/dejavu_sans_72.c \
	$(GUI)/font/dejavu_sans_bold_16.c $(GUI)/font/dejavu_sans_bold_18.c $(GUI)/font/dejavu_sans_bold_20.c \
	$(GUI)/font/dejavu_sans_bold_22.c $(GUI)/font/dejavu_sans_bold_24.c $(GUI)/font/dejavu_sans_bold_28.c \
	$(GUI)/font/dejavu_sans_cbold_16.c $(GUI)/font/dejavu_sans_cbold_18.c $(GUI)/font/dejavu_sans_cbold_20.c \
	$(GUI)/font/dejavu_sans_cbold_22.c $(GUI)/font/dejavu_sans_cbold_24.c $(GUI)/font/dejavu_sans_cbold_28.c
SRC = multidec.c md_draw.c md_touch.c md_util.c md_dec.c md_aprs_sym.c md_sonde_icon.c

all: multidec md_level md_ft8 md_audio md_fmdemod

multidec: $(SRC) md.h $(GUI)/Graphics.h $(GUI)/button3d.h $(GUI)/ffunc.c
	gcc $(CFLAGS) -o multidec $(SRC) $(GUI)/ffunc.c $(FONTS) -lpthread -lm -lfcgi -ldl -rdynamic

md_level: md_level.c
	gcc -O2 -Wall -o md_level md_level.c -lm

md_ft8: md_ft8.c
	gcc -O2 -Wall -o md_ft8 md_ft8.c -lm

md_audio: md_audio.c
	gcc -O2 -Wall -o md_audio md_audio.c

md_fmdemod: md_fmdemod.c
	gcc -O2 -Wall -o md_fmdemod md_fmdemod.c -lm

clean:
	rm -f multidec md_level md_ft8 md_audio md_fmdemod
