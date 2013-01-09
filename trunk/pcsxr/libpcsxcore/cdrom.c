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
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.           *
 ***************************************************************************/

/*
* Handles all CD-ROM registers and functions.
*/

#include "cdrom.h"
#include "ppf.h"
#include "psxdma.h"

/* logging */
#if 0
#define CDR_LOG SysPrintf
#else
#define CDR_LOG(...)
#endif
#if 0
#define CDR_LOG_I SysPrintf
#else
#define CDR_LOG_I(...)
#endif
#if 0
#define CDR_LOG_IO SysPrintf
#else
#define CDR_LOG_IO(...)
#endif
//#define CDR_LOG_CMD_IRQ

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

/* Modes flags */
#define MODE_SPEED       (1<<7) // 0x80
#define MODE_STRSND      (1<<6) // 0x40 ADPCM on/off
#define MODE_SIZE_2340   (1<<5) // 0x20
#define MODE_SIZE_2328   (1<<4) // 0x10
#define MODE_SIZE_2048   (0<<4) // 0x00
#define MODE_SF          (1<<3) // 0x08 channel on/off
#define MODE_REPORT      (1<<2) // 0x04
#define MODE_AUTOPAUSE   (1<<1) // 0x02
#define MODE_CDDA        (1<<0) // 0x01

/* Status flags */
#define STATUS_PLAY      (1<<7) // 0x80
#define STATUS_SEEK      (1<<6) // 0x40
#define STATUS_READ      (1<<5) // 0x20
#define STATUS_SHELLOPEN (1<<4) // 0x10
#define STATUS_UNKNOWN3  (1<<3) // 0x08
#define STATUS_UNKNOWN2  (1<<2) // 0x04
#define STATUS_ROTATING  (1<<1) // 0x02
#define STATUS_ERROR     (1<<0) // 0x01


// 1x = 75 sectors per second
// PSXCLK = 1 sec in the ps
// so (PSXCLK / 75) = cdr read time (linuzappz)
#define cdReadTime (PSXCLK / 75)

// for cdr.Seeked
enum seeked_state {
	SEEK_PENDING = 0,
	SEEK_DONE = 1,
};

static struct CdrStat stat;

extern unsigned int msf2sec(const char *msf);
extern void sec2msf(unsigned int s, const char *msf);

// for that weird psemu API..
static unsigned int fsm2sec(const u8 *msf) {
	return ((msf[2] * 60 + msf[1]) * 75) + msf[0];
}


extern long CALLBACK ISOinit(void);
extern void CALLBACK SPUirq(void);
extern SPUregisterCallback SPU_registerCallback;

// A bit of a kludge, but it will get rid of the "macro redefined" warnings

#ifdef H_SPUirqAddr
#undef H_SPUirqAddr
#endif

#ifdef H_SPUaddr
#undef H_SPUaddr
#endif

#ifdef H_SPUctrl
#undef H_SPUctrl
#endif

#define H_SPUirqAddr		0x1f801da4
#define H_SPUaddr				0x1f801da6
#define H_SPUctrl				0x1f801daa
#define H_CDLeft				0x1f801db0
#define H_CDRight				0x1f801db2


// cdrInterrupt
#define CDR_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDR); \
	psxRegs.intCycle[PSXINT_CDR].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDR].sCycle = psxRegs.cycle; \
}

// cdrReadInterrupt
#define CDREAD_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDREAD); \
	psxRegs.intCycle[PSXINT_CDREAD].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDREAD].sCycle = psxRegs.cycle; \
}

// cdrDecodedBufferInterrupt
#define CDRDBUF_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDRDBUF); \
	psxRegs.intCycle[PSXINT_CDRDBUF].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDRDBUF].sCycle = psxRegs.cycle; \
}

// cdrLidSeekInterrupt
#define CDRLID_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDRLID); \
	psxRegs.intCycle[PSXINT_CDRLID].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDRLID].sCycle = psxRegs.cycle; \
}

// cdrPlayInterrupt
#define CDRMISC_INT(eCycle) { \
	psxRegs.interrupt |= (1 << PSXINT_CDRPLAY); \
	psxRegs.intCycle[PSXINT_CDRPLAY].cycle = eCycle; \
	psxRegs.intCycle[PSXINT_CDRPLAY].sCycle = psxRegs.cycle; \
}

#define StopReading() { \
	if (cdr.Reading) { \
		cdr.Reading = 0; \
		psxRegs.interrupt &= ~(1 << PSXINT_CDREAD); \
	} \
	cdr.StatP &= ~STATUS_READ;\
}

#define StopCdda() { \
	if (cdr.Play) { \
		if (!Config.Cdda) CDR_stop(); \
		cdr.StatP &= ~STATUS_PLAY; \
		cdr.Play = FALSE; \
		cdr.FastForward = 0; \
		cdr.FastBackward = 0; \
		SPU_registerCallback( SPUirq ); \
	} \
}

#define SetResultSize(size) { \
	cdr.ResultP = 0; \
	cdr.ResultC = size; \
	cdr.ResultReady = 1; \
}

static void setIrq(void)
{
	if (cdr.Stat & cdr.Reg2)
		psxHu32ref(0x1070) |= SWAP32((u32)0x4);
}

