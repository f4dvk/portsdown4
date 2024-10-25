####################################################
#Add these imports to the top
#####################################################

import os
import errno


#######################################################
# Manually add just before the Main () Function
# to provide support for Piped commands
#######################################################
def docommands(tb):
  try:
    os.mkfifo("/tmp/langstoneRx")
  except OSError as oe:
    if oe.errno != errno.EEXIST:
      raise
  ex=False
  lastbase=0
  while not ex:
    fifoin=open("/tmp/langstoneRx",'r')
    while True:
       try:
        with fifoin as filein:
         for line in filein:
           line=line.strip()
           if line[0]=='Q':
              ex=True
           if line[0]=='U':
              value=int(line[1:])
              tb.set_Rx_Mute(value)
           if line[0]=='H':
              value=int(line[1:])
              if value==1:
                  tb.lock()
              if value==0:
                  tb.unlock()
           if line[0]=='O':
              value=int(line[1:])
              tb.set_RxOffset(value)
           if line[0]=='V':
              value=int(line[1:])
              tb.set_AFGain(value)
           if line[0]=='L':
              value=int(line[1:])
              tb.set_Rx_LO(value)
           if line[0]=='A':
              value=int(line[1:])
              tb.set_Rx_Gain(value)
           if line[0]=='F':
              value=int(line[1:])
              tb.set_Rx_Filt_High(value)
           if line[0]=='I':
              value=int(line[1:])
              tb.set_Rx_Filt_Low(value)
           if line[0]=='M':
              value=int(line[1:])
              tb.set_Rx_Mode(value)
           if line[0]=='C':
              value=int(line[1:])
              tb.set_CTCSS(value)
           if line[0]=='W':
              value=int(line[1:])
              tb.set_FFT_SEL(value)

       except:
         break

########################################################


#########################################################
#Replace the Main() function with this
########################################################
    tb = top_block_cls()
    tb.start()
    docommands(tb)
    tb.stop()
    tb.wait()
#########################################################
