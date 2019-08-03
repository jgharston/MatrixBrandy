#include <stdio.h>
#include <math.h>
#include <string.h>
#include <SDL.h>
#include <SDL_audio.h>
#include "basicdefs.h"
#include "target.h"
#include "mos.h"
#include "screen.h"
#include "soundsdl.h"

/*

Middle C is 261.63 Hz - A above middle C is 440Hz (432Hz)

The pitch number 53 is the number for middle C. Pitch is represented by a number from 1 to 255, as follows:

              Octave number

   Note   1    2     3     4     5     6

   A          41    89   137   185   233
   A#         45    93   141   189   237
   B      1   49    97   145   193   241
   C      5   53   101   149   197   245
   C#     9   57   105   153   201   249
   D     13   61   109   157   205   253
   D#    17   65   113   161   209
   E     21   69   117   165   213
   F     25   73   121   169   217
   F#    29   77   125   173   221
   G     33   81   129   177   225
   G#    37   85   133   181   229

Octave 2 is the one containing middle C.

It is also possible to represent pitch by a number from 0x100 (256) to 32767 (0x7FFF), in which case middle C is 0x4000.
*/


static int snd_nvoices=1;
static int snd_beat=0;
static int snd_beats=0;
static int snd_tempo=0;
static int snd_ison=0;
static int snd_volume=127;

static unsigned int snd_inited = 0;
static int snd_paused = 0;

static SDL_AudioSpec desiredSpec;

/* */
typedef struct sndent {
signed int count;
unsigned short int step;
unsigned char vol, chant;
} sndent;
/* */


#define SNDTABWIDTH 32
static sndent sndtab[8][SNDTABWIDTH];

static unsigned char snd_rd[8],snd_wr[8];

static unsigned short int soffset[8], *poffset;

static int sactive=0;

static unsigned char ssl[8],ssr[8];

static unsigned char chanvoice[8];
static unsigned char sintab[1025];
static unsigned int steptab[388];

static unsigned int stime[8];

static void audio_callback(void *unused, Uint8 *ByteStream, int Length) {

 /* Length is length of buffer in bytes */
 int i,vl,vr,ilen,tmp,s;
 unsigned int *ptr;

 int cm1,bit;
 sndent *snd, *tptr;

 ilen = (Length)>>2;
 ptr = (unsigned int*)ByteStream;
 for(i=0; i<ilen; i++)
  ptr[i]=0x80808080;

 if(sactive == 0){
  SDL_PauseAudio(1);
  snd_paused = 1;
  return;
 } else {
  for(bit=1, cm1=0; cm1 < snd_nvoices; cm1++, bit<<=1) {
   if(sactive & bit){

    snd = & sndtab[cm1][snd_rd[cm1]];
    poffset = & soffset[cm1];

    s = (snd->vol)*snd_volume;
    vl = s >> (5 + ssl[cm1]);
    vr = s >> (5 + ssr[cm1]);

    if((vl>0 || vr>0) && snd->step>0 )
     switch(snd->chant) {

      case 0 : /* WaveSynth beep :- sine wave */
       for(i=0; i<Length; i++){
        *poffset += snd->step;
        s = sintab[(*poffset)>>6]-128;

        tmp=((int)ByteStream[i])+((vl*s)>>6);
        if(tmp<0)tmp=0;else if(tmp>255)tmp=255;
        ByteStream[i] = tmp;

        i++;

        tmp=((int)ByteStream[i])+((vr*s)>>6);
        if(tmp<0)tmp=0;else if(tmp>255)tmp=255;
        ByteStream[i] = tmp;

       }
      break;

      case 1 : /* stringlib :- square wave */
       for(i=0; i<Length; i++){
        *poffset += snd->step;

        if(*poffset & 0x8000) {

         tmp = ByteStream[i] + vl;
         if(tmp>255)tmp=255;
         ByteStream[i] = tmp;

         i++;

         tmp = ByteStream[i] + vr;
         if(tmp>255)tmp=255;
         ByteStream[i] = tmp;

        } else {
         tmp = ByteStream[i] - vl;
         if(tmp < 0) tmp= 0;
         ByteStream[i] = tmp;

         i++;

         tmp = ByteStream[i] - vr;
         if(tmp < 0) tmp= 0;
         ByteStream[i] = tmp;

        }
       }
      break;

      case 2 : /* percussion :- square wave with vibrato */
       for(i=0; i<Length; i++){
        *poffset += snd->step;
        if(i & 0x100){
         i += 0xff;
         *poffset += (((snd->step)<<7) - snd->step); 
        }else{
         if(*poffset & 0x8000) {

          tmp = ByteStream[i] + vl;
          if(tmp>255)tmp=255;
          ByteStream[i] = tmp;

          i++;

          tmp = ByteStream[i] + vr;
          if(tmp>255)tmp=255;
          ByteStream[i] = tmp;

         } else {
          tmp = ByteStream[i] - vl;
          if(tmp < 0) tmp= 0;
          ByteStream[i] = tmp;

          i++;

          tmp = ByteStream[i] - vr;
          if(tmp < 0) tmp= 0;
          ByteStream[i] = tmp;

         }
        }
       }
      
      break;

      default:
      break;
      }
    snd->count -= Length;
    if(snd->count <= 0){
     snd->count = 0;
     snd_rd[cm1] = (snd_rd[cm1]+1)&(SNDTABWIDTH-1); /* move to next sound in list */
     tptr= & sndtab[cm1][snd_rd[cm1]];
     if( tptr->count <= 0) { /* deactivate this channel if new entry is empty */
      sactive &= ~bit;
     }
    }
    /* pause sound system if all channels are inactive */
    if( (sactive & ((1<<snd_nvoices)-1)) == 0){
     sactive = 0;
     SDL_PauseAudio(1);
     snd_paused = 1;
    }
   }
  }
 }
}

