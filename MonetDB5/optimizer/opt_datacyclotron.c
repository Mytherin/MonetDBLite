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
#include "monetdb_config.h"
#include "opt_datacyclotron.h"
#include "mal_instruction.h"

str
addRegWrap (Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr pc) {
	int *res = (int*) getArgReference(stk,pc,0);
	str *sch = (str*) getArgReference(stk,pc,1);
	str *tab = (str*) getArgReference(stk,pc,2);
	str *col = (str*) getArgReference(stk,pc,3);
	int acc = *(int*) getArgReference(stk,pc,4);
	int part = *(int*) getArgReference(stk,pc,5);
	int f_b = *(int*) getArgReference(stk,pc,6);
	int l_b = *(int*) getArgReference(stk,pc,7);

	(void) res;
	(void) cntxt;
	(void) mb;

	addRegist(*sch, *tab, *col, acc, part, f_b, l_b);

	return MAL_SUCCEED;
}

str
printRegists(void) {
	DCYcatalog *reg = NULL;
	int chunks = 0, parts = 0, j = 0;

	reg = catalog;

	while(reg) {
		chunks++;
		for (j = 0; j < reg->partitions; j++)
			printf("X1 := datacyclotron.addReg(\"%s\",\"%s\",\"%s\",%d,%d,%d,%d);\n", reg->schema, reg->table, reg->column, reg->access, reg->part_id[j], reg->f_bun[j], reg->l_bun[j]);
		parts += reg->partitions;
                reg = reg->next;
        }

	printf("The catalog for the datacyclotron optimizer contains %d chunks and %d partitions.\n", chunks, parts);

	return MAL_SUCCEED;
}

DCYcatalog*
addRegist( str sch, str tab, str col, int acc, int part, int f_bun, int l_bun ) {
	DCYcatalog *reg = NULL;

	if (!(reg = findRegist(sch, tab, col, acc))) {
		reg = GDKmalloc(sizeof(DCYcatalog));
		strcpy(reg->schema, sch);
		strcpy(reg->table, tab);
		strcpy(reg->column, col);
		reg->access = acc;
		reg->partitions = 0;
		reg->next = catalog;
		catalog = reg;

		reg->part_id = GDKmalloc(DCYPARTITIONS*sizeof(int));
		reg->f_bun = GDKmalloc(DCYPARTITIONS*sizeof(int));
		reg->l_bun = GDKmalloc(DCYPARTITIONS*sizeof(int));
	}

	if ( reg->partitions && !(reg->partitions%DCYPARTITIONS) ) {
		reg->part_id = GDKrealloc(reg->part_id, (reg->partitions + DCYPARTITIONS) *sizeof(int));
		reg->f_bun = GDKrealloc(reg->f_bun, (reg->partitions + DCYPARTITIONS) *sizeof(int));
		reg->l_bun = GDKrealloc(reg->l_bun, (reg->partitions + DCYPARTITIONS) *sizeof(int));
	}

	reg->part_id[reg->partitions] = part;
	reg->f_bun[reg->partitions] = f_bun;
	reg->l_bun[reg->partitions] = l_bun;
	reg->partitions++;

	return reg;		
}

DCYcatalog*
removePartRegist( str sch, str tab, str col, int acc, int part) {
	int i = 0;
	DCYcatalog *reg = NULL;

	if (!(reg = findRegist(sch, tab, col, acc))) 
		return NULL;

	for (i = part; i < (reg->partitions-1); i++) {
		reg->part_id[i] = reg->part_id[i+1];
		reg->f_bun[i] = reg->f_bun[i+1];
		reg->l_bun[i] = reg->l_bun[i+1];
	}

	reg->partitions--;

	return reg;		
}

