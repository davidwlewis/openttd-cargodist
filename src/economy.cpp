/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file economy.cpp Handling of the economy. */

#include "stdafx.h"
#include "company_func.h"
#include "command_func.h"
#include "industry.h"
#include "town.h"
#include "news_func.h"
#include "network/network.h"
#include "network/network_func.h"
#include "vehicle_gui.h"
#include "ai/ai.hpp"
#include "aircraft.h"
#include "newgrf_engine.h"
#include "engine_base.h"
#include "ground_vehicle.hpp"
#include "newgrf_cargo.h"
#include "newgrf_sound.h"
#include "newgrf_industrytiles.h"
#include "newgrf_station.h"
#include "newgrf_airporttiles.h"
#include "object.h"
#include "group.h"
#include "strings_func.h"
#include "window_func.h"
#include "date_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "autoreplace_func.h"
#include "company_gui.h"
#include "signs_base.h"
#include "subsidy_base.h"
#include "subsidy_func.h"
#include "station_base.h"
#include "waypoint_base.h"
#include "economy_base.h"
#include "core/pool_func.hpp"
#include "newgrf.h"
#include "core/backup_type.hpp"
#include "cargo_type.h"
#include "water.h"
#include "game/game.hpp"

#include "table/strings.h"
#include "table/pricebase.h"


/* Initialize the cargo payment-pool */
CargoPaymentPool _cargo_payment_pool("CargoPayment");
INSTANTIATE_POOL_METHODS(CargoPayment)

/**
 * Multiply two integer values and shift the results to right.
 *
 * This function multiplies two integer values. The result is
 * shifted by the amount of shift to right.
 *
 * @param a The first integer
 * @param b The second integer
 * @param shift The amount to shift the value to right.
 * @return The shifted result
 */
static inline int32 BigMulS(const int32 a, const int32 b, const uint8 shift)
{
	return (int32)((int64)a * (int64)b >> shift);
}

typedef SmallVector<Industry *, 16> SmallIndustryList;

/**
 * Score info, values used for computing the detailed performance rating.
 */
const ScoreInfo _score_info[] = {
	{     120, 100}, // SCORE_VEHICLES
	{      80, 100}, // SCORE_STATIONS
	{   10000, 100}, // SCORE_MIN_PROFIT
	{   50000,  50}, // SCORE_MIN_INCOME
	{  100000, 100}, // SCORE_MAX_INCOME
	{   40000, 400}, // SCORE_DELIVERED
	{       8,  50}, // SCORE_CARGO
	{10000000,  50}, // SCORE_MONEY
	{  250000,  50}, // SCORE_LOAN
	{       0,   0}  // SCORE_TOTAL
};

int _score_part[MAX_COMPANIES][SCORE_END];
Economy _economy;
Prices _price;
Money _additional_cash_required;
static PriceMultipliers _price_base_multiplier;

/**
 * Calculate the value of the company. That is the value of all
 * assets (vehicles, stations, etc) and money minus the loan,
 * except when including_loan is \c false which is useful when
 * we want to calculate the value for bankruptcy.
 * @param c              the company to get the value of.
 * @param including_loan include the loan in the company value.
 * @return the value of the company.
 */
Money CalculateCompanyValue(const Company *c, bool including_loan)
{
	Owner owner = c->index;

	Station *st;
	uint num = 0;

	FOR_ALL_STATIONS(st) {
		if (st->owner == owner) num += CountBits((byte)st->facilities);
	}

	Money value = num * _price[PR_STATION_VALUE] * 25;

	Vehicle *v;
	FOR_ALL_VEHICLES(v) {
		if (v->owner != owner) continue;

		if (v->type == VEH_TRAIN ||
				v->type == VEH_ROAD ||
				(v->type == VEH_AIRCRAFT && Aircraft::From(v)->IsNormalAircraft()) ||
				v->type == VEH_SHIP) {
			value += v->value * 3 >> 1;
		}
	}

	/* Add real money value */
	if (including_loan) value -= c->current_loan;
	value += c->money;

	return max(value, (Money)1);
}

/**
 * if update is set to true, the economy is updated with this score
 *  (also the house is updated, should only be true in the on-tick event)
 * @param update the economy with calculated score
 * @param c company been evaluated
 * @return actual score of this company
 *
 */
int UpdateCompanyRatingAndValue(Company *c, bool update)
{
	Owner owner = c->index;
	int score = 0;

	memset(_score_part[owner], 0, sizeof(_score_part[owner]));

	/* Count vehicles */
	{
		Vehicle *v;
		Money min_profit = 0;
		bool min_profit_first = true;
		uint num = 0;

		FOR_ALL_VEHICLES(v) {
			if (v->owner != owner) continue;
			if (IsCompanyBuildableVehicleType(v->type) && v->IsPrimaryVehicle()) {
				if (v->profit_last_year > 0) num++; // For the vehicle score only count profitable vehicles
				if (v->age > 730) {
					/* Find the vehicle with the lowest amount of profit */
					if (min_profit_first || min_profit > v->profit_last_year) {
						min_profit = v->profit_last_year;
						min_profit_first = false;
					}
				}
			}
		}

		min_profit >>= 8; // remove the fract part

		_score_part[owner][SCORE_VEHICLES] = num;
		/* Don't allow negative min_profit to show */
		if (min_profit > 0) {
			_score_part[owner][SCORE_MIN_PROFIT] = ClampToI32(min_profit);
		}
	}

	/* Count stations */
	{
		uint num = 0;
		const Station *st;

		FOR_ALL_STATIONS(st) {
			/* Only count stations that are actually serviced */
			if (st->owner == owner && (st->time_since_load <= 20 || st->time_since_unload <= 20)) num += CountBits((byte)st->facilities);
		}
		_score_part[owner][SCORE_STATIONS] = num;
	}

	/* Generate statistics depending on recent income statistics */
	{
		int numec = min(c->num_valid_stat_ent, 12);
		if (numec != 0) {
			const CompanyEconomyEntry *cee = c->old_economy;
			Money min_income = cee->income + cee->expenses;
			Money max_income = cee->income + cee->expenses;

			do {
				min_income = min(min_income, cee->income + cee->expenses);
				max_income = max(max_income, cee->income + cee->expenses);
			} while (++cee, --numec);

			if (min_income > 0) {
				_score_part[owner][SCORE_MIN_INCOME] = ClampToI32(min_income);
			}

			_score_part[owner][SCORE_MAX_INCOME] = ClampToI32(max_income);
		}
	}

	/* Generate score depending on amount of transported cargo */
	{
		const CompanyEconomyEntry *cee;
		int numec;
		uint32 total_delivered;

		numec = min(c->num_valid_stat_ent, 4);
		if (numec != 0) {
			cee = c->old_economy;
			total_delivered = 0;
			do {
				total_delivered += cee->delivered_cargo;
			} while (++cee, --numec);

			_score_part[owner][SCORE_DELIVERED] = total_delivered;
		}
	}

	/* Generate score for variety of cargo */
	{
		uint num = CountBits(c->cargo_types);
		_score_part[owner][SCORE_CARGO] = num;
		if (update) c->cargo_types = 0;
	}

	/* Generate score for company's money */
	{
		if (c->money > 0) {
			_score_part[owner][SCORE_MONEY] = ClampToI32(c->money);
		}
	}

	/* Generate score for loan */
	{
		_score_part[owner][SCORE_LOAN] = ClampToI32(_score_info[SCORE_LOAN].needed - c->current_loan);
	}

	/* Now we calculate the score for each item.. */
	{
		int total_score = 0;
		int s;
		score = 0;
		for (ScoreID i = SCORE_BEGIN; i < SCORE_END; i++) {
			/* Skip the total */
			if (i == SCORE_TOTAL) continue;
			/*  Check the score */
			s = Clamp(_score_part[owner][i], 0, _score_info[i].needed) * _score_info[i].score / _score_info[i].needed;
			score += s;
			total_score += _score_info[i].score;
		}

		_score_part[owner][SCORE_TOTAL] = score;

		/*  We always want the score scaled to SCORE_MAX (1000) */
		if (total_score != SCORE_MAX) score = score * SCORE_MAX / total_score;
	}

	if (update) {
		c->old_economy[0].performance_history = score;
		UpdateCompanyHQ(c->location_of_HQ, score);
		c->old_economy[0].company_value = CalculateCompanyValue(c);
	}

	SetWindowDirty(WC_PERFORMANCE_DETAIL, 0);
	return score;
}

/**
 * Change the ownership of all the items of a company.
 * @param old_owner The company that gets removed.
 * @param new_owner The company to merge to, or INVALID_OWNER to remove the company.
 */
