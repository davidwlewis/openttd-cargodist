/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file cargotype.h Types/functions related to cargoes. */

#ifndef CARGOTYPE_H
#define CARGOTYPE_H

#include "economy_type.h"
#include "cargo_type.h"
#include "gfx_type.h"
#include "strings_type.h"
#include "landscape_type.h"

/** Globally unique label of a cargo type. */
typedef uint32 CargoLabel;

/** Town growth effect when delivering cargo. */
enum TownEffect {
	TE_BEGIN = 0,
	TE_NONE = TE_BEGIN, ///< Cargo has no effect.
	TE_PASSENGERS,      ///< Cargo behaves passenger-like.
	TE_MAIL,            ///< Cargo behaves mail-like.
	TE_GOODS,           ///< Cargo behaves goods/candy-like.
	TE_WATER,           ///< Cargo behaves water-like.
	TE_FOOD,            ///< Cargo behaves food/fizzy-drinks-like.
	TE_END,             ///< End of town effects.
	NUM_TE = TE_END,    ///< Amount of town effects.
};

/** Cargo classes. */
enum CargoClass {
	CC_NOAVAILABLE  = 0,       ///< No cargo class has been specified
	CC_PASSENGERS   = 1 <<  0, ///< Passengers
	CC_MAIL         = 1 <<  1, ///< Mail
	CC_EXPRESS      = 1 <<  2, ///< Express cargo (Goods, Food, Candy, but also possible for passengers)
	CC_ARMOURED     = 1 <<  3, ///< Armoured cargo (Valuables, Gold, Diamonds)
	CC_BULK         = 1 <<  4, ///< Bulk cargo (Coal, Grain etc., Ores, Fruit)
	CC_PIECE_GOODS  = 1 <<  5, ///< Piece goods (Livestock, Wood, Steel, Paper)
	CC_LIQUID       = 1 <<  6, ///< Liquids (Oil, Water, Rubber)
	CC_REFRIGERATED = 1 <<  7, ///< Refrigerated cargo (Food, Fruit)
	CC_HAZARDOUS    = 1 <<  8, ///< Hazardous cargo (Nuclear Fuel, Explosives, etc.)
	CC_COVERED      = 1 <<  9, ///< Covered/Sheltered Freight (Transporation in Box Vans, Silo Wagons, etc.)
	CC_SPECIAL      = 1 << 15, ///< Special bit used for livery refit tricks instead of normal cargoes.
};

static const byte INVALID_CARGO = 0xFF; ///< Constant representing invalid cargo

/** Specification of a cargo type. */
struct CargoSpec {
	uint8 bitnum;                    ///< Cargo bit number, is #INVALID_CARGO for a non-used spec.
	CargoLabel label;                ///< Unique label of the cargo type.
	uint8 legend_colour;
	uint8 rating_colour;
	uint8 weight;                    ///< Weight of a single unit of this cargo type in 1/16 ton (62.5 kg).
	uint16 initial_payment;
	uint8 transit_days[2];

	bool is_freight;                 ///< Cargo type is considered to be freight (affects train freight multiplier).
	TownEffect town_effect;          ///< The effect that delivering this cargo type has on towns. Also affects destination of subsidies.
	uint16 multipliertowngrowth;     ///< Size of the effect.
	uint8 callback_mask;             ///< Bitmask of cargo callbacks that have to be called

	StringID name;                   ///< Name of this type of cargo.
	StringID name_single;            ///< Name of a single entity of this type of cargo.
	StringID units_volume;           ///< Name of a single unit of cargo of this type.
	StringID quantifier;             ///< Text for multiple units of cargo of this type.
	StringID abbrev;                 ///< Two letter abbreviation for this cargo type.

	SpriteID sprite;                 ///< Icon to display this cargo type, may be \c 0xFFF (which means to resolve an action123 chain).

	uint16 classes;                  ///< Classes of this cargo type. @see CargoClass
	const struct GRFFile *grffile;   ///< NewGRF where #group belongs to.
	const struct SpriteGroup *group;

	Money current_payment;

	/**
	 * Determines index of this cargospec
	 * @return index (in the CargoSpec::array array)
	 */
	inline CargoID Index() const
	{
		return this - CargoSpec::array;
	}

	/**
	 * Tests for validity of this cargospec
	 * @return is this cargospec valid?
	 * @note assert(cs->IsValid()) can be triggered when GRF config is modified
	 */
	inline bool IsValid() const
	{
		return this->bitnum != INVALID_CARGO;
	}

	/**
	 * Total number of cargospecs, both valid and invalid
	 * @return length of CargoSpec::array
	 */
	static inline size_t GetArraySize()
	{
		return lengthof(CargoSpec::array);
	}

	/**
	 * Retrieve cargo details for the given cargo ID
	 * @param index ID of cargo
	 * @pre index is a valid cargo ID
	 */
	static inline CargoSpec *Get(size_t index)
	{
		assert(index < lengthof(CargoSpec::array));
		return &CargoSpec::array[index];
	}

	SpriteID GetCargoIcon() const;

private:
	static CargoSpec array[NUM_CARGO]; ///< Array holding all CargoSpecs

	friend void SetupCargoForClimate(LandscapeID l);
};

extern uint32 _cargo_mask;

void SetupCargoForClimate(LandscapeID l);
CargoID GetCargoIDByLabel(CargoLabel cl);
CargoID GetCargoIDByBitnum(uint8 bitnum);

void InitializeSortedCargoSpecs();
extern const CargoSpec *_sorted_cargo_specs[NUM_CARGO];
extern uint8 _sorted_cargo_specs_size;
extern uint8 _sorted_standard_cargo_specs_size;

/**
 * Does cargo \a c have cargo class \a cc?
 * @param c  Cargo type.
 * @param cc Cargo class.
 * @return The type fits in the class.
 */
static inline bool IsCargoInClass(CargoID c, CargoClass cc)
{
	return (CargoSpec::Get(c)->classes & cc) != 0;
}

#define FOR_ALL_CARGOSPECS_FROM(var, start) for (size_t cargospec_index = start; var = NULL, cargospec_index < CargoSpec::GetArraySize(); cargospec_index++) \
		if ((var = CargoSpec::Get(cargospec_index))->IsValid())
#define FOR_ALL_CARGOSPECS(var) FOR_ALL_CARGOSPECS_FROM(var, 0)

#define FOR_EACH_SET_CARGO_ID(var, cargo_bits) FOR_EACH_SET_BIT_EX(CargoID, var, uint, cargo_bits)

#define FOR_ALL_SORTED_CARGOSPECS(var) for (uint8 index = 0; var = _sorted_cargo_specs[index], index < _sorted_cargo_specs_size; index++)

#define FOR_ALL_SORTED_STANDARD_CARGOSPECS(var) for (uint8 index = 0; var = _sorted_cargo_specs[index], index < _sorted_standard_cargo_specs_size; index++)

#endif /* CARGOTYPE_H */
