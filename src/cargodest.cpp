/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargodest.cpp Implementation of cargo destinations. */

#include "stdafx.h"
#include "cargodest_type.h"
#include "cargodest_base.h"
#include "cargodest_func.h"
#include "core/bitmath_func.hpp"
#include "core/random_func.hpp"
#include "core/pool_func.hpp"
#include "cargotype.h"
#include "settings_type.h"
#include "town.h"
#include "industry.h"
#include "window_func.h"
#include "vehicle_base.h"
#include "station_base.h"
#include "pathfinder/yapf/yapf.h"
#include "company_base.h"


static const uint MAX_EXTRA_LINKS       = 2;    ///< Number of extra links allowed.
static const uint MAX_IND_STOCKPILE     = 1000; ///< Maximum stockpile to consider for industry link weight.

static const uint BASE_TOWN_LINKS       = 0; ///< Index into _settings_game.economy.cargodest.base_town_links for normal cargo
static const uint BASE_TOWN_LINKS_SYMM  = 1; ///< Index into _settings_game.economy.cargodest.base_town_links for symmteric cargos
static const uint BASE_IND_LINKS        = 0; ///< Index into _settings_game.economy.cargodest.base_ind_links for normal cargo
static const uint BASE_IND_LINKS_TOWN   = 1; ///< Index into _settings_game.economy.cargodest.base_ind_links for town cargos
static const uint BASE_IND_LINKS_SYMM   = 2; ///< Index into _settings_game.economy.cargodest.base_ind_links for symmetric cargos
static const uint BIG_TOWN_POP_MAIL     = 0; ///< Index into _settings_game.economy.cargodest.big_town_pop for mail
static const uint BIG_TOWN_POP_PAX      = 1; ///< Index into _settings_game.economy.cargodest.big_town_pop for passengers
static const uint SCALE_TOWN            = 0; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for normal cargo
static const uint SCALE_TOWN_BIG        = 1; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for normal cargo of big towns
static const uint SCALE_TOWN_PAX        = 2; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for passengers
static const uint SCALE_TOWN_BIG_PAX    = 3; ///< Index into _settings_game.economy.cargodest.pop_scale_town/weight_scale_town for passengers of big towns
static const uint CARGO_SCALE_IND       = 0; ///< Index into _settings_game.economy.cargodest.cargo_scale_ind for normal cargo
static const uint CARGO_SCALE_IND_TOWN  = 1; ///< Index into _settings_game.economy.cargodest.cargo_scale_ind for town cargos
static const uint MIN_WEIGHT_TOWN       = 0; ///< Index into _settings_game.economy.cargodest.min_weight_town for normal cargo
static const uint MIN_WEIGHT_TOWN_PAX   = 1; ///< Index into _settings_game.economy.cargodest.min_weight_town for passengers
static const uint WEIGHT_SCALE_IND_PROD = 0; ///< Index into _settings_game.economy.cargodest.weight_scale_ind for produced cargo
static const uint WEIGHT_SCALE_IND_PILE = 1; ///< Index into _settings_game.economy.cargodest.weight_scale_ind for stockpiled cargo

/**
 * ID of the last iteration through the route graph.
 * Doesn't need to be saved as it's only important that subsequent iterations
 * get different IDs and none of them gets an ID of 0.
 */
static uint _route_graph_iteration = 1;

/** Are cargo destinations for this cargo type enabled? */
bool CargoHasDestinations(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	if (spec->town_effect == TE_PASSENGERS || spec->town_effect == TE_MAIL) {
		return HasBit(_settings_game.economy.cargodest.mode, CRM_TOWN_CARGOS);
	} else {
		return HasBit(_settings_game.economy.cargodest.mode, CRM_INDUSTRY_CARGOS);
	}
}

/** Should this cargo type primarily have towns as a destination? */
static bool IsTownCargo(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	return spec->town_effect != TE_NONE;
}

/** Does this cargo have a symmetric demand?  */
static bool IsSymmetricCargo(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	return spec->town_effect == TE_PASSENGERS;
}

/** Is this a passenger cargo. */
static bool IsPassengerCargo(CargoID cid)
{
	const CargoSpec *spec = CargoSpec::Get(cid);
	return spec->town_effect == TE_PASSENGERS;
}

/** Are two tiles near each other according to the given distance modifier. */
static bool IsNearby(TileIndex source_xy, TileIndex dest_xy, uint32 nearby_dist)
{
	/* Scale distance by 1D map size to make sure that there are still
	 * candidates left on larger maps with few towns, but don't scale
	 * by 2D map size so the map still feels bigger. */
	return DistanceSquare(source_xy, dest_xy) < ScaleByMapSize1D(nearby_dist);
}

/** Is a town near to a tile. */
static bool IsTownNearby(TileIndex source_xy, const Town *t)
{
	return IsNearby(source_xy, t->xy, _settings_game.economy.cargodest.town_nearby_dist);
}

/** Is an industry near to a tile. */
static bool IsIndustryNearby(TileIndex source_xy, const Industry *ind)
{
	return IsNearby(source_xy, ind->location.tile, _settings_game.economy.cargodest.ind_nearby_dist);
}

/** Does this town produce a lot of the given cargo. */
static bool IsBigTown(const Town *t, CargoID cid)
{
	return IsPassengerCargo(cid) ? 
		t->pass.old_max > _settings_game.economy.cargodest.big_town_pop[BIG_TOWN_POP_PAX] : 
		t->mail.old_max > _settings_game.economy.cargodest.big_town_pop[BIG_TOWN_POP_MAIL];
}

/** Does this industry produce anything. */
static bool IsProducingIndustry(const Industry *ind)
{
	return ind->produced_cargo[0] != CT_INVALID || ind->produced_cargo[1] != CT_INVALID;
}

/** Information for the town/industry enumerators. */
struct EnumRandomData {
	CargoSourceSink *source;
	TileIndex       source_xy;
	CargoID         cid;
	bool            limit_links;
};

/** Common helper for town/industry enumeration. */
static bool EnumAnyDest(const CargoSourceSink *dest, EnumRandomData *erd)
{
	/* Already a destination? */
	if (erd->source->HasLinkTo(erd->cid, dest)) return false;

	/* Destination already has too many links? */
	if (erd->limit_links && dest->cargo_links[erd->cid].Length() > dest->num_links_expected[erd->cid] + MAX_EXTRA_LINKS) return false;

	return true;
}

/** Enumerate any town not already a destination and accepting a specific cargo.*/
static bool EnumAnyTown(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyDest(t, erd) && t->AcceptsCargo(erd->cid);
}