void ChangeOwnershipOfCompanyItems(Owner old_owner, Owner new_owner)
{
	/* We need to set _current_company to old_owner before we try to move
	 * the client. This is needed as it needs to know whether "you" really
	 * are the current local company. */
	Backup<CompanyByte> cur_company(_current_company, old_owner, FILE_LINE);
#ifdef ENABLE_NETWORK
	/* In all cases, make spectators of clients connected to that company */
	if (_networking) NetworkClientsToSpectators(old_owner);
#endif /* ENABLE_NETWORK */
	if (old_owner == _local_company) {
		/* Single player cheated to AI company.
		 * There are no specatators in single player, so we must pick some other company. */
		assert(!_networking);
		Backup<CompanyByte> cur_company(_current_company, FILE_LINE);
		Company *c;
		FOR_ALL_COMPANIES(c) {
			if (c->index != old_owner) {
				SetLocalCompany(c->index);
				break;
			}
		}
		cur_company.Restore();
		assert(old_owner != _local_company);
	}

	Town *t;

	assert(old_owner != new_owner);

	{
		Company *c;
		uint i;

		/* See if the old_owner had shares in other companies */
		FOR_ALL_COMPANIES(c) {
			for (i = 0; i < 4; i++) {
				if (c->share_owners[i] == old_owner) {
					/* Sell his shares */
					CommandCost res = DoCommand(0, c->index, 0, DC_EXEC | DC_BANKRUPT, CMD_SELL_SHARE_IN_COMPANY);
					/* Because we are in a DoCommand, we can't just execute another one and
					 *  expect the money to be removed. We need to do it ourself! */
					SubtractMoneyFromCompany(res);
				}
			}
		}

		/* Sell all the shares that people have on this company */
		Backup<CompanyByte> cur_company2(_current_company, FILE_LINE);
		c = Company::Get(old_owner);
		for (i = 0; i < 4; i++) {
			cur_company2.Change(c->share_owners[i]);
			if (_current_company != INVALID_OWNER) {
				/* Sell the shares */
				CommandCost res = DoCommand(0, old_owner, 0, DC_EXEC | DC_BANKRUPT, CMD_SELL_SHARE_IN_COMPANY);
				/* Because we are in a DoCommand, we can't just execute another one and
				 *  expect the money to be removed. We need to do it ourself! */
				SubtractMoneyFromCompany(res);
			}
		}
		cur_company2.Restore();
	}

	/* Temporarily increase the company's money, to be sure that
	 * removing his/her property doesn't fail because of lack of money.
	 * Not too drastically though, because it could overflow */
	if (new_owner == INVALID_OWNER) {
		Company::Get(old_owner)->money = UINT64_MAX >> 2; // jackpot ;p
	}

	Subsidy *s;
	FOR_ALL_SUBSIDIES(s) {
		if (s->awarded == old_owner) {
			if (new_owner == INVALID_OWNER) {
				delete s;
			} else {
				s->awarded = new_owner;
			}
		}
	}
	if (new_owner == INVALID_OWNER) RebuildSubsidisedSourceAndDestinationCache();

	/* Take care of rating in towns */
	FOR_ALL_TOWNS(t) {
		/* If a company takes over, give the ratings to that company. */
		if (new_owner != INVALID_OWNER) {
			if (HasBit(t->have_ratings, old_owner)) {
				if (HasBit(t->have_ratings, new_owner)) {
					/* use max of the two ratings. */
					t->ratings[new_owner] = max(t->ratings[new_owner], t->ratings[old_owner]);
				} else {
					SetBit(t->have_ratings, new_owner);
					t->ratings[new_owner] = t->ratings[old_owner];
				}
			}
		}

		/* Reset the ratings for the old owner */
		t->ratings[old_owner] = RATING_INITIAL;
		ClrBit(t->have_ratings, old_owner);
	}

	{
		Vehicle *v;
		FOR_ALL_VEHICLES(v) {
			if (v->owner == old_owner && IsCompanyBuildableVehicleType(v->type)) {
				if (new_owner == INVALID_OWNER) {
					if (v->Previous() == NULL) delete v;
				} else {
					if (v->IsEngineCountable()) GroupStatistics::CountEngine(v, -1);
					if (v->IsPrimaryVehicle()) GroupStatistics::CountVehicle(v, -1);
				}
			}
		}
	}

	/* In all cases clear replace engine rules.
	 * Even if it was copied, it could interfere with new owner's rules */
	RemoveAllEngineReplacementForCompany(Company::Get(old_owner));

	if (new_owner == INVALID_OWNER) {
		RemoveAllGroupsForCompany(old_owner);
	} else {
		Group *g;
		FOR_ALL_GROUPS(g) {
			if (g->owner == old_owner) g->owner = new_owner;
		}
	}

	{
		FreeUnitIDGenerator unitidgen[] = {
			FreeUnitIDGenerator(VEH_TRAIN, new_owner), FreeUnitIDGenerator(VEH_ROAD,     new_owner),
			FreeUnitIDGenerator(VEH_SHIP,  new_owner), FreeUnitIDGenerator(VEH_AIRCRAFT, new_owner)
		};

		Vehicle *v;
		FOR_ALL_VEHICLES(v) {
			if (v->owner == old_owner && IsCompanyBuildableVehicleType(v->type)) {
				assert(new_owner != INVALID_OWNER);

				v->owner = new_owner;
				v->colourmap = PAL_NONE;

				if (v->IsEngineCountable()) {
					GroupStatistics::CountEngine(v, 1);
				}
				if (v->IsPrimaryVehicle()) {
					GroupStatistics::CountVehicle(v, 1);
					v->unitnumber = unitidgen[v->type].NextID();
				}

				/* Invalidate the vehicle's cargo payment "owner cache". */
				if (v->cargo_payment != NULL) v->cargo_payment->owner = NULL;
			}
		}

		if (new_owner != INVALID_OWNER) GroupStatistics::UpdateAutoreplace(new_owner);
	}

	/*  Change ownership of tiles */
	{
		TileIndex tile = 0;
		do {
			ChangeTileOwner(tile, old_owner, new_owner);
		} while (++tile != MapSize());

		if (new_owner != INVALID_OWNER) {
			/* Update all signals because there can be new segment that was owned by two companies
			 * and signals were not propagated
			 * Similiar with crossings - it is needed to bar crossings that weren't before
			 * because of different owner of crossing and approaching train */
			tile = 0;

			do {
				if (IsTileType(tile, MP_RAILWAY) && IsTileOwner(tile, new_owner) && HasSignals(tile)) {
					TrackBits tracks = GetTrackBits(tile);
					do { // there may be two tracks with signals for TRACK_BIT_HORZ and TRACK_BIT_VERT
						Track track = RemoveFirstTrack(&tracks);
						if (HasSignalOnTrack(tile, track)) AddTrackToSignalBuffer(tile, track, new_owner);
					} while (tracks != TRACK_BIT_NONE);
				} else if (IsLevelCrossingTile(tile) && IsTileOwner(tile, new_owner)) {
					UpdateLevelCrossing(tile);
				}
			} while (++tile != MapSize());
		}

		/* update signals in buffer */
		UpdateSignalsInBuffer();
	}

	/* convert owner of stations (including deleted ones, but excluding buoys) */
	Station *st;
	FOR_ALL_STATIONS(st) {
		if (st->owner == old_owner) {
			/* if a company goes bankrupt, set owner to OWNER_NONE so the sign doesn't disappear immediately
			 * also, drawing station window would cause reading invalid company's colour */
			st->owner = new_owner == INVALID_OWNER ? OWNER_NONE : new_owner;
		}
	}

	/* do the same for waypoints (we need to do this here so deleted waypoints are converted too) */
	Waypoint *wp;
	FOR_ALL_WAYPOINTS(wp) {
		if (wp->owner == old_owner) {
			wp->owner = new_owner == INVALID_OWNER ? OWNER_NONE : new_owner;
		}
	}

	Sign *si;
	FOR_ALL_SIGNS(si) {
		if (si->owner == old_owner) si->owner = new_owner == INVALID_OWNER ? OWNER_NONE : new_owner;
	}

	/* Change colour of existing windows */
	if (new_owner != INVALID_OWNER) ChangeWindowOwner(old_owner, new_owner);

	cur_company.Restore();

	MarkWholeScreenDirty();
}

/**
 * Check for bankruptcy of a company. Called every three months.
 * @param c Company to check.
 */
