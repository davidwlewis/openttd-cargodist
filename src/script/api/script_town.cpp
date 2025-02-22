/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_town.cpp Implementation of ScriptTown. */

#include "../../stdafx.h"
#include "script_town.hpp"
#include "script_map.hpp"
#include "script_cargo.hpp"
#include "script_error.hpp"
#include "../../town.h"
#include "../../strings_func.h"
#include "../../company_func.h"
#include "../../station_base.h"
#include "../../landscape.h"
#include "table/strings.h"

/* static */ int32 ScriptTown::GetTownCount()
{
	return (int32)::Town::GetNumItems();
}

/* static */ bool ScriptTown::IsValidTown(TownID town_id)
{
	return ::Town::IsValidID(town_id);
}

/* static */ char *ScriptTown::GetName(TownID town_id)
{
	if (!IsValidTown(town_id)) return NULL;
	static const int len = 64;
	char *town_name = MallocT<char>(len);

	::SetDParam(0, town_id);
	::GetString(town_name, STR_TOWN_NAME, &town_name[len - 1]);

	return town_name;
}

/* static */ bool ScriptTown::SetText(TownID town_id, Text *text)
{
	CCountedPtr<Text> counter(text);

	EnforcePrecondition(false, text != NULL);
	EnforcePrecondition(false, IsValidTown(town_id));

	return ScriptObject::DoCommand(::Town::Get(town_id)->xy, town_id, 0, CMD_TOWN_SET_TEXT, text->GetEncodedText());
}

/* static */ int32 ScriptTown::GetPopulation(TownID town_id)
{
	if (!IsValidTown(town_id)) return -1;
	const Town *t = ::Town::Get(town_id);
	return t->population;
}

/* static */ int32 ScriptTown::GetHouseCount(TownID town_id)
{
	if (!IsValidTown(town_id)) return -1;
	const Town *t = ::Town::Get(town_id);
	return t->num_houses;
}

/* static */ TileIndex ScriptTown::GetLocation(TownID town_id)
{
	if (!IsValidTown(town_id)) return INVALID_TILE;
	const Town *t = ::Town::Get(town_id);
	return t->xy;
}

/* static */ int32 ScriptTown::GetLastMonthProduction(TownID town_id, CargoID cargo_id)
{
	if (!IsValidTown(town_id)) return -1;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return -1;

	const Town *t = ::Town::Get(town_id);

	return t->supplied[cargo_id].old_max;
}

/* static */ int32 ScriptTown::GetLastMonthSupplied(TownID town_id, CargoID cargo_id)
{
	if (!IsValidTown(town_id)) return -1;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return -1;

	const Town *t = ::Town::Get(town_id);

	return t->supplied[cargo_id].old_act;
}

/* static */ int32 ScriptTown::GetLastMonthTransportedPercentage(TownID town_id, CargoID cargo_id)
{
	if (!IsValidTown(town_id)) return -1;
	if (!ScriptCargo::IsValidCargo(cargo_id)) return -1;

	const Town *t = ::Town::Get(town_id);
	return ::ToPercent8(t->GetPercentTransported(cargo_id));
}

/* static */ int32 ScriptTown::GetLastMonthReceived(TownID town_id, ScriptCargo::TownEffect towneffect_id)
{
	if (!IsValidTown(town_id)) return -1;
	if (!ScriptCargo::IsValidTownEffect(towneffect_id)) return -1;

	const Town *t = ::Town::Get(town_id);

	return t->received[towneffect_id].old_act;
}

/* static */ bool ScriptTown::SetCargoGoal(TownID town_id, ScriptCargo::TownEffect towneffect_id, uint32 goal)
{
	EnforcePrecondition(false, IsValidTown(town_id));
	EnforcePrecondition(false, ScriptCargo::IsValidTownEffect(towneffect_id));

	return ScriptObject::DoCommand(::Town::Get(town_id)->xy, town_id | (towneffect_id << 16), goal, CMD_TOWN_CARGO_GOAL);
}

/* static */ uint32 ScriptTown::GetCargoGoal(TownID town_id, ScriptCargo::TownEffect towneffect_id)
{
	if (!IsValidTown(town_id)) return -1;
	if (!ScriptCargo::IsValidTownEffect(towneffect_id)) return -1;

	const Town *t = ::Town::Get(town_id);

	switch (t->goal[towneffect_id]) {
		case TOWN_GROWTH_WINTER:
			if (TileHeight(t->xy) >= GetSnowLine() && t->population > 90) return 1;
			return 0;

		case TOWN_GROWTH_DESERT:
			if (GetTropicZone(t->xy) == TROPICZONE_DESERT && t->population > 60) return 1;
			return 0;

		default: return t->goal[towneffect_id];
	}
}

/* static */ bool ScriptTown::SetGrowthRate(TownID town_id, uint16 days_between_town_growth)
{
	days_between_town_growth = days_between_town_growth * DAY_TICKS / TOWN_GROWTH_TICKS;

	EnforcePrecondition(false, IsValidTown(town_id));
	EnforcePrecondition(false, (days_between_town_growth & TOWN_GROW_RATE_CUSTOM) == 0);

	return ScriptObject::DoCommand(::Town::Get(town_id)->xy, town_id, days_between_town_growth, CMD_TOWN_GROWTH_RATE);
}

/* static */ int32 ScriptTown::GetGrowthRate(TownID town_id)
{
	if (!IsValidTown(town_id)) return false;

	const Town *t = ::Town::Get(town_id);

	return (t->growth_rate * TOWN_GROWTH_TICKS + DAY_TICKS) / DAY_TICKS;
}

