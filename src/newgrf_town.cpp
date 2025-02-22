/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file newgrf_town.cpp Implementation of the town part of NewGRF houses. */

#include "stdafx.h"
#include "debug.h"
#include "town.h"
#include "newgrf.h"
#include "newgrf_spritegroup.h"

/**
 * This function implements the town variables that newGRF defines.
 * @param variable that is queried
 * @param parameter unused
 * @param available will return false if ever the variable asked for does not exist
 * @param t is of course the town we are inquiring
 * @param caller_grffile #GRFFile of the entity asking for a town variable.
 * @return the value stored in the corresponding variable
 */
uint32 TownGetVariable(byte variable, uint32 parameter, bool *available, Town *t, const GRFFile *caller_grffile)
{
	switch (variable) {
		/* Larger towns */
		case 0x40:
			if (_settings_game.economy.larger_towns == 0) return 2;
			if (t->larger_town) return 1;
			return 0;

		/* Town index */
		case 0x41: return t->index;

		/* Get a variable from the persistent storage */
		case 0x7C: {
			/* Check the persistent storage for the GrfID stored in register 100h. */
			uint32 grfid = GetRegister(0x100);
			if (grfid == 0xFFFFFFFF) {
				if (caller_grffile == NULL) return 0;
				grfid = caller_grffile->grfid;
			}

			std::list<PersistentStorage *>::iterator iter;
			for (iter = t->psa_list.begin(); iter != t->psa_list.end(); iter++) {
				if ((*iter)->grfid == grfid) return (*iter)->GetValue(parameter);
			}

			return 0;
		}

		/* Town properties */
		case 0x80: return t->xy;
		case 0x81: return GB(t->xy, 8, 8);
		case 0x82: return ClampToU16(t->population);
		case 0x83: return GB(ClampToU16(t->population), 8, 8);
		case 0x8A: return t->grow_counter;
		case 0x92: return t->flags;  // In original game, 0x92 and 0x93 are really one word. Since flags is a byte, this is to adjust
		case 0x93: return 0;
		case 0x94: return ClampToU16(t->squared_town_zone_radius[0]);
		case 0x95: return GB(ClampToU16(t->squared_town_zone_radius[0]), 8, 8);
		case 0x96: return ClampToU16(t->squared_town_zone_radius[1]);
		case 0x97: return GB(ClampToU16(t->squared_town_zone_radius[1]), 8, 8);
		case 0x98: return ClampToU16(t->squared_town_zone_radius[2]);
		case 0x99: return GB(ClampToU16(t->squared_town_zone_radius[2]), 8, 8);
		case 0x9A: return ClampToU16(t->squared_town_zone_radius[3]);
		case 0x9B: return GB(ClampToU16(t->squared_town_zone_radius[3]), 8, 8);
		case 0x9C: return ClampToU16(t->squared_town_zone_radius[4]);
		case 0x9D: return GB(ClampToU16(t->squared_town_zone_radius[4]), 8, 8);
		case 0x9E: return t->ratings[0];
		case 0x9F: return GB(t->ratings[0], 8, 8);
		case 0xA0: return t->ratings[1];
		case 0xA1: return GB(t->ratings[1], 8, 8);
		case 0xA2: return t->ratings[2];
		case 0xA3: return GB(t->ratings[2], 8, 8);
		case 0xA4: return t->ratings[3];
		case 0xA5: return GB(t->ratings[3], 8, 8);
		case 0xA6: return t->ratings[4];
		case 0xA7: return GB(t->ratings[4], 8, 8);
		case 0xA8: return t->ratings[5];
		case 0xA9: return GB(t->ratings[5], 8, 8);
		case 0xAA: return t->ratings[6];
		case 0xAB: return GB(t->ratings[6], 8, 8);
		case 0xAC: return t->ratings[7];
		case 0xAD: return GB(t->ratings[7], 8, 8);
		case 0xAE: return t->have_ratings;
		case 0xB2: return t->statues;
		case 0xB6: return ClampToU16(t->num_houses);
		case 0xB9: return t->growth_rate & (~TOWN_GROW_RATE_CUSTOM);
		case 0xBA: return ClampToU16(t->supplied[CT_PASSENGERS].new_max);
		case 0xBB: return GB(ClampToU16(t->supplied[CT_PASSENGERS].new_max), 8, 8);
		case 0xBC: return ClampToU16(t->supplied[CT_MAIL].new_max);
		case 0xBD: return GB(ClampToU16(t->supplied[CT_MAIL].new_max), 8, 8);
		case 0xBE: return ClampToU16(t->supplied[CT_PASSENGERS].new_act);
		case 0xBF: return GB(ClampToU16(t->supplied[CT_PASSENGERS].new_act), 8, 8);
		case 0xC0: return ClampToU16(t->supplied[CT_MAIL].new_act);
		case 0xC1: return GB(ClampToU16(t->supplied[CT_MAIL].new_act), 8, 8);
		case 0xC2: return ClampToU16(t->supplied[CT_PASSENGERS].old_max);
		case 0xC3: return GB(ClampToU16(t->supplied[CT_PASSENGERS].old_max), 8, 8);
		case 0xC4: return ClampToU16(t->supplied[CT_MAIL].old_max);
		case 0xC5: return GB(ClampToU16(t->supplied[CT_MAIL].old_max), 8, 8);
		case 0xC6: return ClampToU16(t->supplied[CT_PASSENGERS].old_act);
		case 0xC7: return GB(ClampToU16(t->supplied[CT_PASSENGERS].old_act), 8, 8);
		case 0xC8: return ClampToU16(t->supplied[CT_MAIL].old_act);
		case 0xC9: return GB(ClampToU16(t->supplied[CT_MAIL].old_act), 8, 8);
		case 0xCA: return t->GetPercentTransported(CT_PASSENGERS);
		case 0xCB: return t->GetPercentTransported(CT_MAIL);
		case 0xCC: return t->received[TE_FOOD].new_act;
		case 0xCD: return GB(t->received[TE_FOOD].new_act, 8, 8);
		case 0xCE: return t->received[TE_WATER].new_act;
		case 0xCF: return GB(t->received[TE_WATER].new_act, 8, 8);
		case 0xD0: return t->received[TE_FOOD].old_act;
		case 0xD1: return GB(t->received[TE_FOOD].old_act, 8, 8);
		case 0xD2: return t->received[TE_WATER].old_act;
		case 0xD3: return GB(t->received[TE_WATER].old_act, 8, 8);
		case 0xD4: return t->road_build_months;
		case 0xD5: return t->fund_buildings_months;
	}

	DEBUG(grf, 1, "Unhandled town variable 0x%X", variable);

	*available = false;
	return UINT_MAX;
}

