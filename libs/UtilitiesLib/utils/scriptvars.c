// scriptvars.c - provides a random variable system for use with story arc
// scripts.  

#include "scriptvars.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "error.h"
#include "textparser.h"
#include "HashFunctions.h"


#define VARCMP stricmp // vars are case-insensitive

//////////////////////////////////////////////////////////// rule-30 based random function for script vars
static U32 sv_c = 0xf244f343;

// from rule30
unsigned int svRoll(unsigned int seed, unsigned int mod)
{
	unsigned int l,r;
	sv_c = seed;
	l = r = sv_c;
	l = _lrotr(l, 1);	/* bit i of l equals bit just left of bit i in c */
	r = _lrotl(r, 1);	/* bit i of r euqals bit just right of bit i in c */
	sv_c |= r;
	sv_c ^= l;           /* c = l xor (c or r), aka rule 30, named by wolfram */
	return sv_c % mod;
}

////////////////////////////////////////////////////// VarXxx utility functions for looking up var info
// these functions kind of define the var syntax of: var VAR_NAME [optional group] = VALUE1, VALUE2

char* VarName(TokenizerParams* var)
{
	if (eaSize(&var->params)) return var->params[0];
	return NULL;
}

// as VarGroupName, but printf errors if any syntax problems
// here is where we verify '=' placement, 
char* VarVerifyGroupName(TokenizerParams* var, int showerr)
{
	int nump = eaSize(&var->params);

	// verify minimum lengths
	if (nump < 1)
	{
		if (showerr) printf("VarGroupName: empty var line detected\n");
		return NULL;
	}
	if (nump < 3) 
	{
		if (showerr) printf("VarGroupName: var %s has an invalid syntax\n", var->params[0]);
		return NULL;
	}

	// look for the equals
	if (!strcmp(var->params[1], "=")) // if equals in second position
	{
		return NULL;
	}
	if (strcmp(var->params[2], "=")) // if we don't have equals in third position
	{
		if (showerr) printf("VarGroupName: couldn't find equals sign for var %s\n", var->params[0]);
		return NULL;
	}

	// we have a group name and an equals sign in third position
	if (nump < 4)
	{
		if (showerr) printf("VarGroupName: var %s has no values", var->params[0]);
		return NULL;
	}
	return var->params[1];
}

// return the group name or NULL if we don't have one
char* VarGroupName(TokenizerParams* var)
{
	return VarVerifyGroupName(var,0);
}

int VarNumValues(TokenizerParams* var)
{
	int result = eaSize(&var->params) - 2;		// it takes two positions for name of var and "="
	if (VarGroupName(var)) result--;				// if we have a group, we lose a space for that
	if (result < 0) return 0;
	return result;
}

char* VarValue(TokenizerParams* var, int index)
{
	int numvalues = VarNumValues(var);
	if (index >= numvalues) return NULL;
	if (VarGroupName(var)) index++;
	return var->params[index+2];
}

///////////////////////////////////////////////////// functions related to looking up var values based on scope

