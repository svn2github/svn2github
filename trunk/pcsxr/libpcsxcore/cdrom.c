/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

/*
* Handles all CD-ROM registers and functions.
*/

#include "cdrom.h"
#include "ppf.h"
#include "psxdma.h"

cdrStruct cdr;

/* CD-ROM magic numbers */
#define CdlSync        0
#define CdlNop         1
#define CdlSetloc      2
#define CdlPlay        3
#define CdlForward     4
#define CdlBackward    5
#define CdlReadN       6
#define CdlStandby     7
#define CdlStop        8
#define CdlPause       9
#define CdlInit        10
#define CdlMute        11
#define CdlDemute      12
#define CdlSetfilter   13
#define CdlSetmode     14
#define CdlGetmode     15
#define CdlGetlocL     16
#define CdlGetlocP     17
#define CdlReadT       18
#define CdlGetTN       19
#define CdlGetTD       20
#define CdlSeekL       21
#define CdlSeekP       22
#define CdlSetclock    23
#define CdlGetclock    24
#define CdlTest        25
#define CdlID          26
#define CdlReadS       27
#define CdlReset       28
#define CdlReadToc     30

#define AUTOPAUSE      249
#define READ_ACK       250
#define READ           251
#define REPPLAY_ACK    252
#define REPPLAY        253
#define ASYNC          254
/* don't set 255, it's reserved */

char *CmdName[0x100]= {
    "CdlSync",     "CdlNop",       "CdlSetloc",  "CdlPlay",
    "CdlForward",  "CdlBackward",  "CdlReadN",   "CdlStandby",
    "CdlStop",     "CdlPause",     "CdlInit",    "CdlMute",
    "CdlDemute",   "CdlSetfilter", "CdlSetmode", "CdlGetmode",
    "CdlGetlocL",  "CdlGetlocP",   "CdlReadT",   "CdlGetTN",
    "CdlGetTD",    "CdlSeekL",     "CdlSeekP",   "CdlSetclock",
    "CdlGetclock", "CdlTest",      "CdlID",      "CdlReadS",
    "CdlReset",    NULL,           "CDlReadToc", NULL
};

unsigned char Test04[] = { 0 };
unsigned char Test05[] = { 0 };
unsigned char Test20[] = { 0x98, 0x06, 0x10, 0xC3 };
unsigned char Test22[] = { 0x66, 0x6F, 0x72, 0x20, 0x45, 0x75, 0x72, 0x6F };
unsigned char Test23[] = { 0x43, 0x58, 0x44, 0x32, 0x39 ,0x34, 0x30, 0x51 };

// cdr.Stat:
#define NoIntr		0
#define DataReady	1
#define Complete	2
#define Acknowledge	3
#define DataEnd		4
#define DiskError	5

// 1x = 75 sectors per second
// PSXCLK = 1 sec in the ps
// so (PSXCLK / 75) = cdr read time (linuzappz)
#define cdReadTime (PSXCLK / 75)

static struct CdrStat stat;
static struct SubQ *subq;


extern unsigned int msf2sec(char *msf);
extern void sec2msf(unsigned int s, char *msf);


extern u16 *iso_play_cdbuf;
extern u16 iso_play_bufptr;
extern long CALLBACK ISOinit(void);
extern void CALLBACK SPUirq(void);
extern SPUregisterCallback SPU_registerCallback;

#define H_SPUirqAddr		0x1f801da4
#define H_SPUaddr				0x1f801da6
#define H_SPUctrl				0x1f801daa
#define H_CDLeft				0x1f801db0
#define H_CDRight				0x1f801db2


#define CDR_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDR); \
	psxRegs.intCycle[PSXINT_CDR].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDR].sCycle = psxRegs.cycle; \
}

#define CDREAD_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDREAD); \
	psxRegs.intCycle[PSXINT_CDREAD].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDREAD].sCycle = psxRegs.cycle; \
}

#define CDREPPLAY_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDREPPLAY); \
	psxRegs.intCycle[PSXINT_CDREPPLAY].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDREPPLAY].sCycle = psxRegs.cycle; \
}

#define CDRDBUF_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDRDBUF); \
	psxRegs.intCycle[PSXINT_CDRDBUF].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDRDBUF].sCycle = psxRegs.cycle; \
}

#define CDRLID_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDRLID); \
	psxRegs.intCycle[PSXINT_CDRLID].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDRLID].sCycle = psxRegs.cycle; \
}

#define CDRPLAY_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDRPLAY); \
	psxRegs.intCycle[PSXINT_CDRPLAY].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDRPLAY].sCycle = psxRegs.cycle; \
}

#define StartReading(type, eCycle) { \
   	cdr.Reading = type; \
  	cdr.FirstSector = 1; \
  	cdr.Readed = 0xff; \
	AddIrqQueue(READ_ACK, eCycle); \
}

#define StopReading() { \
	if (cdr.Reading) { \
		cdr.Reading = 0; \
		psxRegs.interrupt &= ~(1 << PSXINT_CDREAD); \
	} \
	cdr.StatP &= ~0x20;\
}

#define StopCdda() { \
	if (cdr.Play) { \
		if (!Config.Cdda) CDR_stop(); \
		cdr.StatP &= ~0x80; \
		cdr.Play = FALSE; \
		cdr.FastForward = 0; \
		cdr.FastBackward = 0; \
	} \
}

#define SetResultSize(size) { \
    cdr.ResultP = 0; \
	cdr.ResultC = size; \
	cdr.ResultReady = 1; \
}


void cdrDecodedBufferInterrupt()
{
	u16 buf_ptr[0x400], lcv;

	// ISO reader only
	if( CDR_init != ISOinit ) return;


	// check dbuf IRQ still active
	if( cdr.Play == 0 ) return;
	if( (SPU_readRegister( H_SPUctrl ) & 0x40) == 0 ) return;
	if( (SPU_readRegister( H_SPUirqAddr ) * 8) >= 0x800 ) return;



	// turn off plugin SPU IRQ decoded buffer handling
	SPU_registerCallback( 0 );



	/*
	Vib Ribbon

	000-3FF = left CDDA
	400-7FF = right CDDA

	Assume IRQ every wrap
	*/

	if( iso_play_cdbuf )
	{
		for( lcv = 0; lcv < 0x200; lcv++ )
		{
			// left
			buf_ptr[ lcv ] = iso_play_cdbuf[ iso_play_bufptr ];

			// right
			buf_ptr[ lcv+0x200 ] = iso_play_cdbuf[ iso_play_bufptr+1 ];

			iso_play_bufptr += 2;
		}
	}
	else
	{
		memset( buf_ptr, 0, sizeof(buf_ptr) );
	}


	// feed CDDA decoded buffer manually
	SPU_writeRegister( H_SPUaddr,0 );
	SPU_writeDMAMem( buf_ptr, 0x800 / 2 );


	// signal CDDA data ready
	psxHu32ref(0x1070) |= SWAP32((u32)0x200);


	// time for next full buffer
	//CDRDBUF_INT( PSXCLK / 44100 * 0x200 );
	CDRDBUF_INT( PSXCLK / 44100 * 0x100 );
}