/* static */ int32 ScriptTown::GetDistanceManhattanToTile(TownID town_id, TileIndex tile)
{
	return ScriptMap::DistanceManhattan(tile, GetLocation(town_id));
}

/* static */ int32 ScriptTown::GetDistanceSquareToTile(TownID town_id, TileIndex tile)
{
	return ScriptMap::DistanceSquare(tile, GetLocation(town_id));
}

/* static */ bool ScriptTown::IsWithinTownInfluence(TownID town_id, TileIndex tile)
{
	if (!IsValidTown(town_id)) return false;

	const Town *t = ::Town::Get(town_id);
	return ((uint32)GetDistanceSquareToTile(town_id, tile) <= t->squared_town_zone_radius[0]);
}

/* static */ bool ScriptTown::HasStatue(TownID town_id)
{
	if (ScriptObject::GetCompany() == OWNER_DEITY) return false;
	if (!IsValidTown(town_id)) return false;

	return ::HasBit(::Town::Get(town_id)->statues, ScriptObject::GetCompany());
}

/* static */ bool ScriptTown::IsCity(TownID town_id)
{
	if (!IsValidTown(town_id)) return false;

	return ::Town::Get(town_id)->larger_town;
}

/* static */ int ScriptTown::GetRoadReworkDuration(TownID town_id)
{
	if (!IsValidTown(town_id)) return -1;

	return ::Town::Get(town_id)->road_build_months;
}

/* static */ ScriptCompany::CompanyID ScriptTown::GetExclusiveRightsCompany(TownID town_id)
{
	if (ScriptObject::GetCompany() == OWNER_DEITY) return ScriptCompany::COMPANY_INVALID;
	if (!IsValidTown(town_id)) return ScriptCompany::COMPANY_INVALID;

	return (ScriptCompany::CompanyID)(int8)::Town::Get(town_id)->exclusivity;
}

/* static */ int32 ScriptTown::GetExclusiveRightsDuration(TownID town_id)
{
	if (!IsValidTown(town_id)) return -1;

	return ::Town::Get(town_id)->exclusive_counter;
}

/* static */ bool ScriptTown::IsActionAvailable(TownID town_id, TownAction town_action)
{
	if (ScriptObject::GetCompany() == OWNER_DEITY) return false;
	if (!IsValidTown(town_id)) return false;

	return HasBit(::GetMaskOfTownActions(NULL, ScriptObject::GetCompany(), ::Town::Get(town_id)), town_action);
}

/* static */ bool ScriptTown::PerformTownAction(TownID town_id, TownAction town_action)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, IsValidTown(town_id));
	EnforcePrecondition(false, IsActionAvailable(town_id, town_action));

	return ScriptObject::DoCommand(::Town::Get(town_id)->xy, town_id, town_action, CMD_DO_TOWN_ACTION);
}

/* static */ bool ScriptTown::ExpandTown(TownID town_id, int houses)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() == OWNER_DEITY);
	EnforcePrecondition(false, IsValidTown(town_id));
	EnforcePrecondition(false, houses > 0);

	return ScriptObject::DoCommand(::Town::Get(town_id)->xy, town_id, houses, CMD_EXPAND_TOWN);
}

/* static */ ScriptTown::TownRating ScriptTown::GetRating(TownID town_id, ScriptCompany::CompanyID company_id)
{
	if (!IsValidTown(town_id)) return TOWN_RATING_INVALID;
	ScriptCompany::CompanyID company = ScriptCompany::ResolveCompanyID(company_id);
	if (company == ScriptCompany::COMPANY_INVALID) return TOWN_RATING_INVALID;

	const Town *t = ::Town::Get(town_id);
	if (!HasBit(t->have_ratings, company)) {
		return TOWN_RATING_NONE;
	} else if (t->ratings[company] <= RATING_APPALLING) {
		return TOWN_RATING_APPALLING;
	} else if (t->ratings[company] <= RATING_VERYPOOR) {
		return TOWN_RATING_VERY_POOR;
	} else if (t->ratings[company] <= RATING_POOR) {
		return TOWN_RATING_POOR;
	} else if (t->ratings[company] <= RATING_MEDIOCRE) {
		return TOWN_RATING_MEDIOCRE;
	} else if (t->ratings[company] <= RATING_GOOD) {
		return TOWN_RATING_GOOD;
	} else if (t->ratings[company] <= RATING_VERYGOOD) {
		return TOWN_RATING_VERY_GOOD;
	} else if (t->ratings[company] <= RATING_EXCELLENT) {
		return TOWN_RATING_EXCELLENT;
	} else {
		return TOWN_RATING_OUTSTANDING;
	}
}

/* static */ int ScriptTown::GetAllowedNoise(TownID town_id)
{
	if (!IsValidTown(town_id)) return -1;

	const Town *t = ::Town::Get(town_id);
	if (_settings_game.economy.station_noise_level) {
		return t->MaxTownNoise() - t->noise_reached;
	}

	int num = 0;
	const Station *st;
	FOR_ALL_STATIONS(st) {
		if (st->town == t && (st->facilities & FACIL_AIRPORT) && st->airport.type != AT_OILRIG) num++;
	}
	return max(0, 2 - num);
}

/* static */ ScriptTown::RoadLayout ScriptTown::GetRoadLayout(TownID town_id)
{
	if (!IsValidTown(town_id)) return ROAD_LAYOUT_INVALID;

	return (ScriptTown::RoadLayout)((TownLayout)::Town::Get(town_id)->layout);
}
