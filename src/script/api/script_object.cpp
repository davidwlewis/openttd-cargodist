/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_object.cpp Implementation of ScriptObject. */

#include "../../stdafx.h"
#include "../../script/squirrel.hpp"
#include "../../command_func.h"
#include "../../company_func.h"
#include "../../company_base.h"
#include "../../network/network.h"
#include "../../tunnelbridge.h"
#include "../../genworld.h"
#include "../../goal_type.h"

#include "../script_storage.hpp"
#include "../script_instance.hpp"
#include "../script_fatalerror.hpp"
#include "../script_suspend.hpp"
#include "script_error.hpp"

/**
 * Get the storage associated with the current ScriptInstance.
 * @return The storage.
 */
static ScriptStorage *GetStorage()
{
	return ScriptObject::GetActiveInstance()->GetStorage();
}


/* static */ ScriptInstance *ScriptObject::ActiveInstance::active = NULL;

ScriptObject::ActiveInstance::ActiveInstance(ScriptInstance *instance)
{
	this->last_active = ScriptObject::ActiveInstance::active;
	ScriptObject::ActiveInstance::active = instance;
}

ScriptObject::ActiveInstance::~ActiveInstance()
{
	ScriptObject::ActiveInstance::active = this->last_active;
}

/* static */ ScriptInstance *ScriptObject::GetActiveInstance()
{
	assert(ScriptObject::ActiveInstance::active != NULL);
	return ScriptObject::ActiveInstance::active;
}


/* static */ void ScriptObject::SetDoCommandDelay(uint ticks)
{
	assert(ticks > 0);
	GetStorage()->delay = ticks;
}

/* static */ uint ScriptObject::GetDoCommandDelay()
{
	return GetStorage()->delay;
}

/* static */ void ScriptObject::SetDoCommandMode(ScriptModeProc *proc, ScriptObject *instance)
{
	GetStorage()->mode = proc;
	GetStorage()->mode_instance = instance;
}

/* static */ ScriptModeProc *ScriptObject::GetDoCommandMode()
{
	return GetStorage()->mode;
}

/* static */ ScriptObject *ScriptObject::GetDoCommandModeInstance()
{
	return GetStorage()->mode_instance;
}

/* static */ void ScriptObject::SetDoCommandCosts(Money value)
{
	GetStorage()->costs = CommandCost(value);
}

/* static */ void ScriptObject::IncreaseDoCommandCosts(Money value)
{
	GetStorage()->costs.AddCost(value);
}

/* static */ Money ScriptObject::GetDoCommandCosts()
{
	return GetStorage()->costs.GetCost();
}

/* static */ void ScriptObject::SetLastError(ScriptErrorType last_error)
{
	GetStorage()->last_error = last_error;
}

/* static */ ScriptErrorType ScriptObject::GetLastError()
{
	return GetStorage()->last_error;
}

/* static */ void ScriptObject::SetLastCost(Money last_cost)
{
	GetStorage()->last_cost = last_cost;
}

/* static */ Money ScriptObject::GetLastCost()
{
	return GetStorage()->last_cost;
}

/* static */ void ScriptObject::SetRoadType(RoadType road_type)
{
	GetStorage()->road_type = road_type;
}

/* static */ RoadType ScriptObject::GetRoadType()
{
	return GetStorage()->road_type;
}

/* static */ void ScriptObject::SetRailType(RailType rail_type)
{
	GetStorage()->rail_type = rail_type;
}

/* static */ RailType ScriptObject::GetRailType()
{
	return GetStorage()->rail_type;
}

/* static */ void ScriptObject::SetLastCommandRes(bool res)
{
	GetStorage()->last_command_res = res;
	/* Also store the results of various global variables */
	SetNewVehicleID(_new_vehicle_id);
	SetNewSignID(_new_sign_id);
	SetNewGroupID(_new_group_id);
	SetNewGoalID(_new_goal_id);
}

/* static */ bool ScriptObject::GetLastCommandRes()
{
	return GetStorage()->last_command_res;
}

/* static */ void ScriptObject::SetNewVehicleID(VehicleID vehicle_id)
{
	GetStorage()->new_vehicle_id = vehicle_id;
}

/* static */ VehicleID ScriptObject::GetNewVehicleID()
{
	return GetStorage()->new_vehicle_id;
}

/* static */ void ScriptObject::SetNewSignID(SignID sign_id)
{
	GetStorage()->new_sign_id = sign_id;
}

/* static */ SignID ScriptObject::GetNewSignID()
{
	return GetStorage()->new_sign_id;
}

/* static */ void ScriptObject::SetNewGroupID(GroupID group_id)
{
	GetStorage()->new_group_id = group_id;
}