/** Enumerate cities. */
static bool EnumCity(const Town *t, void *data)
{
	return EnumAnyTown(t, data) && t->larger_town;
}

/** Enumerate towns with a big population. */
static bool EnumBigTown(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyTown(t, erd) && IsBigTown(t, erd->cid);
}

/** Enumerate nearby towns. */
static bool EnumNearbyTown(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyTown(t, data) && IsTownNearby(erd->source_xy, t);
}

/** Enumerate any industry not already a destination and accepting a specific cargo. */
static bool EnumAnyIndustry(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyDest(ind, erd) && ind->AcceptsCargo(erd->cid);
}

/** Enumerate nearby industries. */
static bool EnumNearbyIndustry(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnyIndustry(ind, data) && IsIndustryNearby(erd->source_xy, ind);
}

/** Enumerate industries that are producing cargo. */
static bool EnumProducingIndustry(const Industry *ind, void *data)
{
	return EnumAnyIndustry(ind, data) && IsProducingIndustry(ind);
}

/** Enumerate cargo sources supplying a specific cargo. */
template <typename T>
static bool EnumAnySupplier(const T *css, void *data)
{
	return css->SuppliesCargo(((EnumRandomData *)data)->cid);
}

/** Enumerate nearby cargo sources supplying a specific cargo. */
static bool EnumNearbySupplier(const Industry *ind, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnySupplier(ind, data) && IsIndustryNearby(erd->source_xy, ind);
}

/** Enumerate nearby cargo sources supplying a specific cargo. */
static bool EnumNearbySupplier(const Town *t, void *data)
{
	EnumRandomData *erd = (EnumRandomData *)data;
	return EnumAnySupplier(t, data) && IsTownNearby(erd->source_xy, t);
}


/** Find a town as a destination. */
static CargoSourceSink *FindTownDestination(byte &weight_mod, CargoSourceSink *source, TileIndex source_xy, CargoID cid, const uint8 destclass_chance[4], TownID skip = INVALID_TOWN)
{
	/* Enum functions for: nearby town, city, big town, and any town. */
	static const Town::EnumTownProc destclass_enum[] = {
		&EnumNearbyTown, &EnumCity, &EnumBigTown, &EnumAnyTown
	};
	static const byte weight_mods[] = {LWM_TOWN_NEARBY, LWM_CITY, LWM_TOWN_BIG, LWM_TOWN_ANY};
	assert_compile(lengthof(destclass_enum) == lengthof(weight_mods));

	EnumRandomData erd = {source, source_xy, cid, IsSymmetricCargo(cid)};

	/* Determine destination class. If no town is found in this class,
	 * the search falls through to the following classes. */
	byte destclass = RandomRange(destclass_chance[3]);

	weight_mod = LWM_ANYWHERE;
	Town *dest = NULL;
	for (uint i = 0; i < lengthof(destclass_enum) && dest == NULL; i++) {
		/* Skip if destination class not reached. */
		if (destclass > destclass_chance[i]) continue;

		dest = Town::GetRandom(destclass_enum[i], skip, &erd);
		weight_mod = weight_mods[i];
	}

	return dest;
}

/** Find an industry as a destination. */
static CargoSourceSink *FindIndustryDestination(byte &weight_mod, CargoSourceSink *source, TileIndex source_xy, CargoID cid, IndustryID skip = INVALID_INDUSTRY)
{
	/* Enum functions for: nearby industry, producing industry, and any industry. */
	static const Industry::EnumIndustryProc destclass_enum[] = {
		&EnumNearbyIndustry, &EnumProducingIndustry, &EnumAnyIndustry
	};
	static const byte weight_mods[] = {LWM_INDUSTRY_NEARBY, LWM_INDUSTRY_PRODUCING, LWM_INDUSTRY_ANY};
	assert_compile(lengthof(destclass_enum) == lengthof(_settings_game.economy.cargodest.ind_chances));

	EnumRandomData erd = {source, source_xy, cid, IsSymmetricCargo(cid)};

	/* Determine destination class. If no industry is found in this class,
	 * the search falls through to the following classes. */
	byte destclass = RandomRange(*lastof(_settings_game.economy.cargodest.ind_chances));

	weight_mod = LWM_ANYWHERE;
	Industry *dest = NULL;
	for (uint i = 0; i < lengthof(destclass_enum) && dest == NULL; i++) {
		/* Skip if destination class not reached. */
		if (destclass > _settings_game.economy.cargodest.ind_chances[i]) continue;

		dest = Industry::GetRandom(destclass_enum[i], skip, &erd);
		weight_mod = weight_mods[i];
	}

	return dest;
}

/** Find a supply for a cargo type. */
static CargoSourceSink *FindSupplySource(Industry *dest, CargoID cid)
{
	EnumRandomData erd = {dest, dest->location.tile, cid, false};

	CargoSourceSink *source = NULL;

	/* Even chance for industry source first, town second and vice versa.
	 * Try a nearby supplier first, then check all suppliers. */
	if (Chance16(1, 2)) {
		source = Industry::GetRandom(&EnumNearbySupplier, dest->index, &erd);
		if (source == NULL) source = Town::GetRandom(&EnumNearbySupplier, INVALID_TOWN, &erd);
		if (source == NULL) source = Industry::GetRandom(&EnumAnySupplier, dest->index, &erd);
		if (source == NULL) source = Town::GetRandom(&EnumAnySupplier, INVALID_TOWN, &erd);
	} else {
		source = Town::GetRandom(&EnumNearbySupplier, INVALID_TOWN, &erd);
		if (source == NULL) source = Industry::GetRandom(&EnumNearbySupplier, dest->index, &erd);
		if (source == NULL) source = Town::GetRandom(&EnumAnySupplier, INVALID_TOWN, &erd);
		if (source == NULL) source = Industry::GetRandom(&EnumAnySupplier, dest->index, &erd);
	}

	return source;
}

/* virtual */ void CargoSourceSink::CreateSpecialLinks(CargoID cid)
{
	if (this->cargo_links[cid].Length() == 0) {
		/* First link is for undetermined destinations. */
		*this->cargo_links[cid].Append() = CargoLink(NULL);
	}
}

/* virtual */ void Town::CreateSpecialLinks(CargoID cid)
{
	CargoSourceSink::CreateSpecialLinks(cid);

	if (this->AcceptsCargo(cid)) {
		/* Add special link for town-local demand if not already present. */
		if (this->cargo_links[cid].Length() < 2) *this->cargo_links[cid].Append() = CargoLink(this, LWM_INTOWN);
		if (this->cargo_links[cid].Get(1)->dest != this) {
			/* Insert link at second place. */
			*this->cargo_links[cid].Append() = *this->cargo_links[cid].Get(1);
			*this->cargo_links[cid].Get(1) = CargoLink(this, LWM_INTOWN);
		}
	} else {
		/* Remove link for town-local demand if present. */
		if (this->cargo_links[cid].Length() > 1 && this->cargo_links[cid].Get(1)->dest == this) {
			this->cargo_links[cid].Erase(this->cargo_links[cid].Get(1));
		}
	}
}