void cdrLidSeekInterrupt()
{
	// turn back on checking
	if( cdr.LidCheck == 0x10 )
	{
		cdr.LidCheck = 0;
	}

	// official lid close
	else if( cdr.LidCheck == 0x30 )
	{
		// GS CDX 3.3: $13
		cdr.StatP |= 0x02;


		// GS CDX 3.3 - ~50 getlocp tries
		CDRLID_INT( cdReadTime * 3 );
		cdr.LidCheck = 0x40;
	}

	// turn off ready
	else if( cdr.LidCheck == 0x40 )
	{
		// GS CDX 3.3: $01
		cdr.StatP &= ~0x10;
		cdr.StatP &= ~0x02;


		// GS CDX 3.3 - ~50 getlocp tries
		CDRLID_INT( cdReadTime * 3 );
		cdr.LidCheck = 0x50;
	}

	// now seek
	else if( cdr.LidCheck == 0x50 )
	{
		// GameShark Lite: Start seeking ($42)
		cdr.StatP |= 0x40;
		cdr.StatP |= 0x02;
		cdr.StatP &= ~0x01;


		CDRLID_INT( cdReadTime * 3 );
		cdr.LidCheck = 0x60;
	}

	// done = cd ready
	else if( cdr.LidCheck == 0x60 )
	{
		// GameShark Lite: Seek detection done ($02)
		cdr.StatP &= ~0x40;

		cdr.LidCheck = 0;
	}
}


void Check_Shell( int Irq )
{
	// check case open/close
	if (cdr.LidCheck > 0)
	{
#ifdef CDR_LOG
		CDR_LOG( "LidCheck\n" );
#endif

		// $20 = check lid state
		if( cdr.LidCheck == 0x20 )
		{
			u32 i;

			i = stat.Status;
			if (CDR_getStatus(&stat) != -1)
			{
				if (stat.Type == 0xff)
					cdr.Stat = DiskError;

				// case now open
				else if (stat.Status & 0x10)
				{
					// Vib Ribbon: pre-CD swap
					StopCdda();


					// GameShark Lite: Death if DiskError happens
					//
					// Vib Ribbon: Needs DiskError for CD swap

					if (Irq != CdlNop)
					{
						cdr.Stat = DiskError;

						cdr.StatP |= 0x01;
						cdr.Result[0] |= 0x01;
					}

					// GameShark Lite: Wants -exactly- $10
					cdr.StatP |= 0x10;
					cdr.StatP &= ~0x02;


					CDRLID_INT( cdReadTime * 3 );
					cdr.LidCheck = 0x10;


					// GS CDX 3.3 = $11
				}

				// case just closed
				else if ( i & 0x10 )
				{
					cdr.StatP |= 0x2;

					CheckCdrom();


					if( cdr.Stat == NoIntr )
						cdr.Stat = Acknowledge;

					psxHu32ref(0x1070) |= SWAP32((u32)0x4);


					// begin close-seek-ready cycle
					CDRLID_INT( cdReadTime * 3 );
					cdr.LidCheck = 0x30;


					// GameShark Lite: Wants -exactly- $42, then $02
					// GS CDX 3.3: Wants $11/$80, $13/$80, $01/$00
				}

				// case still closed - wait for recheck
				else
				{
					CDRLID_INT( cdReadTime * 3 );
					cdr.LidCheck = 0x10;
				}
			}
		}


		// GS CDX: clear all values but #1,#2
		if( (cdr.LidCheck >= 0x30) || (cdr.StatP & 0x10) )
		{
			SetResultSize(16);
			memset( cdr.Result, 0, 16 );

			cdr.Result[0] = cdr.StatP;


			// GS CDX: special return value
			if( cdr.StatP & 0x10 )
			{
				cdr.Result[1] = 0x80;
			}


			if( cdr.Stat == NoIntr )
				cdr.Stat = Acknowledge;

			psxHu32ref(0x1070) |= SWAP32((u32)0x4);
		}
	}
}


void Find_CurTrack() {
	cdr.CurTrack = 0;

	if (CDR_getTN(cdr.ResultTN) != -1) {
		int lcv;

		for( lcv = 1; lcv < cdr.ResultTN[1]; lcv++ ) {
			if (CDR_getTD((u8)(lcv), cdr.ResultTD) != -1) {
				u32 sect1, sect2;

#ifdef CDR_LOG___0
				CDR_LOG( "curtrack %d %d %d | %d %d %d | %d\n",
					cdr.SetSectorPlay[0], cdr.SetSectorPlay[1], cdr.SetSectorPlay[2],
					cdr.ResultTD[2], cdr.ResultTD[1], cdr.ResultTD[0],
					cdr.CurTrack );
#endif

				// find next track boundary - only need m:s accuracy
				sect1 = cdr.SetSectorPlay[0] * 60 * 75 + cdr.SetSectorPlay[1] * 75;
				sect2 = cdr.ResultTD[2] * 60 * 75 + cdr.ResultTD[1] * 75;
				if( sect1 >= sect2 ) {
					cdr.CurTrack++;
					continue;
				}
			}

			break;
		}
	}
}

static void ReadTrack( u8 *time ) {
	cdr.Prev[0] = itob( time[0] );
	cdr.Prev[1] = itob( time[1] );
	cdr.Prev[2] = itob( time[2] );

#ifdef CDR_LOG
	CDR_LOG("ReadTrack() Log: KEY *** %x:%x:%x\n", cdr.Prev[0], cdr.Prev[1], cdr.Prev[2]);
#endif
	cdr.RErr = CDR_readTrack(cdr.Prev);
}

void AddIrqQueue(unsigned char irq, unsigned long ecycle) {
	cdr.Irq = irq;
	cdr.eCycle = ecycle;

	// Doom: Force rescheduling
	// - Fixes boot
	CDR_INT(ecycle);
}


void Set_Track()
{
	if (CDR_getTN(cdr.ResultTN) != -1) {
		int lcv;

		for( lcv = 1; lcv < cdr.ResultTN[1]; lcv++ ) {
			if (CDR_getTD((u8)(lcv), cdr.ResultTD) != -1) {
#ifdef CDR_LOG___0
				CDR_LOG( "settrack %d %d %d | %d %d %d | %d\n",
					cdr.SetSectorPlay[0], cdr.SetSectorPlay[1], cdr.SetSectorPlay[2],
					cdr.ResultTD[2], cdr.ResultTD[1], cdr.ResultTD[0],
					cdr.CurTrack );
#endif

				// check if time matches track start (only need min, sec accuracy)
				// - m:s:f vs f:s:m
				if( cdr.SetSectorPlay[0] == cdr.ResultTD[2] &&
						cdr.SetSectorPlay[1] == cdr.ResultTD[1] ) {
					// skip pregap frames
					if( cdr.SetSectorPlay[2] < cdr.ResultTD[0] )
						cdr.SetSectorPlay[2] = cdr.ResultTD[0];

					break;
				}
				else if( cdr.SetSectorPlay[0] < cdr.ResultTD[2] )
					break;
			}
		}
	}
}


