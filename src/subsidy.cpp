/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file subsidy.cpp Handling of subsidies. */

#include "stdafx.h"
#include "company_func.h"
#include "industry.h"
#include "town.h"
#include "news_func.h"
#include "ai/ai.hpp"
#include "station_base.h"
#include "cargotype.h"
#include "strings_func.h"
#include "window_func.h"
#include "subsidy_base.h"
#include "subsidy_func.h"
#include "core/pool_func.hpp"
#include "core/random_func.hpp"
#include "game/game.hpp"
#include "command_func.h"

#include "table/strings.h"

SubsidyPool _subsidy_pool("Subsidy");
INSTANTIATE_POOL_METHODS(Subsidy)

/**
 * Marks subsidy as awarded, creates news and AI event
 * @param company awarded company
 */
void Subsidy::AwardTo(CompanyID company)
{
	assert(!this->IsAwarded());

	this->awarded = company;
	this->remaining = SUBSIDY_CONTRACT_MONTHS;

	char company_name[MAX_LENGTH_COMPANY_NAME_CHARS * MAX_CHAR_LENGTH];
	SetDParam(0, company);
	GetString(company_name, STR_COMPANY_NAME, lastof(company_name));

	char *cn = strdup(company_name);

	/* Add a news item */
	Pair reftype = SetupSubsidyDecodeParam(this, false);
	InjectDParam(1);

	SetDParamStr(0, cn);
	AddNewsItem(
		STR_NEWS_SERVICE_SUBSIDY_AWARDED_HALF + _settings_game.difficulty.subsidy_multiplier,
		NS_SUBSIDIES,
		(NewsReferenceType)reftype.a, this->src, (NewsReferenceType)reftype.b, this->dst,
		cn
	);
	AI::BroadcastNewEvent(new ScriptEventSubsidyAwarded(this->index));
	Game::NewEvent(new ScriptEventSubsidyAwarded(this->index));

	InvalidateWindowData(WC_SUBSIDIES_LIST, 0);
}

Pair SetupSubsidyDecodeParam(const Subsidy *s, bool mode)
{
	NewsReferenceType reftype1 = NR_NONE;
	NewsReferenceType reftype2 = NR_NONE;

	/* if mode is false, use the singular form */
	const CargoSpec *cs = CargoSpec::Get(s->cargo_type);
	SetDParam(0, mode ? cs->name : cs->name_single);

	switch (s->src_type) {
		case ST_INDUSTRY:
			reftype1 = NR_INDUSTRY;
			SetDParam(1, STR_INDUSTRY_NAME);
			break;
		case ST_TOWN:
			reftype1 = NR_TOWN;
			SetDParam(1, STR_TOWN_NAME);
			break;
		default: NOT_REACHED();
	}
	SetDParam(2, s->src);

	switch (s->dst_type) {
		case ST_INDUSTRY:
			reftype2 = NR_INDUSTRY;
			SetDParam(4, STR_INDUSTRY_NAME);
			break;
		case ST_TOWN:
			reftype2 = NR_TOWN;
			SetDParam(4, STR_TOWN_NAME);
			break;
		default: NOT_REACHED();
	}
	SetDParam(5, s->dst);

	Pair p;
	p.a = reftype1;
	p.b = reftype2;
	return p;
}

/**
 * Sets a flag indicating that given town/industry is part of subsidised route.
 * @param type is it a town or an industry?
 * @param index index of town/industry
 * @param flag flag to set
 */
static inline void SetPartOfSubsidyFlag(SourceType type, SourceID index, PartOfSubsidy flag)
{
	switch (type) {
		case ST_INDUSTRY: Industry::Get(index)->part_of_subsidy |= flag; return;
		case ST_TOWN:         Town::Get(index)->part_of_subsidy |= flag; return;
		default: NOT_REACHED();
	}
}