// see if the passed group name must be tied to a particular value because of
// the contents of the last scope.  0-based index of correct value returned,
// or -1 otherwise.
//
// DeriveGroupForce lets me do something kind of weird and specific to 
// ScriptVars.  If you have grouped random variables like this:
//
// vargroup depends_on_victim
// var VICTIM_NAME depends_on_victim = "Old Lady", "Pirate"
// var VICTIM_MODEL depends_on_victim = eGranny, ePirate
// 
// at then at a lower scope, you explicity set:
// 
// var VICTIM_MODEL = eGranny.
// 
// I want a lookup of VICTIM_NAME to notice that the group "depends_on_victim" 
// was forced to 0, and make sure to return "Old Lady".
//
// This forcing only works one level deep, and DeriveGroupForce uses the 
// next lower scope to see if a specific group was forced to a value.
int DeriveGroupForce(ScriptVarsScope* currentscope, ScriptVarsScope* overridescope, char* groupname)
{
	int v, subv, val;
	int retval = -1; // assume we don't have a valid override

	// cycle through vars in current scope, looking for ones attached to group
	for (v = 0; v < eaSize(&currentscope->vars); v++)
	{
		TokenizerParams* var = currentscope->vars[v];
		char* varname = VarName(var);
		if (VARCMP(groupname, VarGroupName(var))) continue; // only look at vars attached to same group

		// look through vars that may be overrides
		for (subv = 0; subv < eaSize(&overridescope->vars); subv++)
		{
			TokenizerParams* subvar = overridescope->vars[subv];
			char* subvarname = VarName(subvar);
			char* subvarvalue = VarValue(subvar, 0);
			if (VARCMP(varname, subvarname)) // only look at overrides for this variable

			if (VarNumValues(subvar) != 1) goto DeriveGroupForce_fail;	// we tried to override with a random list
		
			// try to match the override value with a value in the random list
			for (val = 0; val < VarNumValues(var); val++)
			{
				if (!VARCMP(subvarvalue, VarValue(var, val))) // we found a match
				{
					if (retval == -1)
					{
						retval = val;
						break;
					}
					else if (retval == val)
					{
						break; // ok
					}
					else
					{
						goto DeriveGroupForce_fail;	// hit a conflicting override
					}
				}
			}
			// we're ok if we don't match any, just ignore and don't force a group selection yet
		} // every var in overridescope
	} // every var in current scope

	return retval;
DeriveGroupForce_fail:	// we found a confusing override condition, and don't need to check any others
	return -1;
}


// internal version, supports chaining and forced variable groups from the last scope
char* InnerScriptVarsLookup(ScriptVarsScope* currentscope, ScriptVarsScope* overridescope, char* str, unsigned int rseed)
{
	int i;
	int numvars;
	int select;
	int found;
	int groupforce;
	char* hashkeystring;
	unsigned int varseed;

	if (!str) return NULL;
	if (!str[0]) return str;

	// look for a match for str (first one)
	numvars = eaSize(&currentscope->vars);
	found = 0;
	for (i = 0; i < numvars; i++)
	{
		if (!VARCMP(str, VarName(currentscope->vars[i])))
		{
			found = 1;
			break;
		}
	}

	// found var, figure out result
	if (found)
	{
		// check to see if we are bound to a random group
		char* groupname = VarGroupName(currentscope->vars[i]);
		int numvalues = VarNumValues(currentscope->vars[i]);

		// if we're grouped, see if we have a forced value
		groupforce = -1;
		if (groupname && overridescope)
		{
			groupforce = DeriveGroupForce(currentscope, overridescope, groupname);
		}

		// the hashkeystring is either our name, or the name of our random group
		if (groupname) hashkeystring = groupname;
		else hashkeystring = VarName(currentscope->vars[i]);

		if (numvalues <= 0) 
		{
			//printf("ScriptVarsLookup: not enough selections!\n");
			return str;
		}
		else if (numvalues == 1)
		{
			select = 0;
		}
		else if (groupforce >= 0)
		{
			select = groupforce;
		}
		else // > 1 selection possible
		{
			varseed = rseed ^ hashCalc(hashkeystring, (U32)strlen(hashkeystring), DEFAULT_HASH_SEED);
			select = svRoll(varseed, numvalues);
		}
		
		// Don't allow variables to chain to themselves.
		if( VARCMP(VarName(currentscope->vars[i]),VarValue(currentscope->vars[i], select)) )
			return ScriptVarsLookup(currentscope, VarValue(currentscope->vars[i], select), rseed);
		else
			return str;
	}

	// didn't find the var at the current scope..
	if (currentscope->higherscope) return ScriptVarsLookup(currentscope->higherscope, str, rseed);
	else return str;
}

// looks for match through hierarchy, chains variables for further lookups, may return <str> if no matches
// ScriptVarsLookup is passed a seed value for the random generator, so the user can set up this value and 
// consistently get the same random variables.  The seed is xor'd with a hash of the variable name or 
// the group name, and a random selection made.  
char* ScriptVarsLookup(ScriptVarsScope* currentscope, char* str, unsigned int rseed)
{
	return InnerScriptVarsLookup(currentscope, NULL, str, rseed);
}