void cdrPlayInterrupt()
{
	if( !cdr.Play ) return;

	if( (cdr.Mode & 0x02) == 0 ) return;

	if( CDR_getStatus(&stat) != -1) {
		subq = (struct SubQ *)CDR_getBufferSub();

		if (subq != NULL ) {
#ifdef CDR_LOG
			CDR_LOG( "CDDA IRQ - %X:%X:%X\n", 
				subq->AbsoluteAddress[0], subq->AbsoluteAddress[1], subq->AbsoluteAddress[2] );
#endif

			/*
			CDDA Autopause

			Silhouette Mirage ($3)
			Tomb Raider 1 ($7)
			*/

			if( cdr.CurTrack < btoi( subq->TrackNumber ) ) {
				StopCdda();
			}
		} else {
			if (CDR_getTN(cdr.ResultTN) != -1) {
				if( cdr.CurTrack+1 <= cdr.ResultTN[1] ) {
					if( CDR_getTD(cdr.CurTrack+1, cdr.ResultTD) != -1 ) {
						u8 temp_cur[3], temp_next[3];

						temp_cur[0] = stat.Time[0];
						temp_cur[1] = stat.Time[1];
						temp_cur[2] = stat.Time[2];

						temp_next[0] = cdr.ResultTD[2];
						temp_next[1] = cdr.ResultTD[1];
						temp_next[2] = cdr.ResultTD[0];

						if( msf2sec(temp_cur) >= msf2sec( temp_next ) ) {
							StopCdda();
							
							cdr.CurTrack++;
						}
					}
				}
			}
		}
	}


	CDRPLAY_INT( cdReadTime );


	Check_Shell(0);
}


void cdrRepplayInterrupt()
{
	if( !cdr.Play ) return;
	
	
	// Wait for IRQ to be acknowledged
	if (cdr.Stat) {
		CDREAD_INT( cdReadTime );
		return;
	}

#ifdef CDR_LOG
	CDR_LOG("cdrRepplayInterrupt() Log: KEY END\n");
#endif

	if ((cdr.Mode & 5) != 5) return;

	memset( cdr.Result, 0, 8 );
	if( CDR_getStatus(&stat) != -1) {
		// BIOS - HACK: Switch between local / absolute times
		static u8 report_time = 1;


		subq = (struct SubQ *)CDR_getBufferSub();

		if (subq != NULL ) {
#ifdef CDR_LOG
			CDR_LOG( "REPPLAY IRQ - %X:%X:%X\n",
				subq->AbsoluteAddress[0], subq->AbsoluteAddress[1], subq->AbsoluteAddress[2] );
#endif


			/*
			skip subQ integrity check (audio playback)
			- mainly useful for DATA LibCrypt checking
			*/
			//if( SWAP16(subq->CRC) != calcCrc((unsigned char *)subq + 12, 10) )

			cdr.Result[0] = cdr.StatP;


			// Rayman: audio pregap flag / track change
			// - not all CDs will use PREGAPs, so we track it manually
			if( cdr.CurTrack < btoi( subq->TrackNumber ) ) {
				cdr.Result[0] |= 0x10;

				cdr.CurTrack = btoi( subq->TrackNumber );
			}


			// BIOS CD Player: data already BCD format
			cdr.Result[1] = subq->TrackNumber;
			cdr.Result[2] = subq->IndexNumber;


			// BIOS CD Player: switch between local / absolute times
			if( report_time == 0 ) {
				cdr.Result[3] = subq->AbsoluteAddress[0];
				cdr.Result[4] = subq->AbsoluteAddress[1];
				cdr.Result[5] = subq->AbsoluteAddress[2];

				report_time = 1;
			}
			else {
				cdr.Result[3] = subq->TrackRelativeAddress[0];
				cdr.Result[4] = subq->TrackRelativeAddress[1];
				cdr.Result[5] = subq->TrackRelativeAddress[2];

				cdr.Result[4] |= 0x80;

				report_time = 0;
			}
		} else {
			// Rayman: check track change
			if (CDR_getTN(cdr.ResultTN) != -1) {
				if( cdr.CurTrack+1 <= cdr.ResultTN[1] ) {
					if( CDR_getTD(cdr.CurTrack+1, cdr.ResultTD) != -1 ) {
						u8 temp_cur[3], temp_next[3];

						temp_cur[0] = stat.Time[0];
						temp_cur[1] = stat.Time[1];
						temp_cur[2] = stat.Time[2];

						temp_next[0] = cdr.ResultTD[2];
						temp_next[1] = cdr.ResultTD[1];
						temp_next[2] = cdr.ResultTD[0];

						if( msf2sec(temp_cur) >= msf2sec( temp_next ) ) {
							cdr.Result[0] |= 0x10;
							
							cdr.CurTrack++;
						}
					}
				}
			}

			
			// track # / index # (assume no pregaps)
			cdr.Result[1] = cdr.CurTrack;
			cdr.Result[2] = 1;

			if( report_time == 0 ) {
				// absolute
				cdr.Result[3] = stat.Time[0];
				cdr.Result[4] = stat.Time[1];
				cdr.Result[5] = itob( stat.Time[2] );

				// m:s adjustment
				if ((s8)cdr.Result[4] < 0) {
					cdr.Result[4] += 60;
					cdr.Result[3] -= 1;
				}

				cdr.Result[4] = itob(cdr.Result[4]);
				cdr.Result[5] = itob(cdr.Result[5]);

				report_time = 1;
			} else {
				// local
				cdr.Result[3] = stat.Time[0];
				cdr.Result[4] = stat.Time[1] - 2;
				cdr.Result[5] = itob( stat.Time[2] );

				// m:s adjustment
				if ((s8)cdr.Result[4] < 0) {
					cdr.Result[4] += 60;
					cdr.Result[3] -= 1;
				}

				cdr.Result[4] = itob(cdr.Result[4]);
				cdr.Result[5] = itob(cdr.Result[5]);

				cdr.Result[4] |= 0x80;

				report_time = 0;
			}

			cdr.Result[6] = 0;
			cdr.Result[7] = 0;
		}
	}

	// Rayman: Logo freeze
	cdr.ResultReady = 1;

	cdr.Stat = DataReady;
	SetResultSize(8);


	// Wild 9: Do not use REPPLAY_ACK
	CDREPPLAY_INT(cdReadTime);

	Check_Shell( 0 );

	psxHu32ref(0x1070) |= SWAP32((u32)0x4);
}

