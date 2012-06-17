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
 * Copyright August 2008-2012 MonetDB B.V.
 * All Rights Reserved.
*/
/*
 * Post-optimization. After the join path has been constructed
 * we could search for common subpaths. This heuristic is to
 * remove any pair which is used more than once.
 * Inner paths are often foreign key walks.
 * The heuristics is sufficient for the code produced by SQL frontend.
 * The alternative is to search for all possible subpaths and materialize them.
 * For example, using recursion for all common paths.
 */
#include "monetdb_config.h"
#include "opt_joinpath.h"
#include "cluster.h"

typedef struct{
	int cnt;
	int lvar, rvar;
	str fcn;
	InstrPtr p;
} Candidate;

/*
 * The join path type analysis should also be done at run time,
 * because the expressive power of MAL is insufficient to
 * enforce a proper join type list.
 * The current  costmodel is rather limited. It takes as default
 * the order presented and catches a few more cases that do not
 * lead to materialization of large views. This solved the earlier
 * problem noticable on TPC-H that join order was producing slower
 * results. Now, on TPCH-H, all queries run equal or better.
 * about 12 out of 84 joinpaths are improved with more than 10%.
 * (affecting Q2, Q5, Q7, Q8,Q11,Q13)
 */
static int
OPTjoinSubPath(Client cntxt, MalBlkPtr mb)
{
	int i,j,k,top=0, actions =0;
	str joinPathRef = putName("joinPath",8);
	str leftjoinPathRef = putName("leftjoinPath",12);
	str semijoinPathRef = putName("semijoinPath",12);
	InstrPtr q = NULL, p, *old;
	int limit, slimit;
	Candidate *candidate;

	candidate = (Candidate *) GDKzalloc(mb->stop * sizeof(Candidate));
	if ( candidate == NULL)
		return 0;
	(void) cntxt;

	/* collect all candidates */
	limit= mb->stop;
	slimit= mb->ssize;
	for(i=0, p= getInstrPtr(mb, i); i< limit; i++, p= getInstrPtr(mb, i))
		if ( getFunctionId(p)== joinPathRef || getFunctionId(p)== leftjoinPathRef || getFunctionId(p) == semijoinPathRef)
			for ( j= p->retc; j< p->argc-1; j++){
				for (k= top-1; k >= 0 ; k--)
					if ( candidate[k].lvar == getArg(p,j) && candidate[k].rvar == getArg(p,j+1) && candidate[k].fcn == getFunctionId(p)){
						candidate[k].cnt++;
						break;
					}
				if (k < 0) k = top;
				if ( k == top && top < mb->stop ){
					candidate[k].cnt =1;
					candidate[k].lvar = getArg(p,j);
					candidate[k].rvar = getArg(p,j+1);
					candidate[k].fcn = getFunctionId(p);
					top++;
				}
			}

	if (top == 0) {
		GDKfree(candidate);
		return 0;
	}

	/* now inject and replace the subpaths */
	old = mb->stmt;
	if ( newMalBlkStmt(mb,mb->ssize) < 0) {
		GDKfree(candidate);
		return 0;
	}

	for(i=0, p= old[i]; i< limit; i++, p= old[i]) {
		if( getFunctionId(p)== joinPathRef || getFunctionId(p)== leftjoinPathRef || getFunctionId(p) == semijoinPathRef)
			for ( j= p->retc ; j< p->argc-1; j++){
				for (k= top-1; k >= 0 ; k--)
					if ( candidate[k].lvar == getArg(p,j) && candidate[k].rvar == getArg(p,j+1) && candidate[k].fcn == getFunctionId(p) && candidate[k].cnt > 1){
						if ( candidate[k].p == 0 ) {
							if ( candidate[k].fcn == joinPathRef)
								q= newStmt(mb, algebraRef, joinRef);
							else if ( candidate[k].fcn == leftjoinPathRef) 
								q= newStmt(mb, algebraRef, leftjoinRef);
							else if ( candidate[k].fcn == semijoinPathRef)
								q= newStmt(mb, algebraRef, semijoinRef);
							q= pushArgument(mb,q, candidate[k].lvar);
							q= pushArgument(mb,q, candidate[k].rvar);
							candidate[k].p = q;
						} 
						delArgument(p,j);
						getArg(p,j) = getArg(candidate[k].p,0);
						if ( p->argc == 3 ){
							if (getFunctionId(p) == leftjoinPathRef)
								setFunctionId(p, leftjoinRef);
							else if ( getFunctionId(p) == semijoinPathRef)
								setFunctionId(p, semijoinRef);
							else if ( getFunctionId(p) == joinPathRef)
								setFunctionId(p, joinRef);
						}
						actions ++;
						OPTDEBUGjoinPath {
							mnstr_printf(cntxt->fdout,"re-use pair\n");
							printInstruction(cntxt->fdout,mb,0,candidate[k].p,0);
							printInstruction(cntxt->fdout,mb,0,p,0);
						}
						goto breakout;
					}
			}
	breakout:
		pushInstruction(mb,p);
	}
	for(; i<slimit; i++)
		if( old[i])
			freeInstruction(old[i]);
	
	GDKfree(old);
	GDKfree(candidate);
	/* there may be new opportunities to remove common expressions 
	   avoid the recursion
	if ( actions )
		return actions + OPTjoinSubPath(cntxt, mb);
	*/
	return actions;
}