void RebuildSubsidisedSourceAndDestinationCache()
{
	Town *t;
	FOR_ALL_TOWNS(t) t->part_of_subsidy = POS_NONE;

	Industry *i;
	FOR_ALL_INDUSTRIES(i) i->part_of_subsidy = POS_NONE;

	const Subsidy *s;
	FOR_ALL_SUBSIDIES(s) {
		SetPartOfSubsidyFlag(s->src_type, s->src, POS_SRC);
		SetPartOfSubsidyFlag(s->dst_type, s->dst, POS_DST);
	}
}

void DeleteSubsidyWith(SourceType type, SourceID index)
{
	bool dirty = false;

	Subsidy *s;
	FOR_ALL_SUBSIDIES(s) {
		if ((s->src_type == type && s->src == index) || (s->dst_type == type && s->dst == index)) {
			delete s;
			dirty = true;
		}
	}

	if (dirty) {
		InvalidateWindowData(WC_SUBSIDIES_LIST, 0);
		RebuildSubsidisedSourceAndDestinationCache();
	}
}

static bool CheckSubsidyDuplicate(CargoID cargo, SourceType src_type, SourceID src, SourceType dst_type, SourceID dst)
{
	const Subsidy *s;
	FOR_ALL_SUBSIDIES(s) {
		if (s->cargo_type == cargo &&
				s->src_type == src_type && s->src == src &&
				s->dst_type == dst_type && s->dst == dst) {
			return true;
		}
	}
	return false;
}

/** Checks if the source and destination of a subsidy are inside the distance limit.
 * @param src_type Type of #src.
 * @param src      Index of source.
 * @param dst_type Type of #dst.
 * @param dst      Index of destination.
 * @return True if they are inside the distance limit.
 */
static bool CheckSubsidyDistance(SourceType src_type, SourceID src, SourceType dst_type, SourceID dst)
{
	TileIndex tile_src = (src_type == ST_TOWN) ? Town::Get(src)->xy : Industry::Get(src)->location.tile;
	TileIndex tile_dst = (dst_type == ST_TOWN) ? Town::Get(dst)->xy : Industry::Get(dst)->location.tile;

	return (DistanceManhattan(tile_src, tile_dst) <= SUBSIDY_MAX_DISTANCE);
}

/** Creates a subsidy with the given parameters.
 * @param cid      Subsidised cargo.
 * @param src_type Type of #src.
 * @param src      Index of source.
 * @param dst_type Type of #dst.
 * @param dst      Index of destination.
 */
void CreateSubsidy(CargoID cid, SourceType src_type, SourceID src, SourceType dst_type, SourceID dst)
{
	Subsidy *s = new Subsidy();
	s->cargo_type = cid;
	s->src_type = src_type;
	s->src = src;
	s->dst_type = dst_type;
	s->dst = dst;
	s->remaining = SUBSIDY_OFFER_MONTHS;
	s->awarded = INVALID_COMPANY;

	Pair reftype = SetupSubsidyDecodeParam(s, false);
	AddNewsItem(STR_NEWS_SERVICE_SUBSIDY_OFFERED, NS_SUBSIDIES, (NewsReferenceType)reftype.a, s->src, (NewsReferenceType)reftype.b, s->dst);
	SetPartOfSubsidyFlag(s->src_type, s->src, POS_SRC);
	SetPartOfSubsidyFlag(s->dst_type, s->dst, POS_DST);
	AI::BroadcastNewEvent(new ScriptEventSubsidyOffer(s->index));
	Game::NewEvent(new ScriptEventSubsidyOffer(s->index));

	InvalidateWindowData(WC_SUBSIDIES_LIST, 0);
}

/**
 * Create a new subsidy.
 * @param tile unused.
 * @param flags type of operation
 * @param p1 various bitstuffed elements
 * - p1 = (bit  0 -  7) - SourceType of source.
 * - p1 = (bit  8 - 23) - SourceID of source.
 * - p1 = (bit 24 - 31) - CargoID of subsidy.
 * @param p2 various bitstuffed elements
 * - p2 = (bit  0 -  7) - SourceType of destination.
 * - p2 = (bit  8 - 23) - SourceID of destionation.
 * @param text unused.
 * @return the cost of this operation or an error
 */