void cdrInterrupt() {
	int i;
	unsigned char Irq = cdr.Irq;

	// Reschedule IRQ
	if (cdr.Stat) {
		CDR_INT( cdr.eCycle );
		return;
	}

	cdr.Irq = 0xff;
	cdr.Ctrl &= ~0x80;

	switch (Irq) {
		case CdlSync:
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case CdlNop:
			SetResultSize(1);
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;

			if (cdr.LidCheck == 0) cdr.LidCheck = 0x20;
			break;

		case CdlSetloc:
			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case CdlPlay:
			/*
			Rayman: detect track changes
			- fixes logo freeze

			Twisted Metal 2: skip PREGAP + starting accurate SubQ
			- plays tracks without retry play

			Wild 9: skip PREGAP + starting accurate SubQ
			- plays tracks without retry play
			*/
			Set_Track();
			Find_CurTrack();
			ReadTrack( cdr.SetSectorPlay );


			// GameShark CD Player: Calls 2x + Play 2x
			if( cdr.FastBackward || cdr.FastForward ) {
				if( cdr.FastForward ) cdr.FastForward--;
				if( cdr.FastBackward ) cdr.FastBackward--;

				if( cdr.FastBackward == 0 && cdr.FastForward == 0 ) {
					if( cdr.Play && CDR_getStatus(&stat) != -1 ) {
						cdr.SetSectorPlay[0] = stat.Time[0];
						cdr.SetSectorPlay[1] = stat.Time[1];
						cdr.SetSectorPlay[2] = stat.Time[2];
					}
				}
			}


			if (!Config.Cdda) {
				// BIOS CD Player
				// - Pause player, hit Track 01/02/../xx (Setloc issued!!)

				// GameShark CD Player: Resume play
				if( cdr.ParamC == 0 ) {
#ifdef CDR_LOG___0
					CDR_LOG( "PLAY Resume @ %d:%d:%d\n",
						cdr.SetSectorPlay[0], cdr.SetSectorPlay[1], cdr.SetSectorPlay[2] );
#endif

					CDR_play( cdr.SetSectorPlay );
				}
				else
				{
					// BIOS CD Player: Resume play
					if( cdr.Param[0] == 0 ) {
#ifdef CDR_LOG___0
						CDR_LOG( "PLAY Resume T0 @ %d:%d:%d\n",
							cdr.SetSectorPlay[0], cdr.SetSectorPlay[1], cdr.SetSectorPlay[2] );
#endif

						CDR_play( cdr.SetSectorPlay );
					}
					else {
#ifdef CDR_LOG___0
						CDR_LOG( "PLAY Resume Td @ %d:%d:%d\n",
							cdr.SetSectorPlay[0], cdr.SetSectorPlay[1], cdr.SetSectorPlay[2] );
#endif

						// BIOS CD Player: Allow track replaying
						StopCdda();


						cdr.CurTrack = btoi( cdr.Param[0] );

						if (CDR_getTN(cdr.ResultTN) != -1) {
							// check last track
							if (cdr.CurTrack > cdr.ResultTN[1])
								cdr.CurTrack = cdr.ResultTN[1];

							if (CDR_getTD((u8)(cdr.CurTrack), cdr.ResultTD) != -1) {
								cdr.SetSectorPlay[0] = cdr.ResultTD[2];
								cdr.SetSectorPlay[1] = cdr.ResultTD[1];
								cdr.SetSectorPlay[2] = cdr.ResultTD[0];

								CDR_play(cdr.SetSectorPlay);
							}
						}
					}
				}
			}


			// Vib Ribbon: gameplay checks flag
			cdr.StatP &= ~0x40;


			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;

			cdr.StatP |= 0x80;
			cdr.Play = TRUE;

			// Lemmings: report play times
			if ((cdr.Mode & 0x5) == 0x5) {
				CDREPPLAY_INT( cdReadTime );
			}

			// autopause cdda
			if ((cdr.Mode & 0x3) == 0x3) {
				CDRPLAY_INT( cdReadTime );
			}
			break;

    	case CdlForward:
			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP |= 0x2;
     	cdr.Result[0] = cdr.StatP;
     	cdr.Stat = Complete;


			// GameShark CD Player: Calls 2x + Play 2x
			if( cdr.FastForward == 0 ) cdr.FastForward = 2;
			else cdr.FastForward++;

			cdr.FastBackward = 0;
			break;

    	case CdlBackward:
			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP |= 0x2;
      cdr.Result[0] = cdr.StatP;
      cdr.Stat = Complete;


			// GameShark CD Player: Calls 2x + Play 2x
			if( cdr.FastBackward == 0 ) cdr.FastBackward = 2;
			else cdr.FastBackward++;

			cdr.FastForward = 0;
			break;

    	case CdlStandby:
			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
			break;

		case CdlStop:
			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP &= ~0x2;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
//			cdr.Stat = Acknowledge;

			if (cdr.LidCheck == 0) cdr.LidCheck = 0x20;
			break;

		case CdlPause:
			SetResultSize(1);
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;

			AddIrqQueue(CdlPause + 0x20, 0x1000);
			cdr.Ctrl |= 0x80;
			break;

		case CdlPause + 0x20:
			SetResultSize(1);
        	cdr.StatP &= ~0x20;
			cdr.StatP |= 0x2;
			cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Complete;
			break;

    	case CdlInit:
			SetResultSize(1);
        	cdr.StatP = 0x2;
			cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
//			if (!cdr.Init) {
				AddIrqQueue(CdlInit + 0x20, 0x1000);
//			}
        	break;

		case CdlInit + 0x20:
			SetResultSize(1);
			cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Complete;
			cdr.Init = 1;
			break;

    	case CdlMute:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
			break;

    	case CdlDemute:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
			break;

    	case CdlSetfilter:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
        	break;

		case CdlSetmode:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
        	break;

    	case CdlGetmode:
			SetResultSize(6);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Result[1] = cdr.Mode;
        	cdr.Result[2] = cdr.File;
        	cdr.Result[3] = cdr.Channel;
        	cdr.Result[4] = 0;
        	cdr.Result[5] = 0;
        	cdr.Stat = Acknowledge;
        	break;

    case CdlGetlocL:
			SetResultSize(8);
     	for (i = 0; i < 8; i++)
				cdr.Result[i] = cdr.Transfer[i];
			cdr.Stat = Acknowledge;
      break;

		case CdlGetlocP:
			// GameShark CDX CD Player: uses 17 bytes output (wraps around)
			SetResultSize(17);
			memset( cdr.Result, 0, 16 );

			subq = (struct SubQ *)CDR_getBufferSub();

			if (subq != NULL) {
				cdr.Result[0] = subq->TrackNumber;
				cdr.Result[1] = subq->IndexNumber;
				memcpy(cdr.Result+2, subq->TrackRelativeAddress, 3);
				memcpy(cdr.Result+5, subq->AbsoluteAddress, 3);


				// subQ integrity check - data only (skip audio)
				if( subq->TrackNumber == 1 && stat.Type == 0x01 ) {
					if (calcCrc((u8 *)subq + 12, 10) != (((u16)subq->CRC[0] << 8) | subq->CRC[1])) {
						memset(cdr.Result + 2, 0, 3 + 3); // CRC wrong, wipe out time data
					}
				}
			} else {
				// check track change
				if (CDR_getTN(cdr.ResultTN) != -1) {
					if( cdr.CurTrack+1 <= cdr.ResultTN[1] ) {
						if( CDR_getTD(cdr.CurTrack+1, cdr.ResultTD) != -1 ) {
							u8 temp_cur[3], temp_next[3];

							temp_cur[0] = btoi( stat.Time[0] );
							temp_cur[1] = btoi( stat.Time[1] );
							temp_cur[2] = btoi( stat.Time[2] );

							temp_next[0] = cdr.ResultTD[2];
							temp_next[1] = cdr.ResultTD[1];
							temp_next[2] = cdr.ResultTD[0];

							if( msf2sec(temp_cur) >= msf2sec( temp_next ) ) {
								cdr.CurTrack++;
							}
						}
					}
				}


				// assume no pregaps
				cdr.Result[0] = cdr.CurTrack;
				cdr.Result[1] = 1;


				if( cdr.Play ) {
					// NOTE: This only works for TRACK 01 (local)
					cdr.Result[2] = stat.Time[0];
					cdr.Result[3] = stat.Time[1]- 2;
					cdr.Result[4] = itob( stat.Time[2] );

					// m:s adjustment
					if ((s8)cdr.Result[3] < 0) {
						cdr.Result[3] += 60;
						cdr.Result[2] -= 1;
					}

					cdr.Result[2] = itob(cdr.Result[2]);
					cdr.Result[3] = itob(cdr.Result[3]);



					// absolute time
					cdr.Result[5] = itob( stat.Time[0] );
					cdr.Result[6] = itob( stat.Time[1] );
					cdr.Result[7] = itob( stat.Time[2] );
				}
				else {
					// NOTE: This only works for TRACK 01 (local)
					cdr.Result[2] = btoi(cdr.Prev[0]);
					cdr.Result[3] = btoi(cdr.Prev[1]) - 2;
					cdr.Result[4] = cdr.Prev[2];

					// m:s adjustment
					if ((s8)cdr.Result[3] < 0) {
						cdr.Result[3] += 60;
						cdr.Result[2] -= 1;
					}

					cdr.Result[2] = itob(cdr.Result[2]);
					cdr.Result[3] = itob(cdr.Result[3]);


					memcpy(cdr.Result + 5, cdr.Prev, 3);
				}
			}

			cdr.Stat = Acknowledge;
			break;

    	case CdlGetTN:
			// 5-Star Racing: don't stop CDDA
			//
			// Vib Ribbon: CD swap
			StopReading();

			cdr.CmdProcess = 0;
			SetResultSize(3);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	if (CDR_getTN(cdr.ResultTN) == -1) {
				cdr.Stat = DiskError;
				cdr.Result[0] |= 0x01;
        	} else {
        		cdr.Stat = Acknowledge;
        	    cdr.Result[1] = itob(cdr.ResultTN[0]);
        	    cdr.Result[2] = itob(cdr.ResultTN[1]);
        	}
        	break;

    	case CdlGetTD:
			cdr.CmdProcess = 0;
			cdr.Track = btoi(cdr.Param[0]);
			SetResultSize(4);
			cdr.StatP |= 0x2;
			if (CDR_getTD(cdr.Track, cdr.ResultTD) == -1) {
				cdr.Stat = DiskError;
				cdr.Result[0] |= 0x01;
			} else {
				cdr.Stat = Acknowledge;
				cdr.Result[0] = cdr.StatP;
				cdr.Result[1] = itob(cdr.ResultTD[2]);
				cdr.Result[2] = itob(cdr.ResultTD[1]);
				cdr.Result[3] = itob(cdr.ResultTD[0]);
			}
			break;

    	case CdlSeekL:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
			cdr.StatP |= 0x40;
        	cdr.Stat = Acknowledge;
			AddIrqQueue(CdlSeekL + 0x20, 0x1000);
			break;

    	case CdlSeekL + 0x20:
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.StatP &= ~0x40;
        	cdr.Result[0] = cdr.StatP;
			cdr.Seeked = TRUE;
        	cdr.Stat = Complete;
			break;

    	case CdlSeekP:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
			cdr.StatP |= 0x40;
        	cdr.Stat = Acknowledge;
			AddIrqQueue(CdlSeekP + 0x20, 0x1000);
			break;

    	case CdlSeekP + 0x20:
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.StatP &= ~0x40;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
			cdr.Seeked = TRUE;

			// Tomb Raider 2: must update read cursor for getlocp
			ReadTrack( cdr.SetSectorPlay );
			break;

		case CdlTest:
        	cdr.Stat = Acknowledge;
        	switch (cdr.Param[0]) {
        	    case 0x20: // System Controller ROM Version
					SetResultSize(4);
					memcpy(cdr.Result, Test20, 4);
					break;
				case 0x22:
					SetResultSize(8);
					memcpy(cdr.Result, Test22, 4);
					break;
				case 0x23: case 0x24:
					SetResultSize(8);
					memcpy(cdr.Result, Test23, 4);
					break;
        	}
			break;

    	case CdlID:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
			AddIrqQueue(CdlID + 0x20, 0x1000);
			break;

		case CdlID + 0x20:
			SetResultSize(8);
			if (CDR_getStatus(&stat) == -1) {
				cdr.Result[0] = 0x00; // 0x08 and cdr.Result[1]|0x10 : audio cd, enters cd player
				cdr.Result[1] = 0x00; // 0x80 leads to the menu in the bios, else loads CD
			}
			else {
				if (stat.Type == 2) {
					// Music CD
					cdr.Result[0] = 0x08;
					cdr.Result[1] = 0x10;

					cdr.Result[1] |= 0x80;
				}
				else {
					// Data CD
					cdr.Result[0] = 0x08;
					cdr.Result[1] = 0x00;
				}
			}

			cdr.Result[2] = 0x00;
			cdr.Result[3] = 0x00;
			strncpy((char *)&cdr.Result[4], "PCSX", 4);
			cdr.Stat = Complete;
			break;

		case CdlReset:
			SetResultSize(1);
        	cdr.StatP = 0x2;
			cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
			break;

		case CdlReadT:
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			AddIrqQueue(CdlReadT + 0x20, 0x1000);
			break;

		case CdlReadT + 0x20:
			SetResultSize(1);
			cdr.StatP |= 0x2;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
			break;

    	case CdlReadToc:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Acknowledge;
			AddIrqQueue(CdlReadToc + 0x20, 0x1000);
			break;

    	case CdlReadToc + 0x20:
			SetResultSize(1);
			cdr.StatP |= 0x2;
        	cdr.Result[0] = cdr.StatP;
        	cdr.Stat = Complete;
			break;

		case AUTOPAUSE:
			cdr.OCUP = 0;
/*			SetResultSize(1);
			StopCdda();
			StopReading();
			cdr.OCUP = 0;
        	cdr.StatP&=~0x20;
			cdr.StatP|= 0x2;
        	cdr.Result[0] = cdr.StatP;
    		cdr.Stat = DataEnd;
*/			AddIrqQueue(CdlPause, 0x800);
			break;

		case READ_ACK:
			if (!cdr.Reading) return;

			
			// Fighting Force 2 - update subq time immediately
			// - fixes new game
			ReadTrack( cdr.SetSector );


			/*
			Duke Nukem: Land of the Babes - seek then delay read for one frame
			- fixes cutscenes

			Judge Dredd - don't delay too long
			- breaks gameplay movies
			*/

			if (!cdr.Seeked) {
				cdr.Seeked = TRUE;

				cdr.StatP |= 0x40;
				cdr.StatP &= ~0x20;
			} else {
				cdr.StatP |= 0x20;
				cdr.StatP &= ~0x40;
			}

			CDREAD_INT((cdr.Mode & 0x80) ? (cdReadTime * 4) : cdReadTime * 8);

			SetResultSize(1);
			cdr.StatP |= 0x02;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case 0xff:
			return;

		default:
			cdr.Stat = Complete;
			break;
	}

	Check_Shell( Irq );

	if (cdr.Stat != NoIntr && cdr.Reg2 != 0x18) {
		psxHu32ref(0x1070) |= SWAP32((u32)0x4);
	}

#ifdef CDR_LOG
	CDR_LOG("cdrInterrupt() Log: CDR Interrupt IRQ %x\n", Irq);
#endif
}