int
OPTjoinPathImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int i,j,k, actions=0;
	int *pc;
	str joinPathRef = putName("joinPath",8);
	str leftjoinPathRef = putName("leftjoinPath",12);
	str semijoinPathRef = putName("semijoinPath",12);
	InstrPtr q,r;
	InstrPtr *old;
	int *varcnt;		/* use count */
	int limit,slimit;

	(void) cntxt;
	(void) stk;
	if (varGetProp(mb, getArg(mb->stmt[0], 0), inlineProp) != NULL)
		return 0;

	old= mb->stmt;
	limit= mb->stop;
	slimit= mb->ssize;
	if ( newMalBlkStmt(mb,mb->ssize) < 0)
		return 0;

	/* beware, new variables and instructions are introduced */
	pc= (int*) GDKzalloc(sizeof(int)* mb->vtop * 2); /* to find last assignment */
	varcnt= (int*) GDKzalloc(sizeof(int)* mb->vtop * 2); 
	if (pc == NULL || varcnt == NULL){
		if (pc ) GDKfree(pc);
		if (varcnt ) GDKfree(varcnt);
		return 0;
	}
	/*
	 * @-
	 * Count the variable use as arguments first.
	 */
	for (i = 0; i<limit; i++){
		p= old[i];
		for(j=p->retc; j<p->argc; j++)
			varcnt[getArg(p,j)]++;
	}

	for (i = 0; i<limit; i++){
		p= old[i];
		if( getModuleId(p)== algebraRef && (getFunctionId(p)== joinRef || getFunctionId(p) == leftjoinRef || getFunctionId(p) == semijoinRef)){
			/*
			 * @-
			 * Try to expand its argument list with what we have found so far.
			 * This creates a series of join paths, many of which will be removed during deadcode elimination.
			 */
			q= copyInstruction(p);
			q->argc=1;
			for(j=p->retc; j<p->argc; j++){
				r= getInstrPtr(mb,pc[getArg(p,j)]);
				/*
				 * @-
				 * Don't inject a pattern when it is used more than once.
				 */
				if (r && varcnt[getArg(p,j)] > 1){
					OPTDEBUGjoinPath {
						mnstr_printf(cntxt->fdout,"#double use %d %d\n", getArg(p,j), varcnt[getArg(p,j)]);
						printInstruction(cntxt->fdout,mb, 0, p, LIST_MAL_ALL);
					}
					r = 0;
				}
				OPTDEBUGjoinPath {
					mnstr_printf(cntxt->fdout,"#expand list \n");
					printInstruction(cntxt->fdout,mb, 0, p, LIST_MAL_ALL);
					printInstruction(cntxt->fdout,mb, 0, q, LIST_MAL_ALL);
				}
				if ( getFunctionId(p) == joinRef){
					if( r &&  getModuleId(r)== algebraRef && ( getFunctionId(r)== joinRef  || getFunctionId(r)== joinPathRef) ){
						for(k= r->retc; k<r->argc; k++) 
							q = pushArgument(mb,q,getArg(r,k));
					} else 
						q = pushArgument(mb,q,getArg(p,j));
				} else if ( getFunctionId(p) == leftjoinRef){
					if( r &&  getModuleId(r)== algebraRef && ( getFunctionId(r)== leftjoinRef  || getFunctionId(r)== leftjoinPathRef) ){
						for(k= r->retc; k<r->argc; k++) 
							q = pushArgument(mb,q,getArg(r,k));
					} else 
						q = pushArgument(mb,q,getArg(p,j));
				} else if ( getFunctionId(p) == semijoinRef){
					if( r &&  getModuleId(r)== algebraRef && ( getFunctionId(r)== semijoinRef  || getFunctionId(r)== semijoinPathRef) ){
						for(k= r->retc; k<r->argc; k++) 
							q = pushArgument(mb,q,getArg(r,k));
					} else 
						q = pushArgument(mb,q,getArg(p,j));
				}
			}
			OPTDEBUGjoinPath {
				chkTypes(cntxt->fdout, cntxt->nspace,mb,TRUE);
				mnstr_printf(cntxt->fdout,"#new [left]joinPath instruction\n");
				printInstruction(cntxt->fdout,mb, 0, q, LIST_MAL_ALL);
			}
			if(q->argc<= p->argc){
				/* no change */
				freeInstruction(q);
				goto wrapup;
			}
			/*
			 * @-
			 * Final type check and hardwire the result type, because that  can not be inferred directly from the signature
			 */
			for(j=1; j<q->argc-1; j++)
				if( getTailType(getArgType(mb,q,j)) != getHeadType(getArgType(mb,q,j+1)) &&
				!( getTailType(getArgType(mb,q,j))== TYPE_oid  &&
				getHeadType(getArgType(mb,q,j))== TYPE_void) &&
				!( getTailType(getArgType(mb,q,j))== TYPE_void &&
				getHeadType(getArgType(mb,q,j))== TYPE_oid)){
				/* don't use it */
					freeInstruction(q);
					goto wrapup;
				}

			/* fix the type */
			setVarUDFtype(mb, getArg(q,0));
			setVarType(mb, getArg(q,0), newBatType( getHeadType(getArgType(mb,q,q->retc)), getTailType(getArgType(mb,q,q->argc-1))));
			if ( q->argc > 3  &&  getFunctionId(q) == joinRef)
				setFunctionId(q,joinPathRef);
			else if ( q->argc > 3  &&  getFunctionId(q) == leftjoinRef)
				setFunctionId(q,leftjoinPathRef);
			else if ( q->argc > 2  &&  getFunctionId(q) == semijoinRef)
				setFunctionId(q,semijoinPathRef);
			freeInstruction(p);
			p= q;
			actions++;
		} 
	wrapup:
		pushInstruction(mb,p);
		for(j=0; j< p->retc; j++)
			pc[getArg(p,j)]= mb->stop-1;
	}
	for(; i<slimit; i++)
		if(old[i])
			freeInstruction(old[i]);
	/* perform the second phase, try out */
	if (actions )
		actions += OPTjoinSubPath(cntxt, mb);
	GDKfree(old);
	GDKfree(pc);
	if (varcnt ) GDKfree(varcnt);
	DEBUGoptimizers
		mnstr_printf(cntxt->fdout,"#opt_joinpath: %d statements glued\n",actions);
	return actions;
}
/*
 * @-
 * The join path optimizer takes a join sequence and
 * attempts to minimize the intermediate result.
 * The choice depends on a good estimate of intermediate
 * results using properties.
 * For the time being, we use a simplistic model, based
 * on the assumption that most joins are foreign key joins anyway.
 *
 * We use a sample based approach for sizeable  tables.
 * The model is derived from the select statement. However, we did not succeed.
 * The code is now commented for future improvement.
 *
 * Final conclusion from this exercise is:
 * The difference between the join input size and the join output size is not
 * the correct (or unique) metric which should be used to decide which order
 * should be used in the joinPath.
 * A SMALL_OPERAND is preferrable set to those cases where the table
 * fits in the cache. This depends on the cache size and operand type.
 * For the time being we limit ourself to a default of 1Kelements
 */