/**
 * Remove the link with the lowest weight from a cargo source. The
 * reverse link is removed as well if the cargo has symmetric demand.
 * @param source Remove the link from this cargo source.
 * @param cid Cargo type of the link to remove.
 */
static void RemoveLowestLink(CargoSourceSink *source, CargoID cid)
{
	uint lowest_weight = UINT_MAX;
	CargoLink *lowest_link = NULL;

	for (CargoLink *l = source->cargo_links[cid].Begin(); l != source->cargo_links[cid].End(); l++) {
		/* Don't remove special links. */
		if (l->dest == NULL || l->dest == source) continue;

		if (l->weight < lowest_weight) {
			lowest_weight = l->weight;
			lowest_link = l;
		}
	}

	if (lowest_link != NULL) {
		/* If this is a symmetric cargo, also remove the reverse link. */
		if (IsSymmetricCargo(cid) && lowest_link->dest->HasLinkTo(cid, source)) {
			source->num_incoming_links[cid]--;
			lowest_link->dest->cargo_links[cid].Erase(lowest_link->dest->cargo_links[cid].Find(CargoLink(source)));
		}
		lowest_link->dest->num_incoming_links[cid]--;
		source->cargo_links[cid].Erase(lowest_link);
	}
}

/**
 * Get the link weight modifier for a link.
 * @param from Source of cargo.
 * @param to Destination of cargo.
 * @param cid Cargo to be transported.
 * @return A LinkWeightModifier.
 */
static byte GetLinkWeightModifier(CargoSourceSink *from, CargoSourceSink *to, CargoID cid)
{
	if (to == NULL) return LWM_ANYWHERE;
	
	TileIndex from_xy = INVALID_TILE;
	switch (from->GetType()) {
		case ST_TOWN:
			from_xy = static_cast<Town *>(from)->xy;
			break;
		case ST_INDUSTRY:
			from_xy = static_cast<Industry *>(from)->location.tile;
			break;
		case ST_HEADQUARTERS:
			NOT_REACHED();
	}

	switch (to->GetType()) {
		case ST_TOWN: {
			Town *t = static_cast<Town *>(to);
			if (from == to) {
				return LWM_INTOWN;
			} else if (IsTownNearby(from_xy, t)) {
				return LWM_TOWN_NEARBY;
			} else if (t->larger_town) {
				return LWM_CITY;
			} else if (IsBigTown(t, cid)) {
				return LWM_TOWN_BIG;
			} else {
				return LWM_TOWN_ANY;
			}
		}
		case ST_INDUSTRY: {
			Industry *ind = static_cast<Industry *>(to);
			if (IsIndustryNearby(from_xy, ind)) {
				return LWM_INDUSTRY_NEARBY;
			} else if (IsProducingIndustry(ind)) {
				return LWM_INDUSTRY_PRODUCING;
			} else {
				return LWM_INDUSTRY_ANY;
			}
		}
		default:
			NOT_REACHED();
			return LWM_INVALID;
	}
}

/**
 * 1. Build the link graph component containing the given station by using BFS on the routes.
 * 2. Set every included station's checked_at to _tick_counter.
 * 3. Create all possible cargo links between towns and industries around stations in the component.
 * @param first Station to start the search at.
 * @param cid Cargo to build the component for
 */
void CreateRouteGraphComponent(Station *first, CargoID cid)
{
	typedef std::list<Station *> StationList;
	typedef std::list<CargoSourceSink *> NodeList;
	StationList search_queue;
	NodeList accepting;
	NodeList supplying;
	search_queue.push_back(first);

	/* find all stations belonging to the current component */
	while (!search_queue.empty()) {
		Station *st = search_queue.front();
		search_queue.pop_front();

		if (st->goods[cid].checked_at != _route_graph_iteration) {
			bool supplies = HasBit(st->goods[cid].acceptance_pickup, GoodsEntry::PICKUP);
			bool accepts = HasBit(st->goods[cid].acceptance_pickup, GoodsEntry::ACCEPTANCE);
			if (supplies && st->town->SuppliesCargo(cid)) supplying.push_back(st->town);
			if (accepts && st->town->AcceptsCargo(cid)) accepting.push_back(st->town);
			for (Industry **i = st->industries_near.Begin(); 
					i != st->industries_near.End(); ++i) {
				if (supplies && (*i)->SuppliesCargo(cid)) supplying.push_back(*i);
				if (accepts && (*i)->AcceptsCargo(cid)) accepting.push_back(*i);
			}
			st->goods[cid].checked_at = _route_graph_iteration;
		
			const RouteLinkList &links = st->goods[cid].routes;
			for (RouteLinkList::const_iterator i = links.begin(); i != links.end(); ++i) {
				Station *target = Station::GetIfValid((*i)->GetDestination());
				if (target == NULL) continue;
				search_queue.push_back(target);
			}
		}
	}
	
	for (NodeList::iterator j = supplying.begin(); j != supplying.end(); ++j) {
		CargoSourceSink *from = *j;
		for (NodeList::iterator i = accepting.begin(); i != accepting.end(); ++i) {
			CargoSourceSink *to = *i;
			if (from != to) {
				CargoLink *link = from->cargo_links[cid].Find(CargoLink(to));
				if (link == from->cargo_links[cid].End()) {
					*from->cargo_links[cid].Append() = CargoLink(to, LWM_INVALID);
					to->num_incoming_links[cid]++;
				} else {
					link->weight_mod = LWM_INVALID;
				}
			}
		}
		for (CargoLink *link = from->cargo_links[cid].Begin(); link != from->cargo_links[cid].End();) {
			if (link->weight_mod != LWM_INVALID && from != link->dest && link->dest != NULL) {
				from->cargo_links[cid].Erase(link);
				link->dest->num_incoming_links[cid]--;
			} else {
				link->weight_mod = GetLinkWeightModifier(from, link->dest, cid);
				++link;
			}
		}
	}
}

/** Create all cargo links possible in the given transport network around a station. */
static void CreateConnectedNewLinks(Station *st)
{
	for (CargoID cid = 0; cid != NUM_CARGO; ++cid) {
		if (st->goods[cid].checked_at != _route_graph_iteration && (st->goods[cid].acceptance_pickup != 0)) {
			CreateRouteGraphComponent(st, cid);
		}
	}
}

