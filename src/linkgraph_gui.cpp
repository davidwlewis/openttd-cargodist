/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_gui.cpp Implementation of linkgraph overlay GUI. */

#include "stdafx.h"
#include "station_base.h"
#include "widget_type.h"
#include "window_gui.h"
#include "linkgraph_gui.h"

template<class Twindow>
void LinkGraphOverlay<Twindow>::Draw() const
{
	std::set<StationID> seen_stations;
	std::set<std::pair<StationID, StationID> > seen_links;

	const Station *sta;
	FOR_ALL_STATIONS(sta) {
		/* Show links between own stations or "neutral" ones like oilrigs.*/
		if (!HasBit(this->company_mask, sta->owner)) continue;
		CargoID c;
		FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
			if (!CargoSpec::Get(c)->IsValid()) continue;

			const LinkStatMap &links = sta->goods[c].link_stats;
			for (LinkStatMap::const_iterator i = links.begin(); i != links.end(); ++i) {
				StationID from = sta->index;
				StationID to = i->first;
				if (Station::IsValidID(to) && seen_stations.find(to) == seen_stations.end()) {
					const Station *stb = Station::Get(to);

					if (!HasBit(this->company_mask, stb->owner)) continue;
					if (sta->rect.IsEmpty() || stb->rect.IsEmpty()) continue;
					if (seen_links.find(std::make_pair(to, from)) != seen_links.end()) continue;

					Point pta = this->window->GetStationMiddle(sta);
					Point ptb = this->window->GetStationMiddle(stb);
					if (!this->IsLinkVisible(pta, ptb)) continue;

					this->DrawForwBackLinks(pta, sta->index, ptb, stb->index);
					seen_stations.insert(to);
				}
				seen_links.insert(std::make_pair(from, to));
			}
		}
		seen_stations.clear();
	}

	this->DrawStationDots();
}

template<class Twindow>
FORCEINLINE bool LinkGraphOverlay<Twindow>::IsLinkVisible(Point pta, Point ptb) const
{
	const NWidgetBase *wi = static_cast<const Window *>(this->window)->GetWidget<NWidgetBase>(this->widget_id);
	return !((pta.x < 0 && ptb.x < 0) ||
			(pta.y < 0 && ptb.y < 0) ||
			(pta.x > (int)wi->current_x && ptb.x > (int)wi->current_x) ||
			(pta.y > (int)wi->current_y && ptb.y > (int)wi->current_y));
}

template<class Twindow>
void LinkGraphOverlay<Twindow>::AddLinks(StationID sta, StationID stb, LinkProperties &cargo) const
{
	CargoID c;
	FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
		if (!CargoSpec::Get(c)->IsValid()) continue;
		GoodsEntry &ge = Station::Get(sta)->goods[c];
		FlowStat sum_flows = ge.GetSumFlowVia(stb);
		const LinkStatMap &ls_map = ge.link_stats;
		LinkStatMap::const_iterator i = ls_map.find(stb);
		if (i != ls_map.end()) {
			const LinkStat &link_stat = i->second;
			this->AddStats(link_stat, sum_flows, cargo);
		}
	}
}

template<class Twindow>
/* static */ void LinkGraphOverlay<Twindow>::AddStats(const LinkStat &orig_link, const FlowStat &orig_flow, LinkProperties &cargo)
{
	uint new_cap = orig_link.Capacity();
	uint new_usg = orig_link.Usage();
	uint new_plan = orig_flow.Planned();

	/* multiply the numbers by 32 in order to avoid comparing to 0 too often. */
	if (cargo.capacity == 0 ||
			max(cargo.usage, cargo.planned) * 32 / (cargo.capacity + 1) < max(new_usg, new_plan) * 32 / (new_cap + 1)) {
		cargo.capacity = new_cap;
		cargo.usage = new_usg;
		cargo.planned = new_plan;
	}
}

