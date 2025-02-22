/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_group.cpp Implementation of ScriptGroup. */

#include "../../stdafx.h"
#include "script_group.hpp"
#include "script_engine.hpp"
#include "../script_instance.hpp"
#include "../../company_func.h"
#include "../../group.h"
#include "../../string_func.h"
#include "../../strings_func.h"
#include "../../autoreplace_func.h"
#include "../../settings_func.h"
#include "table/strings.h"

/* static */ bool ScriptGroup::IsValidGroup(GroupID group_id)
{
	const Group *g = ::Group::GetIfValid(group_id);
	return g != NULL && g->owner == ScriptObject::GetCompany();
}

/* static */ ScriptGroup::GroupID ScriptGroup::CreateGroup(ScriptVehicle::VehicleType vehicle_type)
{
	if (!ScriptObject::DoCommand(0, (::VehicleType)vehicle_type, 0, CMD_CREATE_GROUP, NULL, &ScriptInstance::DoCommandReturnGroupID)) return GROUP_INVALID;

	/* In case of test-mode, we return GroupID 0 */
	return (ScriptGroup::GroupID)0;
}

/* static */ bool ScriptGroup::DeleteGroup(GroupID group_id)
{
	EnforcePrecondition(false, IsValidGroup(group_id));

	return ScriptObject::DoCommand(0, group_id, 0, CMD_DELETE_GROUP);
}

/* static */ ScriptVehicle::VehicleType ScriptGroup::GetVehicleType(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return ScriptVehicle::VT_INVALID;

	return (ScriptVehicle::VehicleType)((::VehicleType)::Group::Get(group_id)->vehicle_type);
}

/* static */ bool ScriptGroup::SetName(GroupID group_id, Text *name)
{
	CCountedPtr<Text> counter(name);

	EnforcePrecondition(false, IsValidGroup(group_id));
	EnforcePrecondition(false, name != NULL);
	const char *text = name->GetEncodedText();
	EnforcePrecondition(false, !::StrEmpty(text));
	EnforcePreconditionCustomError(false, ::Utf8StringLength(text) < MAX_LENGTH_GROUP_NAME_CHARS, ScriptError::ERR_PRECONDITION_STRING_TOO_LONG);

	return ScriptObject::DoCommand(0, group_id, 0, CMD_RENAME_GROUP, text);
}

/* static */ char *ScriptGroup::GetName(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return NULL;

	static const int len = 64;
	char *group_name = MallocT<char>(len);

	::SetDParam(0, group_id);
	::GetString(group_name, STR_GROUP_NAME, &group_name[len - 1]);
	return group_name;
}

/* static */ bool ScriptGroup::EnableAutoReplaceProtection(GroupID group_id, bool enable)
{
	EnforcePrecondition(false, IsValidGroup(group_id));

	return ScriptObject::DoCommand(0, group_id, enable ? 1 : 0, CMD_SET_GROUP_REPLACE_PROTECTION);
}

/* static */ bool ScriptGroup::GetAutoReplaceProtection(GroupID group_id)
{
	if (!IsValidGroup(group_id)) return false;

	return ::Group::Get(group_id)->replace_protection;
}

/* static */ int32 ScriptGroup::GetNumEngines(GroupID group_id, EngineID engine_id)
{
	if (!IsValidGroup(group_id) && group_id != GROUP_DEFAULT && group_id != GROUP_ALL) return -1;

	return GetGroupNumEngines(ScriptObject::GetCompany(), group_id, engine_id);
}

/* static */ bool ScriptGroup::MoveVehicle(GroupID group_id, VehicleID vehicle_id)
{
	EnforcePrecondition(false, IsValidGroup(group_id) || group_id == GROUP_DEFAULT);
	EnforcePrecondition(false, ScriptVehicle::IsValidVehicle(vehicle_id));

	return ScriptObject::DoCommand(0, group_id, vehicle_id, CMD_ADD_VEHICLE_GROUP);
}

/* static */ bool ScriptGroup::EnableWagonRemoval(bool enable_removal)
{
	if (HasWagonRemoval() == enable_removal) return true;

	return ScriptObject::DoCommand(0, ::GetCompanySettingIndex("company.renew_keep_length"), enable_removal ? 1 : 0, CMD_CHANGE_COMPANY_SETTING);
}

/* static */ bool ScriptGroup::HasWagonRemoval()
{
	return ::Company::Get(ScriptObject::GetCompany())->settings.renew_keep_length;
}

/* static */ bool ScriptGroup::SetAutoReplace(GroupID group_id, EngineID engine_id_old, EngineID engine_id_new)
{
	EnforcePrecondition(false, IsValidGroup(group_id) || group_id == GROUP_DEFAULT || group_id == GROUP_ALL);
	EnforcePrecondition(false, ScriptEngine::IsBuildable(engine_id_new));

	return ScriptObject::DoCommand(0, group_id << 16, (engine_id_new << 16) | engine_id_old, CMD_SET_AUTOREPLACE);
}

/* static */ EngineID ScriptGroup::GetEngineReplacement(GroupID group_id, EngineID engine_id)
{
	if (!IsValidGroup(group_id) && group_id != GROUP_DEFAULT && group_id != GROUP_ALL) return ::INVALID_ENGINE;

	return ::EngineReplacementForCompany(Company::Get(ScriptObject::GetCompany()), engine_id, group_id);
}

/* static */ bool ScriptGroup::StopAutoReplace(GroupID group_id, EngineID engine_id)
{
	EnforcePrecondition(false, IsValidGroup(group_id) || group_id == GROUP_DEFAULT || group_id == GROUP_ALL);

	return ScriptObject::DoCommand(0, group_id << 16, (::INVALID_ENGINE << 16) | engine_id, CMD_SET_AUTOREPLACE);
}