/** Create missing cargo links for a source. */
static void CreateNewLinks(CargoSourceSink *source, TileIndex source_xy, CargoID cid, uint chance_a, uint chance_b, const uint8 town_chance[], TownID skip_town, IndustryID skip_ind)
{
	uint num_links = source->num_links_expected[cid];

	/* Remove the link with the lowest weight if the
	 * town has more than links more than expected. */
	if (source->cargo_links[cid].Length() > num_links + MAX_EXTRA_LINKS) {
		RemoveLowestLink(source, cid);
	}

	/* Add new links until the expected link count is reached. */
	while (source->cargo_links[cid].Length() < num_links) {
		CargoSourceSink *dest = NULL;
		byte weight_mod = LWM_ANYWHERE;

		/* Chance for town/industry is chance_a/chance_b, otherwise try industry/town. */
		if (Chance16(chance_a, chance_b)) {
			dest = FindTownDestination(weight_mod, source, source_xy, cid, town_chance, skip_town);
			/* No town found? Try an industry. */
			if (dest == NULL) dest = FindIndustryDestination(weight_mod, source, source_xy, cid, skip_ind);
		} else {
			dest = FindIndustryDestination(weight_mod, source, source_xy, cid, skip_ind);
			/* No industry found? Try a town. */
			if (dest == NULL) dest = FindTownDestination(weight_mod, source, source_xy, cid, town_chance, skip_town);
		}

		/* If we didn't find a destination, break out of the loop because no
		 * more destinations are left on the map. */
		if (dest == NULL) break;

		/* If this is a symmetric cargo and we accept it as well, create a back link. */
		if (IsSymmetricCargo(cid) && dest->SuppliesCargo(cid) && source->AcceptsCargo(cid)) {
			*dest->cargo_links[cid].Append() = CargoLink(source, weight_mod);
			source->num_incoming_links[cid]++;
		}

		*source->cargo_links[cid].Append() = CargoLink(dest, weight_mod);
		dest->num_incoming_links[cid]++;
	}
}

/** Remove invalid links from a cargo source/sink. */
static void RemoveInvalidLinks(CargoSourceSink *css)
{
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		/* Remove outgoing links if cargo isn't supplied anymore. */
		if (!css->SuppliesCargo(cid)) {
			for (CargoLink *l = css->cargo_links[cid].Begin(); l != css->cargo_links[cid].End(); l++) {
				if (l->dest != NULL && l->dest != css) l->dest->num_incoming_links[cid]--;
			}
			css->cargo_links[cid].Clear();
			css->cargo_links_weight[cid] = 0;
		}

		/* Remove outgoing links if the dest doesn't accept the cargo anymore. */
		for (CargoLink *l = css->cargo_links[cid].Begin(); l != css->cargo_links[cid].End(); ) {
			if (l->dest != NULL && !l->dest->AcceptsCargo(cid)) {
				if (l->dest != css) l->dest->num_incoming_links[cid]--;
				css->cargo_links[cid].Erase(l);
			} else {
				l++;
			}
		}
	}
}

/** Create special links for a town if they don't exist yet. */
void UpdateSpecialLinks(Town *t)
{
	CargoID cid;
	FOR_EACH_SET_CARGO_ID(cid, t->cargo_produced) {
		if (CargoHasDestinations(cid)) t->CreateSpecialLinks(cid);
	}
}

/** Create special links for an industry if they don't exist yet. */
void UpdateSpecialLinks(Industry *ind)
{
	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		CargoID cid = ind->produced_cargo[i];
		if (cid != INVALID_CARGO && CargoHasDestinations(cid)) ind->CreateSpecialLinks(cid);
	}
}

/** Updated the desired link count for each cargo. */
void UpdateExpectedLinks(Town *t)
{
	CargoID cid;

	FOR_EACH_SET_CARGO_ID(cid, t->cargo_produced) {
		if (CargoHasDestinations(cid)) {
			t->CreateSpecialLinks(cid);

			uint max_amt = IsPassengerCargo(cid) ? t->pass.old_max : t->mail.old_max;
			uint big_amt = _settings_game.economy.cargodest.big_town_pop[IsPassengerCargo(cid) ? BIG_TOWN_POP_PAX : BIG_TOWN_POP_MAIL];

			uint num_links = _settings_game.economy.cargodest.base_town_links[IsSymmetricCargo(cid) ? BASE_TOWN_LINKS_SYMM : BASE_TOWN_LINKS];
			/* Add links based on the available cargo amount. */
			num_links += min(max_amt, big_amt) / _settings_game.economy.cargodest.pop_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_PAX : SCALE_TOWN];
			if (max_amt > big_amt) num_links += (max_amt - big_amt) / _settings_game.economy.cargodest.pop_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_BIG_PAX : SCALE_TOWN_BIG];
			/* Ensure a city has at least city_town_links more than the base value.
			 * This improves the link distribution at the beginning of a game when
			 * the towns are still small. */
			if (t->larger_town) num_links = max<uint>(num_links, _settings_game.economy.cargodest.city_town_links + _settings_game.economy.cargodest.base_town_links[IsSymmetricCargo(cid) ? BASE_TOWN_LINKS_SYMM : BASE_TOWN_LINKS]);

			/* Account for the two special links. */
			num_links++;
			if (t->cargo_links[cid].Length() > 1 && t->cargo_links[cid].Get(1)->dest == t) num_links++;

			t->num_links_expected[cid] = ClampToU16(num_links);
		}
	}
}

/** Updated the desired link count for each cargo. */
void UpdateExpectedLinks(Industry *ind)
{
	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		CargoID cid = ind->produced_cargo[i];
		if (cid == INVALID_CARGO) continue;

		if (CargoHasDestinations(cid)) {
			ind->CreateSpecialLinks(cid);

			uint num_links;
			/* Use different base values for symmetric cargos, cargos
			 * with a town effect and all other cargos. */
			num_links = _settings_game.economy.cargodest.base_ind_links[IsSymmetricCargo(cid) ? BASE_IND_LINKS_SYMM : (IsTownCargo(cid) ? BASE_IND_LINKS_TOWN : BASE_IND_LINKS)];
			/* Add links based on the average industry production. */
			num_links += ind->average_production[i] / _settings_game.economy.cargodest.cargo_scale_ind[IsTownCargo(cid) ? CARGO_SCALE_IND_TOWN : CARGO_SCALE_IND];

			/* Account for the one special link. */
			num_links++;

			ind->num_links_expected[cid] = ClampToU16(num_links);
		}
	}
}