CommandCost CmdCreateSubsidy(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	if (!Subsidy::CanAllocateItem()) return CMD_ERROR;

	CargoID cid = GB(p1, 24, 8);
	SourceType src_type = (SourceType)GB(p1, 0, 8);
	SourceID src = GB(p1, 8, 16);
	SourceType dst_type = (SourceType)GB(p2, 0, 8);
	SourceID dst = GB(p2, 8, 16);

	if (_current_company != OWNER_DEITY) return CMD_ERROR;

	if (cid >= NUM_CARGO || !::CargoSpec::Get(cid)->IsValid()) return CMD_ERROR;

	switch (src_type) {
		case ST_TOWN:
			if (!Town::IsValidID(src)) return CMD_ERROR;
			break;
		case ST_INDUSTRY:
			if (!Industry::IsValidID(src)) return CMD_ERROR;
			break;
		default:
			return CMD_ERROR;
	}
	switch (dst_type) {
		case ST_TOWN:
			if (!Town::IsValidID(dst)) return CMD_ERROR;
			break;
		case ST_INDUSTRY:
			if (!Industry::IsValidID(dst)) return CMD_ERROR;
			break;
		default:
			return CMD_ERROR;
	}

	if (flags & DC_EXEC) {
		CreateSubsidy(cid, src_type, src, dst_type, dst);
	}

	return CommandCost();
}

/** Tries to create a passenger subsidy between two towns.
 * @return True iff the subsidy was created.
 */
bool FindSubsidyPassengerRoute()
{
	if (!Subsidy::CanAllocateItem()) return false;

	const Town *src = Town::GetRandom();
	if (src->population < SUBSIDY_PAX_MIN_POPULATION ||
			src->GetPercentTransported(CT_PASSENGERS) > SUBSIDY_MAX_PCT_TRANSPORTED) {
		return false;
	}

	const Town *dst = Town::GetRandom();
	if (dst->population < SUBSIDY_PAX_MIN_POPULATION || src == dst) {
		return false;
	}

	if (DistanceManhattan(src->xy, dst->xy) > SUBSIDY_MAX_DISTANCE) return false;
	if (CheckSubsidyDuplicate(CT_PASSENGERS, ST_TOWN, src->index, ST_TOWN, dst->index)) return false;

	CreateSubsidy(CT_PASSENGERS, ST_TOWN, src->index, ST_TOWN, dst->index);

	return true;
}

bool FindSubsidyCargoDestination(CargoID cid, SourceType src_type, SourceID src);


/** Tries to create a cargo subsidy with a town as source.
 * @return True iff the subsidy was created.
 */
bool FindSubsidyTownCargoRoute()
{
	if (!Subsidy::CanAllocateItem()) return false;

	SourceType src_type = ST_TOWN;

	/* Select a random town. */
	const Town *src_town = Town::GetRandom();

	uint32 town_cargo_produced = src_town->cargo_produced;

	/* Passenger subsidies are not handled here. */
	ClrBit(town_cargo_produced, CT_PASSENGERS);

	/* Choose a random cargo that is produced in the town. */
	uint8 cargo_number = RandomRange(CountBits(town_cargo_produced));
	CargoID cid;
	FOR_EACH_SET_CARGO_ID(cid, town_cargo_produced) {
		if (cargo_number == 0) break;
		cargo_number--;
	}

	/* Avoid using invalid NewGRF cargoes. */
	if (!CargoSpec::Get(cid)->IsValid()) return false;

	/* Quit if the percentage transported is large enough. */
	if (src_town->GetPercentTransported(cid) > SUBSIDY_MAX_PCT_TRANSPORTED) return false;

	SourceID src = src_town->index;

	return FindSubsidyCargoDestination(cid, src_type, src);
}

/** Tries to create a cargo subsidy with an industry as source.
 * @return True iff the subsidy was created.
 */