static void adjustTransferIndex(void)
{
	unsigned int bufSize = 0;
	
	switch (cdr.Mode & (MODE_SIZE_2340|MODE_SIZE_2328)) {
		case MODE_SIZE_2340: bufSize = 2340; break;
		case MODE_SIZE_2328: bufSize = 12 + 2328; break;
		default:
		case MODE_SIZE_2048: bufSize = 12 + 2048; break;
	}
	
	if (cdr.transferIndex >= bufSize)
		cdr.transferIndex -= bufSize;
}

// FIXME: do this in SPU instead
void cdrDecodedBufferInterrupt()
{
	u16 buf_ptr[0x400], lcv;

#if 0
	return;
#endif


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
		cdr.StatP |= STATUS_ROTATING;


		// GS CDX 3.3 - ~50 getlocp tries
		CDRLID_INT( cdReadTime * 3 );
		cdr.LidCheck = 0x40;
	}

	// turn off ready
	else if( cdr.LidCheck == 0x40 )
	{
		// GS CDX 3.3: $01
		cdr.StatP &= ~STATUS_SHELLOPEN;
		cdr.StatP &= ~STATUS_ROTATING;


		// GS CDX 3.3 - ~50 getlocp tries
		CDRLID_INT( cdReadTime * 3 );
		cdr.LidCheck = 0x50;
	}

	// now seek
	else if( cdr.LidCheck == 0x50 )
	{
		// GameShark Lite: Start seeking ($42)
		cdr.StatP |= STATUS_SEEK;
		cdr.StatP |= STATUS_ROTATING;
		cdr.StatP &= ~STATUS_ERROR;


		CDRLID_INT( cdReadTime * 3 );
		cdr.LidCheck = 0x60;
	}

	// done = cd ready
	else if( cdr.LidCheck == 0x60 )
	{
		// GameShark Lite: Seek detection done ($02)
		cdr.StatP &= ~STATUS_SEEK;

		cdr.LidCheck = 0;
	}
}

