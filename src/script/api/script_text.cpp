/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_text.cpp Implementation of ScriptText. */

#include "../../stdafx.h"
#include "../../string_func.h"
#include "script_text.hpp"
#include "../squirrel.hpp"
#include "../../table/control_codes.h"

ScriptText::ScriptText(HSQUIRRELVM vm) :
	ZeroedMemoryAllocator()
{
	int nparam = sq_gettop(vm) - 1;
	if (nparam < 1) {
		throw sq_throwerror(vm, _SC("You need to pass at least a StringID to the constructor"));
	}

	/* First resolve the StringID. */
	SQInteger sqstring;
	if (SQ_FAILED(sq_getinteger(vm, 2, &sqstring))) {
		throw sq_throwerror(vm, _SC("First argument must be a valid StringID"));
	}
	this->string = sqstring;

	/* The rest of the parameters must be arguments. */
	for (int i = 0; i < nparam - 1; i++) {
		/* Push the parameter to the top of the stack. */
		sq_push(vm, i + 3);

		if (SQ_FAILED(this->_SetParam(i, vm))) {
			this->~ScriptText();
			throw sq_throwerror(vm, _SC("Invalid parameter"));
		}

		/* Pop the parameter again. */
		sq_pop(vm, 1);
	}
}

ScriptText::~ScriptText()
{
	for (int i = 0; i < SCRIPT_TEXT_MAX_PARAMETERS; i++) {
		free(this->params[i]);
		if (this->paramt[i] != NULL) this->paramt[i]->Release();
	}
}

SQInteger ScriptText::_SetParam(int parameter, HSQUIRRELVM vm)
{
	if (parameter >= SCRIPT_TEXT_MAX_PARAMETERS) return SQ_ERROR;

	free(this->params[parameter]);
	if (this->paramt[parameter] != NULL) this->paramt[parameter]->Release();

	this->parami[parameter] = 0;
	this->params[parameter] = NULL;
	this->paramt[parameter] = NULL;

	switch (sq_gettype(vm, -1)) {
		case OT_STRING: {
			const SQChar *value;
			sq_getstring(vm, -1, &value);

			this->params[parameter] = strdup(SQ2OTTD(value));
			break;
		}

		case OT_INTEGER: {
			SQInteger value;
			sq_getinteger(vm, -1, &value);

			this->parami[parameter] = value;
			break;
		}

		case OT_INSTANCE: {
			SQUserPointer real_instance = NULL;
			HSQOBJECT instance;

			sq_getstackobj(vm, -1, &instance);

			/* Validate if it is a GSText instance */
			sq_pushroottable(vm);
			sq_pushstring(vm, _SC("GSText"), -1);
			sq_get(vm, -2);
			sq_pushobject(vm, instance);
			if (sq_instanceof(vm) != SQTrue) return SQ_ERROR;
			sq_pop(vm, 3);

			/* Get the 'real' instance of this class */
			sq_getinstanceup(vm, -1, &real_instance, 0);
			if (real_instance == NULL) return SQ_ERROR;

			ScriptText *value = static_cast<ScriptText *>(real_instance);
			value->AddRef();
			this->paramt[parameter] = value;
			break;
		}

		default: return SQ_ERROR;
	}

	if (this->paramc <= parameter) this->paramc = parameter + 1;
	return 0;
}

SQInteger ScriptText::SetParam(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) != OT_INTEGER) return SQ_ERROR;

	SQInteger k;
	sq_getinteger(vm, 2, &k);

	if (k > SCRIPT_TEXT_MAX_PARAMETERS) return SQ_ERROR;
	if (k < 1) return SQ_ERROR;
	k--;

	return this->_SetParam(k, vm);
}

SQInteger ScriptText::AddParam(HSQUIRRELVM vm)
{
	SQInteger res;
	res = this->_SetParam(this->paramc, vm);
	if (res != 0) return res;

	/* Push our own instance back on top of the stack */
	sq_push(vm, 1);
	return 1;
}

SQInteger ScriptText::_set(HSQUIRRELVM vm)
{
	int32 k;

	if (sq_gettype(vm, 2) == OT_STRING) {
		const SQChar *key;
		sq_getstring(vm, 2, &key);
		const char *key_string = SQ2OTTD(key);

		if (strncmp(key_string, "param_", 6) != 0 || strlen(key_string) > 8) return SQ_ERROR;
		k = atoi(key_string + 6);
	} else if (sq_gettype(vm, 2) == OT_INTEGER) {
		SQInteger key;
		sq_getinteger(vm, 2, &key);
		k = (int32)key;
	} else {
		return SQ_ERROR;
	}

	if (k > SCRIPT_TEXT_MAX_PARAMETERS) return SQ_ERROR;
	if (k < 1) return SQ_ERROR;
	k--;

	return this->_SetParam(k, vm);
}

const char *ScriptText::GetEncodedText()
{
	static char buf[1024];
	this->_GetEncodedText(buf, lastof(buf));
	return buf;
}

char *ScriptText::_GetEncodedText(char *p, char *lastofp)
{
	p += Utf8Encode(p, SCC_ENCODED);
	p += seprintf(p, lastofp, "%X", this->string);
	for (int i = 0; i < this->paramc; i++) {
		if (this->params[i] != NULL) {
			p += seprintf(p, lastofp, ":\"%s\"", this->params[i]);
			continue;
		}
		if (this->paramt[i] != NULL) {
			p += seprintf(p, lastofp, ":");
			p = this->paramt[i]->_GetEncodedText(p, lastofp);
			continue;
		}
		p += seprintf(p, lastofp,":%X", this->parami[i]);
	}

	return p;
}