/** Make sure an industry has at least one incoming link for each accepted cargo. */
void AddMissingIndustryLinks(Industry *ind)
{
	for (uint i = 0; i < lengthof(ind->accepts_cargo); i++) {
		CargoID cid = ind->accepts_cargo[i];
		if (cid == INVALID_CARGO) continue;

		/* Do we already have at least one cargo source? */
		if (ind->num_incoming_links[cid] > 0) continue;

		CargoSourceSink *source = FindSupplySource(ind, cid);
		if (source == NULL) continue; // Too bad...

		if (source->cargo_links[cid].Length() >= source->num_links_expected[cid] + MAX_EXTRA_LINKS) {
			/* Increase the expected link count if adding another link would
			 * exceed the count, as otherwise this (or another) link would
			 * get removed right again. */
			source->num_links_expected[cid]++;
		}

		*source->cargo_links[cid].Append() = CargoLink(ind, 2);
		ind->num_incoming_links[cid]++;

		/* If this is a symmetric cargo and we produce it as well, create a back link. */
		if (IsSymmetricCargo(cid) && ind->SuppliesCargo(cid) && source->AcceptsCargo(cid)) {
			*ind->cargo_links[cid].Append() = CargoLink(source, 2);
			source->num_incoming_links[cid]++;
		}
	}
}

/** Update the demand links. */
void UpdateCargoLinks(Town *t)
{
	CargoID cid;

	FOR_EACH_SET_CARGO_ID(cid, t->cargo_produced) {
		if (CargoHasDestinations(cid)) {
			/* If this is a town cargo, 95% chance for town/industry destination and
			 * 5% for industry/town. The reverse chance otherwise. */
			CreateNewLinks(t, t->xy, cid, IsTownCargo(cid) ? 19 : 1, 20, t->larger_town ? _settings_game.economy.cargodest.town_chances_city : _settings_game.economy.cargodest.town_chances_town, t->index, INVALID_INDUSTRY);
		}
	}
}

/** Update the demand links. */
void UpdateCargoLinks(Industry *ind)
{
	for (uint i = 0; i < lengthof(ind->produced_cargo); i++) {
		CargoID cid = ind->produced_cargo[i];
		if (cid == INVALID_CARGO) continue;

		if (CargoHasDestinations(cid)) {
			/* If this is a town cargo, 75% chance for town/industry destination and
			 * 25% for industry/town. The reverse chance otherwise. */
			CreateNewLinks(ind, ind->location.tile, cid, IsTownCargo(cid) ? 3 : 1, 4, _settings_game.economy.cargodest.town_chances_town, INVALID_TOWN, ind->index);
		}
	}
}

/* virtual */ uint Town::GetDestinationWeight(CargoID cid, byte weight_mod) const
{
	uint max_amt = IsPassengerCargo(cid) ? this->pass.old_max : this->mail.old_max;
	uint big_amt = _settings_game.economy.cargodest.big_town_pop[IsPassengerCargo(cid) ? BIG_TOWN_POP_PAX : BIG_TOWN_POP_MAIL];

	/* The weight is calculated by a piecewise function. We start with a predefined
	 * minimum weight and then add the weight for the cargo amount up to the big
	 * town amount. If the amount is more than the big town amount, this is also
	 * added to the weight with a different scale factor to make sure that big towns
	 * don't siphon the cargo away too much from the smaller destinations. */
	uint weight = _settings_game.economy.cargodest.min_weight_town[IsPassengerCargo(cid) ? MIN_WEIGHT_TOWN_PAX : MIN_WEIGHT_TOWN];
	weight += min(max_amt, big_amt) * weight_mod / _settings_game.economy.cargodest.weight_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_PAX : SCALE_TOWN];
	if (max_amt > big_amt) weight += (max_amt - big_amt) * weight_mod / _settings_game.economy.cargodest.weight_scale_town[IsPassengerCargo(cid) ? SCALE_TOWN_BIG_PAX : SCALE_TOWN_BIG];

	return weight;
}

/* virtual */ uint Industry::GetDestinationWeight(CargoID cid, byte weight_mod) const
{
	uint weight = _settings_game.economy.cargodest.min_weight_ind;

	for (uint i = 0; i < lengthof(this->accepts_cargo); i++) {
		if (this->accepts_cargo[i] != cid) continue;
		/* Empty stockpile means more weight for the link. Stockpiles
		 * above a fixed maximum have no further effect. */
		uint stockpile = ClampU(this->incoming_cargo_waiting[i], 0, MAX_IND_STOCKPILE);
		weight += (MAX_IND_STOCKPILE - stockpile) * weight_mod / _settings_game.economy.cargodest.weight_scale_ind[WEIGHT_SCALE_IND_PILE];
	}

	/* Add a weight for the produced cargo. Use the average production
	 * here so the weight isn't fluctuating that much when the input
	 * cargo isn't delivered regularly. */
	weight += (this->average_production[0] + this->average_production[1]) * weight_mod / _settings_game.economy.cargodest.weight_scale_ind[WEIGHT_SCALE_IND_PROD];

	return weight;
}

/** Recalculate the link weights. */
void UpdateLinkWeights(Town *t)
{
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		uint weight_sum = 0;

		if (t->cargo_links[cid].Length() == 0) continue;

		t->cargo_links[cid].Begin()->amount.NewMonth();

		/* Skip the special link for undetermined destinations. */
		for (CargoLink *l = t->cargo_links[cid].Begin() + 1; l != t->cargo_links[cid].End(); l++) {
			l->weight = l->dest->GetDestinationWeight(cid, l->weight_mod);
			weight_sum += l->weight;

			l->amount.NewMonth();
		}

		/* Limit the weight of the in-town link to at most 1/3 of the total weight. */
		if (t->cargo_links[cid].Length() > 1 && t->cargo_links[cid].Get(1)->dest == t) {
			uint new_weight = min(t->cargo_links[cid].Get(1)->weight, weight_sum / 3);
			weight_sum -= t->cargo_links[cid].Get(1)->weight - new_weight;
			t->cargo_links[cid].Get(1)->weight = new_weight;
		}

		/* Set weight for the undetermined destination link to random_dest_chance%. */
		t->cargo_links[cid].Begin()->weight = weight_sum == 0 ? 1 : (weight_sum * _settings_game.economy.cargodest.random_dest_chance) / (100 - _settings_game.economy.cargodest.random_dest_chance);

		t->cargo_links_weight[cid] = weight_sum + t->cargo_links[cid].Begin()->weight;
	}
}