int
dropRegist( str sch, str tab, str col, int acc ) {
	DCYcatalog *reg = NULL, *prev_reg = NULL;

	reg = catalog;
	
	while(reg && !(
                (strcmp(reg->schema, sch) == 0) &&
                (strcmp(reg->table, tab) == 0) &&
                (strcmp(reg->column, col) == 0) &&
                reg->access == acc
        )) {
		prev_reg = reg;
                reg = reg->next;
        }

	if (prev_reg)
		prev_reg->next = reg->next;
	else
		catalog = reg->next;
	if (reg->part_id)
		GDKfree(reg->part_id);
	if (reg->f_bun)
		GDKfree(reg->f_bun);
	if (reg->l_bun)
		GDKfree(reg->l_bun);
	GDKfree(reg);

	return 1;		
}

DCYcatalog*
findRegist( str sch, str tab, str col, int acc) {
	DCYcatalog *reg = NULL;

	reg = catalog;
	
	while(reg && !(
                (strcasecmp(reg->schema, sch) == 0) &&
                (strcasecmp(reg->table, tab) == 0) &&
                (strcasecmp(reg->column, col) == 0) &&
                reg->access == acc
        )) {
                reg = reg->next;
        }

	return reg;		
}

static int
check_instr(InstrPtr *stmt, int stop, InstrPtr p)
{
	int i;

	for (i = 0; i< stop; i++) {
		if (stmt[i] == p)
			return 1;
	}
	return 0;
}

