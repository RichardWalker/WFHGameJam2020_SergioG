static struct {
   Platform* plat;
} *gLogger;

u64
logGlobalSize() {return sizeof(*gLogger); }

void
logGlobalSet(u8* ptr) {gLogger = (decltype(gLogger))ptr; }


void
logInit(Platform* p)
{
   gLogger->plat = p;
}

void
logMsg(char* msg, ...)
{
   const int maxLogSz = 1024;
   char s[maxLogSz] = {};

   va_list args;
   va_start(args, msg);
   vsnprintf(s, maxLogSz, msg, args);
   gLogger->plat->consoleLog(s);
   va_end(args);
}

