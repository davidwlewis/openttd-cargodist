/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_gui.h Declaration of linkgraph overlay GUI. */

#ifndef LINKGRAPH_GUI_H_
#define LINKGRAPH_GUI_H_

#include "company_func.h"
#include "station_base.h"
#include <map>
#include <set>

/**
 * Properties of a link between two stations.
 */
struct LinkProperties {
	LinkProperties() : capacity(0), usage(0), planned(0) {}

	uint capacity;
	uint usage;
	uint planned;
};

/**
 * Handles drawing of links into some window.
 * @tparam Twindow window type to be drawn into. Must provide "Point GetStationMiddle(const Station *st) const"
 * @tparam Twidget_id ID of widget in window to be drawn into.
 */
template<class Twindow, uint Twidget_id>
class LinkGraphOverlay {
public:
	typedef LinkGraphOverlay<Twindow, Twidget_id> Self;
	typedef std::map<StationID, LinkProperties> StationLinkMap;
	typedef std::map<StationID, StationLinkMap> LinkMap;
	typedef std::set<StationID> StationSet;

	static const uint8 LINK_COLOURS[];
	static const uint REFRESH_INTERVAL = 0x1F;

	/**
	 * Create a link graph overlay for the specified window.
	 * @param w Window to be drawn into.
	 */
	LinkGraphOverlay(const Twindow *w, uint32 cargo_mask = 0xFFFF,
			uint32 company_mask = 1 << _local_company) :
			window(w), cargo_mask(cargo_mask), company_mask(company_mask)
	{}

	void DrawOrRebuildCache();
	void SetCargoMask(uint32 cargo_mask) {this->cargo_mask = cargo_mask; this->last_refresh_tick = 0;}
	void SetCompanyMask(uint32 company_mask) {this->company_mask = company_mask; this->last_refresh_tick = 0;}

protected:
	const Twindow *window;      ///< Window to be drawn into.
	uint32 cargo_mask;          ///< Bitmask of cargos to be displayed.
	uint32 company_mask;        ///< Bitmask of companies to be displayed.
	LinkMap cached_links;       ///< Cache for links to reduce recalculation.
	StationSet cached_stations; ///< Cache for stations to be drawn.
	uint16 last_refresh_tick;   ///< Last time of cache rebuild.

	void DrawForwBackLinks(Point pta, StationID sta, Point ptb, StationID stb) const;
	void AddLinks(const Station *sta, const Station *stb);
	void BuildCache();
	void DrawLinks() const;
	void DrawStationDots() const;
	bool IsLinkVisible(Point pta, Point ptb) const;

	static void AddStats(const LinkStat &orig_link, const FlowStat &orig_flow, LinkProperties &cargo);
	static void DrawContent(Point pta, Point ptb, LinkProperties &cargo);
	static void DrawVertex(int x, int y, int size, int colour, int border_colour);
};

#endif /* LINKGRAPH_GUI_H_ */
