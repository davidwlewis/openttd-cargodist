/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_tile.cpp Implementation of ScriptTile. */

#include "../../stdafx.h"
#include "script_tile.hpp"
#include "script_map.hpp"
#include "script_town.hpp"
#include "script_cargo.hpp"
#include "../../station_func.h"
#include "../../company_func.h"
#include "../../water_map.h"
#include "../../clear_map.h"
#include "../../tree_map.h"
#include "../../town.h"
#include "../../landscape.h"
#include "../../economy_func.h"

/* static */ bool ScriptTile::IsBuildable(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	switch (::GetTileType(tile)) {
		default: return false;
		case MP_CLEAR: return true;
		case MP_TREES: return true;
		case MP_WATER: return IsCoast(tile);
		case MP_ROAD:
			/* Tram bits aren't considered buildable */
			if (::GetRoadTypes(tile) != ROADTYPES_ROAD) return false;
			/* Depots and crossings aren't considered buildable */
			if (::GetRoadTileType(tile) != ROAD_TILE_NORMAL) return false;
			if (!HasExactlyOneBit(::GetRoadBits(tile, ROADTYPE_ROAD))) return false;
			if (::IsRoadOwner(tile, ROADTYPE_ROAD, OWNER_TOWN)) return true;
			if (::IsRoadOwner(tile, ROADTYPE_ROAD, ScriptObject::GetCompany())) return true;
			return false;
	}
}

/* static */ bool ScriptTile::IsBuildableRectangle(TileIndex tile, uint width, uint height)
{
	uint tx, ty;

	tx = ScriptMap::GetTileX(tile);
	ty = ScriptMap::GetTileY(tile);

	for (uint x = tx; x < width + tx; x++) {
		for (uint y = ty; y < height + ty; y++) {
			if (!IsBuildable(ScriptMap::GetTileIndex(x, y))) return false;
		}
	}

	return true;
}

/* static */ bool ScriptTile::IsWaterTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_WATER) && !::IsCoast(tile);
}

/* static */ bool ScriptTile::IsCoastTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_WATER) && ::IsCoast(tile)) ||
		(::IsTileType(tile, MP_TREES) && ::GetTreeGround(tile) == TREE_GROUND_SHORE);
}

/* static */ bool ScriptTile::IsStationTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_STATION);
}

/* static */ bool ScriptTile::IsSteepSlope(Slope slope)
{
	if ((slope & ~(SLOPE_ELEVATED | SLOPE_STEEP | SLOPE_HALFTILE_MASK)) != 0) return false;

	return ::IsSteepSlope((::Slope)slope);
}

/* static */ bool ScriptTile::IsHalftileSlope(Slope slope)
{
	if ((slope & ~(SLOPE_ELEVATED | SLOPE_STEEP | SLOPE_HALFTILE_MASK)) != 0) return false;

	return ::IsHalftileSlope((::Slope)slope);
}

/* static */ bool ScriptTile::HasTreeOnTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return ::IsTileType(tile, MP_TREES);
}

/* static */ bool ScriptTile::IsFarmTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_CLEAR) && ::IsClearGround(tile, CLEAR_FIELDS));
}

/* static */ bool ScriptTile::IsRockTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_CLEAR) && ::GetRawClearGround(tile) == ::CLEAR_ROCKS);
}

/* static */ bool ScriptTile::IsRoughTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_CLEAR) && ::GetRawClearGround(tile) == ::CLEAR_ROUGH);
}

/* static */ bool ScriptTile::IsSnowTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_CLEAR) && ::IsSnowTile(tile));
}

/* static */ bool ScriptTile::IsDesertTile(TileIndex tile)
{
	if (!::IsValidTile(tile)) return false;

	return (::IsTileType(tile, MP_CLEAR) && ::IsClearGround(tile, CLEAR_DESERT));
}

/* static */ ScriptTile::Slope ScriptTile::GetSlope(TileIndex tile)
{
	if (!::IsValidTile(tile)) return SLOPE_INVALID;

	return (Slope)::GetTileSlope(tile);
}

/* static */ ScriptTile::Slope ScriptTile::GetComplementSlope(Slope slope)
{
	if ((slope & ~SLOPE_ELEVATED) != 0) return SLOPE_INVALID;

	return (Slope)::ComplementSlope((::Slope)slope);
}

/* static */ int32 ScriptTile::GetMinHeight(TileIndex tile)
{
	if (!::IsValidTile(tile)) return -1;

	return ::GetTileZ(tile);
}

/* static */ int32 ScriptTile::GetMaxHeight(TileIndex tile)
{
	if (!::IsValidTile(tile)) return -1;

	return ::GetTileMaxZ(tile);
}

/* static */ int32 ScriptTile::GetCornerHeight(TileIndex tile, Corner corner)
{
	if (!::IsValidTile(tile) || !::IsValidCorner((::Corner)corner)) return -1;

	int z;
	::Slope slope = ::GetTileSlope(tile, &z);
	return (z + ::GetSlopeZInCorner(slope, (::Corner)corner));
}

/* static */ ScriptCompany::CompanyID ScriptTile::GetOwner(TileIndex tile)
{
	if (!::IsValidTile(tile)) return ScriptCompany::COMPANY_INVALID;
	if (::IsTileType(tile, MP_HOUSE)) return ScriptCompany::COMPANY_INVALID;
	if (::IsTileType(tile, MP_INDUSTRY)) return ScriptCompany::COMPANY_INVALID;

	return ScriptCompany::ResolveCompanyID((ScriptCompany::CompanyID)(byte)::GetTileOwner(tile));
}