void cdrReadInterrupt() {
	u8 *buf;

	if (!cdr.Reading)
		return;

	if (cdr.Stat) {
		CDREAD_INT(0x1000);
		return;
	}

#ifdef CDR_LOG
	CDR_LOG("cdrReadInterrupt() Log: KEY END");
#endif

    cdr.OCUP = 1;
	SetResultSize(1);
	cdr.StatP |= 0x22;
	cdr.StatP &= ~0x40;
    cdr.Result[0] = cdr.StatP;

	ReadTrack( cdr.SetSector );

	buf = CDR_getBuffer();
	if (buf == NULL)
		cdr.RErr = -1;

	if (cdr.RErr == -1) {
#ifdef CDR_LOG
		fprintf(emuLog, "cdrReadInterrupt() Log: err\n");
#endif
		memset(cdr.Transfer, 0, DATA_SIZE);
		cdr.Stat = DiskError;
		cdr.Result[0] |= 0x01;
		CDREAD_INT((cdr.Mode & 0x80) ? (cdReadTime / 2) : cdReadTime);
		return;
	}

	memcpy(cdr.Transfer, buf, DATA_SIZE);
	CheckPPFCache(cdr.Transfer, cdr.Prev[0], cdr.Prev[1], cdr.Prev[2]);


#ifdef CDR_LOG
	fprintf(emuLog, "cdrReadInterrupt() Log: cdr.Transfer %x:%x:%x\n", cdr.Transfer[0], cdr.Transfer[1], cdr.Transfer[2]);
#endif

	if ((!cdr.Muted) && (cdr.Mode & 0x40) && (!Config.Xa) && (cdr.FirstSector != -1)) { // CD-XA
		// Firemen 2: Multi-XA files - briefings, cutscenes
		if( cdr.FirstSector == 1 && (cdr.Mode & 0x8)==0 ) {
			cdr.File = cdr.Transfer[4 + 0];
		}

		if ((cdr.Transfer[4 + 2] & 0x4) &&
			((cdr.Mode & 0x8) ? (cdr.Transfer[4 + 1] == cdr.Channel) : 1) &&
			(cdr.Transfer[4 + 0] == cdr.File)) {
			int ret = xa_decode_sector(&cdr.Xa, cdr.Transfer+4, cdr.FirstSector);

			if (!ret) {
				SPU_playADPCMchannel(&cdr.Xa);
				cdr.FirstSector = 0;


				// Crash Team Racing: music, speech

				// signal ADPCM data ready
				psxHu32ref(0x1070) |= SWAP32((u32)0x200);
			}
			else cdr.FirstSector = -1;
		}
	}

	cdr.SetSector[2]++;
    if (cdr.SetSector[2] == 75) {
        cdr.SetSector[2] = 0;
        cdr.SetSector[1]++;
        if (cdr.SetSector[1] == 60) {
            cdr.SetSector[1] = 0;
            cdr.SetSector[0]++;
        }
    }

    cdr.Readed = 0;

	// G-Police: Don't autopause ADPCM even if mode set (music)
	if ((cdr.Transfer[4 + 2] & 0x80) && (cdr.Mode & 0x2) &&
			(cdr.Transfer[4 + 2] & 0x4) != 0x4 ) { // EOF
#ifdef CDR_LOG
		CDR_LOG("cdrReadInterrupt() Log: Autopausing read\n");
#endif
//		AddIrqQueue(AUTOPAUSE, 0x2000);
		AddIrqQueue(CdlPause, 0x2000);
	}
	else {
		CDREAD_INT((cdr.Mode & 0x80) ? (cdReadTime / 2) : cdReadTime);
	}

	/*
	Croc 2: $40 - only FORM1 (*)
	Crusaders of Might and Magic: $E0 - FORM1 and FORM2-XA (*) - !!!
	Judge Dredd: $C8 - only FORM1 (*)
	*/

	if( (cdr.Mode & 0x40) == 0 || (cdr.Transfer[4+2] & 0x4) != 0x4 ||
			(cdr.Mode & 0x20) ) {
    cdr.Stat = DataReady;
  } else {
    cdr.Stat = Acknowledge;
  }
  psxHu32ref(0x1070) |= SWAP32((u32)0x4);

	Check_Shell(0);
}