static void CompanyCheckBankrupt(Company *c)
{
	/*  If the company has money again, it does not go bankrupt */
	if (c->money >= 0) {
		c->quarters_of_bankruptcy = 0;
		c->bankrupt_asked = 0;
		return;
	}

	c->quarters_of_bankruptcy++;

	switch (c->quarters_of_bankruptcy) {
		case 0:
		case 1:
			break;

		case 2: {
			CompanyNewsInformation *cni = MallocT<CompanyNewsInformation>(1);
			cni->FillData(c);
			SetDParam(0, STR_NEWS_COMPANY_IN_TROUBLE_TITLE);
			SetDParam(1, STR_NEWS_COMPANY_IN_TROUBLE_DESCRIPTION);
			SetDParamStr(2, cni->company_name);
			AddCompanyNewsItem(STR_MESSAGE_NEWS_FORMAT, NS_COMPANY_TROUBLE, cni);
			AI::BroadcastNewEvent(new ScriptEventCompanyInTrouble(c->index));
			Game::NewEvent(new ScriptEventCompanyInTrouble(c->index));
			break;
		}

		case 3: {
			/* Check if the company has any value.. if not, declare it bankrupt
			 *  right now */
			Money val = CalculateCompanyValue(c, false);
			if (val > 0) {
				c->bankrupt_value = val;
				c->bankrupt_asked = 1 << c->index; // Don't ask the owner
				c->bankrupt_timeout = 0;
				break;
			}
			/* FALL THROUGH to case 4... */
		}
		default:
		case 4:
			if (!_networking && _local_company == c->index) {
				/* If we are in offline mode, leave the company playing. Eg. there
				 * is no THE-END, otherwise mark the client as spectator to make sure
				 * he/she is no long in control of this company. However... when you
				 * join another company (cheat) the "unowned" company can bankrupt. */
				c->bankrupt_asked = MAX_UVALUE(CompanyMask);
				break;
			}

			/* Actually remove the company, but not when we're a network client.
			 * In case of network clients we will be getting a command from the
			 * server. It is done in this way as we are called from the
			 * StateGameLoop which can't change the current company, and thus
			 * updating the local company triggers an assert later on. In the
			 * case of a network game the command will be processed at a time
			 * that changing the current company is okay. In case of single
			 * player we are sure (the above check) that we are not the local
			 * company and thus we won't be moved. */
			if (!_networking || _network_server) DoCommandP(0, 2 | (c->index << 16), CRR_BANKRUPT, CMD_COMPANY_CTRL);
			break;
	}
}

/**
 * Update the finances of all companies.
 * Pay for the stations, update the history graph, update ratings and company values, and deal with bankruptcy.
 */
static void CompaniesGenStatistics()
{
	Station *st;

	Backup<CompanyByte> cur_company(_current_company, FILE_LINE);
	Company *c;

	if (!_settings_game.economy.infrastructure_maintenance) {
		FOR_ALL_STATIONS(st) {
			cur_company.Change(st->owner);
			CommandCost cost(EXPENSES_PROPERTY, _price[PR_STATION_VALUE] >> 1);
			SubtractMoneyFromCompany(cost);
		}
	} else {
		/* Improved monthly infrastructure costs. */
		FOR_ALL_COMPANIES(c) {
			cur_company.Change(c->index);

			CommandCost cost(EXPENSES_PROPERTY);
			for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
				if (c->infrastructure.rail[rt] != 0) cost.AddCost(RailMaintenanceCost(rt, c->infrastructure.rail[rt]));
			}
			cost.AddCost(SignalMaintenanceCost(c->infrastructure.signal));
			for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
				if (c->infrastructure.road[rt] != 0) cost.AddCost(RoadMaintenanceCost(rt, c->infrastructure.road[rt]));
			}
			cost.AddCost(CanalMaintenanceCost(c->infrastructure.water));
			cost.AddCost(StationMaintenanceCost(c->infrastructure.station));
			cost.AddCost(AirportMaintenanceCost(c->index));

			SubtractMoneyFromCompany(cost);
		}
	}
	cur_company.Restore();

	/* Only run the economic statics and update company stats every 3rd month (1st of quarter). */
	if (!HasBit(1 << 0 | 1 << 3 | 1 << 6 | 1 << 9, _cur_month)) return;

	FOR_ALL_COMPANIES(c) {
		memmove(&c->old_economy[1], &c->old_economy[0], sizeof(c->old_economy) - sizeof(c->old_economy[0]));
		c->old_economy[0] = c->cur_economy;
		memset(&c->cur_economy, 0, sizeof(c->cur_economy));

		if (c->num_valid_stat_ent != MAX_HISTORY_QUARTERS) c->num_valid_stat_ent++;

		UpdateCompanyRatingAndValue(c, true);
		if (c->block_preview != 0) c->block_preview--;
		CompanyCheckBankrupt(c);
	}

	SetWindowDirty(WC_INCOME_GRAPH, 0);
	SetWindowDirty(WC_OPERATING_PROFIT, 0);
	SetWindowDirty(WC_DELIVERED_CARGO, 0);
	SetWindowDirty(WC_PERFORMANCE_HISTORY, 0);
	SetWindowDirty(WC_COMPANY_VALUE, 0);
	SetWindowDirty(WC_COMPANY_LEAGUE, 0);
}

/**
 * Add monthly inflation
 * @param check_year Shall the inflation get stopped after 170 years?
 */
void AddInflation(bool check_year)
{
	/* The cargo payment inflation differs from the normal inflation, so the
	 * relative amount of money you make with a transport decreases slowly over
	 * the 170 years. After a few hundred years we reach a level in which the
	 * games will become unplayable as the maximum income will be less than
	 * the minimum running cost.
	 *
	 * Furthermore there are a lot of inflation related overflows all over the
	 * place. Solving them is hardly possible because inflation will always
	 * reach the overflow threshold some day. So we'll just perform the
	 * inflation mechanism during the first 170 years (the amount of years that
	 * one had in the original TTD) and stop doing the inflation after that
	 * because it only causes problems that can't be solved nicely and the
	 * inflation doesn't add anything after that either; it even makes playing
	 * it impossible due to the diverging cost and income rates.
	 */
	if (check_year && (_cur_year - _settings_game.game_creation.starting_year) >= (ORIGINAL_MAX_YEAR - ORIGINAL_BASE_YEAR)) return;

	/* Approximation for (100 + infl_amount)% ** (1 / 12) - 100%
	 * scaled by 65536
	 * 12 -> months per year
	 * This is only a good approxiamtion for small values
	 */
	_economy.inflation_prices  += min((_economy.inflation_prices  * _economy.infl_amount    * 54) >> 16, MAX_INFLATION);
	_economy.inflation_payment += min((_economy.inflation_payment * _economy.infl_amount_pr * 54) >> 16, MAX_INFLATION);
}

/**
 * Computes all prices, payments and maximum loan.
 */
void RecomputePrices()
{
	/* Setup maximum loan */
	_economy.max_loan = (_settings_game.difficulty.max_loan * _economy.inflation_prices >> 16) / 50000 * 50000;

	/* Setup price bases */
	for (Price i = PR_BEGIN; i < PR_END; i++) {
		Money price = _price_base_specs[i].start_price;

		/* Apply difficulty settings */
		uint mod = 1;
		switch (_price_base_specs[i].category) {
			case PCAT_RUNNING:
				mod = _settings_game.difficulty.vehicle_costs;
				break;

			case PCAT_CONSTRUCTION:
				mod = _settings_game.difficulty.construction_cost;
				break;

			default: break;
		}
		switch (mod) {
			case 0: price *= 6; break;
			case 1: price *= 8; break; // normalised to 1 below
			case 2: price *= 9; break;
			default: NOT_REACHED();
		}

		/* Apply inflation */
		price = (int64)price * _economy.inflation_prices;

		/* Apply newgrf modifiers, remove fractional part of inflation, and normalise on medium difficulty. */
		int shift = _price_base_multiplier[i] - 16 - 3;
		if (shift >= 0) {
			price <<= shift;
		} else {
			price >>= -shift;
		}

		/* Make sure the price does not get reduced to zero.
		 * Zero breaks quite a few commands that use a zero
		 * cost to see whether something got changed or not
		 * and based on that cause an error. When the price
		 * is zero that fails even when things are done. */
		if (price == 0) {
			price = Clamp(_price_base_specs[i].start_price, -1, 1);
			/* No base price should be zero, but be sure. */
			assert(price != 0);
		}
		/* Store value */
		_price[i] = price;
	}

	/* Setup cargo payment */
	CargoSpec *cs;
	FOR_ALL_CARGOSPECS(cs) {
		cs->current_payment = ((int64)cs->initial_payment * _economy.inflation_payment) >> 16;
	}

	SetWindowClassesDirty(WC_BUILD_VEHICLE);
	SetWindowClassesDirty(WC_REPLACE_VEHICLE);
	SetWindowClassesDirty(WC_VEHICLE_DETAILS);
	SetWindowClassesDirty(WC_COMPANY_INFRASTRUCTURE);
	InvalidateWindowData(WC_PAYMENT_RATES, 0);
}