static void clear_sndtab()
{

 int i;

 memset(sndtab, 0, sizeof(sndtab));

 for (i=0; i< 8;i++) {
   snd_rd[i]=1;
   snd_wr[i]=0;
 }

}

void init_sound(){

 int s,i,rv;
 double fhz;

 // fprintf(stderr,"init_sound called\n");

 SDL_InitSubSystem(SDL_INIT_AUDIO);

 desiredSpec.freq     = 20480;
 desiredSpec.format   = AUDIO_U8;
 desiredSpec.channels = 2;
 desiredSpec.samples  = 2048;
 desiredSpec.callback = audio_callback;
 desiredSpec.userdata = (void*)0;

 rv=SDL_OpenAudio(&desiredSpec, (SDL_AudioSpec *)0);
 if(rv < 0){ 
  fprintf(stderr,"init_sound: Failed to open audio device\n");
  snd_inited = 0;
  snd_ison = 0;
  return;
 }

 snd_inited = (unsigned int)basicvars.centiseconds;

 for(i=0; i<8; i++){
   /* init all voices as 'synth wave' */
   chanvoice[i]=1;
   /* stereo centred */
   ssl[i]=0;
   ssr[i]=0;

   stime[i] = 0;
 }

 /* init sintab */
 for(i=0; i<=256; i++){
  s=(int)floor(128.0+(127.5*sin(((double)i)*M_PI*1.0/512.0)));
  sintab[i]     = s;
  sintab[512-i] = s;
  sintab[512+i] = 255-s;
  sintab[1024-i]= 255-s;
 }

/* init step tab */
 for(i=0;i<= 48;i++){
  fhz = 440.0*pow(2.0, ((double)(i-89))*(1.0/48.0));
  steptab[i] = (int)floor((fhz * ((double)0xffffffffu/20480.0))+0.5);
  // fprintf(stderr,"steptab[%d] is %u\n",i,steptab[i]);
 }
 for(i=49;i<=388;i++){
  steptab[i] = (steptab[i-48])<< 1;
 }

 clear_sndtab();


 for(i=0;i<8;i++) soffset[i]=0;

 SDL_Delay(40); /* Allow time for sound system to start. */

 SDL_PauseAudio(1);

 snd_paused = 1;
 snd_ison = 1;
}

