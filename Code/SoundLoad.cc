
LoadedSound
mp3Load(Platform* plat, char* pathToMp3, Lifetime life)
{
   mp3dec_ex_t dec = {};

   LoadedSound sound = {};

   u8* bytes = {};
   u64 numBytes = plat->fileContentsAscii(pathToMp3, bytes, Lifetime_Frame);

   if (!bytes) {
      logMsg("Could not load mp3!\n");
   }
   else {

   }
   if (mp3dec_ex_open_buf(&dec, bytes, numBytes, MP3D_SEEK_TO_SAMPLE))
   {
      logMsg("Could not open mp3 file %s\n", pathToMp3);
   }

   if (mp3dec_ex_seek(&dec, 0))
   {
      logMsg("Error seeking to start of mp3 %s\n", pathToMp3);
   }

   sound.hz = dec.info.hz;
   sound.channels = dec.info.channels;
   sound.numBytes = sound.channels * dec.samples * sizeof(i16);

   pushApiLifetime(life);
   {
      arrsetcap(sound.samples, dec.samples);

      size_t remaining = dec.samples;
      while (true) {
          remaining -= mp3dec_ex_read(&dec, sound.samples, remaining);
          if (!remaining) { break; }
      }
      logMsg("Done reading\n");
   }
   popApiLifetime();


   return sound;
}