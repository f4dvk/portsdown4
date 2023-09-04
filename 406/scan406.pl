#!/usr/bin/env perl
# Script perl pour scanner la fréquence des balises 406MHz

# Script perl inspiré  de  "rtlsdr_scan" des auteurs de "RS-master"
# https://github.com/rs1729

# ce script utilise "rtl_power" et "rtl_fm" pour la réception sur une clé SDR
# https://github.com/keenerd/rtl-sdr

# ainsi que "reset_dvbt"
# http://askubuntu.com/questions/645/how-do-you-reset-a-usb-device-from-the-command-line

# le décodage se fait avec "dec406_v6" contenu dans "scan406.zip"
# http://jgsenlis.free.fr/406_PI.html
# Auteur F4EHY 8-6-2020

use POSIX qw(strftime);
use strict;
use warnings;
my $f1_scan = "406M";
my $f2_scan = "406.1M";
my $ppm = 0;
my $line;
my $utc;
my $date=0;
my $time=0;
my $f1=0;
my $f2=0;
my $step=0;
my @db;
my $i;
my $j;
my $frq=0;
my $dec = '/home/pi/rpidatv/406/dec406_V6 --100 --M3 --une_minute';
my $dec1 = '/home/pi/rpidatv/406/dec406_V6 --100 --M3 --une_minute --no_checksum';
my $dec3 = '/home/pi/rpidatv/406/dec406_V6 --100 --M3';
my $dec4 = '/home/pi/rpidatv/406/dec406_V6 --100 --M3 --no_checksum';
my $timeout = "timeout 56s";
my $fixe = 0;
my $no_checksum = 0;
my $mic = 0;
my $card = 0;
my $filter = "lowpass 3000 highpass 400"; #highpass de 10Hz à 400Hz selon la qualité du signal

my $largeur = "12k";
my $WFM="";
my $squelch=-200;
my $snr = 6;
my $trouve;
my $mean;
my $powfile = sprintf 'log_power.csv';
my $smtp_serveur='smtp.gmail.com:587';
my $utilisateur='toto@gmail.com';
my $password='mot_de_passe_mail';
my $destinataires='dede@free.fr,lili@orange.fr';
#lit_config_mail();
#print "$smtp_serveur\n$utilisateur\n$password \n$destinataires\n";

my $var=@ARGV;
if ($var<2)
	{print "\n SYNTAXE:  scan406.pl  freq_Depart  freq_Fin [decalage_ppm] [no_checksum]\n";
	print " Exemple:\n	scan406.pl 406M 406.1M\n	scan406.pl 406M 406.1M 0 no_checksum\n\n";
	exit(0);
	}

for (my $i=0;$i<@ARGV;$i++)
{
	#print "\n $ARGV[$i]";
	if ($i==0)
	{$f1_scan=$ARGV[0];
	}
	if ($i==1)
	{$f2_scan=$ARGV[1];
	}
	if ($i==2)
	{$ppm=$ARGV[2];
	}
	if ($i==3)
	{$card=$ARGV[3]
	}
	if ($i==4)
	{  if ($ARGV[4] eq 'no_checksum')
	   {
	     $dec=$dec1;
	     $no_checksum=1;
	   }
	   elsif ($ARGV[4] eq 'mic')
	   {
	     $mic=1;
	   }
	}
	if ($i==5)
	{  if ($ARGV[5] eq 'mic')
	   {
	     $mic=1;
	   }
	}

}


