/* $Id$ */

#ifndef AIRCRAFT_H
#define AIRCRAFT_H

#include "station_map.h"
#include "vehicle.h"

typedef enum AircraftSubTypes {
	AIR_HELICOPTER = 0,
	AIR_AIRCRAFT   = 2,
	AIR_SHADOW     = 4,
	AIR_ROTOR      = 6
} AircraftSubType;


/** Check if the aircraft type is a normal flying device; eg
 * not a rotor or a shadow
 * @param v vehicle to check
 * @return Returns true if the aircraft is a helicopter/airplane and
 * false if it is a shadow or a rotor) */
static inline bool IsNormalAircraft(const Vehicle *v)
{
	assert(v->type == VEH_Aircraft);
	/* To be fully correct the commented out functionality is the proper one,
	 * but since value can only be 0 or 2, it is sufficient to only check <= 2
	 * return (v->subtype == AIR_HELICOPTER) || (v->subtype == AIR_AIRCRAFT); */
	return v->subtype <= AIR_AIRCRAFT;
}


static inline bool IsAircraftInHangar(const Vehicle* v)
{
	assert(v->type == VEH_Aircraft);
	return v->vehstatus & VS_HIDDEN && IsHangarTile(v->tile);
}

static inline bool IsAircraftInHangarStopped(const Vehicle* v)
{
	return IsAircraftInHangar(v) && v->vehstatus & VS_STOPPED;
}

uint16 AircraftDefaultCargoCapacity(CargoID cid, const AircraftVehicleInfo*);

void CcBuildAircraft(bool success, TileIndex tile, uint32 p1, uint32 p2);
void CcCloneAircraft(bool success, TileIndex tile, uint32 p1, uint32 p2);
void HandleAircraftEnterHangar(Vehicle *v);
void GetAircraftSpriteSize(EngineID engine, uint &width, uint &height);

void UpdateAirplanesOnNewStation(const Station *st);

#endif /* AIRCRAFT_H */
