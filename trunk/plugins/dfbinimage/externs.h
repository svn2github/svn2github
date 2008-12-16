/************************************************************************

externs.h

Copyright (C) 2007 Virus
Copyright (C) 2002 mooby

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

************************************************************************/

#pragma warning(disable:4786)


#ifndef EXTERNS_H
#define EXTERNS_H

#include "defines.h"

// sets all the callbacks as extern "c" for linux compatability
extern "C"
{

char * CALLBACK PSEgetLibName(void);
unsigned long CALLBACK PSEgetLibType(void);
unsigned long CALLBACK PSEgetLibVersion(void);
void CALLBACK CDRabout(void);
long CALLBACK CDRtest(void);
long CALLBACK CDRconfigure(void);
long CALLBACK CDRclose(void);
long CALLBACK CDRopen(void);
long CALLBACK CDRshutdown(void);
long CALLBACK CDRplay(unsigned char * sector);
long CALLBACK CDRstop(void);
long CALLBACK CDRsetfilename(char *);

// gcc doenst like this...
#if defined _WINDOWS || defined __CYGWIN32__
long CALLBACK CDRgetStatus(struct CdrStat *stat) ;
#endif

char CALLBACK CDRgetDriveLetter(void);
long CALLBACK CDRinit(void);
long CALLBACK CDRgetTN(unsigned char *buffer);
unsigned char * CALLBACK CDRgetBufferSub(void);
long CALLBACK CDRgetTD(unsigned char track, unsigned char *buffer);
long CALLBACK CDRreadTrack(unsigned char *time);
unsigned char * CALLBACK CDRgetBuffer(void);


void   CD_About(UINT32 *par);
int CD_Wait(void);
void CD_Close(void);
int CD_Open(unsigned int* par);
int CD_Play(unsigned char * sector);
int CD_Stop(void);
int CD_GetTN(char* result);
unsigned char* CD_GetSeek(void);
unsigned char* CD_Read(unsigned char* time);
int CD_GetTD(char* result, int track);
int    CD_Configure(UINT32 *par);

/* PS2 callbacks */

u32   CALLBACK PS2EgetLibType(void);
u32   CALLBACK PS2EgetLibVersion(void);
char* CALLBACK PS2EgetLibName(void);

s32  CALLBACK CDVDinit();
s32  CALLBACK CDVDopen();
void CALLBACK CDVDclose();
void CALLBACK CDVDshutdown();
s32  CALLBACK CDVDreadTrack(cdvdLoc *Time);

// return can be NULL (for async modes)
u8*  CALLBACK CDVDgetBuffer();
s32  CALLBACK CDVDgetTN(cdvdTN *Buffer);
s32  CALLBACK CDVDgetTD(u8 Track, cdvdLoc *Buffer);

// extended funcs
void CALLBACK CDVDconfigure();
void CALLBACK CDVDabout();
s32  CALLBACK CDVDtest();


}

#endif
