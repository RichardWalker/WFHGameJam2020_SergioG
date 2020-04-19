#pragma once

enum EventFlags
{

};

struct KeyboardState
{
   bool special[Key_Count];
   int specialTransition[Key_Count];
};

struct ResizeInput
{
   bool withResize;
   int width;
   int height;
};

struct Input
{
   MouseInput m;
   KeyboardState k;
   ResizeInput r;
};