/** Let all companies pay the monthly interest on their loan. */
static void CompaniesPayInterest()
{
	const Company *c;

	Backup<CompanyByte> cur_company(_current_company, FILE_LINE);
	FOR_ALL_COMPANIES(c) {
		cur_company.Change(c->index);

		/* Over a year the paid interest should be "loan * interest percentage",
		 * but... as that number is likely not dividable by 12 (pay each month),
		 * one needs to account for that in the monthly fee calculations.
		 * To easily calculate what one should pay "this" month, you calculate
		 * what (total) should have been paid up to this month and you subtract
		 * whatever has been paid in the previous months. This will mean one month
		 * it'll be a bit more and the other it'll be a bit less than the average
		 * monthly fee, but on average it will be exact. */
		Money yearly_fee = c->current_loan * _economy.interest_rate / 100;
		Money up_to_previous_month = yearly_fee * _cur_month / 12;
		Money up_to_this_month = yearly_fee * (_cur_month + 1) / 12;

		SubtractMoneyFromCompany(CommandCost(EXPENSES_LOAN_INT, up_to_this_month - up_to_previous_month));

		SubtractMoneyFromCompany(CommandCost(EXPENSES_OTHER, _price[PR_STATION_VALUE] >> 2));
	}
	cur_company.Restore();
}

static void HandleEconomyFluctuations()
{
	if (_settings_game.difficulty.economy != 0) {
		/* When economy is Fluctuating, decrease counter */
		_economy.fluct--;
	} else if (EconomyIsInRecession()) {
		/* When it's Steady and we are in recession, end it now */
		_economy.fluct = -12;
	} else {
		/* No need to do anything else in other cases */
		return;
	}

	if (_economy.fluct == 0) {
		_economy.fluct = -(int)GB(Random(), 0, 2);
		AddNewsItem(STR_NEWS_BEGIN_OF_RECESSION, NS_ECONOMY);
	} else if (_economy.fluct == -12) {
		_economy.fluct = GB(Random(), 0, 8) + 312;
		AddNewsItem(STR_NEWS_END_OF_RECESSION, NS_ECONOMY);
	}
}


/**
 * Reset changes to the price base multipliers.
 */
void ResetPriceBaseMultipliers()
{
	memset(_price_base_multiplier, 0, sizeof(_price_base_multiplier));
}

/**
 * Change a price base by the given factor.
 * The price base is altered by factors of two.
 * NewBaseCost = OldBaseCost * 2^n
 * @param price Index of price base to change.
 * @param factor Amount to change by.
 */
void SetPriceBaseMultiplier(Price price, int factor)
{
	assert(price < PR_END);
	_price_base_multiplier[price] = Clamp(factor, MIN_PRICE_MODIFIER, MAX_PRICE_MODIFIER);
}

/**
 * Initialize the variables that will maintain the daily industry change system.
 * @param init_counter specifies if the counter is required to be initialized
 */
void StartupIndustryDailyChanges(bool init_counter)
{
	uint map_size = MapLogX() + MapLogY();
	/* After getting map size, it needs to be scaled appropriately and divided by 31,
	 * which stands for the days in a month.
	 * Using just 31 will make it so that a monthly reset (based on the real number of days of that month)
	 * would not be needed.
	 * Since it is based on "fractionnal parts", the leftover days will not make much of a difference
	 * on the overall total number of changes performed */
	_economy.industry_daily_increment = (1 << map_size) / 31;

	if (init_counter) {
		/* A new game or a savegame from an older version will require the counter to be initialized */
		_economy.industry_daily_change_counter = 0;
	}
}

void StartupEconomy()
{
	_economy.interest_rate = _settings_game.difficulty.initial_interest;
	_economy.infl_amount = _settings_game.difficulty.initial_interest;
	_economy.infl_amount_pr = max(0, _settings_game.difficulty.initial_interest - 1);
	_economy.fluct = GB(Random(), 0, 8) + 168;

	/* Set up prices */
	RecomputePrices();

	StartupIndustryDailyChanges(true); // As we are starting a new game, initialize the counter too

}

/**
 * Resets economy to initial values
 */
void InitializeEconomy()
{
	_economy.inflation_prices = _economy.inflation_payment = 1 << 16;
}

/**
 * Determine a certain price
 * @param index Price base
 * @param cost_factor Price factor
 * @param grf_file NewGRF to use local price multipliers from.
 * @param shift Extra bit shifting after the computation
 * @return Price
 */
Money GetPrice(Price index, uint cost_factor, const GRFFile *grf_file, int shift)
{
	if (index >= PR_END) return 0;

	Money cost = _price[index] * cost_factor;
	if (grf_file != NULL) shift += grf_file->price_base_multipliers[index];

	if (shift >= 0) {
		cost <<= shift;
	} else {
		cost >>= -shift;
	}

	return cost;
}

Money GetTransportedGoodsIncome(uint num_pieces, uint dist, byte transit_days, CargoID cargo_type)
{
	const CargoSpec *cs = CargoSpec::Get(cargo_type);
	if (!cs->IsValid()) {
		/* User changed newgrfs and some vehicle still carries some cargo which is no longer available. */
		return 0;
	}

	/* Use callback to calculate cargo profit, if available */
	if (HasBit(cs->callback_mask, CBM_CARGO_PROFIT_CALC)) {
		uint32 var18 = min(dist, 0xFFFF) | (min(num_pieces, 0xFF) << 16) | (transit_days << 24);
		uint16 callback = GetCargoCallback(CBID_CARGO_PROFIT_CALC, 0, var18, cs);
		if (callback != CALLBACK_FAILED) {
			int result = GB(callback, 0, 14);

			/* Simulate a 15 bit signed value */
			if (HasBit(callback, 14)) result -= 0x4000;

			/* "The result should be a signed multiplier that gets multiplied
			 * by the amount of cargo moved and the price factor, then gets
			 * divided by 8192." */
			return result * num_pieces * cs->current_payment / 8192;
		}
	}

	static const int MIN_TIME_FACTOR = 31;
	static const int MAX_TIME_FACTOR = 255;

	const int days1 = cs->transit_days[0];
	const int days2 = cs->transit_days[1];
	const int days_over_days1 = max(   transit_days - days1, 0);
	const int days_over_days2 = max(days_over_days1 - days2, 0);

	/*
	 * The time factor is calculated based on the time it took
	 * (transit_days) compared two cargo-depending values. The
	 * range is divided into three parts:
	 *
	 *  - constant for fast transits
	 *  - linear decreasing with time with a slope of -1 for medium transports
	 *  - linear decreasing with time with a slope of -2 for slow transports
	 *
	 */
	const int time_factor = max(MAX_TIME_FACTOR - days_over_days1 - days_over_days2, MIN_TIME_FACTOR);

	return BigMulS(dist * time_factor * num_pieces, cs->current_payment, 21);
}

/** The industries we've currently brought cargo to. */
static SmallIndustryList _cargo_delivery_destinations;

/**
 * Transfer goods from station to industry.
 * All cargo is delivered to the nearest (Manhattan) industry to the station sign, which is inside the acceptance rectangle and actually accepts the cargo.
 * @param st The station that accepted the cargo
 * @param cargo_type Type of cargo delivered
 * @param num_pieces Amount of cargo delivered
 * @param source The source of the cargo
 * @return actually accepted pieces of cargo
 */
static uint DeliverGoodsToIndustry(const Station *st, CargoID cargo_type, uint num_pieces, IndustryID source)
{
	/* Find the nearest industrytile to the station sign inside the catchment area, whose industry accepts the cargo.
	 * This fails in three cases:
	 *  1) The station accepts the cargo because there are enough houses around it accepting the cargo.
	 *  2) The industries in the catchment area temporarily reject the cargo, and the daily station loop has not yet updated station acceptance.
	 *  3) The results of callbacks CBID_INDUSTRY_REFUSE_CARGO and CBID_INDTILE_CARGO_ACCEPTANCE are inconsistent. (documented behaviour)
	 */

	uint accepted = 0;

	for (uint i = 0; i < st->industries_near.Length() && num_pieces != 0; i++) {
		Industry *ind = st->industries_near[i];
		if (ind->index == source) continue;

		uint cargo_index;
		for (cargo_index = 0; cargo_index < lengthof(ind->accepts_cargo); cargo_index++) {
			if (cargo_type == ind->accepts_cargo[cargo_index]) break;
		}
		/* Check if matching cargo has been found */
		if (cargo_index >= lengthof(ind->accepts_cargo)) continue;

		/* Check if industry temporarily refuses acceptance */
		if (IndustryTemporarilyRefusesCargo(ind, cargo_type)) continue;

		/* Insert the industry into _cargo_delivery_destinations, if not yet contained */
		_cargo_delivery_destinations.Include(ind);

		uint amount = min(num_pieces, 0xFFFFU - ind->incoming_cargo_waiting[cargo_index]);
		ind->incoming_cargo_waiting[cargo_index] += amount;
		num_pieces -= amount;
		accepted += amount;
	}

	return accepted;
}