/**
 * Store a value in town persistent storage.
 * @param t Town owning the persistent storage.
 * @param caller_grffile #GRFFile of the entity that wants to use the storage.
 * @param pos Position to write at.
 * @param value Value to write.
 * @return the value stored in the corresponding variable
 */
void TownStorePSA(Town *t, const GRFFile *caller_grffile, uint pos, int32 value)
{
	assert(t != NULL);
	/* We can't store anything if the caller has no #GRFFile. */
	if (caller_grffile == NULL) return;

	/* Check the persistent storage for the GrfID stored in register 100h. */
	uint32 grfid = GetRegister(0x100);

	/* A NewGRF can only write in the persistent storage associated to its own GRFID. */
	if (grfid == 0xFFFFFFFF) grfid = caller_grffile->grfid;
	if (grfid != caller_grffile->grfid) return;

	/* Check if the storage exists. */
	std::list<PersistentStorage *>::iterator iter;
	for (iter = t->psa_list.begin(); iter != t->psa_list.end(); iter++) {
		if ((*iter)->grfid == grfid) {
			(*iter)->StoreValue(pos, value);
			return;
		}
	}

	/* Create a new storage. */
	assert(PersistentStorage::CanAllocateItem());
	PersistentStorage *psa = new PersistentStorage(grfid);
	psa->StoreValue(pos, value);
	t->psa_list.push_back(psa);
}