static void Check_Shell( int Irq )
{
	// check case open/close
	if (cdr.LidCheck > 0)
	{
		CDR_LOG( "LidCheck\n" );

		// $20 = check lid state
		if( cdr.LidCheck == 0x20 )
		{
			u32 i;

			i = stat.Status;
			if (CDR_getStatus(&stat) != -1)
			{
				// BIOS hangs + BIOS error messages
				//if (stat.Type == 0xff)
					//cdr.Stat = DiskError;

				// case now open
				if (stat.Status & STATUS_SHELLOPEN)
				{
					// Vib Ribbon: pre-CD swap
					StopCdda();


					// GameShark Lite: Death if DiskError happens
					//
					// Vib Ribbon: Needs DiskError for CD swap

					if (Irq != CdlNop)
					{
						cdr.Stat = DiskError;

						cdr.StatP |= STATUS_ERROR;
						cdr.Result[0] |= STATUS_ERROR;
					}

					// GameShark Lite: Wants -exactly- $10
					cdr.StatP |= STATUS_SHELLOPEN;
					cdr.StatP &= ~STATUS_ROTATING;


					CDRLID_INT( cdReadTime * 3 );
					cdr.LidCheck = 0x10;


					// GS CDX 3.3 = $11
				}

				// case just closed
				else if ( i & STATUS_SHELLOPEN )
				{
					cdr.StatP |= STATUS_ROTATING;

					CheckCdrom();


					if( cdr.Stat == NoIntr )
						cdr.Stat = Acknowledge;

					setIrq();

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
		if( (cdr.LidCheck >= 0x30) || (cdr.StatP & STATUS_SHELLOPEN) )
		{
			SetResultSize(16);
			memset( cdr.Result, 0, 16 );

			cdr.Result[0] = cdr.StatP;


			// GS CDX: special return value
			if( cdr.StatP & STATUS_SHELLOPEN )
			{
				cdr.Result[1] = 0x80;
			}


			if( cdr.Stat == NoIntr )
				cdr.Stat = Acknowledge;

			setIrq();
		}
	}
}

static void Find_CurTrack(const u8 *time)
{
	int current, sect;

	current = msf2sec(time);

	for (cdr.CurTrack = 1; cdr.CurTrack < cdr.ResultTN[1]; cdr.CurTrack++) {
		CDR_getTD(cdr.CurTrack + 1, cdr.ResultTD);
		sect = fsm2sec(cdr.ResultTD);
		if (sect - current >= 150)
			break;
	}
}

static void generate_subq(const u8 *time)
{
	unsigned char start[3], next[3];
	unsigned int this_s, start_s, next_s, pregap;
	int relative_s;

	CDR_getTD(cdr.CurTrack, start);
	if (cdr.CurTrack + 1 <= cdr.ResultTN[1]) {
		pregap = 150;
		CDR_getTD(cdr.CurTrack + 1, next);
	}
	else {
		// last track - cd size
		pregap = 0;
		next[0] = cdr.SetSectorEnd[2];
		next[1] = cdr.SetSectorEnd[1];
		next[2] = cdr.SetSectorEnd[0];
	}

	this_s = msf2sec(time);
	start_s = fsm2sec(start);
	next_s = fsm2sec(next);

	cdr.TrackChanged = FALSE;

	if (next_s - this_s < pregap) {
		cdr.TrackChanged = TRUE;
		cdr.CurTrack++;
		start_s = next_s;
	}

	cdr.subq.Index = 1;

	relative_s = this_s - start_s;
	if (relative_s < 0) {
		cdr.subq.Index = 0;
		relative_s = -relative_s;
	}
	sec2msf(relative_s, cdr.subq.Relative);

	cdr.subq.Track = itob(cdr.CurTrack);
	cdr.subq.Relative[0] = itob(cdr.subq.Relative[0]);
	cdr.subq.Relative[1] = itob(cdr.subq.Relative[1]);
	cdr.subq.Relative[2] = itob(cdr.subq.Relative[2]);
	cdr.subq.Absolute[0] = itob(time[0]);
	cdr.subq.Absolute[1] = itob(time[1]);
	cdr.subq.Absolute[2] = itob(time[2]);
}

static void ReadTrack(const u8 *time) {
	unsigned char tmp[3];
	struct SubQ *subq;
	u16 crc;

	tmp[0] = itob(time[0]);
	tmp[1] = itob(time[1]);
	tmp[2] = itob(time[2]);

	if (memcmp(cdr.Prev, tmp, 3) == 0)
		return;

	CDR_LOG("ReadTrack *** %02x:%02x:%02x\n", tmp[0], tmp[1], tmp[2]);

	cdr.RErr = CDR_readTrack(tmp);
	memcpy(cdr.Prev, tmp, 3);

	if (CheckSBI(time))
		return;

	subq = (struct SubQ *)CDR_getBufferSub();
	if (subq != NULL && cdr.CurTrack == 1) {
		crc = calcCrc((u8 *)subq + 12, 10);
		if (crc == (((u16)subq->CRC[0] << 8) | subq->CRC[1])) {
			cdr.subq.Track = subq->TrackNumber;
			cdr.subq.Index = subq->IndexNumber;
			memcpy(cdr.subq.Relative, subq->TrackRelativeAddress, 3);
			memcpy(cdr.subq.Absolute, subq->AbsoluteAddress, 3);
		}
		else {
			CDR_LOG_I("subq bad crc @%02x:%02x:%02x\n",
				tmp[0], tmp[1], tmp[2]);
		}
	}
	else {
		generate_subq(time);
	}

	CDR_LOG(" -> %02x,%02x %02x:%02x:%02x %02x:%02x:%02x\n",
		cdr.subq.Track, cdr.subq.Index,
		cdr.subq.Relative[0], cdr.subq.Relative[1], cdr.subq.Relative[2],
		cdr.subq.Absolute[0], cdr.subq.Absolute[1], cdr.subq.Absolute[2]);
}

static void AddIrqQueue(unsigned char irq, unsigned long ecycle) {
	if (cdr.Irq != 0)
		CDR_LOG_I("cdr: override cmd %02x -> %02x\n", cdr.Irq, irq);

	cdr.Irq = irq;
	cdr.eCycle = ecycle;

	CDR_INT(ecycle);
}

static void cdrPlayInterrupt_Autopause()
{
	if ((cdr.Mode & MODE_AUTOPAUSE) && cdr.TrackChanged) {
		CDR_LOG( "CDDA STOP\n" );

		// Magic the Gathering
		// - looping territory cdda

		// ...?
		//cdr.ResultReady = 1;
		//cdr.Stat = DataReady;
		cdr.Stat = DataEnd;
		setIrq();

		StopCdda();
	}
	else if (cdr.Mode & MODE_REPORT) {

		cdr.Result[0] = cdr.StatP;
		cdr.Result[1] = cdr.subq.Track;
		cdr.Result[2] = cdr.subq.Index;

		if (cdr.subq.Absolute[2] & 0x10) {
			cdr.Result[3] = cdr.subq.Relative[0];
			cdr.Result[4] = cdr.subq.Relative[1] | 0x80;
			cdr.Result[5] = cdr.subq.Relative[2];
		}
		else {
			cdr.Result[3] = cdr.subq.Absolute[0];
			cdr.Result[4] = cdr.subq.Absolute[1];
			cdr.Result[5] = cdr.subq.Absolute[2];
		}

		cdr.Result[6] = 0;
		cdr.Result[7] = 0;

		// Rayman: Logo freeze (resultready + dataready)
		cdr.ResultReady = 1;
		cdr.Stat = DataReady;

		SetResultSize(8);
		setIrq();
	}
}

// also handles seek
void cdrPlayInterrupt()
{
	if (cdr.Seeked == SEEK_PENDING) {
		if (cdr.Stat) {
			CDRMISC_INT( 0x100 );
			return;
		}
		SetResultSize(1);
		cdr.StatP |= STATUS_ROTATING;
		cdr.StatP &= ~STATUS_SEEK;
		cdr.Result[0] = cdr.StatP;
		cdr.Seeked = SEEK_DONE;
		if (cdr.Irq == 0) {
			cdr.Stat = Complete;
			setIrq();
		}

		memcpy(cdr.SetSectorPlay, cdr.SetSector, 4);
		Find_CurTrack(cdr.SetSectorPlay);
		ReadTrack(cdr.SetSectorPlay);
		cdr.TrackChanged = FALSE;
	}

	if (!cdr.Play) return;

	CDR_LOG( "CDDA - %d:%d:%d\n",
		cdr.SetSectorPlay[0], cdr.SetSectorPlay[1], cdr.SetSectorPlay[2] );

	if (memcmp(cdr.SetSectorPlay, cdr.SetSectorEnd, 3) == 0) {
		StopCdda();
		cdr.TrackChanged = TRUE;
	}

	if (!cdr.Irq && !cdr.Stat && (cdr.Mode & (MODE_AUTOPAUSE|MODE_REPORT)))
		cdrPlayInterrupt_Autopause();

	if (!cdr.Play) return;

	if (CDR_readCDDA && !cdr.Muted && cdr.CurTrack > 1) {
		CDR_readCDDA(cdr.SetSectorPlay[0], cdr.SetSectorPlay[1],
			cdr.SetSectorPlay[2], cdr.Transfer);

		cdrAttenuate((s16 *)cdr.Transfer, CD_FRAMESIZE_RAW / 4, 1);
		if (SPU_playCDDAchannel)
			SPU_playCDDAchannel((short *)cdr.Transfer, CD_FRAMESIZE_RAW);
	}

	cdr.SetSectorPlay[2]++;
	if (cdr.SetSectorPlay[2] == 75) {
		cdr.SetSectorPlay[2] = 0;
		cdr.SetSectorPlay[1]++;
		if (cdr.SetSectorPlay[1] == 60) {
			cdr.SetSectorPlay[1] = 0;
			cdr.SetSectorPlay[0]++;
		}
	}

	CDRMISC_INT(cdReadTime);

	// update for CdlGetlocP/autopause
	generate_subq(cdr.SetSectorPlay);
}

void cdrInterrupt() {
	int i;
	unsigned char Irq = cdr.Irq;

	// Reschedule IRQ
	if (cdr.Stat) {
		CDR_INT( 0x100 );
		return;
	}

	cdr.Irq = 0;
	cdr.Ctrl &= ~0x80;

	switch (Irq) {
		case CdlSync:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
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
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case CdlPlay:
			if (cdr.Seeked == SEEK_PENDING) {
				// XXX: wrong, should seek instead..
				memcpy( cdr.SetSectorPlay, cdr.SetSector, 4 );
				cdr.Seeked = SEEK_DONE;
			}

			// BIOS CD Player
			// - Pause player, hit Track 01/02/../xx (Setloc issued!!)

			if (cdr.ParamC == 0 || cdr.Param[0] == 0) {
				CDR_LOG("PLAY Resume @ %d:%d:%d\n",
					cdr.SetSectorPlay[0], cdr.SetSectorPlay[1], cdr.SetSectorPlay[2]);
			}
			else
			{
				int track = btoi( cdr.Param[0] );

				if (track <= cdr.ResultTN[1])
					cdr.CurTrack = track;

				CDR_LOG("PLAY track %d\n", cdr.CurTrack);

				if (CDR_getTD((u8)cdr.CurTrack, cdr.ResultTD) != -1) {
					cdr.SetSectorPlay[0] = cdr.ResultTD[2];
					cdr.SetSectorPlay[1] = cdr.ResultTD[1];
					cdr.SetSectorPlay[2] = cdr.ResultTD[0];
				}
			}

			/*
			Rayman: detect track changes
			- fixes logo freeze

			Twisted Metal 2: skip PREGAP + starting accurate SubQ
			- plays tracks without retry play

			Wild 9: skip PREGAP + starting accurate SubQ
			- plays tracks without retry play
			*/
			Find_CurTrack(cdr.SetSectorPlay);
			ReadTrack(cdr.SetSectorPlay);
			cdr.TrackChanged = FALSE;

			if (!Config.Cdda)
				CDR_play(cdr.SetSectorPlay);

			// Vib Ribbon: gameplay checks flag
			cdr.StatP &= ~STATUS_SEEK;

			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;

			cdr.StatP |= STATUS_PLAY;

			
			// BIOS player - set flag again
			cdr.Play = TRUE;

			CDRMISC_INT( cdReadTime );
			break;

		case CdlForward:
			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
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
			cdr.StatP |= STATUS_ROTATING;
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
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
			break;

		case CdlStop:
			cdr.CmdProcess = 0;
			SetResultSize(1);
			cdr.StatP &= ~STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
//			cdr.Stat = Acknowledge;

			if (cdr.LidCheck == 0) cdr.LidCheck = 0x20;
			break;

		case CdlPause:
			SetResultSize(1);
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;

			/*
			Gundam Battle Assault 2: much slower (*)
			- Fixes boot, gameplay

			Hokuto no Ken 2: slower
			- Fixes intro + subtitles

			InuYasha - Feudal Fairy Tale: slower
			- Fixes battles
			*/
			AddIrqQueue(CdlPause + 0x20, cdReadTime * 3);
			cdr.Ctrl |= 0x80;
			break;

		case CdlPause + 0x20:
			SetResultSize(1);
			cdr.StatP &= ~STATUS_READ;
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
			break;

		case CdlInit:
			SetResultSize(1);
			cdr.StatP = STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
//			if (!cdr.Init) {
				AddIrqQueue(CdlInit + 0x20, 0x800);
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
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case CdlDemute:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case CdlSetfilter:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge; 
			break;

		case CdlSetmode:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case CdlGetmode:
			SetResultSize(6);
			cdr.StatP |= STATUS_ROTATING;
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
			SetResultSize(8);
			memcpy(&cdr.Result, &cdr.subq, 8);

			if (!cdr.Play && !cdr.Reading)
				cdr.Result[1] = 0; // HACK?

			cdr.Stat = Acknowledge;
			break;

		case CdlGetTN:
			// 5-Star Racing: don't stop CDDA
			//
			// Vib Ribbon: CD swap
			StopReading();

			cdr.CmdProcess = 0;
			SetResultSize(3);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			if (CDR_getTN(cdr.ResultTN) == -1) {
				cdr.Stat = DiskError;
				cdr.Result[0] |= STATUS_ERROR;
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
			cdr.StatP |= STATUS_ROTATING;
			if (CDR_getTD(cdr.Track, cdr.ResultTD) == -1) {
				cdr.Stat = DiskError;
				cdr.Result[0] |= STATUS_ERROR;
			} else {
				cdr.Stat = Acknowledge;
				cdr.Result[0] = cdr.StatP;
				cdr.Result[1] = itob(cdr.ResultTD[2]);
				cdr.Result[2] = itob(cdr.ResultTD[1]);
				cdr.Result[3] = itob(cdr.ResultTD[0]);
			}
			break;

		case CdlSeekL:
		case CdlSeekP:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.StatP |= STATUS_SEEK;
			cdr.Stat = Acknowledge;

			/*
			Crusaders of Might and Magic = 0.5x-4x
			- fix cutscene speech start

			Eggs of Steel = 2x-?
			- fix new game

			Medievil = ?-4x
			- fix cutscene speech

			Rockman X5 = 0.5-4x
			- fix capcom logo
			*/
			CDRMISC_INT(cdr.Seeked == SEEK_DONE ? 0x800 : cdReadTime * 4);
			cdr.Seeked = SEEK_PENDING;
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
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			AddIrqQueue(CdlID + 0x20, 0x800);
			break;

		case CdlID + 0x20:
			SetResultSize(8);

			if (CDR_getStatus(&stat) == -1) {
				cdr.Result[0] = 0x00; // 0x08 and cdr.Result[1]|0x10 : audio cd, enters cd player
				cdr.Result[1] = 0x80; // 0x80 leads to the menu in the bios, else loads CD
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
					if (CdromId[0] == '\0') {
						cdr.Result[0] = 0x00;
						cdr.Result[1] = 0x80;
					}
					else {
						cdr.Result[0] = 0x08;
						cdr.Result[1] = 0x00;
					}
				}
			}

			cdr.Result[2] = 0x00;
			cdr.Result[3] = 0x00;
			strncpy((char *)&cdr.Result[4], "PCSX", 4);
			cdr.Stat = Complete;
			break;

		case CdlReset:
			SetResultSize(1);
			cdr.StatP = STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		case CdlReadT:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			AddIrqQueue(CdlReadT + 0x20, 0x800);
			break;

		case CdlReadT + 0x20:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
			break;

		case CdlReadToc:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			AddIrqQueue(CdlReadToc + 0x20, 0x800);
			break;

		case CdlReadToc + 0x20:
			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Complete;
			break;

		case CdlReadN:
		case CdlReadS:
			if (!cdr.Reading) return;

			// Fighting Force 2 - update subq time immediately
			// - fixes new game
			Find_CurTrack(cdr.SetSector);
			ReadTrack(cdr.SetSector);


			// Crusaders of Might and Magic - update getlocl now
			// - fixes cutscene speech
			{
				u8 *buf = CDR_getBuffer();
				if (buf != NULL)
					memcpy(cdr.Transfer, buf, 8);
			}

			/*
			Duke Nukem: Land of the Babes - seek then delay read for one frame
			- fixes cutscenes
			C-12 - Final Resistance - doesn't like seek
			*/

			if (cdr.Seeked != SEEK_DONE) {
				cdr.StatP |= STATUS_SEEK;
				cdr.StatP &= ~STATUS_READ;

				// Crusaders of Might and Magic - use short time
				// - fix cutscene speech (startup)

				// ??? - use more accurate seek time later
				CDREAD_INT((cdr.Mode & 0x80) ? (cdReadTime) : cdReadTime * 2);
			} else {
				cdr.StatP |= STATUS_READ;
				cdr.StatP &= ~STATUS_SEEK;

				CDREAD_INT((cdr.Mode & 0x80) ? (cdReadTime) : cdReadTime * 2);
			}

			SetResultSize(1);
			cdr.StatP |= STATUS_ROTATING;
			cdr.Result[0] = cdr.StatP;
			cdr.Stat = Acknowledge;
			break;

		default:
			cdr.Stat = Complete;
			break;
	}

	Check_Shell( Irq );

	cdr.ParamC = 0;

	setIrq();

#ifdef CDR_LOG_CMD_IRQ
	SysPrintf("cdrInterrupt() Log: CDR Interrupt IRQ %d %02x: ",
		cdr.Stat != NoIntr && cdr.Reg2 != 0x18, Irq);
	for (i = 0; i < cdr.ResultC; i++)
		SysPrintf("%02x ", cdr.Result[i]);
	SysPrintf("\n");
#endif
}

#define ssat32_to_16(v) do { \
	if (v < -32768) v = -32768; \
	else if (v > 32767) v = 32767; \
} while (0)

void cdrAttenuate(s16 *buf, int samples, int stereo)
{
	int i, l, r;
	int ll = cdr.AttenuatorLeftToLeft;
	int lr = cdr.AttenuatorLeftToRight;
	int rl = cdr.AttenuatorRightToLeft;
	int rr = cdr.AttenuatorRightToRight;

	if (lr == 0 && rl == 0 && 0x78 <= ll && ll <= 0x88 && 0x78 <= rr && rr <= 0x88)
		return;

	if (!stereo && ll == 0x40 && lr == 0x40 && rl == 0x40 && rr == 0x40)
		return;

	if (stereo) {
		for (i = 0; i < samples; i++) {
			l = buf[i * 2];
			r = buf[i * 2 + 1];
			l = (l * ll + r * rl) >> 7;
			r = (r * rr + l * lr) >> 7;
			ssat32_to_16(l);
			ssat32_to_16(r);
			buf[i * 2] = l;
			buf[i * 2 + 1] = r;
		}
	}
	else {
		for (i = 0; i < samples; i++) {
			l = buf[i];
			l = l * (ll + rl) >> 7;
			//r = r * (rr + lr) >> 7;
			ssat32_to_16(l);
			//ssat32_to_16(r);
			buf[i] = l;
		}
	}
}

void cdrReadInterrupt() {
	u8 *buf;

	if (!cdr.Reading)
		return;

	if (cdr.Irq || cdr.Stat) {
		CDREAD_INT(0x100);
		return;
	}

	cdr.OCUP = 1;
	SetResultSize(1);
	cdr.StatP |= STATUS_READ|STATUS_ROTATING;
	cdr.StatP &= ~STATUS_SEEK;
	cdr.Result[0] = cdr.StatP;
	cdr.Seeked = SEEK_DONE;

	ReadTrack(cdr.SetSector);

	buf = CDR_getBuffer();
	if (buf == NULL)
		cdr.RErr = -1;

	if (cdr.RErr == -1) {
		CDR_LOG_I("cdrReadInterrupt() Log: err\n");
		memset(cdr.Transfer, 0, DATA_SIZE);
		cdr.Stat = DiskError;
		cdr.Result[0] |= STATUS_ERROR;
		CDREAD_INT((cdr.Mode & 0x80) ? (cdReadTime / 2) : cdReadTime);
		return;
	}

	memcpy(cdr.Transfer, buf, DATA_SIZE);
	CheckPPFCache(cdr.Transfer, cdr.Prev[0], cdr.Prev[1], cdr.Prev[2]);


	CDR_LOG("cdrReadInterrupt() Log: cdr.Transfer %x:%x:%x\n", cdr.Transfer[0], cdr.Transfer[1], cdr.Transfer[2]);

	if ((!cdr.Muted) && (cdr.Mode & MODE_STRSND) && (!Config.Xa) && (cdr.FirstSector != -1)) { // CD-XA
		// Firemen 2: Multi-XA files - briefings, cutscenes
		if( cdr.FirstSector == 1 && (cdr.Mode & MODE_SF)==0 ) {
			cdr.File = cdr.Transfer[4 + 0];
			cdr.Channel = cdr.Transfer[4 + 1];
		}

		if((cdr.Transfer[4 + 2] & 0x4) &&
			 (cdr.Transfer[4 + 1] == cdr.Channel) &&
			 (cdr.Transfer[4 + 0] == cdr.File)) {
			int ret = xa_decode_sector(&cdr.Xa, cdr.Transfer+4, cdr.FirstSector);
			if (!ret) {
				cdrAttenuate(cdr.Xa.pcm, cdr.Xa.nsamples, cdr.Xa.stereo);
				SPU_playADPCMchannel(&cdr.Xa);
				cdr.FirstSector = 0;
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

	CDREAD_INT((cdr.Mode & MODE_SPEED) ? (cdReadTime / 2) : cdReadTime);

	/*
	Croc 2: $40 - only FORM1 (*)
	Judge Dredd: $C8 - only FORM1 (*)
	Sim Theme Park - no adpcm at all (zero)
	*/

	if (!(cdr.Mode & MODE_STRSND) || !(cdr.Transfer[4+2] & 0x4)) {
		cdr.Stat = DataReady;
		setIrq();
	}

	// update for CdlGetlocP
	ReadTrack(cdr.SetSector);

	Check_Shell(0);
}

/*
cdrRead0:
	bit 0,1 - mode
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

	CDR_LOG_IO("cdr r0: %02x\n", cdr.Ctrl);

	return psxHu8(0x1800) = cdr.Ctrl;
}

void cdrWrite0(unsigned char rt) {
	CDR_LOG_IO("cdr w0: %02x\n", rt);

	cdr.Ctrl = (rt & 3) | (cdr.Ctrl & ~3);
}

unsigned char cdrRead1(void) {
	if ((cdr.ResultP & 0xf) < cdr.ResultC)
		psxHu8(0x1801) = cdr.Result[cdr.ResultP & 0xf];
	else
		psxHu8(0x1801) = 0;
	cdr.ResultP++;
	if (cdr.ResultP == cdr.ResultC)
		cdr.ResultReady = 0;

	CDR_LOG_IO("cdr r1: %02x\n", psxHu8(0x1801));

	return psxHu8(0x1801);
}

void cdrWrite1(unsigned char rt) {
	u8 set_loc[3];
	int i;

	CDR_LOG_IO("cdr w1: %02x\n", rt);

	switch (cdr.Ctrl & 3) {
	case 0:
		break;
	case 3:
		cdr.AttenuatorRightToRightT = rt;
		return;
	default:
		return;
	}

	cdr.Cmd = rt;
	cdr.OCUP = 0;

#ifdef CDR_LOG_CMD_IRQ
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

	cdr.ResultReady = 0;
	cdr.Ctrl |= 0x80;
	// cdr.Stat = NoIntr; 
	AddIrqQueue(cdr.Cmd, 0x800);

	switch (cdr.Cmd) {
	case CdlSync:
	case CdlNop:
	case CdlForward:
	case CdlBackward:
	case CdlReadT:
	case CdlTest:
	case CdlID:
	case CdlReadToc:
	case CdlGetmode:
	case CdlGetlocL:
	case CdlGetlocP:
	case CdlGetTD:
		break;

	case CdlSetloc:
		StopReading();
		for (i = 0; i < 3; i++)
			set_loc[i] = btoi(cdr.Param[i]);

		// FIXME: clean up this SetSector/SetSectorPlay mess,
		// there should be single var tracking current sector pos
		if (cdr.Play)
			i = msf2sec(cdr.SetSectorPlay);
		else
			i = msf2sec(cdr.SetSector);
		i = abs(i - msf2sec(set_loc));
		if (i > 16)
			cdr.Seeked = SEEK_PENDING;

		memcpy(cdr.SetSector, set_loc, 3);
		cdr.SetSector[3] = 0;
		break;

	case CdlPlay:
		// Vib Ribbon: try same track again
		StopCdda();

		// Vib Ribbon - decoded buffer IRQ for CDDA reading
		// - fixes ribbon timing + music CD mode
		//CDRDBUF_INT( PSXCLK / 44100 * 0x100 );

		cdr.Play = TRUE;

		cdr.StatP |= STATUS_SEEK;
		cdr.StatP &= ~STATUS_ROTATING;
		break;

	case CdlReadN:
		StopReading();
		cdr.Reading = 1;
		cdr.FirstSector = 1;
		cdr.Readed = 0xff;
		break;

	case CdlStandby:
		StopCdda();
		StopReading();
		break;

	case CdlStop:
		// GameShark CD Player: Reset CDDA to track start
		if (cdr.Play) {
			// grab time for current track
			CDR_getTD((u8)(cdr.CurTrack), cdr.ResultTD);

			cdr.SetSectorPlay[0] = cdr.ResultTD[2];
			cdr.SetSectorPlay[1] = cdr.ResultTD[1];
			cdr.SetSectorPlay[2] = cdr.ResultTD[0];
		}

		StopCdda();
		StopReading();
		break;

	case CdlPause:
		/*
		   GameShark CD Player: save time for resume

		   Twisted Metal - World Tour: don't mix Setloc / CdlPlay cursors
		*/

		StopCdda();
		StopReading();
		break;

	case CdlReset:
	case CdlInit:
		cdr.Seeked = SEEK_DONE;
		StopCdda();
		StopReading();
		break;

	case CdlMute:
		cdr.Muted = TRUE;
			// Duke Nukem - Time to Kill
			// - do not directly set cd-xa volume
			//SPU_writeRegister( H_CDLeft, 0x0000 );
			//SPU_writeRegister( H_CDRight, 0x0000 );
		break;

	case CdlDemute:
		cdr.Muted = FALSE;

			// Duke Nukem - Time to Kill
			// - do not directly set cd-xa volume
			//SPU_writeRegister( H_CDLeft, 0x7f00 );
			//SPU_writeRegister( H_CDRight, 0x7f00 );
		break;

    	case CdlSetfilter:
        	cdr.File = cdr.Param[0];
        	cdr.Channel = cdr.Param[1];
        	break;

    	case CdlSetmode:
		CDR_LOG("cdrWrite1() Log: Setmode %x\n", cdr.Param[0]);

        	cdr.Mode = cdr.Param[0];

		// Squaresoft on PlayStation 1998 Collector's CD Vol. 1
		// - fixes choppy movie sound
		if( cdr.Play && (cdr.Mode & MODE_CDDA) == 0 )
			StopCdda();
        	break;

    	case CdlGetTN:
		//AddIrqQueue(cdr.Cmd, 0x800);

		// GameShark CDX CD Player: very long time
		AddIrqQueue(cdr.Cmd, 0x100000);
		break;

    	case CdlSeekL:
    	case CdlSeekP:
		// Tomb Raider 2 - reset cdda
		StopCdda();
		StopReading();
		break;

    	case CdlReadS:
		StopReading();
		cdr.Reading = 2;
		cdr.FirstSector = 1;
		cdr.Readed = 0xff;
		break;

	default:
		cdr.ParamC = 0;
		CDR_LOG_I("cdrWrite1() Log: Unknown command: %x\n", cdr.Cmd);
		return;
	}
}

unsigned char cdrRead2(void) {
	unsigned char ret;

	if (cdr.Readed == 0) {
		ret = 0;
	} else {
		ret = cdr.Transfer[cdr.transferIndex];
		cdr.transferIndex++;
		adjustTransferIndex();
	}

	CDR_LOG_IO("cdr r2: %02x\n", ret);
	return ret;
}

void cdrWrite2(unsigned char rt) {
	CDR_LOG_IO("cdr w2: %02x\n", rt);

	switch (cdr.Ctrl & 3) {
	case 0:
		if (cdr.ParamC < 8) // FIXME: size and wrapping
			cdr.Param[cdr.ParamC++] = rt;
		return;
	case 1:
		cdr.Reg2 = rt;
		setIrq();
		return;
	case 2:
		cdr.AttenuatorLeftToLeftT = rt;
		return;
	case 3:
		cdr.AttenuatorRightToLeftT = rt;
		return;
	}
}

unsigned char cdrRead3(void) {
	if (cdr.Ctrl & 0x1)
		psxHu8(0x1803) = cdr.Stat | 0xE0;
	else
		psxHu8(0x1803) = cdr.Reg2 | 0xE0;

	CDR_LOG_IO("cdr r3: %02x\n", psxHu8(0x1803));
	return psxHu8(0x1803);
}

void cdrWrite3(unsigned char rt) {
	CDR_LOG_IO("cdr w3: %02x\n", rt);

	switch (cdr.Ctrl & 3) {
	case 0:
		break; // transfer
	case 1:
		cdr.Stat &= ~rt;

		if (rt & 0x40)
			cdr.ParamC = 0;
		return;
	case 2:
		cdr.AttenuatorLeftToRightT = rt;
		return;
	case 3:
		if (rt & 0x20) {
			memcpy(&cdr.AttenuatorLeftToLeft, &cdr.AttenuatorLeftToLeftT, 4);
			CDR_LOG_I("CD-XA Volume: %02x %02x | %02x %02x\n",
				cdr.AttenuatorLeftToLeft, cdr.AttenuatorLeftToRight,
				cdr.AttenuatorRightToLeft, cdr.AttenuatorRightToRight);
		}
		return;
	}

	if ((rt & 0x80) && cdr.Readed == 0) {
		cdr.Readed = 1;
		cdr.transferIndex = 0;

		switch (cdr.Mode & (MODE_SIZE_2340|MODE_SIZE_2328)) {
			case MODE_SIZE_2328:
			case MODE_SIZE_2048:
				cdr.transferIndex += 12;
				break;

			case MODE_SIZE_2340:
				cdr.transferIndex += 0;
				break;

			default:
				break;
		}
	}
}

void psxDma3(u32 madr, u32 bcr, u32 chcr) {
	u32 cdsize;
	int i;
	u8 *ptr;

	CDR_LOG("psxDma3() Log: *** DMA 3 *** %x addr = %x size = %x\n", chcr, madr, bcr);

	switch (chcr) {
		case 0x11000000:
		case 0x11400100:
			if (cdr.Readed == 0) {
				CDR_LOG("psxDma3() Log: *** DMA 3 *** NOT READY\n");
				break;
			}

			cdsize = (bcr & 0xffff) * 4;

			// Ape Escape: bcr = 0001 / 0000
			// - fix boot
			if( cdsize == 0 )
			{
				switch (cdr.Mode & (MODE_SIZE_2340|MODE_SIZE_2328)) {
					case MODE_SIZE_2340: cdsize = 2340; break;
					case MODE_SIZE_2328: cdsize = 2328; break;
					default:
					case MODE_SIZE_2048: cdsize = 2048; break;
				}
			}


			ptr = (u8 *)PSXM(madr);
			if (ptr == NULL) {
				CDR_LOG("psxDma3() Log: *** DMA 3 *** NULL Pointer!\n");
				break;
			}

			/*
			GS CDX: Enhancement CD crash
			- Setloc 0:0:0
			- CdlPlay
			- Spams DMA3 and gets buffer overrun
			*/
			for(i = 0; i < cdsize; ++i) {
				ptr[i] = cdr.Transfer[cdr.transferIndex];
				cdr.transferIndex++;
				adjustTransferIndex();
			}

			psxCpu->Clear(madr, cdsize / 4);

			// burst vs normal
			if( chcr == 0x11400100 ) {
				CDRDMA_INT( (cdsize/4) / 4 );
			}
			else if( chcr == 0x11000000 ) {
				CDRDMA_INT( (cdsize/4) * 1 );
			}
			return;

		default:
			CDR_LOG("psxDma3() Log: Unknown cddma %x\n", chcr);
			break;
	}

	HW_DMA3_CHCR &= SWAP32(~0x01000000);
	DMA_INTERRUPT(3);
}

void cdrDmaInterrupt()
{
	if (HW_DMA3_CHCR & SWAP32(0x01000000))
	{
		HW_DMA3_CHCR &= SWAP32(~0x01000000);
		DMA_INTERRUPT(3);
	}
}

static void getCdInfo(void)
{
	u8 tmp;

	CDR_getTN(cdr.ResultTN);
	CDR_getTD(0, cdr.SetSectorEnd);
	tmp = cdr.SetSectorEnd[0];
	cdr.SetSectorEnd[0] = cdr.SetSectorEnd[2];
	cdr.SetSectorEnd[2] = tmp;
}

void cdrReset() {
	memset(&cdr, 0, sizeof(cdr));
	cdr.CurTrack = 1;
	cdr.File = 1;
	cdr.Channel = 1;
	cdr.transferIndex = 0;

	// BIOS player - default values
	cdr.AttenuatorLeftToLeft = 0x80;
	cdr.AttenuatorLeftToRight = 0x00;
	cdr.AttenuatorRightToLeft = 0x00;
	cdr.AttenuatorRightToRight = 0x80;

	getCdInfo();
}

int cdrFreeze(gzFile f, int Mode) {
	u8 tmpp[3];

	if (Mode == 0 && !Config.Cdda)
		CDR_stop();
	
	gzfreeze(&cdr, sizeof(cdr));
	
	if (Mode == 1)
		cdr.ParamP = cdr.ParamC;

	if (Mode == 0) {
		getCdInfo();

		// read right sub data
		memcpy(tmpp, cdr.Prev, 3);
		cdr.Prev[0]++;
		ReadTrack(tmpp);

		if (cdr.Play) {
			Find_CurTrack(cdr.SetSectorPlay);
			if (!Config.Cdda)
				CDR_play(cdr.SetSectorPlay);
		}
	}

	return 0;
}

void LidInterrupt() {
	cdr.LidCheck = 0x20; // start checker

	getCdInfo();
       
	StopCdda();
	CDRLID_INT( cdReadTime * 3 );

	// generate interrupt if none active - open or close
	if (cdr.Irq == 0 || cdr.Irq == 0xff) {
		cdr.Ctrl |= 0x80;
		cdr.Stat = NoIntr;
		AddIrqQueue(CdlNop, 0x800);
	}
}