/* static */ bool ScriptTile::HasTransportType(TileIndex tile, TransportType transport_type)
{
	if (!::IsValidTile(tile)) return false;

	return ::TrackStatusToTrackdirBits(::GetTileTrackStatus(tile, (::TransportType)transport_type, UINT32_MAX)) != TRACKDIR_BIT_NONE;
}

/* static */ int32 ScriptTile::GetCargoAcceptance(TileIndex tile, CargoID cargo_type, int width, int height, int radius)
{
	if (!::IsValidTile(tile) || width <= 0 || height <= 0 || radius < 0 || !ScriptCargo::IsValidCargo(cargo_type)) return -1;

	CargoArray acceptance = ::GetAcceptanceAroundTiles(tile, width, height, _settings_game.station.modified_catchment ? radius : (int)CA_UNMODIFIED);
	return acceptance[cargo_type];
}

/* static */ int32 ScriptTile::GetCargoProduction(TileIndex tile, CargoID cargo_type, int width, int height, int radius)
{
	if (!::IsValidTile(tile) || width <= 0 || height <= 0 || radius < 0 || !ScriptCargo::IsValidCargo(cargo_type)) return -1;

	CargoArray produced = ::GetProductionAroundTiles(tile, width, height, _settings_game.station.modified_catchment ? radius : (int)CA_UNMODIFIED);
	return produced[cargo_type];
}

/* static */ int32 ScriptTile::GetDistanceManhattanToTile(TileIndex tile_from, TileIndex tile_to)
{
	return ScriptMap::DistanceManhattan(tile_from, tile_to);
}

/* static */ int32 ScriptTile::GetDistanceSquareToTile(TileIndex tile_from, TileIndex tile_to)
{
	return ScriptMap::DistanceSquare(tile_from, tile_to);
}

/* static */ bool ScriptTile::RaiseTile(TileIndex tile, int32 slope)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, tile < ::MapSize());

	return ScriptObject::DoCommand(tile, slope, 1, CMD_TERRAFORM_LAND);
}

/* static */ bool ScriptTile::LowerTile(TileIndex tile, int32 slope)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, tile < ::MapSize());

	return ScriptObject::DoCommand(tile, slope, 0, CMD_TERRAFORM_LAND);
}

/* static */ bool ScriptTile::LevelTiles(TileIndex start_tile, TileIndex end_tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, start_tile < ::MapSize());
	EnforcePrecondition(false, end_tile < ::MapSize());

	return ScriptObject::DoCommand(end_tile, start_tile, LM_LEVEL << 1, CMD_LEVEL_LAND);
}

/* static */ bool ScriptTile::DemolishTile(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::DoCommand(tile, 0, 0, CMD_LANDSCAPE_CLEAR);
}

/* static */ bool ScriptTile::PlantTree(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::DoCommand(tile, TREE_INVALID, tile, CMD_PLANT_TREE);
}

/* static */ bool ScriptTile::PlantTreeRectangle(TileIndex tile, uint width, uint height)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));
	EnforcePrecondition(false, width >= 1 && width <= 20);
	EnforcePrecondition(false, height >= 1 && height <= 20);
	TileIndex end_tile = tile + ::TileDiffXY(width - 1, height - 1);

	return ScriptObject::DoCommand(tile, TREE_INVALID, end_tile, CMD_PLANT_TREE);
}

/* static */ bool ScriptTile::IsWithinTownInfluence(TileIndex tile, TownID town_id)
{
	return ScriptTown::IsWithinTownInfluence(town_id, tile);
}

/* static */ TownID ScriptTile::GetTownAuthority(TileIndex tile)
{
	if (!::IsValidTile(tile)) return INVALID_TOWN;

	Town *town = ::ClosestTownFromTile(tile, _settings_game.economy.dist_local_authority);
	if (town == NULL) return INVALID_TOWN;

	return town->index;
}

/* static */ TownID ScriptTile::GetClosestTown(TileIndex tile)
{
	if (!::IsValidTile(tile)) return INVALID_TOWN;

	Town *town = ::ClosestTownFromTile(tile, UINT_MAX);
	if (town == NULL) return INVALID_TOWN;

	return town->index;
}

/* static */ Money ScriptTile::GetBuildCost(BuildType build_type)
{
	switch (build_type) {
		case BT_FOUNDATION:   return ::GetPrice(PR_BUILD_FOUNDATION, 1, NULL);
		case BT_TERRAFORM:    return ::GetPrice(PR_TERRAFORM, 1, NULL);
		case BT_BUILD_TREES:  return ::GetPrice(PR_BUILD_TREES, 1, NULL);
		case BT_CLEAR_GRASS:  return ::GetPrice(PR_CLEAR_GRASS, 1, NULL);
		case BT_CLEAR_ROUGH:  return ::GetPrice(PR_CLEAR_ROUGH, 1, NULL);
		case BT_CLEAR_ROCKY:  return ::GetPrice(PR_CLEAR_ROCKS, 1, NULL);
		case BT_CLEAR_FIELDS: return ::GetPrice(PR_CLEAR_FIELDS, 1, NULL);
		case BT_CLEAR_HOUSE:  return ::GetPrice(PR_CLEAR_HOUSE, 1, NULL);
		default: return -1;
	}
}