/** Recalculate the link weights. */
void UpdateLinkWeights(CargoSourceSink *css)
{
	for (uint cid = 0; cid < NUM_CARGO; cid++) {
		uint weight_sum = 0;

		if (css->cargo_links[cid].Length() == 0) continue;

		for (CargoLink *l = css->cargo_links[cid].Begin() + 1; l != css->cargo_links[cid].End(); l++) {
			l->weight = l->dest->GetDestinationWeight(cid, l->weight_mod);
			weight_sum += l->weight;

			l->amount.NewMonth();
		}

		/* Set weight for the undetermined destination link to random_dest_chance%. */
		css->cargo_links[cid].Begin()->weight = weight_sum == 0 ? 1 : (weight_sum * _settings_game.economy.cargodest.random_dest_chance) / (100 - _settings_game.economy.cargodest.random_dest_chance);

		css->cargo_links_weight[cid] = weight_sum + css->cargo_links[cid].Begin()->weight;
	}
}

/* virtual */ CargoSourceSink::~CargoSourceSink()
{
	/* Remove all demand links having us as a destination. */
	Town *t;
	FOR_ALL_TOWNS(t) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			if (t->HasLinkTo(cid, this)) {
				t->cargo_links[cid].Erase(t->cargo_links[cid].Find(CargoLink(this)));
				InvalidateWindowData(WC_TOWN_VIEW, t->index, 1);
			}
		}
	}

	Industry *ind;
	FOR_ALL_INDUSTRIES(ind) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			if (ind->HasLinkTo(cid, this)) {
				ind->cargo_links[cid].Erase(ind->cargo_links[cid].Find(CargoLink(this)));
				InvalidateWindowData(WC_INDUSTRY_VIEW, ind->index, 1);
			}
		}
	}

	/* Decrement incoming link count for all link destinations. */
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		for (CargoLink *l = this->cargo_links[cid].Begin(); l != this->cargo_links[cid].End(); l++) {
			if (l->dest != NULL) l->dest->num_incoming_links[cid]--;
		}
	}
}

/** Rebuild the cached count of incoming cargo links. */
void RebuildCargoLinkCounts()
{
	/* Clear incoming link count of all towns and industries. */
	CargoSourceSink *source;
	FOR_ALL_TOWNS(source) MemSetT(source->num_incoming_links, 0, lengthof(source->num_incoming_links));
	FOR_ALL_INDUSTRIES(source) MemSetT(source->num_incoming_links, 0, lengthof(source->num_incoming_links));

	/* Count all incoming links. */
	FOR_ALL_TOWNS(source) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			for (CargoLink *l = source->cargo_links[cid].Begin(); l != source->cargo_links[cid].End(); l++) {
				if (l->dest != NULL && l->dest != source) l->dest->num_incoming_links[cid]++;
			}
		}
	}
	FOR_ALL_INDUSTRIES(source) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			for (CargoLink *l = source->cargo_links[cid].Begin(); l != source->cargo_links[cid].End(); l++) {
				if (l->dest != NULL && l->dest != source) l->dest->num_incoming_links[cid]++;
			}
		}
	}
}

/** Update the demand links of all towns and industries. */
void UpdateCargoLinks()
{
	if (_settings_game.economy.cargodest.mode == 0) return;

	Town *t;
	Industry *ind;

	/* Remove links that have become invalid. */
	FOR_ALL_TOWNS(t) RemoveInvalidLinks(t);
	FOR_ALL_INDUSTRIES(ind) RemoveInvalidLinks(ind);
			
	switch (_settings_game.economy.cargodest.distribution_mode) {
		case CDM_FIXED:
			/* Recalculate the number of expected links. */
			FOR_ALL_TOWNS(t) UpdateExpectedLinks(t);
			FOR_ALL_INDUSTRIES(ind) UpdateExpectedLinks(ind);
			
			/* Make sure each industry gets at at least some input cargo. */
			FOR_ALL_INDUSTRIES(ind) AddMissingIndustryLinks(ind);

			/* Update the demand link list. */
			FOR_ALL_TOWNS(t) UpdateCargoLinks(t);
			FOR_ALL_INDUSTRIES(ind) UpdateCargoLinks(ind);
			break;

		case CDM_REACHABLE:
			FOR_ALL_TOWNS(t) UpdateSpecialLinks(t);
			FOR_ALL_INDUSTRIES(ind) UpdateSpecialLinks(ind);
			
			if (++_route_graph_iteration == 0) _route_graph_iteration = 1;
			Station *st;
			FOR_ALL_STATIONS(st) CreateConnectedNewLinks(st);
			break;

		default:
			NOT_REACHED();
	}

	/* Recalculate links weights. */
	FOR_ALL_TOWNS(t) UpdateLinkWeights(t);
	FOR_ALL_INDUSTRIES(ind) UpdateLinkWeights(ind);

	InvalidateWindowClassesData(WC_TOWN_VIEW, 1);
	InvalidateWindowClassesData(WC_INDUSTRY_VIEW, 1);
}


/** Get a random destination tile index for this cargo. */
/* virtual */ TileArea Town::GetTileForDestination(CargoID cid)
{
	assert(this->cargo_accepted_weights[cid] != 0);

	/* Randomly choose a target square. */
	uint32 weight = RandomRange(this->cargo_accepted_weights[cid] - 1);

	/* Iterate over all grid squares till the chosen square is found. */
	uint32 weight_sum = 0;
	const TileArea &area = this->cargo_accepted.GetArea();
	TILE_AREA_LOOP(tile, area) {
		if (TileX(tile) % AcceptanceMatrix::GRID == 0 && TileY(tile) % AcceptanceMatrix::GRID == 0) {
			weight_sum += this->cargo_accepted_max_weight - (DistanceMax(this->xy_aligned, tile) / AcceptanceMatrix::GRID) * 2;
			/* Return tile area inside the grid square if this is the chosen square. */
			if (weight < weight_sum) return TileArea(tile + TileDiffXY(1, 1), 2, 2);
		}
	}

	/* Something went wrong here... */
	NOT_REACHED();
}

/** Enumerate all towns accepting a specific cargo. */
static bool EnumAcceptingTown(const Town *t, void *data)
{
	return t->AcceptsCargo((CargoID)(size_t)data);
}

/** Enumerate all industries accepting a specific cargo. */
static bool EnumAcceptingIndustry(const Industry *ind, void *data)
{
	return ind->AcceptsCargo((CargoID)(size_t)data);
}

/**
 * Move cargo to a station with destination information.
 * @param cid Cargo type.
 * @param amount[in,out] Cargo amount, return is actually moved cargo.
 * @param source_type Type of the cargo source.
 * @param source_id ID of the cargo source.
 * @param all_stations List of possible target stations.
 * @param src_tile Source tile.
 * @return True if the cargo was handled has having destinations.
 */