void sdl_sound(int32 channel, int32 amplitude, int32 pitch, int32 duration, int32 delay){

 unsigned int step;
 int tvol;
 int cht;
 int cm1;
 sndent *snd;

 double e,f;
 int t,diff,pl;

 unsigned int tnow=0;

 if(!snd_inited) init_sound();

 if(!snd_ison ) return;


 if(duration <= 0 || channel < 1 || channel > snd_nvoices) return;

 cm1 = channel-1;

 if(pitch > 25766 ) pitch=25766;
 if(pitch < 0 )     pitch=0;

/*
 if(pitch < 256)
  fhz = 440.0*pow(2.0, ((double)(pitch-89))*(1.0/48.0));
 else
  fhz = 440.0*pow(2.0, ((double)(pitch-0x1c00))*(1.0/4096.0));

 step = (int)floor((fhz * (65536.0/20480.0))+0.5);
*/

 if(pitch < 256) {
  step = steptab[pitch] >> 16; 
 }else {
  // step = steptab[(int)floor( (((double)(pitch-0x1c00))*(48.0/4096.0))+89.5)] >> 16;
  e= (((double)(pitch-0x1c00))*(48.0/4096.0))+89;
  f=floor(e);
  e -= f;
  t=(int)f;
  diff =  floor(0.5 + (1.0/65536.0)*e*((double)(steptab[t+1] - steptab[t])));
  step = (steptab[t] >> 16) + diff;
  //fprintf(stderr,"t is %3d e is %6.3f diff is %5d\n",t, e, diff);
 }

 // fprintf(stderr,"sdl_sound called: cm1 (%2d) amplitude (%3d) pitch (%5d) duration (%3d) delay (%d) step is %d\n",cm1, amplitude, pitch, duration, delay, step);


 // fprintf(stderr,"sdl_sound: step is %d delay is %d is_on %d paused %d\n",step, delay, snd_ison, snd_paused);

 tvol= 0;
 if(amplitude < -15)amplitude= -15;
 else
 if(amplitude > 383)amplitude= 383;

 if(amplitude < 0 && amplitude >= -15) tvol = 1-amplitude;
 else
 if(amplitude >=256 && amplitude <= 383) tvol = 1+((amplitude-256)>>3);

 cht=(chanvoice[cm1]+2)>>2;

 if(duration > 254) duration = 254;
 if(delay > 255) delay = 255;

 // fprintf(stderr,"sdl_sound tvol %3d step is %5d snd_wr[%d] = %2d snd_rd[%d] = %2d stime[%d] %4d tnow %4d sactive %2x\n", tvol, step, cm1, snd_wr[cm1], cm1, snd_rd[cm1], cm1, stime[cm1], tnow, sactive);

 if(tvol > 0 && step > 0 && step < 32768  && ((snd_rd[cm1]-snd_wr[cm1])&(SNDTABWIDTH-1)) != 2    ){

  tnow = ((unsigned int)basicvars.centiseconds - snd_inited )/5; /* divide by 5 to covert centiseconds to 20ths */

  if(stime[cm1] < tnow )
     stime[cm1] = tnow;

  SDL_LockAudio();

  if(delay > 0  &&  (pl = tnow+delay-stime[cm1]) > 0 ){

    snd_wr[cm1] = (snd_wr[cm1]+1)&(SNDTABWIDTH-1);

    snd = &sndtab[cm1][snd_wr[cm1]];

    snd->step    = 0; /* play silence durring delay */
    snd->count   = pl << 11;
    snd->vol     = 0;
    snd->chant   = 0;

    stime[cm1] += pl;

    delay = -1;
  } else {
    snd = &sndtab[cm1][snd_wr[cm1]];
  }

  if(delay != 0 || snd_wr[cm1] != snd_rd[cm1]  || snd->count == 0 ){
    /* move to next entry */
    snd_wr[cm1] = (snd_wr[cm1]+1)&(SNDTABWIDTH-1);
    snd = &sndtab[cm1][snd_wr[cm1]];

    stime[cm1] += duration;

  } else { 
    if ( delay == 0 ) {
     int r;
     r = snd_rd[cm1];
     snd = & sndtab[cm1][r]; /* over write playing entry */
     snd_wr[cm1] = r;
    }
    stime[cm1] = tnow + duration;
  }

  snd->step    = step;
  snd->count   = duration << 11;
  snd->vol     = tvol;
  snd->chant   = cht;

  sndtab[cm1][(snd_wr[cm1]+1) & (SNDTABWIDTH-1)].count = 0; /* clear next entry */

  sactive |= (1 << cm1);

  SDL_UnlockAudio();

  // fprintf(stderr,"sdl_sound: step is %d cm1 %d type %d tvol %d sactive %d\n",step, cm1, cht, tvol, sactive);

  if( snd_ison && snd_paused){
   SDL_PauseAudio(0);
   snd_paused = 0;
  }
 }
 
}

void sdl_sound_onoff(int32 onoff){
 // fprintf(stderr, "sdl_sound_onoff(%d) called ison %d paused %d \n",onoff, snd_ison, snd_paused);
 if(onoff && !snd_ison ) {
  if(!snd_inited) init_sound();
  snd_ison = 1;
 } else if ( !onoff && snd_ison) {

  SDL_LockAudio();
  clear_sndtab();
  SDL_UnlockAudio();

  snd_ison = 0;
  SDL_PauseAudio(1);
  snd_paused = 1;
 }
}