/*
cdrRead0:
	bit 0 - 0 REG1 command send / 1 REG1 data read
	bit 1 - 0 data transfer finish / 1 data transfer ready/in progress
	bit 2 - unknown
	bit 3 - unknown
	bit 4 - unknown
	bit 5 - 1 result ready
	bit 6 - 1 dma ready
	bit 7 - 1 command being processed
*/

unsigned char cdrRead0(void) {
	if (cdr.ResultReady)
		cdr.Ctrl |= 0x20;
	else
		cdr.Ctrl &= ~0x20;

	if (cdr.OCUP)
		cdr.Ctrl |= 0x40;
//  else
//		cdr.Ctrl &= ~0x40;

	// What means the 0x10 and the 0x08 bits? I only saw it used by the bios
	cdr.Ctrl |= 0x18;

#ifdef CDR_LOG
	CDR_LOG("cdrRead0() Log: CD0 Read: %x\n", cdr.Ctrl);
#endif

	return psxHu8(0x1800) = cdr.Ctrl;
}

/*
cdrWrite0:
	0 - to send a command / 1 - to get the result
*/

void cdrWrite0(unsigned char rt) {
#ifdef CDR_LOG
	CDR_LOG("cdrWrite0() Log: CD0 write: %x\n", rt);
#endif
	cdr.Ctrl = rt | (cdr.Ctrl & ~0x3);

	if (rt == 0) {
		cdr.ParamP = 0;
		cdr.ParamC = 0;
		cdr.ResultReady = 0;
	}

	// Tekken: CDXA fade-out
	else if( rt == 2 ) {
		cdr.LeftVol = 0;
	}
	else if( rt == 3 ) {
		cdr.RightVol = 0;
	}
}

unsigned char cdrRead1(void) {
  if (cdr.ResultReady) { // && cdr.Ctrl & 0x1) {
		// GameShark CDX CD Player: uses 17 bytes output (wraps around)
		psxHu8(0x1801) = cdr.Result[cdr.ResultP & 0xf];
		cdr.ResultP++;
		if (cdr.ResultP == cdr.ResultC)
			cdr.ResultReady = 0;
	} else {
		psxHu8(0x1801) = 0;
	}
#ifdef CDR_LOG
	CDR_LOG("cdrRead1() Log: CD1 Read: %x\n", psxHu8(0x1801));
#endif
	return psxHu8(0x1801);
}