bool FindSubsidyIndustryCargoRoute()
{
	if (!Subsidy::CanAllocateItem()) return false;

	SourceType src_type = ST_INDUSTRY;

	/* Select a random industry. */
	const Industry *src_ind = Industry::GetRandom();
	if (src_ind == NULL) return false;

	uint trans, total;

	CargoID cid;

	/* Randomize cargo type */
	if (src_ind->produced_cargo[1] != CT_INVALID && HasBit(Random(), 0)) {
		cid = src_ind->produced_cargo[1];
		trans = src_ind->last_month_pct_transported[1];
		total = src_ind->last_month_production[1];
	} else {
		cid = src_ind->produced_cargo[0];
		trans = src_ind->last_month_pct_transported[0];
		total = src_ind->last_month_production[0];
	}

	/* Quit if no production in this industry
	 * or if the pct transported is already large enough */
	if (total == 0 || trans > SUBSIDY_MAX_PCT_TRANSPORTED || cid == CT_INVALID) return false;

	SourceID src = src_ind->index;

	return FindSubsidyCargoDestination(cid, src_type, src);
}

/** Tries to find a suitable destination for the given source and cargo.
 * @param cid      Subsidized cargo.
 * @param src_type Type of #src.
 * @param src      Index of source.
 * @return True iff the subsidy was created.
 */
bool FindSubsidyCargoDestination(CargoID cid, SourceType src_type, SourceID src)
{
	/* Choose a random destination. Only consider towns if they can accept the cargo. */
	SourceType dst_type = (HasBit(_town_cargoes_accepted, cid) && Chance16(1, 2)) ? ST_TOWN : ST_INDUSTRY;

	SourceID dst;
	switch (dst_type) {
		case ST_TOWN: {
			/* Select a random town. */
			const Town *dst_town = Town::GetRandom();

			/* Check if the town can accept this cargo. */
			if (!HasBit(dst_town->cargo_accepted_total, cid)) return false;

			dst = dst_town->index;
			break;
		}

		case ST_INDUSTRY: {
			/* Select a random industry. */
			const Industry *dst_ind = Industry::GetRandom();

			/* The industry must accept the cargo */
			if (dst_ind == NULL ||
					(cid != dst_ind->accepts_cargo[0] &&
					 cid != dst_ind->accepts_cargo[1] &&
					 cid != dst_ind->accepts_cargo[2])) {
				return false;
			}

			dst = dst_ind->index;
			break;
		}

		default: NOT_REACHED();
	}

	/* Check that the source and the destination are not the same. */
	if (src_type == dst_type && src == dst) return false;

	/* Check distance between source and destination. */
	if (!CheckSubsidyDistance(src_type, src, dst_type, dst)) return false;

	/* Avoid duplicate subsidies. */
	if (CheckSubsidyDuplicate(cid, src_type, src, dst_type, dst)) return false;

	CreateSubsidy(cid, src_type, src, dst_type, dst);

	return true;
}

void SubsidyMonthlyLoop()
{
	bool modified = false;

	Subsidy *s;
	FOR_ALL_SUBSIDIES(s) {
		if (--s->remaining == 0) {
			if (!s->IsAwarded()) {
				Pair reftype = SetupSubsidyDecodeParam(s, true);
				AddNewsItem(STR_NEWS_OFFER_OF_SUBSIDY_EXPIRED, NS_SUBSIDIES, (NewsReferenceType)reftype.a, s->src, (NewsReferenceType)reftype.b, s->dst);
				AI::BroadcastNewEvent(new ScriptEventSubsidyOfferExpired(s->index));
				Game::NewEvent(new ScriptEventSubsidyOfferExpired(s->index));
			} else {
				if (s->awarded == _local_company) {
					Pair reftype = SetupSubsidyDecodeParam(s, true);
					AddNewsItem(STR_NEWS_SUBSIDY_WITHDRAWN_SERVICE, NS_SUBSIDIES, (NewsReferenceType)reftype.a, s->src, (NewsReferenceType)reftype.b, s->dst);
				}
				AI::BroadcastNewEvent(new ScriptEventSubsidyExpired(s->index));
				Game::NewEvent(new ScriptEventSubsidyExpired(s->index));
			}
			delete s;
			modified = true;
		}
	}

	if (modified) RebuildSubsidisedSourceAndDestinationCache();

	bool passenger_subsidy = false;
	bool town_subsidy = false;
	bool industry_subsidy = false;

	int random_chance = RandomRange(16);

	if (random_chance < 2) {
		/* There is a 1/8 chance each month of generating a passenger subsidy. */
		int n = 1000;

		do {
			passenger_subsidy = FindSubsidyPassengerRoute();
		} while (!passenger_subsidy && n--);
	} else if (random_chance == 2) {
		/* Cargo subsidies with a town as a source have a 1/16 chance. */
		int n = 1000;

		do {
			town_subsidy = FindSubsidyTownCargoRoute();
		} while (!town_subsidy && n--);
	} else if (random_chance == 3) {
		/* Cargo subsidies with an industry as a source have a 1/16 chance. */
		int n = 1000;

		do {
			industry_subsidy = FindSubsidyTownCargoRoute();
		} while (!industry_subsidy && n--);
	}

	modified |= passenger_subsidy || town_subsidy || industry_subsidy;

	if (modified) InvalidateWindowData(WC_SUBSIDIES_LIST, 0);
}