/* static */ GroupID ScriptObject::GetNewGroupID()
{
	return GetStorage()->new_group_id;
}

/* static */ void ScriptObject::SetNewGoalID(GoalID goal_id)
{
	GetStorage()->new_goal_id = goal_id;
}

/* static */ GroupID ScriptObject::GetNewGoalID()
{
	return GetStorage()->new_goal_id;
}

/* static */ void ScriptObject::SetAllowDoCommand(bool allow)
{
	GetStorage()->allow_do_command = allow;
}

/* static */ bool ScriptObject::GetAllowDoCommand()
{
	return GetStorage()->allow_do_command;
}

/* static */ void ScriptObject::SetCompany(CompanyID company)
{
	if (GetStorage()->root_company == INVALID_OWNER) GetStorage()->root_company = company;
	GetStorage()->company = company;

	_current_company = company;
}

/* static */ CompanyID ScriptObject::GetCompany()
{
	return GetStorage()->company;
}

/* static */ CompanyID ScriptObject::GetRootCompany()
{
	return GetStorage()->root_company;
}

/* static */ bool ScriptObject::CanSuspend()
{
	Squirrel *squirrel = ScriptObject::GetActiveInstance()->engine;
	return GetStorage()->allow_do_command && squirrel->CanSuspend();
}

/* static */ void *&ScriptObject::GetEventPointer()
{
	return GetStorage()->event_data;
}

/* static */ void *&ScriptObject::GetLogPointer()
{
	return GetStorage()->log_data;
}

/* static */ void ScriptObject::SetCallbackVariable(int index, int value)
{
	if ((size_t)index >= GetStorage()->callback_value.size()) GetStorage()->callback_value.resize(index + 1);
	GetStorage()->callback_value[index] = value;
}

/* static */ int ScriptObject::GetCallbackVariable(int index)
{
	return GetStorage()->callback_value[index];
}

/* static */ bool ScriptObject::DoCommand(TileIndex tile, uint32 p1, uint32 p2, uint cmd, const char *text, Script_SuspendCallbackProc *callback)
{
	if (!ScriptObject::CanSuspend()) {
		throw Script_FatalError("You are not allowed to execute any DoCommand (even indirect) in your constructor, Save(), Load(), and any valuator.");
	}

	if (ScriptObject::GetCompany() != OWNER_DEITY && !::Company::IsValidID(ScriptObject::GetCompany())) {
		ScriptObject::SetLastError(ScriptError::ERR_PRECONDITION_INVALID_COMPANY);
		return false;
	}

	/* Set the default callback to return a true/false result of the DoCommand */
	if (callback == NULL) callback = &ScriptInstance::DoCommandReturn;

	/* Are we only interested in the estimate costs? */
	bool estimate_only = GetDoCommandMode() != NULL && !GetDoCommandMode()();

#ifdef ENABLE_NETWORK
	/* Only set p2 when the command does not come from the network. */
	if (GetCommandFlags(cmd) & CMD_CLIENT_ID && p2 == 0) p2 = UINT32_MAX;
#endif

	/* Try to perform the command. */
	CommandCost res = ::DoCommandPInternal(tile, p1, p2, cmd, (_networking && !_generating_world) ? ScriptObject::GetActiveInstance()->GetDoCommandCallback() : NULL, text, false, estimate_only);

	/* We failed; set the error and bail out */
	if (res.Failed()) {
		SetLastError(ScriptError::StringToError(res.GetErrorMessage()));
		return false;
	}

	/* No error, then clear it. */
	SetLastError(ScriptError::ERR_NONE);

	/* Estimates, update the cost for the estimate and be done */
	if (estimate_only) {
		IncreaseDoCommandCosts(res.GetCost());
		return true;
	}

	/* Costs of this operation. */
	SetLastCost(res.GetCost());
	SetLastCommandRes(true);

	if (_generating_world) {
		IncreaseDoCommandCosts(res.GetCost());
		if (callback != NULL) callback(GetActiveInstance());
		return true;
	} else if (_networking) {
		/* Suspend the AI till the command is really executed. */
		throw Script_Suspend(-(int)GetDoCommandDelay(), callback);
	} else {
		IncreaseDoCommandCosts(res.GetCost());

		/* Suspend the AI player for 1+ ticks, so it simulates multiplayer. This
		 *  both avoids confusion when a developer launched his AI in a
		 *  multiplayer game, but also gives time for the GUI and human player
		 *  to interact with the game. */
		throw Script_Suspend(GetDoCommandDelay(), callback);
	}

	NOT_REACHED();
}