/**
 * Delivers goods to industries/towns and calculates the payment
 * @param num_pieces amount of cargo delivered
 * @param cargo_type the type of cargo that is delivered
 * @param dest Station the cargo has been unloaded
 * @param source_tile The origin of the cargo for distance calculation
 * @param days_in_transit Travel time
 * @param company The company delivering the cargo
 * @param src_type Type of source of cargo (industry, town, headquarters)
 * @param src Index of source of cargo
 * @return Revenue for delivering cargo
 * @note The cargo is just added to the stockpile of the industry. It is due to the caller to trigger the industry's production machinery
 */
static Money DeliverGoods(int num_pieces, CargoID cargo_type, StationID dest, TileIndex source_tile, byte days_in_transit, Company *company, SourceType src_type, SourceID src)
{
	assert(num_pieces > 0);

	Station *st = Station::Get(dest);

	/* Give the goods to the industry. */
	uint accepted = DeliverGoodsToIndustry(st, cargo_type, num_pieces, src_type == ST_INDUSTRY ? src : INVALID_INDUSTRY);

	/* If this cargo type is always accepted, accept all */
	if (HasBit(st->always_accepted, cargo_type)) accepted = num_pieces;

	/* Update station statistics */
	if (accepted > 0) {
		SetBit(st->goods[cargo_type].acceptance_pickup, GoodsEntry::GES_EVER_ACCEPTED);
		SetBit(st->goods[cargo_type].acceptance_pickup, GoodsEntry::GES_CURRENT_MONTH);
		SetBit(st->goods[cargo_type].acceptance_pickup, GoodsEntry::GES_ACCEPTED_BIGTICK);
	}

	/* Update company statistics */
	company->cur_economy.delivered_cargo += accepted;
	if (accepted > 0) SetBit(company->cargo_types, cargo_type);

	/* Increase town's counter for town effects */
	const CargoSpec *cs = CargoSpec::Get(cargo_type);
	st->town->received[cs->town_effect].new_act += accepted;

	/* Determine profit */
	Money profit = GetTransportedGoodsIncome(accepted, DistanceManhattan(source_tile, st->xy), days_in_transit, cargo_type);

	/* Modify profit if a subsidy is in effect */
	if (CheckSubsidised(cargo_type, company->index, src_type, src, st))  {
		switch (_settings_game.difficulty.subsidy_multiplier) {
			case 0:  profit += profit >> 1; break;
			case 1:  profit *= 2; break;
			case 2:  profit *= 3; break;
			default: profit *= 4; break;
		}
	}

	return profit;
}

/**
 * Inform the industry about just delivered cargo
 * DeliverGoodsToIndustry() silently incremented incoming_cargo_waiting, now it is time to do something with the new cargo.
 * @param i The industry to process
 */
static void TriggerIndustryProduction(Industry *i)
{
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	uint16 callback = indspec->callback_mask;

	i->was_cargo_delivered = true;
	i->last_cargo_accepted_at = _date;

	if (HasBit(callback, CBM_IND_PRODUCTION_CARGO_ARRIVAL) || HasBit(callback, CBM_IND_PRODUCTION_256_TICKS)) {
		if (HasBit(callback, CBM_IND_PRODUCTION_CARGO_ARRIVAL)) {
			IndustryProductionCallback(i, 0);
		} else {
			SetWindowDirty(WC_INDUSTRY_VIEW, i->index);
		}
	} else {
		for (uint cargo_index = 0; cargo_index < lengthof(i->incoming_cargo_waiting); cargo_index++) {
			uint cargo_waiting = i->incoming_cargo_waiting[cargo_index];
			if (cargo_waiting == 0) continue;

			i->produced_cargo_waiting[0] = min(i->produced_cargo_waiting[0] + (cargo_waiting * indspec->input_cargo_multiplier[cargo_index][0] / 256), 0xFFFF);
			i->produced_cargo_waiting[1] = min(i->produced_cargo_waiting[1] + (cargo_waiting * indspec->input_cargo_multiplier[cargo_index][1] / 256), 0xFFFF);

			i->incoming_cargo_waiting[cargo_index] = 0;
		}
	}

	TriggerIndustry(i, INDUSTRY_TRIGGER_RECEIVED_CARGO);
	StartStopIndustryTileAnimation(i, IAT_INDUSTRY_RECEIVED_CARGO);
}

/**
 * Makes us a new cargo payment helper.
 * @param front The front of the train
 */
CargoPayment::CargoPayment(Vehicle *front) :
	front(front),
	current_station(front->last_station_visited)
{
}

CargoPayment::~CargoPayment()
{
	if (this->CleaningPool()) return;

	this->front->cargo_payment = NULL;

	if (this->visual_profit == 0 && this->visual_transfer == 0) return;

	Backup<CompanyByte> cur_company(_current_company, this->front->owner, FILE_LINE);

	SubtractMoneyFromCompany(CommandCost(this->front->GetExpenseType(true), -this->route_profit));
	this->front->profit_this_year += (this->visual_profit + this->visual_transfer) << 8;

	if (this->route_profit != 0 && IsLocalCompany() && !PlayVehicleSound(this->front, VSE_LOAD_UNLOAD)) {
		SndPlayVehicleFx(SND_14_CASHTILL, this->front);
	}

	if (this->visual_transfer != 0) {
		ShowFeederIncomeAnimation(this->front->x_pos, this->front->y_pos,
				this->front->z_pos, this->visual_transfer, -this->visual_profit);
	} else if (this->visual_profit != 0) {
		ShowCostOrIncomeAnimation(this->front->x_pos, this->front->y_pos,
				this->front->z_pos, -this->visual_profit);
	}

	cur_company.Restore();
}

/**
 * Handle payment for final delivery of the given cargo packet.
 * @param cp The cargo packet to pay for.
 * @param count The number of packets to pay for.
 */
void CargoPayment::PayFinalDelivery(const CargoPacket *cp, uint count)
{
	if (this->owner == NULL) {
		this->owner = Company::Get(this->front->owner);
	}

	/* Handle end of route payment */
	Money profit = DeliverGoods(count, this->ct, this->current_station, cp->SourceStationXY(), cp->DaysInTransit(), this->owner, cp->SourceSubsidyType(), cp->SourceSubsidyID());
	this->route_profit += profit;

	/* The vehicle's profit is whatever route profit there is minus feeder shares. */
	this->visual_profit += profit - cp->FeederShare();
}

/**
 * Handle payment for transfer of the given cargo packet.
 * @param cp The cargo packet to pay for; actual payment won't be made!.
 * @param count The number of packets to pay for.
 * @return The amount of money paid for the transfer.
 */
Money CargoPayment::PayTransfer(const CargoPacket *cp, uint count)
{
	Money profit = GetTransportedGoodsIncome(
			count,
			/* pay transfer vehicle for only the part of transfer it has done: ie. cargo_loaded_at_xy to here */
			DistanceManhattan(cp->LoadedAtXY(), Station::Get(this->current_station)->xy),
			cp->DaysInTransit(),
			this->ct);

	profit = profit * _settings_game.economy.feeder_payment_share / 100;

	this->visual_transfer += profit; // accumulate transfer profits for whole vehicle
	return profit; // account for the (virtual) profit already made for the cargo packet
}

/**
 * Prepare the vehicle to be unloaded.
 * @param curr_station the station where the consist is at the moment
 * @param front_v the vehicle to be unloaded
 */
void PrepareUnload(Vehicle *front_v)
{
	Station *curr_station = Station::Get(front_v->last_station_visited);
	curr_station->loading_vehicles.push_back(front_v);

	/* At this moment loading cannot be finished */
	ClrBit(front_v->vehicle_flags, VF_LOADING_FINISHED);

	/* Start unloading at the first possible moment */
	front_v->load_unload_ticks = 1;

	if (front_v->orders.list != NULL && (front_v->current_order.GetUnloadType() & OUFB_NO_UNLOAD) != 0) {
		/* vehicle will keep all its cargo and LoadUnloadVehicle will never call MoveToStation,
		 * so we have to update the flow stats here.
		 */
		StationID next_station_id = front_v->GetNextStoppingStation();
		if (next_station_id == INVALID_STATION) {
			return;
		}
	} else {
		for (Vehicle *v = front_v; v != NULL; v = v->Next()) {
			if (v->cargo_cap > 0 && !v->cargo.Empty()) {
				SetBit(v->vehicle_flags, VF_CARGO_UNLOADING);
			}
		}
	}

	assert(front_v->cargo_payment == NULL);
	/* One CargoPayment per vehicle and the vehicle limit equals the
	 * limit in number of CargoPayments. Can't go wrong. */
	assert_compile(CargoPaymentPool::MAX_SIZE == VehiclePool::MAX_SIZE);
	assert(CargoPayment::CanAllocateItem());
	front_v->cargo_payment = new CargoPayment(front_v);
}