void cdrWrite1(unsigned char rt) {
	int i;

#ifdef CDR_LOG
	CDR_LOG("cdrWrite1() Log: CD1 write: %x (%s)\n", rt, CmdName[rt]);
#endif


	// Tekken: CDXA fade-out
	if( (cdr.Ctrl & 3) == 3 ) {
		cdr.RightVol |= (rt << 8);
	}


	//	psxHu8(0x1801) = rt;
  cdr.Cmd = rt;
	cdr.OCUP = 0;

#ifdef CDRCMD_DEBUG
	SysPrintf("cdrWrite1() Log: CD1 write: %x (%s)", rt, CmdName[rt]);
	if (cdr.ParamC) {
		SysPrintf(" Param[%d] = {", cdr.ParamC);
		for (i = 0; i < cdr.ParamC; i++)
			SysPrintf(" %x,", cdr.Param[i]);
		SysPrintf("}\n");
	} else {
		SysPrintf("\n");
	}
#endif

	if (cdr.Ctrl & 0x1) return;

    switch (cdr.Cmd) {
    	case CdlSync:
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlNop:
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlSetloc:
				StopReading();
				cdr.Seeked = FALSE;
        for (i = 0; i < 3; i++)
					cdr.SetSector[i] = btoi(cdr.Param[i]);
        cdr.SetSector[3] = 0;

#ifdef DVD5_HACK
				// PS1 DVD5 hack (shalma's disc combining kits)
				dvd5_mode = cdr.Param[2] & 0x80;

				if( CDR__setDVD5 ) {
					if( cdr.Param[2] & 0x80 )
						CDR__setDVD5(1);
					else if( cdr.Param[1] & 0x80 )
						CDR__setDVD5(2);
					else
						CDR__setDVD5(0);
				}

				cdr.Param[1] &= 0x7f;
				cdr.Param[2] &= 0x7f;
#endif

				// GameShark Music Player
				memcpy( cdr.SetSectorPlay, cdr.SetSector, 4 );


				/*
				if ((cdr.SetSector[0] | cdr.SetSector[1] | cdr.SetSector[2]) == 0) {
					*(u32 *)cdr.SetSector = *(u32 *)cdr.SetSectorSeek;
				}*/

				cdr.Ctrl |= 0x80;
        cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        break;

			case CdlPlay:
				// Vib Ribbon: try same track again
				StopCdda();

				// Vib Ribbon - decoded buffer IRQ for CDDA reading
				// - fixes ribbon timing + music CD mode
				CDRDBUF_INT( PSXCLK / 44100 * 0x100 );


				cdr.Play = TRUE;

				cdr.StatP |= 0x40;
				cdr.StatP &= ~0x02;

				cdr.Ctrl |= 0x80;
				cdr.Stat = NoIntr;
				AddIrqQueue(cdr.Cmd, 0x1000);
				break;

    	case CdlForward:
        //if (cdr.CurTrack < 0xaa)
				//	cdr.CurTrack++;
				cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        break;

    	case CdlBackward:
        //if (cdr.CurTrack > 1)
					//cdr.CurTrack--;
				cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        break;

    	case CdlReadN:
			cdr.Irq = 0;
			StopReading();
			cdr.Ctrl|= 0x80;
        	cdr.Stat = NoIntr;
			StartReading(1, 0x1000);
        	break;

    	case CdlStandby:
			StopCdda();
			StopReading();
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlStop:
				// GameShark CD Player: Reset CDDA to track start
				if( cdr.Play && CDR_getStatus(&stat) != -1 ) {
					cdr.SetSectorPlay[0] = stat.Time[0];
					cdr.SetSectorPlay[1] = stat.Time[1];
					cdr.SetSectorPlay[2] = stat.Time[2];

					Find_CurTrack();


					// grab time for current track
					CDR_getTD((u8)(cdr.CurTrack), cdr.ResultTD);

					cdr.SetSectorPlay[0] = cdr.ResultTD[2];
					cdr.SetSectorPlay[1] = cdr.ResultTD[1];
					cdr.SetSectorPlay[2] = cdr.ResultTD[0];
				}

				StopCdda();
				StopReading();

				cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
       	break;

    	case CdlPause:
				/*
				GameShark CD Player: save time for resume

				Twisted Metal - World Tour: don't save times for DATA reads
				- Only get 1 chance to do this right
				*/
				if( cdr.Play && CDR_getStatus(&stat) != -1 ) {
					cdr.SetSectorPlay[0] = stat.Time[0];
					cdr.SetSectorPlay[1] = stat.Time[1];
					cdr.SetSectorPlay[2] = stat.Time[2];
				}

				StopCdda();
				StopReading();
				cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;

				/*
				Gundam Battle Assault 2: much slower (*)
				- Fixes boot, gameplay

				Hokuto no Ken 2: slower
				- Fixes intro + subtitles

				InuYasha - Feudal Fairy Tale: slower
				- Fixes battles
				*/
				AddIrqQueue(cdr.Cmd, cdReadTime * 3);
        break;

		case CdlReset:
    	case CdlInit:
			StopCdda();
			StopReading();
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlMute:
        	cdr.Muted = TRUE;
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);

			// Duke Nukem - Time to Kill
			// - do not directly set cd-xa volume
			//SPU_writeRegister( H_CDLeft, 0x0000 );
			//SPU_writeRegister( H_CDRight, 0x0000 );
        	break;

    	case CdlDemute:
        	cdr.Muted = FALSE;
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);

			// Duke Nukem - Time to Kill
			// - do not directly set cd-xa volume
			//SPU_writeRegister( H_CDLeft, 0x7f00 );
			//SPU_writeRegister( H_CDRight, 0x7f00 );
        	break;

    	case CdlSetfilter:
        	cdr.File = cdr.Param[0];
        	cdr.Channel = cdr.Param[1];
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlSetmode:
#ifdef CDR_LOG
			CDR_LOG("cdrWrite1() Log: Setmode %x\n", cdr.Param[0]);
#endif
        	cdr.Mode = cdr.Param[0];
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);

			// Squaresoft on PlayStation 1998 Collector's CD Vol. 1
			// - fixes choppy movie sound
			if( cdr.Play && (cdr.Mode & 1) == 0 )
				StopCdda();
        	break;

    	case CdlGetmode:
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlGetlocL:
				cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;

				// G-Police: in-game music needs longer time
				AddIrqQueue(cdr.Cmd, 0x4000);
     		break;

    	case CdlGetlocP:
				cdr.Ctrl |= 0x80;
	  		cdr.Stat = NoIntr;
				AddIrqQueue(cdr.Cmd, 0x1000);

				// GameShark CDX / Lite Player: pretty narrow time window
				// - doesn't always work due to time inprecision
				//AddIrqQueue(cdr.Cmd, 0x28);
       	break;

    	case CdlGetTN:
				cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		//AddIrqQueue(cdr.Cmd, 0x1000);

				// GameShark CDX CD Player: very long time
				AddIrqQueue(cdr.Cmd, 0x100000);
        break;

    	case CdlGetTD:
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlSeekL:
//			((u32 *)cdr.SetSectorSeek)[0] = ((u32 *)cdr.SetSector)[0];
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlSeekP:
//        	((u32 *)cdr.SetSectorSeek)[0] = ((u32 *)cdr.SetSector)[0];
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

		// Destruction Derby: read TOC? GetTD after this
		case CdlReadT:
			cdr.Ctrl |= 0x80;
			cdr.Stat = NoIntr;
			AddIrqQueue(cdr.Cmd, 0x1000);
			break;

    	case CdlTest:
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlID:
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	case CdlReadS:
			cdr.Irq = 0;
			StopReading();
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
			StartReading(2, 0x1000);
        	break;

    	case CdlReadToc:
			cdr.Ctrl |= 0x80;
    		cdr.Stat = NoIntr;
    		AddIrqQueue(cdr.Cmd, 0x1000);
        	break;

    	default:
#ifdef CDR_LOG
			CDR_LOG("cdrWrite1() Log: Unknown command: %x\n", cdr.Cmd);
#endif
			return;
    }
	if (cdr.Stat != NoIntr) {
		psxHu32ref(0x1070) |= SWAP32((u32)0x4);
	}
}

unsigned char cdrRead2(void) {
	unsigned char ret;

	if (cdr.Readed == 0) {
		ret = 0;
	} else {
		ret = *cdr.pTransfer++;
	}

#ifdef CDR_LOG
	CDR_LOG("cdrRead2() Log: CD2 Read: %x\n", ret);
#endif
	return ret;
}