bool MoveCargoWithDestinationToStation(CargoID cid, uint *amount, SourceType source_type, SourceID source_id, const StationList *all_stations, TileIndex src_tile)
{
	if (!CargoHasDestinations(cid)) return false;

	CargoSourceSink *source = NULL;
	CargoSourceSink *dest = NULL;
	CargoLink *l = NULL;

	/* Company HQ doesn't have cargo links. */
	if (source_type != ST_HEADQUARTERS) {
		source = source_type == ST_TOWN ? static_cast<CargoSourceSink *>(Town::Get(source_id)) : static_cast<CargoSourceSink *>(Industry::Get(source_id));
		/* No links yet? Create cargo without destination. */
		if (source->cargo_links[cid].Length() == 0) return false;

		/* Randomly choose a cargo link. */
		uint weight = RandomRange(source->cargo_links_weight[cid] - 1);
		uint cur_sum = 0;

		for (l = source->cargo_links[cid].Begin(); l != source->cargo_links[cid].End(); l++) {
			cur_sum += l->weight;
			if (weight < cur_sum) {
				/* Link is valid if it is random destination or accepts the cargo. */
				if (l->dest == NULL || l->dest->AcceptsCargo(cid)) break;
			}
		}

		if (l != source->cargo_links[cid].End()) {
			l->amount.new_max += *amount;
			dest = l->dest;
		}
	}

	/* No destination or random destination? Try a random town. */
	if (dest == NULL) dest = Town::GetRandom(&EnumAcceptingTown, INVALID_TOWN, (void *)(size_t)cid);
	/* No luck? Try a random industry. */
	if (dest == NULL) dest = Industry::GetRandom(&EnumAcceptingIndustry, INVALID_INDUSTRY, (void *)(size_t)cid);
	/* Still no luck, nothing left to try. */
	if (dest == NULL) return false;

	/* Pick a tile that belongs to the destination. */
	TileArea dest_area = dest->GetTileForDestination(cid);

	/* Maximum pathfinder penalty based on distance. */
	uint r = RandomRange(_settings_game.economy.cargodest.max_route_penalty[1]);
	uint max_cost = _settings_game.economy.cargodest.max_route_penalty[0] + r;
	max_cost *= DistanceSquare(src_tile, dest_area.tile);

	/* Randomly determine the routing flags for the packet.
	 * Right now only the two lowest bits are defined. */
	byte flags = r & 0x3;

	/* Find a route to the destination. */
	StationID st, st_unload;
	bool found = false;
	RouteLink *route_link = YapfChooseRouteLink(cid, all_stations, src_tile, dest_area, &st, &st_unload, flags, &found, INVALID_ORDER, max_cost);

	/* Cargo can move to the destination (it might be direct local
	 * delivery though), count it as actually transported. */
	if (found && l != NULL) l->amount.new_act += *amount * (route_link == NULL ? 256 : Station::Get(st)->goods[cid].rating + 1) / 256;

	if (route_link == NULL) {
		/* No suitable link found (or direct delivery), nothing
		 * is moved to the station. */
		*amount = 0;
		return true;
	}

	/* Move cargo to the station. */
	Station *from = Station::Get(st);
	*amount = UpdateStationWaiting(from, cid, *amount * from->goods[cid].rating, source_type, source_id, dest_area.tile, dest->GetType(), dest->GetID(), route_link->GetOriginOrderId(), st_unload, flags);

	/* If this is a symmetric cargo type, try to generate some cargo going from
	 * destination to source as well. It's no error if that is not possible. */
	if (IsSymmetricCargo(cid)) {
		/* Try to find the matching cargo link back to the source. If no
		 * link is found, don't generate return traffic. */
		CargoLink *back_link = dest->cargo_links[cid].Find(CargoLink(source));
		if (back_link == dest->cargo_links[cid].End()) return true;

		back_link->amount.new_max += *amount;

		/* Find stations around the new source area. */
		StationFinder stf(dest_area);
		TileIndex tile = dest_area.tile;

		/* The the new destination area. */
		switch (source_type) {
			case ST_INDUSTRY:
				dest_area = static_cast<Industry *>(source)->location;
				break;
			case ST_TOWN:
				dest_area = TileArea(src_tile, 2, 2);
				break;
			case ST_HEADQUARTERS:
				dest_area = TileArea(Company::Get(source_id)->location_of_HQ, 2, 2);
				break;
		}

		/* Find a route and update transported amount if found. */
		route_link = YapfChooseRouteLink(cid, stf.GetStations(), tile, dest_area, &st, &st_unload, flags, &found, INVALID_ORDER, max_cost);
		if (found) back_link->amount.new_act += *amount;

		if (route_link != NULL) {
			/* Found a back link, move to station. */
			UpdateStationWaiting(Station::Get(st), cid, *amount * 256, dest->GetType(), dest->GetID(), dest_area.tile, source_type, source_id, route_link->GetOriginOrderId(), st_unload, flags);
		}
	}

	return true;
}

/**
 * Get the current best route link for a cargo packet at a station.
 * @param st Station the route starts at.
 * @param cid Cargo type.
 * @param cp Cargo packet with destination information.
 * @param order Incoming order of the cargo packet.
 * @param[out] found Set to true if a route was found.
 * @return The preferred route link or NULL if either no suitable link found or the station is the final destination.
 */
RouteLink *FindRouteLinkForCargo(Station *st, CargoID cid, const CargoPacket *cp, StationID *next_unload, OrderID order, bool *found)
{
	if (cp->DestinationID() == INVALID_SOURCE) return NULL;

	StationList sl;
	*sl.Append() = st;

	TileArea area = (cp->DestinationType() == ST_INDUSTRY) ? Industry::Get(cp->DestinationID())->location : TileArea(cp->DestinationXY(), 2, 2);
	return YapfChooseRouteLink(cid, &sl, st->xy, area, NULL, next_unload, cp->Flags(), found, order);
}


/* Initialize the RouteLink-pool */
RouteLinkPool _routelink_pool("RouteLink");
INSTANTIATE_POOL_METHODS(RouteLink)

/**
 * Update or create a single route link for a specific vehicle and cargo.
 * @param v The vehicle.
 * @param cargos Create links for the cargo types whose bit is set.
 * @param from Originating station.
 * @param from_oid Originating order.
 * @param to_id Destination station ID.
 * @param to_oid Destination order.
 * @param travel_time Travel time for the route.
 */
