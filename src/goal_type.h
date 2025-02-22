/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file goal_type.h basic types related to goals */

#ifndef GOAL_TYPE_H
#define GOAL_TYPE_H

/** Types of goal destinations */
enum GoalType {
	GT_NONE,         ///< Destination is not linked
	GT_TILE,         ///< Destination is a tile
	GT_INDUSTRY,     ///< Destination is an industry
	GT_TOWN,         ///< Destination is a town
	GT_COMPANY,      ///< Destination is a company
};
typedef SimpleTinyEnumT<GoalType, byte> GoalTypeByte; ///< The GoalType packed into a byte for savegame purposes.

typedef uint32 GoalTypeID; ///< Contains either tile, industry ID, town ID or company ID (or INVALID_GOALTYPE)
static const GoalTypeID INVALID_GOALTYPE = 0xFFFFFFFF; ///< Invalid/unknown index of GoalType

typedef uint16 GoalID; ///< ID of a goal
struct Goal;

extern GoalID _new_goal_id;

#endif /* GOAL_TYPE_H */