/*#define SAMPLE_THRESHOLD_lOG 17*/
#define SMALL_OPERAND	1024
static BUN
ALGjoinCost(Client cntxt, BAT *l, BAT *r, int flag)
{
	int actions = 1;
	BUN lc, rc;
	BUN cost=0;
#if 0
	BUN lsize,rsize;
	BAT *lsample, *rsample, *j; 
#endif

	(void) flag;
	lc = BATcount(l);
	rc = BATcount(r);
#if 0	
	/* The sampling method */
	if(flag < 2 && ( lc > 100000 || rc > 100000)){
		lsize= MIN(lc/100, (1<<SAMPLE_THRESHOLD_lOG)/3);
		lsample= BATsample(l,lsize);
		BBPreclaim(lsample);
		rsize= MIN(rc/100, (1<<SAMPLE_THRESHOLD_lOG)/3);
		rsample= BATsample(r,rsize);
		BBPreclaim(rsample);
		j= BATjoin(l,r, MAX(lsize,rsize));
		lsize= BATcount(j);
		BBPreclaim(j);
		return lsize;
	}
#endif

	/* first use logical properties to estimate upper bound of result size */
	if (l->tkey && r->hkey)
		cost = MIN(lc,rc);
	else
	if (l->tkey)
		cost = rc;
	else
	if (r->hkey)
		cost = lc;
	else
	if (lc * rc >= BUN_MAX)
		cost = BUN_MAX;
	else
		cost = lc * rc;

	/* then use physical properties to rank costs */
	if (BATtdense(l) && BAThdense(r))
		/* densefetchjoin -> sequential access */
		cost /= 7;
	else
	if (BATtordered(l) && BAThdense(r))
		/* orderedfetchjoin > sequential access */
		cost /= 6;
	else
	if (BATtdense(l) && BAThordered(r) && flag != 0 /* no leftjoin */)
		/* (reversed-) orderedfetchjoin -> sequential access */
		cost /= 6;
	else
	if (BAThdense(r) && rc <= SMALL_OPERAND)
		/* fetchjoin with random access in L1 */
		cost /= 5;
	else
	if (BATtdense(l) && lc <= SMALL_OPERAND && flag != 0 /* no leftjoin */)
		/* (reversed-) fetchjoin with random access in L1 */
		cost /= 5;
	else
	if (BATtordered(l) && BAThordered(r))
		/* mergejoin > sequential access */
		cost /= 4;
	else
	if (BAThordered(r) && rc <= SMALL_OPERAND)
		/* binary-lookup-join with random access in L1 */
		cost /= 3;
	else
	if (BATtordered(l) && lc <= SMALL_OPERAND && flag != 0 /* no leftjoin */)
		/* (reversed-) binary-lookup-join with random access in L1 */
		cost /= 3;
	else
	if ((BAThordered(r) && lc <= SMALL_OPERAND) || (BATtordered(l) && rc <= SMALL_OPERAND))
		/* sortmergejoin with sorting in L1 */
		cost /= 3;
	else
	if (rc <= SMALL_OPERAND)
		/* hashjoin with hashtable in L1 */
		cost /= 3;
	else
	if (lc <= SMALL_OPERAND && flag != 0 /* no leftjoin */)
		/* (reversed-) hashjoin with hashtable in L1 */
		cost /= 3;
	else
	if (BAThdense(r))
		/* fetchjoin with random access beyond L1 */
		cost /= 2;
	else
	if (BATtdense(l) && flag != 0 /* no leftjoin */)
		/* (reversed-) fetchjoin with random access beyond L1 */
		cost /= 2;
	else
		/* hashjoin with hashtable larger than L1 */
		/* sortmergejoin with sorting beyond L1 */
		cost /= 1;

	DEBUGoptimizers
		mnstr_printf(cntxt->fdout,"#batjoin cost ?"BUNFMT"\n",cost);
	return cost;
}