int ScriptVarsLookupInt(ScriptVarsScope* currentscope, char* str, unsigned int rseed)
{
	char* processed = ScriptVarsLookup(currentscope, str, rseed);
	if (!processed) return 0;
	return atoi(processed);
}

////////////////////////////////////////////////////////////////// verify syntax
void ScriptVarsVerifySyntax(ScriptVarsScope* currentscope, VerifyVarNameFunc varnamefunc, char* filename)
{
	int i, j;

	// verify syntax of groupnames
	for (i = 0; i < eaSize(&currentscope->randomgroups); i++)
	{
		if (eaSize(&currentscope->randomgroups[i]->params) < 1)
		{
			FatalErrorf("ScriptVarsVerifySyntax: error, missing group name\n");
		}
		else if (eaSize(&currentscope->randomgroups[i]->params) > 1)
		{
			FatalErrorf("ScriptVarsVerifySyntax: error, extra parameters on group %s\n", 
				currentscope->randomgroups[i]->params[0]);
		}
	}

	// verify syntax of each var
	for (i = 0; i < eaSize(&currentscope->vars); i++)
	{
		int found = 0;
		TokenizerParams* var = currentscope->vars[i];
		char* varname = VarName(var);
		char* groupname = VarVerifyGroupName(var, 1);	// prints errors in = placement

		// verify name of variable
		if (varnamefunc && varname && !varnamefunc(varname))
		{
			ErrorFilenamef(filename, "Invalid variable name %s", varname);
		}

		// verify groupname exists in list of groupnames
		if (!groupname) continue;
		for (j = 0; j < eaSize(&currentscope->randomgroups); j++)
		{
			TokenizerParams* group = currentscope->randomgroups[j];
			if (!VARCMP(groupname, VarName(group)))
			{
				if (found)
				{
					FatalErrorf("ScriptVarsVerifySyntax: error, duplicate group name %s\n", groupname);
				}
				found = 1;
			}
		}
		if (!found)
		{
			FatalErrorf("ScriptVarsVerifySyntax: error, missing group name definiton for %s\n", groupname);
		}
	}
}

// add any possibilities for str to resultlist
// .. a lot like ScriptVarsLookup, but without the random complications
void ScriptVarsLookupCompleteAdd(ScriptVarsScope* currentscope, char* str, char*** resultlist)
{
	int i;
	int numvars;
	int found;

	if (!str || !str[0]) return;

	// look for a match for str (first one)
	numvars = eaSize(&currentscope->vars);
	found = 0;
	for (i = 0; i < numvars; i++)
	{
		if (!VARCMP(str, VarName(currentscope->vars[i])))
		{
			found = 1;
			break;
		}
	}

	// found var, recurse for each value
	if (found)
	{
		int j;
		int numvalues = VarNumValues(currentscope->vars[i]);
		for (j = 0; j < numvalues; j++)
		{
			ScriptVarsLookupCompleteAdd(currentscope, VarValue(currentscope->vars[i], j), resultlist);
		}
	}
	else
	{
		// didn't find the var at the current scope..
		if (currentscope->higherscope) ScriptVarsLookupCompleteAdd(currentscope->higherscope, str, resultlist);
		else
		{
			eaPush(resultlist, _strdup(str));
		}
	}
}

// external function to get a complete list of every string str could turn into
void ScriptVarsLookupComplete(ScriptVarsScope* currentscope, char* str, char*** resultlist)
{
	eaCreate(resultlist);
	ScriptVarsLookupCompleteAdd(currentscope, str, resultlist);
}

// just free the strings and structure we allocated in LookupComplete
void ScriptVarsFreeList(char*** resultlist)
{
	int i;
	for (i = 0; i < eaSize(resultlist); i++)
	{
		free((*resultlist)[i]);
	}
	eaDestroy(resultlist);
	*resultlist = NULL;
}

////////////////////////////////////////////////////////////////////////// ScriptVarTable
void ScriptVarsTableClear(ScriptVarsTable* table)
{
	ScriptVarsTableClearVars(table);
	memset(table, 0, sizeof(ScriptVarsTable));
}

