char** /*stretchy*/
finderComputeResults(Finder* f, Lifetime life, CommandsEnum* selectedCmd)
{
   CommandsEnum* sCommandsEnum = {};
   char** sCommands = listCommands(life, &sCommandsEnum);

   int* sDistances = {};

   pushApiLifetime(Lifetime_Frame);
      arrsetlen(sDistances, arrlen(sCommands));
   popApiLifetime();

   for (sz i = 0; i < arrlen(sCommands); ++i) {
      int cmdDist = 0;
      // Compute distance for this entry
      {
         char* str = f->sSearchString;

         char* cmd = sCommands[i];
         int cmdLen = strlen(cmd);
         int strLen = arrlen(str);

         for (int strIdx = 0; strIdx < strLen; ++strIdx) {
            char c = str[strIdx];
            int cDist = 20;

            for (int cmdIdx = 0; cmdIdx < cmdLen; ++cmdIdx) {
               if (tolower(c) == tolower(cmd[cmdIdx])) {
                  cDist = Min(cDist, abs(cmdIdx - strIdx));
               }
            }

            cmdDist += cDist;
         }
      }
      sDistances[i] = cmdDist;
   }

   // Just bubblesort for now. - 2020-01-17
   for (sz i = 0; i < arrlen(sCommands); ++i) {
      for (sz j = i+1; j < arrlen(sCommands); ++j) {
         if (sDistances[i] > sDistances[j]) {
            int dTmp = sDistances[i];
            sDistances[i] = sDistances[j];
            sDistances[j] = dTmp;

            char* cmdTmp = sCommands[i];
            sCommands[i] = sCommands[j];
            sCommands[j] = cmdTmp;

            CommandsEnum eTmp = sCommandsEnum[i];
            sCommandsEnum[i] = sCommandsEnum[j];
            sCommandsEnum[j] = eTmp;
         }
      }
   }

   if (selectedCmd) {
      *selectedCmd = sCommandsEnum[f->selectionIdx];
   }

   return sCommands;
}

void
finderExit(Finder* f)
{
   arrsetlen(f->sSearchString, 0);
   f->selectionIdx = 0;
}
