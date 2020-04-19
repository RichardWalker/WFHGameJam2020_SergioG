
static struct
{
  IXAudio2* xaudio;
  AudioStream** sTransientStreams;
} * gAudio;

struct AudioStream
{
   Lifetime life;
   i32 numBuffersInFlight;
   IXAudio2SourceVoice* voice;
};

u64
audioGlobalSize()
{
   u64 size = sizeof(*gAudio);
   return size;
}

void
audioGlobalSet(u8* ptr)
{
   gAudio = (decltype(gAudio))ptr;
}

void
audioInit()
{
   gAudio->xaudio = {};
   HRESULT res = XAudio2Create(
     &gAudio->xaudio,
     0,
     XAUDIO2_DEFAULT_PROCESSOR);

   if (!SUCCEEDED(res)) {
      OutputDebugStringA("Could not load xaudio 2");
      exit(-1);
   }

   IXAudio2MasteringVoice* masterVoice = NULL;
   if ( FAILED(res = gAudio->xaudio->CreateMasteringVoice( &masterVoice ) ) ) {
       OutputDebugStringA("Could not create mastering voice");
       exit(-1);
   }
}

void audioStreamEnd(AudioStream* stream);

struct AudioCallback : IXAudio2VoiceCallback
{
   AudioStream* stream;

   void OnVoiceProcessingPassStart(UINT32 BytesRequired) final {}
   void OnVoiceProcessingPassEnd() final {}
   void OnStreamEnd() final {}
   void OnBufferStart(void* pBufferContext) final {}
   void OnBufferEnd(void* pBufferContext) final
   {
      stream->numBuffersInFlight--;
   }
   void OnLoopEnd(void* pBufferContext) final {}
   void OnVoiceError(void* pBufferContext, HRESULT Error) final {}
};

AudioStream*
audioStreamBegin(int hz, int channels)
{
   Lifetime life = lifetimeBegin();
   auto* stream = AllocateElem(AudioStream, life);

   stream->life = life;

   WAVEFORMATEX wfx = {};

   wfx.wFormatTag = WAVE_FORMAT_PCM;
   wfx.nChannels = channels;
   wfx.nSamplesPerSec = hz;
   wfx.wBitsPerSample = 16;
   wfx.nBlockAlign = wfx.wBitsPerSample * wfx.nChannels / 8;
   wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;
   wfx.cbSize = 0;

   AudioCallback* callback = AllocateElem(AudioCallback, stream->life);

   // Construct the callback...
   AudioCallback* toConstruct = new(callback) AudioCallback;

   callback->stream = stream;


   HRESULT hr;
   if( FAILED(hr = gAudio->xaudio->CreateSourceVoice(
                              &stream->voice,
                              (WAVEFORMATEX*)&wfx,
                              0,  // flags
                              XAUDIO2_DEFAULT_FREQ_RATIO,
                              callback ) ) ) {
      OutputDebugStringA("Could not create voice");
      exit(-1);
   }

   return stream;
}

AudioStream*
transientAudioStream(int hz, int channels)
{
   AudioStream* stream = audioStreamBegin(hz, channels);

   arrpush(gAudio->sTransientStreams, stream);

   return stream;
}

void
audioStreamEnd(AudioStream* stream)
{
   stream->voice->DestroyVoice();

   lifetimeEnd(stream->life);
}

void
submitAudioToStream(AudioStream* stream, i16* samples, int numBytes, bool endStream)
{
   stream->numBuffersInFlight++;

   XAUDIO2_BUFFER buf = {};
   buf.Flags = endStream ? XAUDIO2_END_OF_STREAM : 0;
   buf.AudioBytes = numBytes;
   buf.pAudioData = (BYTE*)samples;

   stream->voice->SubmitSourceBuffer(&buf);

   stream->voice->Start(0);
}

void
playAudio(i16* samples, int hz, int channels, int numBytes)
{
   AudioStream* stream = transientAudioStream(hz, channels);
   submitAudioToStream(stream, samples, numBytes, true);
}

void audioFrameEnd()
{
   // Free up audio streams.
   for (int i = 0; i < arrlen(gAudio->sTransientStreams); ++i) {
      AudioStream* s = gAudio->sTransientStreams[i];
      if (s->numBuffersInFlight == 0) {
         audioStreamEnd(s);
         arrdelswap(gAudio->sTransientStreams, i--);
      }
   }
}