/**
 * Tests whether given delivery is subsidised and possibly awards the subsidy to delivering company
 * @param cargo_type type of cargo
 * @param company company delivering the cargo
 * @param src_type type of #src
 * @param src index of source
 * @param st station where the cargo is delivered to
 * @return is the delivery subsidised?
 */
bool CheckSubsidised(CargoID cargo_type, CompanyID company, SourceType src_type, SourceID src, const Station *st)
{
	/* If the source isn't subsidised, don't continue */
	if (src == INVALID_SOURCE) return false;
	switch (src_type) {
		case ST_INDUSTRY:
			if (!(Industry::Get(src)->part_of_subsidy & POS_SRC)) return false;
			break;
		case ST_TOWN:
			if (!(    Town::Get(src)->part_of_subsidy & POS_SRC)) return false;
			break;
		default: return false;
	}

	/* Remember all towns near this station (at least one house in its catchment radius)
	 * which are destination of subsidised path. Do that only if needed */
	SmallVector<const Town *, 2> towns_near;
	if (!st->rect.IsEmpty()) {
		Subsidy *s;
		FOR_ALL_SUBSIDIES(s) {
			/* Don't create the cache if there is no applicable subsidy with town as destination */
			if (s->dst_type != ST_TOWN) continue;
			if (s->cargo_type != cargo_type || s->src_type != src_type || s->src != src) continue;
			if (s->IsAwarded() && s->awarded != company) continue;

			Rect rect = st->GetCatchmentRect();

			for (int y = rect.top; y <= rect.bottom; y++) {
				for (int x = rect.left; x <= rect.right; x++) {
					TileIndex tile = TileXY(x, y);
					if (!IsTileType(tile, MP_HOUSE)) continue;
					const Town *t = Town::GetByTile(tile);
					if (t->part_of_subsidy & POS_DST) towns_near.Include(t);
				}
			}
			break;
		}
	}

	bool subsidised = false;

	/* Check if there's a (new) subsidy that applies. There can be more subsidies triggered by this delivery!
	 * Think about the case that subsidies are A->B and A->C and station has both B and C in its catchment area */
	Subsidy *s;
	FOR_ALL_SUBSIDIES(s) {
		if (s->cargo_type == cargo_type && s->src_type == src_type && s->src == src && (!s->IsAwarded() || s->awarded == company)) {
			switch (s->dst_type) {
				case ST_INDUSTRY:
					for (const Industry * const *ip = st->industries_near.Begin(); ip != st->industries_near.End(); ip++) {
						if (s->dst == (*ip)->index) {
							assert((*ip)->part_of_subsidy & POS_DST);
							subsidised = true;
							if (!s->IsAwarded()) s->AwardTo(company);
						}
					}
					break;
				case ST_TOWN:
					for (const Town * const *tp = towns_near.Begin(); tp != towns_near.End(); tp++) {
						if (s->dst == (*tp)->index) {
							assert((*tp)->part_of_subsidy & POS_DST);
							subsidised = true;
							if (!s->IsAwarded()) s->AwardTo(company);
						}
					}
					break;
				default:
					NOT_REACHED();
			}
		}
	}

	return subsidised;
}