BAT *
ALGjoinPathBody(Client cntxt, int top, BAT **joins, int flag)
{
	BAT *b = NULL;
	BUN estimate, e = 0;
	int i, j, k;
	int *postpone= (int*) GDKzalloc(sizeof(int) *top);
	int postponed=0;

	/* solve the join by pairing the smallest first */
	while (top > 1) {
		j = 0;
		estimate = ALGjoinCost(cntxt,joins[0],joins[1],flag);
		OPTDEBUGjoinPath
			mnstr_printf(cntxt->fdout,"#joinPath estimate join(%d,%d) %d cnt="BUNFMT" %s\n", joins[0]->batCacheid, 
				joins[1]->batCacheid,(int)estimate, BATcount(joins[0]), postpone[0]?"postpone":"");
		for (i = 1; i < top - 1; i++) {
			e = ALGjoinCost(cntxt,joins[i], joins[i + 1],flag);
			OPTDEBUGjoinPath
				mnstr_printf(cntxt->fdout,"#joinPath estimate join(%d,%d) %d cnt="BUNFMT" %s\n", joins[i]->batCacheid, 
					joins[i+1]->batCacheid,(int)e,BATcount(joins[i]),  postpone[i]?"postpone":"");
			if (e < estimate &&  ( !(postpone[i] && postpone[i+1]) || postponed<top)) {
				estimate = e;
				j = i;
			}
		}
		/*
		 * @-
		 * BEWARE. you may not use a size estimation, because it
		 * may fire a BATproperty check in a few cases.
		 * In case a join fails, we may try another order first before
		 * abandoning the task. It can handle cases where a Cartesian product emerges.
		 *
		 * A left-join sequence only requires the result to be sorted
		 * against the first operand. For all others operand pairs, the cheapest join suffice.
		 */

		switch(flag){
		case 0:
			if ( j == 0) {
				b = BATleftjoin(joins[j], joins[j + 1], BATcount(joins[j]));
				break;
			}
		case 1:
			b = BATjoin(joins[j], joins[j + 1], (BATcount(joins[j]) < BATcount(joins[j + 1])? BATcount(joins[j]):BATcount(joins[ j + 1])));
			break;
		case 2:
			b = BATsemijoin(joins[j], joins[j + 1]);
		}
		if (b==NULL){
			if ( postpone[j] && postpone[j+1]){
				for( --top; top>=0; top--)
					BBPreleaseref(joins[top]->batCacheid);
				GDKfree(postpone);
				return NULL;
			}
			postpone[j] = TRUE;
			postpone[j+1] = TRUE;
			postponed = 0;
			for( k=0; k<top; k++)
				postponed += postpone[k]== TRUE;
			if ( postponed == top){
				for( --top; top>=0; top--)
					BBPreleaseref(joins[top]->batCacheid);
				GDKfree(postpone);
				return NULL;
			}
			/* clear the GDKerrors and retry */
			if( cntxt->errbuf )
				cntxt->errbuf[0]=0;
			continue;
		} else {
			/* reset the postponed joins */
			for( k=0; k<top; k++)
				postpone[k]=FALSE;
			if (!(b->batDirty&2)) b = BATsetaccess(b, BAT_READ);
			postponed = 0;
		}
		ALGODEBUG{
			if (b ) {
				mnstr_printf(GDKout, "#joinPath %d:= join(%d,%d)"
				" arguments %d (cnt= "BUNFMT") against (cnt "BUNFMT") cost "BUNFMT"\n", 
					b->batCacheid, joins[j]->batCacheid, joins[j + 1]->batCacheid,
					j, BATcount(joins[j]),  BATcount(joins[j+1]), e);
			}
		}

		if ( b == 0 ){
			for( --top; top>=0; top--)
				BBPreleaseref(joins[top]->batCacheid);
			GDKfree(postpone);
			return 0;
		}
		BBPdecref(joins[j]->batCacheid, FALSE);
		BBPdecref(joins[j+1]->batCacheid, FALSE);
		joins[j] = b;
		top--;
		for (i = j + 1; i < top; i++)
			joins[i] = joins[i + 1];
	}
	GDKfree(postpone);
	b = joins[0];
	if (b && !(b->batDirty&2)) b = BATsetaccess(b, BAT_READ);
	return b;
}