void cdrWrite2(unsigned char rt) {
#ifdef CDR_LOG
	CDR_LOG("cdrWrite2() Log: CD2 write: %x\n", rt);
#endif


	// Tekken: CDXA fade-out
	if( (cdr.Ctrl & 3) == 2 ) {
		cdr.LeftVol |= (rt << 8);
	}
	else if( (cdr.Ctrl & 3) == 3 ) {
		cdr.RightVol |= (rt << 0);
	}



    if (cdr.Ctrl & 0x1) {
		switch (rt) {
			case 0x07:
	    		cdr.ParamP = 0;
				cdr.ParamC = 0;
				cdr.ResultReady = 1; //0;
				cdr.Ctrl &= ~3; //cdr.Ctrl = 0;
				break;

			default:
				cdr.Reg2 = rt;
				break;
		}
    } else if (!(cdr.Ctrl & 0x1) && cdr.ParamP < 8) {
		cdr.Param[cdr.ParamP++] = rt;
		cdr.ParamC++;
	}
}

unsigned char cdrRead3(void) {
	if (cdr.Stat) {
		if (cdr.Ctrl & 0x1)
			psxHu8(0x1803) = cdr.Stat | 0xE0;
		else
			psxHu8(0x1803) = 0xff;
	} else {
		psxHu8(0x1803) = 0;
	}
#ifdef CDR_LOG
	CDR_LOG("cdrRead3() Log: CD3 Read: %x\n", psxHu8(0x1803));
#endif
	return psxHu8(0x1803);
}

void cdrWrite3(unsigned char rt) {
#ifdef CDR_LOG
	CDR_LOG("cdrWrite3() Log: CD3 write: %x\n", rt);
#endif

	// Tekken: CDXA fade-out
	if( (cdr.Ctrl & 3) == 2 ) {
		cdr.LeftVol |= (rt << 0);
	}
	else if( (cdr.Ctrl & 3) == 3 && rt == 0x20 ) {
#ifdef CDR_LOG
		CDR_LOG( "CD-XA Volume: %X %X\n", cdr.LeftVol, cdr.RightVol );
#endif

		/*
		Eternal SPU: scale volume from [0-ffff] -> [0,8000]
		- Destruction Derby Raw movies (ff00)
		*/

		// write CD-XA volumes
		SPU_writeRegister( H_CDLeft, cdr.LeftVol / 2 );
		SPU_writeRegister( H_CDRight, cdr.RightVol / 2 );
	}


	// GameShark CDX CD Player: Irq timing mania
	if( rt == 0 &&
			cdr.Irq != 0 && cdr.Irq != 0xff &&
			cdr.ResultReady == 0 ) {

		// GS CDX: ~0x28 cycle timing - way too precise
		if( cdr.Irq == CdlGetlocP ) {
			cdrInterrupt();

			psxRegs.interrupt &= ~(1 << PSXINT_CDR);
		}
	}


  if (rt == 0x07 && cdr.Ctrl & 0x1) {
		cdr.Stat = 0;

		if (cdr.Irq == 0xff) {
			cdr.Irq = 0;
			return;
		}

		if (cdr.Reading && !cdr.ResultReady) {
      CDREAD_INT((cdr.Mode & 0x80) ? (cdReadTime / 2) : cdReadTime);
		}

		return;
	}

	if (rt == 0x80 && !(cdr.Ctrl & 0x1) && cdr.Readed == 0) {
		cdr.Readed = 1;
		cdr.pTransfer = cdr.Transfer;

		switch (cdr.Mode & 0x30) {
			case 0x10:
			case 0x00:
				cdr.pTransfer += 12;
				break;

			case 0x20:
				cdr.pTransfer += 0;
				break;

			default:
				break;
		}
	}
}

void psxDma3(u32 madr, u32 bcr, u32 chcr) {
	u32 cdsize;
	u8 *ptr;

#ifdef CDR_LOG
	CDR_LOG("psxDma3() Log: *** DMA 3 *** %x addr = %x size = %x\n", chcr, madr, bcr);
#endif

	switch (chcr) {
		case 0x11000000:
		case 0x11400100:
			if (cdr.Readed == 0) {
#ifdef CDR_LOG
				CDR_LOG("psxDma3() Log: *** DMA 3 *** NOT READY\n");
#endif
				break;
			}

			cdsize = (bcr & 0xffff) * 4;

			// Ape Escape: bcr = 0001 / 0000
			// - fix boot
			if( cdsize == 0 )
			{
				switch (cdr.Mode & 0x30) {
					case 0x00: cdsize = 2048; break;
					case 0x10: cdsize = 2328; break;
					case 0x20: cdsize = 2340; break;
				}
			}


			ptr = (u8 *)PSXM(madr);
			if (ptr == NULL) {
#ifdef CPU_LOG
				CDR_LOG("psxDma3() Log: *** DMA 3 *** NULL Pointer!\n");
#endif
				break;
			}

			/*
			GS CDX: Enhancement CD crash
			- Setloc 0:0:0
			- CdlPlay
			- Spams DMA3 and gets buffer overrun
			*/

			if( (cdr.pTransfer-cdr.Transfer) + cdsize > 2352 )
			{
				// avoid crash - probably should wrap here
				//memcpy(ptr, cdr.pTransfer, cdsize);
			}
			else
			{
				memcpy(ptr, cdr.pTransfer, cdsize);
			}

			psxCpu->Clear(madr, cdsize / 4);
			cdr.pTransfer += cdsize;


			// burst vs normal
			if( chcr == 0x11400100 ) {
				CDRDMA_INT( (cdsize/4) / 4 );
			}
			else if( chcr == 0x11000000 ) {
				CDRDMA_INT( (cdsize/4) * 1 );
			}
			return;

		default:
#ifdef CDR_LOG
			CDR_LOG("psxDma3() Log: Unknown cddma %x\n", chcr);
#endif
			break;
	}

	HW_DMA3_CHCR &= SWAP32(~0x01000000);
	DMA_INTERRUPT(3);
}

void cdrDmaInterrupt()
{
	HW_DMA3_CHCR &= SWAP32(~0x01000000);
	DMA_INTERRUPT(3);
}

void cdrReset() {
	memset(&cdr, 0, sizeof(cdr));
	cdr.CurTrack = 1;
	cdr.File = 1;
	cdr.Channel = 1;
}

int cdrFreeze(gzFile f, int Mode) {
	uintptr_t tmp;


	if( Mode == 0 ) {
		StopCdda();
	}
	if( Mode == 1 ) {
		// get restart time
		if( cdr.Play && CDR_getStatus(&stat) != -1 ) {
			cdr.SetSectorPlay[0] = stat.Time[0];
			cdr.SetSectorPlay[1] = stat.Time[1];
			cdr.SetSectorPlay[2] = stat.Time[2];
		}
	}
	
	
	gzfreeze(&cdr, sizeof(cdr));


	if( Mode == 0 ) {
		// resume cdda
		if( cdr.Play ) {
			Set_Track();
			Find_CurTrack();
			ReadTrack( cdr.SetSectorPlay );

			CDR_play( cdr.SetSectorPlay );
		}
	}


	
	if (Mode == 1)
		tmp = cdr.pTransfer - cdr.Transfer;

	gzfreeze(&tmp, sizeof(tmp));

	if (Mode == 0)
		cdr.pTransfer = cdr.Transfer + tmp;

	return 0;
}

void LidInterrupt() {
	cdr.LidCheck = 0x20; // start checker

	CDRLID_INT( cdReadTime * 3 );
	
	// generate interrupt if none active - open or close
	if (cdr.Irq == 0 || cdr.Irq == 0xff) {
		cdr.Ctrl |= 0x80;
		cdr.Stat = NoIntr;
		AddIrqQueue(CdlNop, 0x800);
	}
}