/**
 * Reserves cargo if the full load order and improved_load is set.
 * @param st The station where the consist is loading at the moment.
 * @param u The front of the loading vehicle consist.
 * @param next_station Station the vehicle will stop at next.
 * @return Bit field for the cargo classes with bits for the reserved cargos set (if anything was reserved).
 */
uint32 ReserveConsist(Station *st, Vehicle *u, StationID next_station)
{
	uint32 ret = 0;
	if (_settings_game.order.improved_load && (u->current_order.GetLoadType() & OLFB_FULL_LOAD)) {
		/* Update reserved cargo */
		for (Vehicle *v = u; v != NULL; v = v->Next()) {
			/* Only reserve if the vehicle is not unloading anymore.
			 *
			 * The packets that are kept in the vehicle because they have the
			 * same destination as the vehicle are stored in the reservation
			 * list while unloading for performance reasons. The reservation
			 * list is swapped with the onboard list after unloading. This
			 * doesn't increase the load/unload time. So if we start reserving
			 * cargo before unloading has stopped we'll load that cargo for free
			 * later. Like this there is a slightly increased probability that
			 * another vehicle which has arrived later loads cargo that should
			 * be loaded by this vehicle but as the algorithm isn't perfect in
			 * that regard anyway we can tolerate it.
			 *
			 * The algorithm isn't perfect as it only counts free capacity for
			 * reservation. If another vehicle arrives later but unloads faster
			 * than this one, this vehicle won't reserve all the cargo it may
			 * be able to take after fully unloading. So the other vehicle may
			 * load it even if it has arrived later.
			 */
			if (HasBit(v->vehicle_flags, VF_CARGO_UNLOADING)) continue;

			int cap = v->cargo_cap - v->cargo.Count();
			if (cap > 0) {
				int reserved = st->goods[v->cargo_type].cargo.MoveTo(&v->cargo, cap, next_station, true);
				if (reserved > 0) {
					cap -= reserved;
					SetBit(ret, v->cargo_type);
				}
			}
		}
	}
	return ret;
}

/**
 * Checks whether an articulated vehicle is empty.
 * @param v Vehicle
 * @return true if all parts are empty.
 */
static bool IsArticulatedVehicleEmpty(Vehicle *v)
{
	v = v->GetFirstEnginePart();

	for (; v != NULL; v = v->HasArticulatedPart() ? v->GetNextArticulatedPart() : NULL) {
		if (v->cargo.Count() != 0) return false;
	}

	return true;
}

/**
 * Loads/unload the vehicle if possible.
 * @param front the vehicle to be (un)loaded
 * @param cargos_reserved bit field: the cargo classes for which cargo has been reserved in this loading cycle
 * @return the updated cargo_reserved
 */