void UpdateVehicleRouteLinks(const Vehicle *v, uint32 cargos, Station *from, OrderID from_oid, StationID to_id, OrderID to_oid, uint32 travel_time)
{
	CargoID cid;
	FOR_EACH_SET_CARGO_ID(cid, cargos) {
		/* Skip cargo types that don't have destinations enabled. */
		if (!CargoHasDestinations(cid)) continue;

		RouteLinkList::iterator link;
		for (link = from->goods[cid].routes.begin(); link != from->goods[cid].routes.end(); ++link) {
			if ((*link)->GetOriginOrderId() == from_oid) {
				/* Update destination if necessary. */
				(*link)->SetDestination(to_id, to_oid);
				(*link)->UpdateTravelTime(travel_time);
				break;
			}
		}

		/* No link found? Append a new one. */
		if (link == from->goods[cid].routes.end() && RouteLink::CanAllocateItem()) {
			from->goods[cid].routes.push_back(new RouteLink(to_id, from_oid, to_oid, v->owner, travel_time, v->type));
		}
	}
}

/**
 * Update route links after a vehicle has arrived at a station.
 * @param v The vehicle.
 * @param arrived_at The station the vehicle arrived at.
 */
void UpdateVehicleRouteLinks(const Vehicle *v, StationID arrived_at)
{
	/* Only update links if we have valid previous station and orders. */
	if (v->last_station_loaded == INVALID_STATION || v->last_order_id == INVALID_ORDER || v->current_order.index == INVALID_ORDER) return;
	/* Loop? Not good. */
	if (v->last_station_loaded == arrived_at) return;

	Station *from = Station::Get(v->last_station_loaded);
	Station *to = Station::Get(arrived_at);

	/* Update incoming route link. */
	UpdateVehicleRouteLinks(v, v->vcache.cached_cargo_mask, from, v->last_order_id, arrived_at, v->current_order.index, v->travel_time);

	/* Update outgoing links. */
	CargoID cid;
	FOR_EACH_SET_CARGO_ID(cid, v->vcache.cached_cargo_mask) {
		/* Skip cargo types that don't have destinations enabled. */
		if (!CargoHasDestinations(cid)) continue;

		for (RouteLinkList::iterator link = to->goods[cid].routes.begin(); link != to->goods[cid].routes.end(); ++link) {
			if ((*link)->GetOriginOrderId() == v->current_order.index) {
				(*link)->VehicleArrived();
				break;
			}
		}
	}
}

/**
 * Pre-fill the route links from the orders of a vehicle.
 * @param v The vehicle to get the orders from.
 */
void PrefillRouteLinks(const Vehicle *v)
{
	if (_settings_game.economy.cargodest.mode == 0) return;
	if (v->orders.list == NULL || v->orders.list->GetNumOrders() < 2) return;

	/* Can't pre-fill if the vehicle has refit or conditional orders. */
	uint count = 0;
	Order *order;
	FOR_VEHICLE_ORDERS(v, order) {
		if (order->IsType(OT_GOTO_DEPOT) && order->IsRefit()) return;
		if (order->IsType(OT_CONDITIONAL)) return;
		if ((order->IsType(OT_AUTOMATIC) || order->IsType(OT_GOTO_STATION)) && (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) count++;
	}

	/* Increment count by one to account for the circular nature of the order list. */
	if (count > 0) count++;

	/* Collect cargo types carried by all vehicles in the shared order list. */
	uint32 transported_cargos = 0;
	for (Vehicle *u = v->FirstShared(); u != NULL; u = u->NextShared()) {
		transported_cargos |= u->vcache.cached_cargo_mask;
	}

	/* Loop over all orders to update/pre-fill the route links. */
	order = v->orders.list->GetFirstOrder();
	Order *prev_order = NULL;
	do {
		/* Goto station or automatic order and not a go via-order, consider as destination. */
		if ((order->IsType(OT_AUTOMATIC) || order->IsType(OT_GOTO_STATION)) && (order->GetNonStopType() & ONSF_NO_STOP_AT_DESTINATION_STATION) == 0) {
			/* Previous destination is set and the new destination is different, create/update route links. */
			if (prev_order != NULL && prev_order != order && prev_order->GetDestination() != order->GetDestination()) {
				Station *from = Station::Get(prev_order->GetDestination());
				Station *to = Station::Get(order->GetDestination());
				/* Use DistanceManhatten * DAY_TICKS as a stupid guess for the initial travel time. */
				UpdateVehicleRouteLinks(v, transported_cargos, from, prev_order->index, order->GetDestination(), order->index, DistanceManhattan(from->xy, to->xy) * DAY_TICKS);
			}

			prev_order = order;
			count--;
		}

		/* Get next order, wrap around if necessary. */
		order = order->next;
		if (order == NULL) order = v->orders.list->GetFirstOrder();
	} while (count > 0);
}

/**
 * Remove all route links to and from a station.
 * @param station Station being removed.
 */
void InvalidateStationRouteLinks(Station *station)
{
	/* Delete all outgoing links. */
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		for (RouteLinkList::iterator link = station->goods[cid].routes.begin(); link != station->goods[cid].routes.end(); ++link) {
			delete *link;
		}
	}

	/* Delete all incoming link. */
	Station *st_from;
	FOR_ALL_STATIONS(st_from) {
		if (st_from == station) continue;

		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			/* Don't increment the iterator directly in the for loop as we don't want to increment when deleting a link. */
			for (RouteLinkList::iterator link = st_from->goods[cid].routes.begin(); link != st_from->goods[cid].routes.end(); ) {
				if ((*link)->GetDestination() == station->index) {
					delete *link;
					link = st_from->goods[cid].routes.erase(link);
				} else {
					++link;
				}
			}
		}
	}
}

/**
 * Remove all route links referencing an order.
 * @param order The order being removed.
 */
void InvalidateOrderRouteLinks(OrderID order)
{
	Station *st;
	FOR_ALL_STATIONS(st) {
		for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
			/* Don't increment the iterator directly in the for loop as we don't want to increment when deleting a link. */
			for (RouteLinkList::iterator link = st->goods[cid].routes.begin(); link != st->goods[cid].routes.end(); ) {
				if ((*link)->GetOriginOrderId() == order || (*link)->GetDestOrderId() == order) {
					delete *link;
					link = st->goods[cid].routes.erase(link);
				} else {
					++link;
				}
			}
		}
	}
}

/** Age and expire route links of a station. */
void AgeRouteLinks(Station *st)
{
	for (CargoID cid = 0; cid < NUM_CARGO; cid++) {
		/* Don't increment the iterator directly in the for loop as we don't want to increment when deleting a link. */
		for (RouteLinkList::iterator link = st->goods[cid].routes.begin(); link != st->goods[cid].routes.end(); ) {
			if ((*link)->wait_time++ > _settings_game.economy.cargodest.max_route_age) {
				delete *link;
				link = st->goods[cid].routes.erase(link);
			} else {
				++link;
			}
		}
	}
}
