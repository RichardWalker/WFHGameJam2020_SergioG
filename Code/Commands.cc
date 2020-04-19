Editor* gEditor;

Editor* editor()
{
   return gEditor;
}

u64
editorGlobalSize()
{
   u64 size = sizeof(*gEditor);
   return size;
}

void
editorGlobalSet(u8* ptr)
{
   gEditor = (decltype(gEditor))ptr;
}

void
commandPerform(Platform* plat, CommandsEnum cmd)
{
   switch(cmd) {
      case Command_MaterialEditor: {
         modeEnable(Mode_MaterialEditor);
      } break;

      case Command_Play: {
         modeEnable(Mode_Play);
      } break;
      case Command_Fly: {
         modeEnable(Mode_Fly);
      } break;

      case Command_Restart: {
         makeAndSetWorld();
         gameInit(plat);
      };

      case Command_RaytracedShadows: {
         gKnobs.useRaytracedShadows = !gKnobs.useRaytracedShadows;
      } break;

      case Command_Quit: {
         plat->engineQuit();
      } break;

      default: {

      } break;
   }
}

char** /* Stretchy char* buffer */
listCommands(Lifetime life, CommandsEnum** sCommandsOut = {})
{
	char** list = NULL;

	#define Command(id, str) SBPush(list, str, life);
	  #include "Commands.inl"
	#undef Command

   if (sCommandsOut) {
      #define Command(id, str) SBPush(*sCommandsOut, Command_##id, life);
         #include "Commands.inl"
      #undef Command
   }

	return list;
}


bool
modeExit()
{
   bool handled = false;
   switch(gEditor->mode) {
      case Mode_Finder: {
         finderExit(&gEditor->finder);
         gEditor->mode = Mode_Play;
         handled = true;
      } break;
   }

   return handled;
}


bool
modeIs(Mode mode)
{
   bool is = gEditor->mode == mode;
   return is;
}

bool
modeHandleEscape()
{
   bool handled = false;
   if (modeIs(Mode_MaterialEditor)) {
      MaterialEditor* matEd = &gEditor->materialEd;
      switch(matEd->state) {
         case MatEd_Pick: {
            handled = true;
            modeExit();
         } break;
         case MatEd_Edit: {
            handled = true;
            matEd->pickedObj = ObjectHandle{0};
            matEd->state = MatEd_Pick;
         } break;
      }
   }
   else {  // Default: Exit on escape
      handled = modeExit();
   }
   return handled;
}

void
finderShow()
{
   immInit(&editor()->defaultFont);
}

bool
handleKeyShortcuts(Platform* plat)
{
   bool handled = false;
   if (keyJustPressed(plat, Key_Escape)) {
      handled = modeHandleEscape();
   }
   // TODO: DO we care about checking for right-ctrl?
   if (keyHeld(plat, Key_Ctrl) && keyJustPressed(plat, Key('p'))) {
      modeEnable(Mode_Finder);
      finderShow();

      handled = true;
   }

   return handled;
}

bool
modeTick(Platform* plat)
{
   bool capturedInput = true;

   if (handleKeyShortcuts(plat)) {

   }
   // TODO: If this gets too hairy, probably introduce a virtual interface.
   else if (modeIs(Mode_Finder)) {
      immSetCursor(5, 20);
      Finder* f = &gEditor->finder;
      immTextInput(plat, &f->sSearchString);

      CommandsEnum selectedCmd = {};
      char** sSortedCommands = finderComputeResults(f, Lifetime_Frame, &selectedCmd);
      immList(sSortedCommands, arrlen(sSortedCommands), f->selectionIdx);

      if (keyJustPressed(plat, Key_Enter)) {
         commandPerform(plat, selectedCmd);
      }

      if (keyJustPressed(plat, Key_Up)) {
         f->selectionIdx -= 1;
      }

      if (keyJustPressed(plat, Key_Down)) {
         f->selectionIdx += 1;
      }

      while (f->selectionIdx < 0) {
         f->selectionIdx += arrlen(sSortedCommands);
      }
      f->selectionIdx %= arrlen(sSortedCommands);
   }
   else if (modeIs(Mode_Fly)) {
      float delta = 0.1f;

      vec3 projDir = getWorld()->cam.lookat - getWorld()->cam.eye;
      projDir.y = 0;

      vec3 front = normalizedOrZero(projDir);
      vec3 up = vec3{0,1,0};
      vec3 right = cross(up, front);

      vec3 diff = {};
      if (keyHeld(plat, Key('w'))) {
         diff += front*delta;
      }
      if (keyHeld(plat, Key('s'))) {
         diff -= front*delta;
      }
      if (keyHeld(plat, Key('a'))) {
         diff -= right*delta;
      }
      if (keyHeld(plat, Key('d'))) {
         diff += right*delta;
      }
      if (keyHeld(plat, Key_Space)) {
         diff.y += delta;
      }
      if (keyHeld(plat, Key_Shift)) {
         diff.y -= delta;
      }
      getWorld()->cam.lookat += diff;
      getWorld()->cam.eye += diff;
   }
   else if (modeIs(Mode_MaterialEditor)) {
      MaterialEditor* matEd = &gEditor->materialEd;
      switch( matEd->state) {
         case MatEd_Pick: {
            immSetCursor(0, 20);
            immText("Pick an object in the world...");
            ObjectHandle oh = immObjectPick();
            if (oh.idx) {
               matEd->pickedObj = oh;
               matEd->state = MatEd_Edit;
            }
         } break;
         case MatEd_Edit: {
            if (matEd->pickedObj.idx > 0) {
               materialSliders(&materialForObject(matEd->pickedObj)->constants);
            }
            else {
               immSetCursor(0, gKnobs.height - 100);
               immText("Selected an invalid handle");
            }
         } break;
      }
   }
   else {
      capturedInput = false;  // Mode is play
   }
   return capturedInput;
}

void
modeEnable(Mode mode)
{
   modeExit();
   gEditor->mode = mode;
}