void sdl_wrbeat(int32 beat){
 snd_beat = beat;
}

int32 sdl_rdbeat() {
 return snd_beat;
}

int32 sdl_rdbeats() {
 return snd_beats;
}

void sdl_wrtempo(int32 tempo) {
 snd_tempo = tempo;
}

int32 sdl_rdtempo() {
 return snd_tempo;
}

static char *voicetab[10]={
  "",
  "WaveSynth-Beep",
  "StringLib-Soft",
  "StringLib-Pluck",
  "StringLib-Steel",
  "StringLib-Hard",
  "Percussion-Soft",
  "Percussion-Medium",
  "Percussion-Snare",
  "Percussion-Noise"
};

void sdl_voice(int32 channel, char *name) {

 int i,ch, n;

 // fprintf(stderr,"sdl_voice called: channel (%d) name \"%s\"\n",channel, name);

 if(!snd_inited) init_sound();

 n=0;
 if ( (ch= *name) >= '1' && ch <= '9'){
  n= ch-'0';
 } else {
  for(i=1;i<=9;i++)
   if( strcmp(name,voicetab[i]) == 0) {
    n = i;
    break;
   }
 }
 if(channel >=1 && channel <=8 && n>=1 && n<=9) chanvoice[channel-1] = n;

 // fprintf(stderr,"sdlvoice - channel number is %d\n",n);
}
 
/*

*voice
            Voice      Name
   1          1   WaveSynth-Beep
              2   StringLib-Soft
              3   StringLib-Pluck
              4   StringLib-Steel
              5   StringLib-Hard
              6   Percussion-Soft
              7   Percussion-Medium
              8   Percussion-Snare
              9   Percussion-Noise
*/

void sdl_star_voices() {
 int i,v;
 
 if(!snd_inited) init_sound();

 emulate_printf("        Voice      Name\r\n");

 for(i=1;i<=9;i++){
  for(v=1;v<=8;v++)
   if(v <= snd_nvoices && chanvoice[v-1] == i)
     emulate_printf("%d",v);
    else
      emulate_printf(" ");

  emulate_printf(" %d %s\r\n", i, voicetab[i]);
 }
 emulate_printf("^^^^^^^^  Channel Allocation Map\r\n");

}

void  sdl_voices(int32 channels) {
 int i,c,n;

 if(!snd_inited) init_sound();

 n=8;
 for(i=1;i<=4;i+=i)
  if(i >= channels){
    n = i;
    break;
  }
 if( n >= 1 && n <= 8 ) {
    snd_nvoices = n;
    /* if nvoices is reduced then the sndtab entries need to be cleared or they will be played if it is increased later */
    SDL_LockAudio();
    sactive &= ((1<snd_nvoices)-1);
    for(c = n; c < 8; c++){
     snd_rd[c] = 1;
     snd_wr[c] = 0;
     stime[c]  = 0;
     for(i = 0; i < SNDTABWIDTH; i++){
      sndtab[c][i].count = 0;
      sndtab[c][i].vol   = 0;
     }
    }
    SDL_UnlockAudio();
 }
}

void sdl_stereo(int32 channel, int32 position) {
 int cm1;
 if(!snd_inited) init_sound();
 cm1 = channel-1;
/*
 -127 to -80 full left
  -79 to -48  2/3 left
  -47 to -16  1/3 left
  -15 to +15 Centre

*/
 /* centre - both channels full volume */
 ssl[cm1] = 0;
 ssr[cm1] = 0;

 if(position < 0){
  /* left full and right reduced */
  if ( position <= -80 ) { ssr[cm1]= 8; return;}
  if ( position <= -48 ) { ssr[cm1]= 2; return;}
  if ( position <= -16 ) { ssr[cm1]= 1; return;}
 }else {
  /* right full and left reduced */
  if ( position >=  80 ) { ssl[cm1]= 8; return;}
  if ( position >=  48 ) { ssl[cm1]= 2; return;}
  if ( position >=  16 ) { ssl[cm1]= 1; return;}
 }
}

void sdl_volume(int32 vol) {

 if(!snd_inited) init_sound();

 if(vol <   0) vol = 0;
 if(vol > 127) vol = 127;
 snd_volume = vol;
}
