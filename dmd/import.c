
// Compiler implementation of the D programming language
// Copyright (c) 1999-2006 by Digital Mars
// All Rights Reserved
// written by Walter Bright
// http://www.digitalmars.com
// License for redistribution is by either the Artistic License
// in artistic.txt, or the GNU General Public License in gnu.txt.
// See the included readme.txt for details.

#include <stdio.h>
#include <assert.h>

#include "root.h"
#include "dsymbol.h"
#include "import.h"
#include "identifier.h"
#include "module.h"
#include "scope.h"
#include "hdrgen.h"
#include "mtype.h"
#include "declaration.h"
#include "id.h"

/********************************* Import ****************************/

Import::Import(Loc loc, Array *packages, Identifier *id, Identifier *aliasId,
	int isstatic)
    : Dsymbol(id)
{
    this->loc = loc;
    this->packages = packages;
    this->id = id;
    this->aliasId = aliasId;
    this->isstatic = isstatic;
    protection = PROTundefined;
    pkg = NULL;
    mod = NULL;

    if (aliasId)
	this->ident = aliasId;
    // Kludge to change Import identifier to first package
    else if (packages && packages->dim)
	this->ident = (Identifier *)packages->data[0];
}

void Import::addAlias(Identifier *name, Identifier *alias)
{
    if (isstatic)
	error("cannot have an import bind list");

    if (!aliasId)
	this->ident = NULL;	// make it an anonymous import

    names.push(name);
    aliases.push(alias);
}

const char *Import::kind()
{
    return isstatic ? (char *)"static import" : (char *)"import";
}

enum PROT Import::prot()
{
    return protection;
}

Dsymbol *Import::syntaxCopy(Dsymbol *s)
{
    assert(!s);

    Import *si;

    si = new Import(loc, packages, id, aliasId, isstatic);

    for (size_t i = 0; i < names.dim; i++)
    {
	si->addAlias((Identifier *)names.data[i], (Identifier *)aliases.data[i]);
    }

    return si;
}

void Import::load(Scope *sc)
{
    DsymbolTable *dst;
    Dsymbol *s;

    //printf("Import::load('%s')\n", toChars());

    // See if existing module
    dst = Package::resolve(packages, NULL, &pkg);

    s = dst->lookup(id);
    if (s)
    {
	if (s->isModule())
	    mod = (Module *)s;
	else
	    error("package and module have the same name");
    }

    if (!mod)
    {
	// Load module
	mod = Module::load(loc, packages, id);
	dst->insert(id, mod);		// id may be different from mod->ident,
					// if so then insert alias
	if (!mod->importedFrom)
	    mod->importedFrom = sc ? sc->module->importedFrom : Module::rootModule;
    }
    if (!pkg)
	pkg = mod;

    //printf("-Import::load('%s'), pkg = %p\n", toChars(), pkg);
}

char* escapePath(char* fname, char* buffer, int bufLen) {
    char* res = buffer;
    bufLen -= 2;    // for \0 and an occasional escape char
    int dst = 0;
    for (; dst < bufLen && *fname; ++dst, ++fname) {
	switch (*fname) {
	    case '(':
	    case ')':
	    case '\\':
		    buffer[dst++] = '\\';
		    // fall through

	    default:
		    buffer[dst] = *fname;
	}
    }
    buffer[dst] = '\0';
    return buffer;
}