while (1) {
    if ($mic==0)
    {
      reset_dvbt();
    }
    my $freq_trouvee=0;
    if (($f1_scan eq $f2_scan) || ($mic==1)){
      $freq_trouvee=1;
      $frq=$f1_scan;
      $fixe=1;
    }
    while ($freq_trouvee==0){
	system("rtl_power -p $ppm -f $f1_scan:$f2_scan:400 -i12 -P -O -1 -e12 -w hamming $powfile 2>/dev/null &");
	if ( $? == -1 ) {print "\nErreur: $!";}
	my $process = qx{ps -C rtl_power | grep rtl_power};
	my $tps=12;
	print "\nScan $f1_scan...$f2_scan ";
	$utc = strftime('%d %m %Y   %Hh%Mm%Ss', gmtime);
	print " $utc UTC ($tps s)";
	print "\n...PATIENTER...";
	while ($process ne ''){
	  sleep 1;
	  print "\nScan $f1_scan...$f2_scan ";
	  --$tps;
	  print " $utc UTC ($tps s)";
	  print "\n...PATIENTER...";
	  $process = qx{ps -C rtl_power | grep rtl_power};
	}
	my $fh;
	open ($fh, '<', "$powfile") or die "Erreur fichier: $!\n";
	my $num_lines = 0;
	$frq=0;
	my $Maxi=-200;
	while ($line = <$fh>) {
	    $num_lines += 1;
	    chomp $line;
	    @db = split(",", $line);
	    $date = $db[0];
	    shift @db;
	    $time = $db[0];
	    shift @db;
	    $f1 = $db[0];
	    shift @db;
	    $f2 = $db[0];
	    shift @db;
	    $step = $db[0];
	    shift @db;
	    shift @db;

	    my $sum = eval join '+', @db;
	    $mean = $sum / scalar(@db);
	    $squelch = $mean + $snr;

	    for ($j = 0; $j < scalar(@db)-1; $j++) {
		if ($db[$j] > $Maxi) {
		    $Maxi=$db[$j];
		    $frq = $f1 + ($j)*$step;
		}

	    }
	}
	#print "Date: $date   $time  $f1  $f2 $step \n";
	close $fh;

	if ($num_lines == 0) {
	    print "\n[reset dvb-t ...]";
	    reset_dvbt();
	}
	if ($Maxi>$squelch) {
	    $freq_trouvee=1;
	    my $f=$frq/1000000.0;
	    printf "\nFréq: %.3fMHz Niveau: %.1fdB Moyenne: %.1fdB ",$f,$Maxi,$mean;
	    }

    }

    do{
#    my $tps2=56;
    if ($fixe == 1) {
      $timeout = "";
      if ($no_checksum == 0)
      {
        $dec=$dec3;
      }
      else
      {
        $dec=$dec4;
      }
    }
    printf "\nLancement du Decodage   ";
    $utc = strftime(' %d %m %Y   %Hh%Mm%Ss', gmtime);
    printf "$utc UTC";
		#AUDIODEV=hw:Loopback,0,2 play - 2>/dev/null
    if ($mic==0)
    {
      my @args = ( "bash", "-c", "$timeout rtl_fm -p $ppm -M fm $WFM -s $largeur -f $frq  2>/dev/null |\
              sox -t raw -r $largeur -e s -b 16 -c 1 - -t wav - $filter 2>/dev/null |\
              tee >(aplay2 -D plughw:Loopback,0,2 2>/dev/null) >($dec) >(aplay -D plughw:$card 2>/dev/null) >/dev/null" );
      system(@args);
    }
    else
    {
      system("AUDIODRIVER=alsa sox -d -t wav - $filter 2>/dev/null |\
	      $dec ");
    }
#    $trouve="PAS encore trouve";
#    my $ligne;
#    if (open (F2, '<', './code')) {
#	while (defined ($ligne = <F2>)) {
#	    chomp $ligne;
	    #print "\nligne : $ligne\n";
#	    if ($ligne eq 'TROUVE') {$trouve='TROUVE';}
#	    }
#	close F2;
#	}

#    affiche_trame();
    #if($trouve eq 'TROUVE'){envoi_mail();}

    } #while ($trouve eq 'TROUVE');

}

## http://askubuntu.com/questions/645/how-do-you-reset-a-usb-device-from-the-command-line
## usbreset.c -> reset_usb

sub reset_dvbt {
    my @devices = split("\n",`lsusb`);
    foreach my $line (@devices) {
        if ($line =~ /\w+\s(\d+)\s\w+\s(\d+):\sID\s([0-9a-f]+):([0-9a-f]+).+Realtek Semiconductor Corp\./) {
            if ($4 eq "2832"  ||  $4 eq "2838") {
                system("/home/pi/rpidatv/406/reset_usb /dev/bus/usb/$1/$2");
            }
        }
    }
}


sub envoi_mail {
    my $a='"./trame"';
    my $u='"Alerte_Balise_406"';
    my $m='" Date et Heure (UTC) du decodage: '.$utc.'"';
    my $l='email.log';
    my $s='"'.$smtp_serveur.'"';;
    my $o='tls=yes';
    my $f='"'.$utilisateur.'"';
    my $xu='"'.$utilisateur.'"';
    my $xp='"'.$password.'"';
    my $t='"'.$destinataires.'"';

system("sendemail -l $l -f $f -u $u -t $t -s $s -o $o -xu $xu -xp $xp -m $m -a $a 2>/dev/null 1>/dev/null");

}


# affichage fichier ./trame
sub affiche_trame {
					my $ligne;
					if (open (F3, '<', './trame')) {
						while (defined ($ligne = <F3>)) {
							#chomp $ligne;
							printf "$ligne";
							}
						close F3;
					}
}

#lire config_mail.txt
sub lit_config_mail {
		my ($k, $v);
		my %h;
		if (open(F4, "</home/pi/rpidatv/406/config_mail.txt")){
			#copie key/value depuis le fichier 'config_mail' dans hash.
			while (<F4>) {
				chomp;
				($k, $v) = split(/=/);
				$h{$k} = $v;
			}
			close F4;
			$utilisateur=$h{'utilisateur'};
			$password=$h{'password'};
			$smtp_serveur=$h{'smtp_serveur'};
			$destinataires=$h{'destinataires'};
		}
}