template<class Twindow>
void LinkGraphOverlay<Twindow>::DrawForwBackLinks(Point pta, StationID sta, Point ptb, StationID stb) const
{
	LinkProperties forward, backward;
	this->AddLinks(sta, stb, forward);
	this->AddLinks(stb, sta, backward);
	GfxDrawLine(pta.x, pta.y, ptb.x, ptb.y, _colour_gradient[COLOUR_GREY][1]);
	this->DrawContent(pta, ptb, forward);
	this->DrawContent(ptb, pta, backward);

}

template<class Twindow>
/* static */ void LinkGraphOverlay<Twindow>::DrawContent(Point pta, Point ptb, LinkProperties &cargo)
{
	if (cargo.capacity <= 0) return;
	int direction_y = (pta.x < ptb.x ? 1 : -1);
	int direction_x = (pta.y > ptb.y ? 1 : -1);;

	uint usage_or_plan = min(cargo.capacity * 2, max(cargo.usage, cargo.planned));
	int colour = LinkGraphOverlay<Twindow>::LINK_COLOURS[usage_or_plan * lengthof(LinkGraphOverlay<Twindow>::LINK_COLOURS) / (cargo.capacity * 2 + 1)];
	GfxDrawLine(pta.x + direction_x, pta.y, ptb.x + direction_x, ptb.y, colour);
	GfxDrawLine(pta.x, pta.y + direction_y, ptb.x, ptb.y + direction_y, colour);
}

/**
 * Draw dots for stations into the smallmap. The dots' sizes are determined by the amount of
 * cargo produced there, their colours by the type of cargo produced.
 */
template<class Twindow>
void LinkGraphOverlay<Twindow>::DrawStationDots() const
{
	const Station *st;
	FOR_ALL_STATIONS(st) {
		if (!HasBit(this->company_mask, st->owner) || st->rect.IsEmpty()) continue;
		Point pt = this->window->GetStationMiddle(st);
		const NWidgetBase *wi = static_cast<const Window *>(this->window)->GetWidget<NWidgetBase>(this->widget_id);
		if (pt.x < 0 || pt.y < 0 || pt.x > (int)wi->current_x || pt.y > (int)wi->current_y) continue;

		/* Add up cargo supplied for each selected cargo type */
		uint q = 0;
		int colour = 0;
		int numCargos = 0;
		CargoID c;
		FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
			if (!CargoSpec::Get(c)->IsValid()) continue;
			uint supply = st->goods[c].supply;
			if (supply > 0) {
				q += supply;
				colour += CargoSpec::Get(c)->legend_colour;
				++numCargos;
			}
		}
		if (numCargos > 1) colour /= numCargos;

		uint r = 1;
		if (q >= 20) r++;
		if (q >= 90) r++;
		if (q >= 160) r++;

		LinkGraphOverlay::DrawVertex(pt.x, pt.y, r, colour, _colour_gradient[COLOUR_GREY][3]);
	}
}

/**
 * Draw a square symbolizing a producer of cargo.
 * @param x the x coordinate of the middle of the vertex
 * @param y the y coordinate of the middle of the vertex
 * @param size the x and y extend of the vertex
 * @param colour the colour with which the vertex will be filled
 * @param border_colour the colour for the border of the vertex
 */
template<class Twindow>
/* static */ void LinkGraphOverlay<Twindow>::DrawVertex(int x, int y, int size, int colour, int border_colour)
{
	size--;
	int w1 = size / 2;
	int w2 = size / 2 + size % 2;

	GfxFillRect(x - w1, y - w1, x + w2, y + w2, colour);

	w1++;
	w2++;
	GfxDrawLine(x - w1, y - w1, x + w2, y - w1, border_colour);
	GfxDrawLine(x - w1, y + w2, x + w2, y + w2, border_colour);
	GfxDrawLine(x - w1, y - w1, x - w1, y + w2, border_colour);
	GfxDrawLine(x + w2, y - w1, x + w2, y + w2, border_colour);
}
