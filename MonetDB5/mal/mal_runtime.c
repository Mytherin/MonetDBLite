/*
 * The contents of this file are subject to the MonetDB Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.monetdb.org/Legal/MonetDBLicense
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
 * License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is the MonetDB Database System.
 *
 * The Initial Developer of the Original Code is CWI.
 * Portions created by CWI are Copyright (C) 1997-July 2008 CWI.
 * Copyright August 2008-2013 MonetDB B.V.
 * All Rights Reserved.
 */

/* Author(s) M.L. Kersten
 * The MAL Runtime Profiler
 * This little helper module is used to perform instruction based profiling.
 */

#include "monetdb_config.h"
#include "mal_utils.h"
#include "mal_runtime.h"
#include "mal_function.h"
#include "mal_profiler.h"
#include "mal_listing.h"

#define heapinfo(X) ((X) && (X)->base ? (X)->free: 0)
#define hashinfo(X) (((X) && (X)->mask)? ((X)->mask + (X)->lim + 1) * sizeof(int) + sizeof(*(X)) + cnt * sizeof(int):  0)
/*
 * Manage the runtime profiling information
 */
void
runtimeProfileInit(MalBlkPtr mb, RuntimeProfile prof, int initmemory)
{
	prof->newclk = 0;
	prof->ppc = -2;
	prof->tcs = 0;
	prof->inblock = 0;
	prof->oublock = 0;
	if (initmemory)
		prof->memory = MT_mallinfo();
	else
		memset(&prof->memory, 0, sizeof(prof->memory));
	if (malProfileMode) {
		setFilterOnBlock(mb, 0, 0);
		prof->ppc = -1;
	}
}

void
runtimeProfileBegin(Client cntxt, MalBlkPtr mb, MalStkPtr stk, int stkpc, RuntimeProfile prof, int start)
{
	if (malProfileMode == 0)
		return; /* mostly true */
	
	if (stk && mb->profiler != NULL) {
		prof->newclk = stk->clk = GDKusec();
		if (mb->profiler[stkpc].trace) {
			MT_lock_set(&mal_delayLock, "DFLOWdelay");
			gettimeofday(&stk->clock, NULL);
			prof->ppc = stkpc;
			mb->profiler[stkpc].clk = 0;
			mb->profiler[stkpc].ticks = 0;
			mb->profiler[stkpc].clock = stk->clock;
			/* emit the instruction upon start as well */
			if (malProfileMode)
				profilerEvent(cntxt->idx, mb, stk, stkpc, start);
#ifdef HAVE_TIMES
			times(&stk->timer);
			mb->profiler[stkpc].timer = stk->timer;
#endif
			mb->profiler[stkpc].clk = stk->clk;
			MT_lock_unset(&mal_delayLock, "DFLOWdelay");
		}
	}
}


void
runtimeProfileExit(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci, RuntimeProfile prof)
{
	int i,j,fnd, stkpc = prof->ppc;

	if (cntxt->flags & footprintFlag && pci){
		for (i = 0; i < pci->retc; i++)
			if ( isaBatType(getArgType(mb,pci,i)) && stk->stk[getArg(pci,i)].val.bval){
				/* avoid simple alias operations */
				fnd= 0;
				for ( j= pci->retc; j< pci->argc; j++)
					if ( isaBatType(getArgType(mb,pci,j)))
						fnd+= stk->stk[getArg(pci,i)].val.bval == stk->stk[getArg(pci,j)].val.bval;
				if (fnd == 0 )
					updateFootPrint(mb,stk,getArg(pci,i));
			}
	}

	if (malProfileMode == 0)
		return; /* mostly true */
	if (stk != NULL && prof->ppc >= 0 && mb->profiler != NULL && mb->profiler[stkpc].trace && mb->profiler[stkpc].clk)
	{
		MT_lock_set(&mal_contextLock, "DFLOWdelay");
		gettimeofday(&mb->profiler[stkpc].clock, NULL);
		mb->profiler[stkpc].counter++;
		mb->profiler[stkpc].ticks = GDKusec() - prof->newclk;
		mb->profiler[stkpc].totalticks += mb->profiler[stkpc].ticks;
		mb->profiler[stkpc].clk += mb->profiler[stkpc].clk;
		if (pci) {
			mb->profiler[stkpc].rbytes = getVolume(stk, pci, 0);
			mb->profiler[stkpc].wbytes = getVolume(stk, pci, 1);
		}
		profilerEvent(cntxt->idx, mb, stk, stkpc, 0);
		prof->ppc = -1;
		MT_lock_unset(&mal_contextLock, "DFLOWdelay");
	}
}

/*
 * For performance evaluation it is handy to know the
 * maximal amount of bytes read/written. The actual
 * amount is harder to guess, because it too much
 * depends on the operation.
 */
lng getVolume(MalStkPtr stk, InstrPtr pci, int rd)
{
	int i, limit;
	lng vol = 0;
	BAT *b;
	int isview = 0;

	limit = rd == 0 ? pci->retc : pci->argc;
	i = rd ? pci->retc : 0;

	if (stk->stk[getArg(pci, 0)].vtype == TYPE_bat) {
		b = BBPquickdesc(ABS(stk->stk[getArg(pci, 0)].val.bval), TRUE);
		if (b)
			isview = isVIEW(b);
	}
	for (; i < limit; i++) {
		if (stk->stk[getArg(pci, i)].vtype == TYPE_bat) {
			oid cnt = 0;

			b = BBPquickdesc(ABS(stk->stk[getArg(pci, i)].val.bval), TRUE);
			if (b == NULL)
				continue;
			cnt = BATcount(b);
			/* Usually reading views cost as much as full bats.
			   But when we output a slice that is not the case. */
			vol += ((rd && !isview) || !VIEWhparent(b)) ? headsize(b, cnt) : 0;
			vol += ((rd && !isview) || !VIEWtparent(b)) ? tailsize(b, cnt) : 0;
		}
	}
	return vol;
}

void displayVolume(Client cntxt, lng vol)
{
	char buf[32];
	formatVolume(buf, (int) sizeof(buf), vol);
	mnstr_printf(cntxt->fdout, "%s", buf);
}
/*
 * The footprint maintained in the stack is the total size all non-persistent objects in MB.
 * It gives an impression of the total extra memory needed during query evaluation.
 * Note, it does imply that all that space is claimed at the same time.
 */

void
updateFootPrint(MalBlkPtr mb, MalStkPtr stk, int varid)
{
    BAT *b;
	BUN cnt;
    lng total = 0;
	int bid;

	if ( !mb || !stk)
		return ;
	if ( isaBatType(getVarType(mb,varid)) && (bid = stk->stk[varid].val.bval) != bat_nil){

		b = BATdescriptor(bid);
        if (b == NULL || isVIEW(b) || b->batPersistence == PERSISTENT)
            return;
		cnt = BATcount(b);
		if( b->H ) total += heapinfo(&b->H->heap);
		if( b->H ) total += heapinfo(b->H->vheap);

		if ( b->T ) total += heapinfo(&b->T->heap);
		if ( b->T ) total += heapinfo(b->T->vheap);
		if ( b->H ) total += hashinfo(b->H->hash);
		if ( b->T ) total += hashinfo(b->T->hash); 
		BBPreleaseref(b->batCacheid);
		// no concurrency protection (yet)
		stk->tmpspace += total/1024/1024; // keep it in MBs
    }
}