void ScriptVarsTablePushScope(ScriptVarsTable* table, ScriptVarsScope* scope)
{
	if (table->numscopes >= MAX_VARSTABLE_SCOPES) { FatalErrorf("Too many scopes!"); return; }
	table->scopes[table->numscopes++] = scope;
}

ScriptVarsScope* ScriptVarsTablePopScope(ScriptVarsTable* table)
{
	if (table->numscopes == 0) return NULL;
	return table->scopes[--table->numscopes];
}

void ScriptVarsTablePushVar(ScriptVarsTable* table, char* key, char* value)
{
	if (!key || !value) return;
	if (table->numvars >= MAX_VARSTABLE_VARS) { FatalErrorf("Too many variables!"); return; }
	table->keys[table->numvars] = key;
	table->values[table->numvars] = value;
	table->types[table->numvars] = 's';
	table->numvars++;
}

void ScriptVarsTablePushVarEx(ScriptVarsTable* table, char* key, void* value, char type, U8 flags)
{
	ScriptVarsTablePushVar(table, key, (char*) value);
	table->types[table->numvars-1] = type;
	table->flags[table->numvars-1] = flags;
	if (flags & SV_COPYVALUE)
	{
		table->values[table->numvars-1] = strdup(table->values[table->numvars-1]);
	}
}

void ScriptVarsTablePopVar(ScriptVarsTable* table)
{

	if (table->numvars) table->numvars--;
	
	assert((table->numvars >= 0) && (table->numvars <MAX_VARSTABLE_VARS));

	if (table->flags[table->numvars] & SV_COPYVALUE)
	{
		free(table->values[table->numvars]);
		table->values[table->numvars] = 0;
	}
}

void ScriptVarsTableClearVars(ScriptVarsTable* table)
{
	int i;
	for (i = 0; i < table->numvars; i++)
		if (table->flags[i] & SV_COPYVALUE)
			free(table->values[i]);
	table->numvars = 0;
}

void ScriptVarsTableSetSeed(ScriptVarsTable* table, unsigned int rseed)
{
	table->seed = rseed;
}

void ScriptVarsTableCopy(ScriptVarsTable* target, ScriptVarsTable* source)
{
	int i;
	memcpy(target, source, sizeof(ScriptVarsTable));
	for (i = 0; i < target->numvars; i++)
	{
		if (target->flags[i] & SV_COPYVALUE)
		{
			target->values[i] = strdup(target->values[i]);
		}
	}
}

// look in our simplified var list first, then pass through each scope
char* ScriptVarsTableLookup(ScriptVarsTable* table, char* str)
{
	int i;
	char * orig;
	if (!str) return NULL;
svtlredo:
	for (i = table->numvars-1; i >= 0; i--)
	{
		if (table->types[i] == 's' && !VARCMP(table->keys[i], str))
		{
			str = table->values[i];
			goto svtlredo;
		}
	}
	orig=str;
	for (i = table->numscopes-1; i >= 0; i--)
	{
		str = ScriptVarsLookup(table->scopes[i], str, table->seed);
		if (str!=orig)
			goto svtlredo;
	}
	
	return str;
}

void* ScriptVarsTableLookupTyped(ScriptVarsTable* table, char* str, char* type)
{
	int i;
	char * orig;
	if (!str) return NULL;
	
svtltredo:
	*type = 's'; // default is a string
	for (i = table->numvars-1; i >= 0; i--)
	{
		if (!VARCMP(table->keys[i], str))
		{
			str = table->values[i];
			*type = table->types[i];
			if (*type!='s')
				return (void*)str;
			goto svtltredo;
		}
	}
	orig=str;
	for (i = table->numscopes-1; i >= 0; i--)
	{
		str = ScriptVarsLookup(table->scopes[i], str, table->seed); //TODO: deal with this? does it ever change str if str was already found?
		if (str!=orig)
			goto svtltredo;
	}
	return (void*)str;
}

// TODO: replace everything with four byte chunks to be either an int or pointer to something, then cast to type
int ScriptVarsTableLookupInt(ScriptVarsTable* table, char* str)
{
	char* processed = ScriptVarsTableLookup(table, str);
	if (!processed) return 0;
	return atoi(processed);
}