void Import::semantic(Scope *sc)
{
    //printf("Import::semantic('%s')\n", toChars());

    load(sc);

    if (mod)
    {
#if 0
	if (mod->loc.linnum != 0)
	{   /* If the line number is not 0, then this is not
	     * a 'root' module, i.e. it was not specified on the command line.
	     */
	    mod->importedFrom = sc->module->importedFrom;
	    assert(mod->importedFrom);
	}
#endif

	// Modules need a list of each imported module
	//printf("%s imports %s\n", sc->module->toChars(), mod->toChars());
	sc->module->aimports.push(mod);

	mod->semantic();

	/* Default to private importing
	 */
	protection = sc->protection;
	if (!sc->explicitProtection)
	    protection = PROTprivate;

	if (!isstatic && !aliasId && !names.dim)
	{
	    sc->scopesym->importScope(mod, protection);
	}

	if (mod->needmoduleinfo)
	    sc->module->needmoduleinfo = 1;

	sc = sc->push(mod);
	for (size_t i = 0; i < aliasdecls.dim; i++)
	{   AliasDeclaration *ad = (AliasDeclaration *)aliasdecls.data[i];

	    //printf("\tImport alias semantic('%s')\n", s->toChars());
	    if (!mod->search(loc, (Identifier *)names.data[i], 0))
		error("%s not found", ((Identifier *)names.data[i])->toChars());

	    ad->importprot = protection;
	    ad->semantic(sc);
	}
	sc = sc->pop();
    }
    //printf("-Import::semantic('%s'), pkg = %p\n", toChars(), pkg);


    if (global.params.moduleDeps != NULL) {
	char fnameBuf[262];		// MAX_PATH+2

	OutBuffer *const ob = global.params.moduleDeps;
	ob->printf("%s (%s) : ",
	    sc->module->toPrettyChars(),
	    escapePath(sc->module->srcfile->toChars(), fnameBuf, sizeof(fnameBuf) / sizeof(*fnameBuf))
	);

	char* protStr = "";
	switch (sc->protection) {
	    case PROTpublic: protStr = "public"; break;
	    case PROTprivate: protStr = "private"; break;
	    case PROTpackage: protStr = "package"; break;
	    default: break;
	}
	ob->writestring(protStr);
	if (isstatic) {
	    ob->writestring(" static");
	}
	ob->writestring(" : ");

	if (this->packages) {
	    for (size_t i = 0; i < this->packages->dim; i++) {
		Identifier *pid = (Identifier *)this->packages->data[i];
		ob->printf("%s.", pid->toChars());
	    }
	}

	ob->printf("%s (%s)",
	    this->id->toChars(),
	    mod ? escapePath(mod->srcfile->toChars(), fnameBuf, sizeof(fnameBuf) / sizeof(*fnameBuf)) : "???"
	);

	if (aliasId) {
	    ob->printf(" -> %s", aliasId->toChars());
	} else {
	    if (names.dim > 0) {
		ob->writestring(" : ");
		for (size_t i = 0; i < names.dim; i++)
		{
		    if (i > 0) {
			ob->writebyte(',');
		    }

		    Identifier *name = (Identifier *)names.data[i];
		    Identifier *alias = (Identifier *)aliases.data[i];

		    if (!alias) {
			ob->printf("%s", name->toChars());
			alias = name;
		    } else {
			ob->printf("%s=%s", alias->toChars(), name->toChars());
		    }
		}
	    }
	}

	ob->writenl();
    }
}

void Import::semantic2(Scope *sc)
{
    //printf("Import::semantic2('%s')\n", toChars());
    mod->semantic2();
    if (mod->needmoduleinfo)
	sc->module->needmoduleinfo = 1;
}

Dsymbol *Import::toAlias()
{
    if (aliasId)
	return mod;
    return this;
}

/*****************************
 * Add import to sd's symbol table.
 */

int Import::addMember(Scope *sc, ScopeDsymbol *sd, int memnum)
{
    int result = 0;

    if (names.dim == 0)
	return Dsymbol::addMember(sc, sd, memnum);

    if (aliasId)
	result = Dsymbol::addMember(sc, sd, memnum);

    /* Instead of adding the import to sd's symbol table,
     * add each of the alias=name pairs
     */
    for (size_t i = 0; i < names.dim; i++)
    {
	Identifier *name = (Identifier *)names.data[i];
	Identifier *alias = (Identifier *)aliases.data[i];

	if (!alias)
	    alias = name;

	TypeIdentifier *tname = new TypeIdentifier(loc, name);
	AliasDeclaration *ad = new AliasDeclaration(loc, alias, tname);
	result |= ad->addMember(sc, sd, memnum);

	aliasdecls.push(ad);
    }

    return result;
}

Dsymbol *Import::search(Loc loc, Identifier *ident, int flags)
{
    //printf("%s.Import::search(ident = '%s', flags = x%x)\n", toChars(), ident->toChars(), flags);

    if (!pkg)
    {	load(NULL);
	mod->semantic();
    }

    // Forward it to the package/module
    return pkg->search(loc, ident, flags);
}

int Import::overloadInsert(Dsymbol *s)
{
    // Allow multiple imports of the same name
    return s->isImport() != NULL;
}

void Import::toCBuffer(OutBuffer *buf, HdrGenState *hgs)
{
    if (hgs->hdrgen && id == Id::object)
	return;		// object is imported by default

    if (isstatic)
	buf->writestring("static ");
    buf->writestring("import ");
    if (aliasId)
    {
	buf->printf("%s = ", aliasId->toChars());
    }
    if (packages && packages->dim)
    {
	for (size_t i = 0; i < packages->dim; i++)
	{   Identifier *pid = (Identifier *)packages->data[i];

	    buf->printf("%s.", pid->toChars());
	}
    }
    buf->printf("%s", id->toChars());
    if (names.dim > 0) {
	buf->writebyte(':');
	for (size_t i = 0; i < names.dim; i++)
	{
	    if (i > 0) {
		    buf->writebyte(',');
	    }

	    Identifier *name = (Identifier *)names.data[i];
	    Identifier *alias = (Identifier *)aliases.data[i];

	    if (!alias) {
		buf->printf("%s", name->toChars());
		alias = name;
	    } else {
		buf->printf("%s=%s", alias->toChars(), name->toChars());
	    }
	}
    }
    buf->writebyte(';');
    buf->writenl();
}