int
OPTdatacyclotronImplementation(Client cntxt, MalBlkPtr mb, MalStkPtr stk, InstrPtr p)
{
	int i=0, actions=0, k=0, z=0, j=0, num_regs=DCYREGS, *tpes=NULL, oldtop=0,slimit=0;
	DCYcatalog **regs=NULL, *reg = NULL;
	VarRecord low,hgh,max_hgh;
	InstrPtr *old=NULL, new, matq;
	int limit=0, errors=0;
	int (*newArg)[1000]=NULL;
	char *used = NULL;
	(void) stk;
	(void) cntxt;

	if (!catalog)
		return actions;

#ifdef BIND_DATACYCLOTRON_OPT
	tpes= (int*) GDKmalloc(num_regs * sizeof(int));
	if ( tpes == NULL )
		goto out;
#endif
	used = (char*) GDKzalloc(num_regs * sizeof(char));
	newArg = GDKmalloc(sizeof(*newArg) * num_regs);
	regs = (DCYcatalog **) GDKmalloc(sizeof(*regs) * num_regs);
	if ( (used == NULL) || (newArg == NULL) || (regs == NULL) )
		goto out;

	OPTDEBUGdatacyclotron {
		mnstr_printf(cntxt->fdout,"ENTERING DATA CYCLOTRON \n");
		printFunction(cntxt->fdout,mb,0,LIST_MAL_ALL);
	}
	limit= mb->stop;
	old = mb->stmt;
	oldtop= mb->stop;
	slimit = mb->ssize;
	newMalBlkStmt(mb, mb->ssize);
	pushInstruction(mb,old[0]);

	for (i = 1; i<limit; i++) {
		p = old[i];

		if ( num_regs <= (mb->vtop + 1000) ) {
#ifdef BIND_DATACYCLOTRON_OPT
			if ( !(tpes = GDKrealloc(tpes, (num_regs + DCYREGS) * sizeof(int))) ){
				errors = 1;
				goto out;
			}
#endif
			regs = GDKrealloc(regs, (num_regs + DCYREGS) * sizeof(*regs));
			newArg = GDKrealloc(newArg, (num_regs + DCYREGS) * sizeof(*newArg));
                        used = GDKrealloc(used, (num_regs + DCYREGS) * sizeof(char));

			if ( (used == NULL) || (newArg == NULL) || (regs == NULL) ) {
				errors = 1;
				goto out;
			}

			memset(used+num_regs, 0, DCYREGS * sizeof(char));
			num_regs += DCYREGS;
		}

		if ( getModuleId(p)== sqlRef && ((getFunctionId(p) == bindRef) || (getFunctionId(p) == bindidxRef)) ){

			/*Check if the BAT is a datacyclotron BAT*/
			str sch      = getVarConstant(mb, getArg(p,2)).val.sval;
			str tab      = getVarConstant(mb, getArg(p,3)).val.sval;
			str col      = getVarConstant(mb, getArg(p,4)).val.sval;
			int acc      = getVarConstant(mb, getArg(p,5)).val.ival;

			regs[getArg(p,0)] = findRegist(sch, tab, col, acc);

			if (regs[getArg(p,0)]) {
				int tpe = TYPE_int;
				reg = regs[getArg(p,0)];
#ifndef BIND_DATACYCLOTRON_OPT
				tpe = getArgType(mb,p,0);
				matq= newInstruction(NULL,ASSIGNsymbol);
				clrFunction(matq);
				setModuleId(matq,matRef);
				setFunctionId(matq,newRef);
				getArg(matq,0)= getArg(p,0);
				hgh.value.vtype= low.value.vtype= TYPE_oid;
#else
				tpes[getArg(p,0)] = getArgType(mb,p,0);
#endif

				for (k = 0; k < reg->partitions; k++ ) {
					new = newInstruction(NULL,ASSIGNsymbol);
					setModuleId(new,datacyclotronRef);
#ifdef BIND_DATACYCLOTRON_OPT
					setFunctionId(new,bindRef);
					getArg(new,0) = newTmpVariable(mb, tpe);
#else
					setFunctionId(new,copyRef);
					new = pushArgument(mb, new, newTmpVariable(mb,TYPE_int));
					new->retc++;
					getArg(new,0) = newTmpVariable(mb, tpe);
					getArg(new,1) = newTmpVariable(mb, TYPE_int);
#endif

					new = pushStr(mb,new,reg->schema);
					new = pushStr(mb,new,reg->table);
					new = pushStr(mb,new,reg->column);
					new = pushInt(mb,new,reg->access);
					new = pushInt(mb,new,reg->part_id[k]);
					new = pushInt(mb,new,reg->f_bun[k]);
					new = pushInt(mb,new,reg->l_bun[k]);
					pushInstruction(mb,new);
					used[getArg(p,0)] = 1;
#ifdef BIND_DATACYCLOTRON_OPT
					newArg[getArg(p,0)][k] = getArg(new,0);
				}
#else
					newArg[getArg(p,0)][k] = getArg(new,1);

					low.value.val.oval= reg->f_bun[k];
					hgh.value.val.oval= reg->l_bun[k];
					if (!k) {
						varSetProp(mb, getArg(matq,0), PropertyIndex("hlb"), op_gte, (ptr) &low.value);
						max_hgh.value = hgh.value;
					}
					if (max_hgh.value.val.oval < hgh.value.val.oval)
						max_hgh.value = hgh.value;

					z = getArg(new, 0);
					varSetProp(mb, z, PropertyIndex("hlb"), op_gte, (ptr) &low.value);
					varSetProp(mb, z, PropertyIndex("hub"), op_lt, (ptr) &hgh.value);

					matq= pushArgument(mb,matq,z);
				}
				varSetProp(mb, getArg(matq,0), PropertyIndex("hub"), op_lt, (ptr) &max_hgh.value);
				pushInstruction(mb,matq);
#endif
			} else 
					pushInstruction(mb,p);
				
			actions++;
		} else { 
#ifdef BIND_DATACYCLOTRON_OPT
			for (j = p->retc; j<p->argc; j++) 
				if ( used[getArg(p,j)] == 1 ) {
					used[getArg(p,j)] = 2;
					reg = regs[getArg(p,j)];
					matq= newInstruction(NULL,ASSIGNsymbol);
					clrFunction(matq);
					setModuleId(matq,matRef);
					setFunctionId(matq,newRef);
					getArg(matq,0)= getArg(p,j);
					hgh.value.vtype= low.value.vtype= TYPE_oid;

					for (k = 0; k < reg->partitions; k++ ) {
						new = newInstruction(NULL,ASSIGNsymbol);
						setModuleId(new,datacyclotronRef);
						setFunctionId(new,pinRef);

						getArg(new,0) = newTmpVariable(mb, tpes[getArg(p,j)]);
						setVarUDFtype(mb,getArg(new,0));

						pushArgument(mb,new,newArg[getArg(p,j)][k]);
						pushInstruction(mb,new);
						low.value.val.oval= reg->f_bun[k];
						hgh.value.val.oval= reg->l_bun[k];
						if (!k) {
							varSetProp(mb, getArg(matq,0), PropertyIndex("hlb"), op_gte, (ptr) &low.value);
							max_hgh.value = hgh.value;
						}

						if (max_hgh.value.val.oval < hgh.value.val.oval)
							max_hgh.value = hgh.value;

						z = getArg(new, 0);
						varSetProp(mb, z, PropertyIndex("hlb"), op_gte, (ptr) &low.value);
						varSetProp(mb, z, PropertyIndex("hub"), op_lt, (ptr) &hgh.value);

						matq= pushArgument(mb,matq,z);
					}
					varSetProp(mb, getArg(matq,0), PropertyIndex("hub"), op_lt, (ptr) &max_hgh.value);
					pushInstruction(mb,matq);
				}
#endif
			if (functionExit(p))
				for (j = 0; j < mb->vtop; j++)
#ifdef BIND_DATACYCLOTRON_OPT
					if ( used[j] == 2) {
#else
					if ( used[j] == 1) {
#endif
						reg = regs[j];
						for (k = 0; k < reg->partitions; k++ ) {
							new= newStmt(mb,"datacyclotron","unpin");
							pushArgument(mb,new,newArg[j][k]);

						}
					}
			pushInstruction(mb,p);
		}
		
	}

out:
	OPTDEBUGdatacyclotron {
		if (errors && mb->errors)
			mnstr_printf(cntxt->fdout,"DATA CYCLOTRON FAILED\n");
		else
			mnstr_printf(cntxt->fdout,"LEAVING DATA CYCLOTRON \n");
		chkProgram(cntxt->fdout, cntxt->nspace, mb);
		printFunction(cntxt->fdout,mb,0,LIST_MAL_ALL);
	}

	if (old && (errors || mb->errors) ) {
		actions= 0;
		for(i=0; i<mb->stop; i++)
			if (mb->stmt[i])
				if (!check_instr(old, limit, mb->stmt[i]))
					freeInstruction(mb->stmt[i]);
		GDKfree(mb->stmt);
		mb->stmt = old;
		mb->ssize = slimit;
		mb->stop = oldtop;
	}

	if (old && errors == 0 && mb->errors == 0) {
		for(i=0; i < limit; i++)
			if (!check_instr(mb->stmt, mb->stop, old[i]))
				freeInstruction(old[i]);
		GDKfree(old);
	}

	if (tpes)
		GDKfree(tpes);
	if (used)
		GDKfree(used);
	if (newArg)
		GDKfree(newArg);
	if (regs) 
		GDKfree(regs);

	return actions;
}

str
DCYbind(int *ret, str *sch, str *tab, str *col, int *kind, int *part, int *fbun, int *lbun){
	(void) ret;
	(void) sch;
	(void) tab;
	(void) col;
	(void) kind;
	(void) part;
	(void) fbun;
	(void) lbun;
	throw(MAL,"datacyclotron.bind",PROGRAM_NYI);
}

str
DCYpin(int *ret, int *bid){
	(void) ret;
	(void) bid;
	throw(MAL,"datacyclotron.pin",PROGRAM_NYI);
}

str
DCYunpin(int *ret, int *bid){
	(void) ret;
	(void) bid;
	throw(MAL,"datacyclotron.unpin",PROGRAM_NYI);
}

str
DCYcopy(int *ret_bat, int *ret_id, str *sch, str *tab, str *col, int *kind, int *part, int *fbun, int *lbun){
	(void) ret_id;
	(void) ret_bat;
	(void) sch;
	(void) tab;
	(void) col;
	(void) kind;
	(void) part;
	(void) fbun;
	(void) lbun;
	throw(MAL,"datacyclotron.copy",PROGRAM_NYI);
}