str
ALGjoinPath(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pci)
{
	int i,*bid,top=0;
	int *r = (int*) getArgReference(stk, pci, 0);
	BAT *b, **joins = (BAT**)GDKmalloc(pci->argc*sizeof(BAT*)); 
	str joinPathRef = putName("joinPath",8);
	str leftjoinPathRef = putName("leftjoinPath",12);
	int error = 0;

	if ( joins == NULL)
		throw(MAL, "algebra.joinPath", MAL_MALLOC_FAIL);
	(void)mb;
	for (i = pci->retc; i < pci->argc; i++) {
		bid = (int *) getArgReference(stk, pci, i);
		b = BATdescriptor(*bid);
		if (  b && top ) {
			if ( !(joins[top-1]->ttype == b->htype) &&
			     !(joins[top-1]->ttype == TYPE_void && b->htype == TYPE_oid) &&
			     !(joins[top-1]->ttype == TYPE_oid && b->htype == TYPE_void) ) {
				b= NULL;
				error = 1;
			}
		}
		if ( b == NULL) {
			for( --top; top>=0; top--)
				BBPreleaseref(joins[top]->batCacheid);
			GDKfree(joins);
			throw(MAL, "algebra.joinPath", error? SEMANTIC_TYPE_MISMATCH: INTERNAL_BAT_ACCESS);
		}
		joins[top++] = b;
	}
	ALGODEBUG{
		mnstr_printf(GDKout,"#joinpath ");
		printInstruction( GDKout,mb,0,pci,0);
	}
	b= ALGjoinPathBody(cntxt,top,joins, (getFunctionId(pci)== joinPathRef?1: (getFunctionId(pci) == leftjoinPathRef? 0:2)));
	GDKfree(joins);
	if ( b)
		BBPkeepref( *r = b->batCacheid);
	else
		throw(MAL, "algebra.joinPath", INTERNAL_OBJ_CREATE);
	return MAL_SUCCEED;
}