static uint32 LoadUnloadVehicle(Vehicle *front, uint32 cargos_reserved)
{
	assert(front->current_order.IsType(OT_LOADING));

	StationID last_visited = front->last_station_visited;
	Station *st = Station::Get(last_visited);

	StationID next_station = front->GetNextStoppingStation();

	/* We have not waited enough time till the next round of loading/unloading */
	if (front->load_unload_ticks != 0) {
		return cargos_reserved | ReserveConsist(st, front, next_station);
	}

	OrderUnloadFlags unload_flags = front->current_order.GetUnloadType();

	if (front->type == VEH_TRAIN && (!IsTileType(front->tile, MP_STATION) || GetStationIndex(front->tile) != st->index)) {
		/* The train reversed in the station. Take the "easy" way
		 * out and let the train just leave as it always did. */
		SetBit(front->vehicle_flags, VF_LOADING_FINISHED);
		front->load_unload_ticks = 1;
		return cargos_reserved;
	}

	int unloading_time = 0;
	bool dirty_vehicle = false;
	bool dirty_station = false;

	bool completely_emptied = true;
	bool anything_unloaded = false;
	bool anything_loaded   = false;
	uint32 full_load_amount = 0;
	uint32 cargo_not_full  = 0;
	uint32 cargo_full      = 0;

	front->cur_speed = 0;

	CargoPayment *payment = front->cargo_payment;

	uint artic_part = 0; // Articulated part we are currently trying to load. (not counting parts without capacity)
	for (Vehicle *v = front; v != NULL; v = v->Next()) {
		if (v == front || !v->Previous()->HasArticulatedPart()) artic_part = 0;
		if (v->cargo_cap == 0) continue;
		artic_part++;

		const Engine *e = v->GetEngine();
		byte load_amount = e->info.load_amount;

		/* The default loadamount for mail is 1/4 of the load amount for passengers */
		if (v->type == VEH_AIRCRAFT && !Aircraft::From(v)->IsNormalAircraft()) load_amount = CeilDiv(load_amount, 4);

		if (_settings_game.order.gradual_loading) {
			uint16 cb_load_amount = CALLBACK_FAILED;
			if (e->GetGRF() != NULL && e->GetGRF()->grf_version >= 8) {
				/* Use callback 36 */
				cb_load_amount = GetVehicleProperty(v, PROP_VEHICLE_LOAD_AMOUNT, CALLBACK_FAILED);
			} else if (HasBit(e->info.callback_mask, CBM_VEHICLE_LOAD_AMOUNT)) {
				/* Use callback 12 */
				cb_load_amount = GetVehicleCallback(CBID_VEHICLE_LOAD_AMOUNT, 0, 0, v->engine_type, v);
			}
			if (cb_load_amount != CALLBACK_FAILED) {
				if (e->GetGRF()->grf_version < 8) cb_load_amount = GB(cb_load_amount, 0, 8);
				if (cb_load_amount >= 0x100) {
					ErrorUnknownCallbackResult(e->GetGRFID(), CBID_VEHICLE_LOAD_AMOUNT, cb_load_amount);
				} else if (cb_load_amount != 0) {
					load_amount = cb_load_amount;
				}
			}
		}

		GoodsEntry *ge = &st->goods[v->cargo_type];

		if (HasBit(v->vehicle_flags, VF_CARGO_UNLOADING)) {
			uint cargo_count = v->cargo.OnboardCount();
			uint amount_unloaded = _settings_game.order.gradual_loading ? min(cargo_count, load_amount) : cargo_count;

			uint prev_count = ge->cargo.Count();
			payment->SetCargo(v->cargo_type);
			uint delivered = ge->cargo.TakeFrom(&v->cargo, amount_unloaded, unload_flags,
					next_station, front->last_loading_station == last_visited, payment);

			st->time_since_unload = 0;
			unloading_time += delivered;

			if (ge->cargo.Count() > prev_count) {
				/* something has been transferred. The station windows need updating. */
				dirty_station = true;
				if (!HasBit(ge->acceptance_pickup, GoodsEntry::GES_PICKUP)) {
					InvalidateWindowData(WC_STATION_LIST, last_visited);
					SetBit(ge->acceptance_pickup, GoodsEntry::GES_PICKUP);
				}
			}

			anything_unloaded = true;
			dirty_vehicle = true;

			/* load_amount might (theoretically) be 0, which would make delivered == 0 even though there is still cargo
			 * in the vehicle. Thus OnboardCount > 0. In that case we can't stop unloading as SwapReserved wouldn't work.
			 * v->cargo also contains the cargo reserved for the vehicle which is not on board at the moment, but will be
			 * swapped back when done unloading.
			 */
			if (v->cargo.OnboardCount() == 0) {
				/* done delivering */
				if (!v->cargo.Empty()) completely_emptied = false;
				ClrBit(v->vehicle_flags, VF_CARGO_UNLOADING);
				v->cargo.SwapReserved();
			}

			continue;
		}

		/* Do not pick up goods when we have no-load set or loading is stopped. */
		if (front->current_order.GetLoadType() & OLFB_NO_LOAD || HasBit(front->vehicle_flags, VF_STOP_LOADING)) continue;

		/* This order has a refit, if this is the first vehicle part carrying cargo and the whole vehicle is empty, try refitting. */
		if (front->current_order.IsRefit() && artic_part == 1 && IsArticulatedVehicleEmpty(v) &&
				(v->type != VEH_AIRCRAFT || (Aircraft::From(v)->IsNormalAircraft() && v->Next()->cargo.Count() == 0))) {
			Vehicle *v_start = v->GetFirstEnginePart();
			CargoID new_cid = front->current_order.GetRefitCargo();
			byte new_subtype = front->current_order.GetRefitSubtype();

			Backup<CompanyByte> cur_company(_current_company, front->owner, FILE_LINE);

			/* Check if all articulated parts are empty and collect refit mask. */
			uint32 refit_mask = e->info.refit_mask;
			Vehicle *w = v_start;
			while (w->HasArticulatedPart()) {
				w = w->GetNextArticulatedPart();
				if (w->cargo.Count() > 0) new_cid = CT_NO_REFIT;
				refit_mask |= EngInfo(w->engine_type)->refit_mask;
			}

			if (new_cid == CT_AUTO_REFIT) {
				/* Get refittable cargo type with the most waiting cargo. */
				uint amount = 0;
				CargoID cid;
				FOR_EACH_SET_CARGO_ID(cid, refit_mask) {
					if (st->goods[cid].cargo.Count() > amount) {
						/* Try to find out if auto-refitting would succeed. In case the refit is allowed,
						 * the returned refit capacity will be greater than zero. */
						new_subtype = GetBestFittingSubType(v, v, cid);
						DoCommand(v_start->tile, v_start->index, cid | 1U << 6 | new_subtype << 8 | 1U << 16, DC_QUERY_COST, GetCmdRefitVeh(v_start)); // Auto-refit and only this vehicle including artic parts.
						if (_returned_refit_capacity > 0) {
							amount = st->goods[cid].cargo.Count();
							new_cid = cid;
						}
					}
				}
			}

			/* Refit if given a valid cargo. */
			if (new_cid < NUM_CARGO) {
				CommandCost cost = DoCommand(v_start->tile, v_start->index, new_cid | 1U << 6 | new_subtype << 8 | 1U << 16, DC_EXEC, GetCmdRefitVeh(v_start)); // Auto-refit and only this vehicle including artic parts.
				if (cost.Succeeded()) front->profit_this_year -= cost.GetCost() << 8;
				ge = &st->goods[v->cargo_type];
			}

			cur_company.Restore();
		}

		/* update stats */
		int t;
		switch (front->type) {
			case VEH_TRAIN: /* FALL THROUGH */
			case VEH_SHIP:
				t = front->vcache.cached_max_speed;
				break;

			case VEH_ROAD:
				t = front->vcache.cached_max_speed / 2;
				break;

			case VEH_AIRCRAFT:
				t = Aircraft::From(front)->GetSpeedOldUnits(); // Convert to old units.
				break;

			default: NOT_REACHED();
		}

		/* if last speed is 0, we treat that as if no vehicle has ever visited the station. */
		ge->last_speed = min(t, 255);
		ge->last_age = _cur_year - front->build_year;
		ge->days_since_pickup = 0;

		/* If there's goods waiting at the station, and the vehicle
		 * has capacity for it, load it on the vehicle. */
		int cap_left = v->cargo_cap - v->cargo.OnboardCount();
		if (cap_left > 0) {
			if (_settings_game.order.gradual_loading) cap_left = min(cap_left, load_amount);
			if (v->cargo.Empty()) TriggerVehicle(v, VEHICLE_TRIGGER_NEW_CARGO);

			int loaded = 0;
			if (_settings_game.order.improved_load) {
				loaded += v->cargo.LoadReserved(cap_left);
			}

			loaded += ge->cargo.MoveTo(&v->cargo, cap_left - loaded, next_station);

			/* Store whether the maximum possible load amount was loaded or not.*/
			if (loaded == cap_left) {
				SetBit(full_load_amount, v->cargo_type);
			} else {
				ClrBit(full_load_amount, v->cargo_type);
			}

			/* TODO: Regarding this, when we do gradual loading, we
			 * should first unload all vehicles and then start
			 * loading them. Since this will cause
			 * VEHICLE_TRIGGER_EMPTY to be called at the time when
			 * the whole vehicle chain is really totally empty, the
			 * completely_emptied assignment can then be safely
			 * removed; that's how TTDPatch behaves too. --pasky */
			if (loaded > 0) {
				completely_emptied = false;
				anything_loaded = true;

				st->time_since_load = 0;
				st->last_vehicle_type = v->type;

				if (ge->cargo.Empty()) {
					TriggerStationAnimation(st, st->xy, SAT_CARGO_TAKEN, v->cargo_type);
					AirportAnimationTrigger(st, AAT_STATION_CARGO_TAKEN, v->cargo_type);
				}

				unloading_time += loaded;

				dirty_vehicle = dirty_station = true;
			} else if (_settings_game.order.improved_load && HasBit(cargos_reserved, v->cargo_type)) {
				/* Skip loading this vehicle if another train/vehicle is already handling
				 * the same cargo type at this station */
				SetBit(cargo_not_full, v->cargo_type);
				continue;
			}
		}

		if (v->cargo.OnboardCount() >= v->cargo_cap) {
			SetBit(cargo_full, v->cargo_type);
		} else {
			SetBit(cargo_not_full, v->cargo_type);
		}
	}

	if (anything_loaded || anything_unloaded) {
		if (front->type == VEH_TRAIN) TriggerStationAnimation(st, st->xy, SAT_TRAIN_LOADS);
	}

	/* Only set completely_emptied, if we just unloaded all remaining cargo */
	completely_emptied &= anything_unloaded;

	if (!anything_unloaded) delete payment;

	ClrBit(front->vehicle_flags, VF_STOP_LOADING);
	if (anything_loaded || anything_unloaded) {
		if (_settings_game.order.gradual_loading) {
			/* The time it takes to load one 'slice' of cargo or passengers depends
			 * on the vehicle type - the values here are those found in TTDPatch */
			const uint gradual_loading_wait_time[] = { 40, 20, 10, 20 };

			unloading_time = gradual_loading_wait_time[front->type];
		}
		/* We loaded less cargo than possible for all cargo types and it's not full
		 * load and we're not supposed to wait any longer: stop loading. */
		if (!anything_unloaded && full_load_amount == 0 && !(front->current_order.GetLoadType() & OLFB_FULL_LOAD) &&
				front->current_order_time >= (uint)max(front->current_order.wait_time - front->lateness_counter, 0)) {
			SetBit(front->vehicle_flags, VF_STOP_LOADING);
		}
	} else {
		bool finished_loading = true;
		if (front->current_order.GetLoadType() & OLFB_FULL_LOAD) {
			if (front->current_order.GetLoadType() == OLF_FULL_LOAD_ANY) {
				/* if the aircraft carries passengers and is NOT full, then
				 * continue loading, no matter how much mail is in */
				if ((front->type == VEH_AIRCRAFT && IsCargoInClass(front->cargo_type, CC_PASSENGERS) && front->cargo_cap > front->cargo.OnboardCount()) ||
						(cargo_not_full && (cargo_full & ~cargo_not_full) == 0)) { // There are still non-full cargoes
					finished_loading = false;
				}
			} else if (cargo_not_full != 0) {
				finished_loading = false;
			}

			/* Refresh next hop stats if we're full loading to avoid deadlocks. */
			if (!finished_loading) front->RefreshNextHopsStats();
		}
		unloading_time = 20;

		SB(front->vehicle_flags, VF_LOADING_FINISHED, 1, finished_loading);
	}

	if (front->type == VEH_TRAIN) {
		/* Each platform tile is worth 2 rail vehicles. */
		int overhang = front->GetGroundVehicleCache()->cached_total_length - st->GetPlatformLength(front->tile) * TILE_SIZE;
		if (overhang > 0) {
			unloading_time <<= 1;
			unloading_time += (overhang * unloading_time) / 8;
		}
	}

	/* Calculate the loading indicator fill percent and display
	 * In the Game Menu do not display indicators
	 * If _settings_client.gui.loading_indicators == 2, show indicators (bool can be promoted to int as 0 or 1 - results in 2 > 0,1 )
	 * if _settings_client.gui.loading_indicators == 1, _local_company must be the owner or must be a spectator to show ind., so 1 > 0
	 * if _settings_client.gui.loading_indicators == 0, do not display indicators ... 0 is never greater than anything
	 */
	if (_game_mode != GM_MENU && (_settings_client.gui.loading_indicators > (uint)(front->owner != _local_company && _local_company != COMPANY_SPECTATOR))) {
		StringID percent_up_down = STR_NULL;
		int percent = CalcPercentVehicleFilled(front, &percent_up_down);
		if (front->fill_percent_te_id == INVALID_TE_ID) {
			front->fill_percent_te_id = ShowFillingPercent(front->x_pos, front->y_pos, front->z_pos + 20, percent, percent_up_down);
		} else {
			UpdateFillingPercent(front->fill_percent_te_id, percent, percent_up_down);
		}
	}

	/* Always wait at least 1, otherwise we'll wait 'infinitively' long. */
	front->load_unload_ticks = max(1, unloading_time);

	if (completely_emptied) {
		TriggerVehicle(front, VEHICLE_TRIGGER_EMPTY);
	}

	if (dirty_vehicle) {
		SetWindowDirty(GetWindowClassForVehicleType(front->type), front->owner);
		SetWindowDirty(WC_VEHICLE_DETAILS, front->index);
		front->MarkDirty();
	}
	if (dirty_station) {
		st->MarkTilesDirty(true);
		SetWindowDirty(WC_STATION_VIEW, last_visited);
	}
	return cargos_reserved;
}

