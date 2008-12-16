/***************************************************************************
                          key.c  -  description
                             -------------------
    begin                : Sun Oct 28 2001
    copyright            : (C) 2001 by Pete Bernert
    email                : BlackDove@addcom.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version. See also the license.txt file for *
 *   additional informations.                                              *
 *                                                                         *
 ***************************************************************************/

//*************************************************************************// 
// History of changes:
//
// 2005/04/15 - Pete
// - Changed user frame limit to floating point value
//
// 2002/10/04 - Pete
// - added Full vram view toggle key
//
// 2002/04/20 - linuzappz
// - added iFastFwd key
//
// 2002/02/23 - Pete
// - added toggle capcom fighter game fix
//
// 2001/12/22 - syo
// - added toggle wait VSYNC key handling
//   (Pete: added 'V' display in the fps menu)
//
// 2001/12/15 - lu
// - Yet another keyevent handling...
//
// 2001/11/09 - Darko Matesic
// - replaced info key with recording key (sorry Pete)
//   (Pete: added new recording key... sorry Darko ;)
//
// 2001/10/28 - Pete  
// - generic cleanup for the Peops release
//
//*************************************************************************// 

#define _IN_KEY

#include "externals.h"
#include "menu.h"
#include "gpu.h"
#include "draw.h"
#include "key.h"

////////////////////////////////////////////////////////////////////////
// LINUX VERSION
////////////////////////////////////////////////////////////////////////


  	//X11
#define VK_INSERT      65379
#define VK_HOME        65360
#define VK_PRIOR       65365
#define VK_NEXT        65366
#define VK_END         65367
#define VK_DEL         65535
#define VK_F5          65474

////////////////////////////////////////////////////////////////////////

void GPUmakeSnapshot(void);

unsigned long          ulKeybits=0;

void GPUkeypressed(int keycode)
{
 switch(keycode)
  {
   case 0xFFC9:			//X11 key: F12
   case ((1<<29) | 0xFF0D):	//special keycode from pcsx-df: alt-enter
	bChangeWinMode=TRUE;
	break;
   case VK_F5:
       GPUmakeSnapshot();
      break;

   case VK_INSERT:
       if(iUseFixes) {iUseFixes=0;dwActFixes=0;}
       else          {iUseFixes=1;dwActFixes=dwCfgFixes;}
       SetFixes();
       if(iFrameLimit==2) SetAutoFrameCap();
       break;

   case VK_DEL:
       if(ulKeybits&KEY_SHOWFPS)
        {
         ulKeybits&=~KEY_SHOWFPS;
         DoClearScreenBuffer();
        }
       else 
        {
         ulKeybits|=KEY_SHOWFPS;
         szDispBuf[0]=0;
         BuildDispMenu(0);
        }
       break;

   case VK_PRIOR: BuildDispMenu(-1);            break;
   case VK_NEXT:  BuildDispMenu( 1);            break;
   case VK_END:   SwitchDispMenu(1);            break;
   case VK_HOME:  SwitchDispMenu(-1);           break;
   case 0x60:     
    {
     iFastFwd = 1 - iFastFwd;  
     bSkipNextFrame = FALSE; 
     UseFrameSkip=iFastFwd;
     BuildDispMenu(0);
     break;
    }
  }
}

////////////////////////////////////////////////////////////////////////

void SetKeyHandler(void)
{
}

////////////////////////////////////////////////////////////////////////

void ReleaseKeyHandler(void)
{
}


