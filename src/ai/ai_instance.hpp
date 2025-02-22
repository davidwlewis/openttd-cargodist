/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file ai_instance.hpp The AIInstance tracks an AI. */

#ifndef AI_INSTANCE_HPP
#define AI_INSTANCE_HPP

#include <squirrel.h>
#include "../script/script_instance.hpp"

/** Runtime information about an AI like a pointer to the squirrel vm and the current state. */
class AIInstance : public ScriptInstance {
public:
	AIInstance();

	/**
	 * Initialize the AI and prepare it for its first run.
	 * @param info The AI to create the instance of.
	 */
	void Initialize(class AIInfo *info);

	/* virtual */ int GetSetting(const char *name);
	/* virtual */ ScriptInfo *FindLibrary(const char *library, int version);

private:
	const char *versionAPI; ///< Current API used by this script.

	/* virtual */ void RegisterAPI();
	/* virtual */ void Died();
	/* virtual */ CommandCallback *GetDoCommandCallback();
	/* virtual */ void LoadDummyScript();

	/**
	 * Load squirrel scripts to emulate an older API.
	 */
	bool LoadCompatibilityScripts(const char *api_version);
};

#endif /* AI_INSTANCE_HPP */