/**
 * Load/unload the vehicles in this station according to the order
 * they entered.
 * @param st the station to do the loading/unloading for
 */
void LoadUnloadStation(Station *st)
{
	/* No vehicle is here... */
	if (st->loading_vehicles.empty()) return;

	Vehicle *last_loading = NULL;
	std::list<Vehicle *>::iterator iter;

	/* Check if anything will be loaded at all. Otherwise we don't need to reserve either. */
	for (iter = st->loading_vehicles.begin(); iter != st->loading_vehicles.end(); ++iter) {
		Vehicle *v = *iter;

		if ((v->vehstatus & (VS_STOPPED | VS_CRASHED))) continue;

		assert(v->load_unload_ticks != 0);
		if (--v->load_unload_ticks == 0) last_loading = v;
	}

	/* We only need to reserve and load/unload up to the last loading vehicle.
	 * Anything else will be forgotten anyway after returning from this function.
	 *
	 * Especially this means we do _not_ need to reserve cargo for a single
	 * consist in a station which is not allowed to load yet because its
	 * load_unload_ticks is still not 0.
	 */
	if (last_loading == NULL) return;

	uint cargos_reserved = 0;

	for (iter = st->loading_vehicles.begin(); iter != st->loading_vehicles.end(); ++iter) {
		Vehicle *v = *iter;
		if (!(v->vehstatus & (VS_STOPPED | VS_CRASHED))) cargos_reserved = LoadUnloadVehicle(v, cargos_reserved);
		if (v == last_loading) break;
	}

	/* Call the production machinery of industries */
	const Industry * const *isend = _cargo_delivery_destinations.End();
	for (Industry **iid = _cargo_delivery_destinations.Begin(); iid != isend; iid++) {
		TriggerIndustryProduction(*iid);
	}
	_cargo_delivery_destinations.Clear();
}

/**
 * Monthly update of the economic data (of the companies as well as economic fluctuations).
 */
void CompaniesMonthlyLoop()
{
	CompaniesGenStatistics();
	if (_settings_game.economy.inflation) {
		AddInflation();
		RecomputePrices();
	}
	CompaniesPayInterest();
	HandleEconomyFluctuations();
}

static void DoAcquireCompany(Company *c)
{
	CompanyID ci = c->index;

	CompanyNewsInformation *cni = MallocT<CompanyNewsInformation>(1);
	cni->FillData(c, Company::Get(_current_company));

	SetDParam(0, STR_NEWS_COMPANY_MERGER_TITLE);
	SetDParam(1, c->bankrupt_value == 0 ? STR_NEWS_MERGER_TAKEOVER_TITLE : STR_NEWS_COMPANY_MERGER_DESCRIPTION);
	SetDParamStr(2, cni->company_name);
	SetDParamStr(3, cni->other_company_name);
	SetDParam(4, c->bankrupt_value);
	AddCompanyNewsItem(STR_MESSAGE_NEWS_FORMAT, NS_COMPANY_MERGER, cni);
	AI::BroadcastNewEvent(new ScriptEventCompanyMerger(ci, _current_company));
	Game::NewEvent(new ScriptEventCompanyMerger(ci, _current_company));

	ChangeOwnershipOfCompanyItems(ci, _current_company);

	if (c->bankrupt_value == 0) {
		Company *owner = Company::Get(_current_company);
		owner->current_loan += c->current_loan;
	}

	if (c->is_ai) AI::Stop(c->index);

	DeleteCompanyWindows(ci);
	InvalidateWindowClassesData(WC_TRAINS_LIST, 0);
	InvalidateWindowClassesData(WC_SHIPS_LIST, 0);
	InvalidateWindowClassesData(WC_ROADVEH_LIST, 0);
	InvalidateWindowClassesData(WC_AIRCRAFT_LIST, 0);

	delete c;
}

extern int GetAmountOwnedBy(const Company *c, Owner owner);

/**
 * Acquire shares in an opposing company.
 * @param tile unused
 * @param flags type of operation
 * @param p1 company to buy the shares from
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuyShareInCompany(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CommandCost cost(EXPENSES_OTHER);
	CompanyID target_company = (CompanyID)p1;
	Company *c = Company::GetIfValid(target_company);

	/* Check if buying shares is allowed (protection against modified clients)
	 * Cannot buy own shares */
	if (c == NULL || !_settings_game.economy.allow_shares || _current_company == target_company) return CMD_ERROR;

	/* Protect new companies from hostile takeovers */
	if (_cur_year - c->inaugurated_year < 6) return_cmd_error(STR_ERROR_PROTECTED);

	/* Those lines are here for network-protection (clients can be slow) */
	if (GetAmountOwnedBy(c, COMPANY_SPECTATOR) == 0) return cost;

	if (GetAmountOwnedBy(c, COMPANY_SPECTATOR) == 1) {
		if (!c->is_ai) return cost; //  We can not buy out a real company (temporarily). TODO: well, enable it obviously.

		if (GetAmountOwnedBy(c, _current_company) == 3 && !MayCompanyTakeOver(_current_company, target_company)) return_cmd_error(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);
	}


	cost.AddCost(CalculateCompanyValue(c) >> 2);
	if (flags & DC_EXEC) {
		OwnerByte *b = c->share_owners;

		while (*b != COMPANY_SPECTATOR) b++; // share owners is guaranteed to contain at least one COMPANY_SPECTATOR
		*b = _current_company;

		for (int i = 0; c->share_owners[i] == _current_company;) {
			if (++i == 4) {
				c->bankrupt_value = 0;
				DoAcquireCompany(c);
				break;
			}
		}
		SetWindowDirty(WC_COMPANY, target_company);
		CompanyAdminUpdate(c);
	}
	return cost;
}

/**
 * Sell shares in an opposing company.
 * @param tile unused
 * @param flags type of operation
 * @param p1 company to sell the shares from
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdSellShareInCompany(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID target_company = (CompanyID)p1;
	Company *c = Company::GetIfValid(target_company);

	/* Cannot sell own shares */
	if (c == NULL || _current_company == target_company) return CMD_ERROR;

	/* Check if selling shares is allowed (protection against modified clients).
	 * However, we must sell shares of companies being closed down. */
	if (!_settings_game.economy.allow_shares && !(flags & DC_BANKRUPT)) return CMD_ERROR;

	/* Those lines are here for network-protection (clients can be slow) */
	if (GetAmountOwnedBy(c, _current_company) == 0) return CommandCost();

	/* adjust it a little to make it less profitable to sell and buy */
	Money cost = CalculateCompanyValue(c) >> 2;
	cost = -(cost - (cost >> 7));

	if (flags & DC_EXEC) {
		OwnerByte *b = c->share_owners;
		while (*b != _current_company) b++; // share owners is guaranteed to contain company
		*b = COMPANY_SPECTATOR;
		SetWindowDirty(WC_COMPANY, target_company);
		CompanyAdminUpdate(c);
	}
	return CommandCost(EXPENSES_OTHER, cost);
}

/**
 * Buy up another company.
 * When a competing company is gone bankrupt you get the chance to purchase
 * that company.
 * @todo currently this only works for AI companies
 * @param tile unused
 * @param flags type of operation
 * @param p1 company to buy up
 * @param p2 unused
 * @param text unused
 * @return the cost of this operation or an error
 */
CommandCost CmdBuyCompany(TileIndex tile, DoCommandFlag flags, uint32 p1, uint32 p2, const char *text)
{
	CompanyID target_company = (CompanyID)p1;
	Company *c = Company::GetIfValid(target_company);
	if (c == NULL) return CMD_ERROR;

	/* Disable takeovers when not asked */
	if (!HasBit(c->bankrupt_asked, _current_company)) return CMD_ERROR;

	/* Disable taking over the local company in single player */
	if (!_networking && _local_company == c->index) return CMD_ERROR;

	/* Do not allow companies to take over themselves */
	if (target_company == _current_company) return CMD_ERROR;

	/* Disable taking over when not allowed. */
	if (!MayCompanyTakeOver(_current_company, target_company)) return CMD_ERROR;

	/* Get the cost here as the company is deleted in DoAcquireCompany. */
	CommandCost cost(EXPENSES_OTHER, c->bankrupt_value);

	if (flags & DC_EXEC) {
		DoAcquireCompany(c);
	}
	return cost;
}
