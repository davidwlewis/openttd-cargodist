/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_gui.cpp Implementation of linkgraph overlay GUI. */

#include "stdafx.h"
#include "widget_type.h"
#include "window_gui.h"
#include "company_base.h"
#include "date_func.h"
#include "linkgraph_gui.h"
#include "smallmap_gui.h"

template<class Twindow, uint Twidget_id>
void LinkGraphOverlay<Twindow, Twidget_id>::BuildCache()
{
	this->cached_links.clear();
	this->stations.clear();

	const NWidgetBase *wi = static_cast<const Window *>(this->window)->GetWidget<NWidgetBase>(Twidget_id);
	const Station *sta;
	FOR_ALL_STATIONS(sta) {
		/* Show links between own stations or "neutral" ones like oilrigs.*/
		if (sta->owner != INVALID_COMPANY && !HasBit(this->company_mask, sta->owner)) continue;
		if (sta->rect.IsEmpty()) continue;

		StationID from = sta->index;
		Point pta = this->window->GetStationMiddle(sta);
		if (pta.x > 0 && pta.y > 0 && pta.x < (int)wi->current_x && pta.y < (int)wi->current_y) {
			this->cached_stations.insert(from);
		}

		StationLinkMap &seen_links = this->cached_links[from];

		CargoID c;
		FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
			if (!CargoSpec::Get(c)->IsValid()) continue;

			const LinkStatMap &links = sta->goods[c].link_stats;
			for (LinkStatMap::const_iterator i = links.begin(); i != links.end(); ++i) {
				StationID to = i->first;
				if (seen_links.find(to) != seen_links.end()) continue;

				if (!Station::IsValidID(to)) continue;
				const Station *stb = Station::Get(to);
				if (stb->owner != INVALID_COMPANY && !HasBit(this->company_mask, stb->owner)) continue;
				if (stb->rect.IsEmpty()) continue;

				if (!this->IsLinkVisible(pta, this->window->GetStationMiddle(stb))) continue;

				this->AddLinks(sta, stb);
				this->AddLinks(stb, sta);
				seen_links[to]; // make sure it is created and marked as seen
			}
		}
	}
}

template<class Twindow, uint Twidget_id>
FORCEINLINE bool LinkGraphOverlay<Twindow, Twidget_id>::IsLinkVisible(Point pta, Point ptb) const
{
	const NWidgetBase *wi = static_cast<const Window *>(this->window)->GetWidget<NWidgetBase>(Twidget_id);
	return !((pta.x < 0 && ptb.x < 0) ||
			(pta.y < 0 && ptb.y < 0) ||
			(pta.x > (int)wi->current_x && ptb.x > (int)wi->current_x) ||
			(pta.y > (int)wi->current_y && ptb.y > (int)wi->current_y));
}

template<class Twindow, uint Twidget_id>
void LinkGraphOverlay<Twindow, Twidget_id>::AddLinks(const Station *from, const Station *to)
{
	CargoID c;
	FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
		if (!CargoSpec::Get(c)->IsValid()) continue;
		GoodsEntry &ge = from->goods[c];
		FlowStat sum_flows = ge.GetSumFlowVia(to->index);
		const LinkStatMap &ls_map = ge.link_stats;
		LinkStatMap::const_iterator i = ls_map.find(to->index);
		if (i != ls_map.end()) {
			const LinkStat &link_stat = i->second;
			this->AddStats(link_stat, sum_flows, this->cached_links[from->index][to->index]);
		}
	}
}

template<class Twindow, uint Twidget_id>
/* static */ void LinkGraphOverlay<Twindow, Twidget_id>::AddStats(const LinkStat &orig_link, const FlowStat &orig_flow, LinkProperties &cargo)
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


template<class Twindow, uint Twidget_id>
void LinkGraphOverlay<Twindow, Twidget_id>::DrawOrRebuildCache()
{
	if (_tick_counter < this->last_refresh_tick ||
			_tick_counter > this->last_refresh_tick + Self::REFRESH_INTERVAL) {
		this->BuildCache();
		this->last_refresh_tick = _tick_counter;
	}
	this->DrawLinks();
	this->DrawStationDots();
}

template<class Twindow, uint Twidget_id>
void LinkGraphOverlay<Twindow, Twidget_id>::DrawLinks() const
{
	for (LinkMap::const_iterator i(this->cached_links.begin()); i != this->cached_links.end(); ++i) {
		if (!Station::IsValidID(i->first)) continue;
		Point pta = this->window->GetStationMiddle(Station::Get(i->first));
		for (StationLinkMap::const_iterator j(i->second.begin()); j != i->second.end(); ++j) {
			if (!Station::IsValidID(j->first)) continue;
			Point ptb = this->window->GetStationMiddle(Station::Get(j->first));
			if (pta.x > ptb.x || (pta.x == ptb.x && pta.y > ptb.y)) {
				GfxDrawLine(pta.x, pta.y, ptb.x, ptb.y, _colour_gradient[COLOUR_GREY][1]);
			}
			this->DrawContent(pta, ptb, j->second);
		}
	}
}

template<class Twindow, uint Twidget_id>
/* static */ void LinkGraphOverlay<Twindow, Twidget_id>::DrawContent(Point pta, Point ptb, LinkProperties &cargo)
{
	if (cargo.capacity <= 0) return;
	int direction_y = (pta.x < ptb.x ? 1 : -1);
	int direction_x = (pta.y > ptb.y ? 1 : -1);;

	uint usage_or_plan = min(cargo.capacity * 2, max(cargo.usage, cargo.planned));
	int colour = Self::LINK_COLOURS[usage_or_plan * lengthof(Self::LINK_COLOURS) / (cargo.capacity * 2 + 1)];
	GfxDrawLine(pta.x + direction_x, pta.y, ptb.x + direction_x, ptb.y, colour);
	GfxDrawLine(pta.x, pta.y + direction_y, ptb.x, ptb.y + direction_y, colour);
}

/**
 * Draw dots for stations into the smallmap. The dots' sizes are determined by the amount of
 * cargo produced there, their colours by the type of cargo produced.
 */
template<class Twindow, uint Twidget_id>
void LinkGraphOverlay<Twindow, Twidget_id>::DrawStationDots() const
{

	for (StationSet::const_iterator i(this->cached_stations.begin()); i != this->cached_stations.end(); ++i) {
		const Station *st = Station::GetIfValid(*i);
		if (st == NULL) continue;

		Point pt = this->window->GetStationMiddle(st);

		/* Add up cargo supplied for each selected cargo type */
		uint q = 0;
		CargoID c;
		FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
			if (!CargoSpec::Get(c)->IsValid()) continue;
			uint supply = st->goods[c].supply;
			if (supply > 0) {
				q += supply;
			}
		}

		uint r = 1;
		if (q >= 20) r++;
		if (q >= 90) r++;
		if (q >= 160) r++;

		Self::DrawVertex(pt.x, pt.y, r, _colour_gradient[Company::Get(st->owner)->colour][5], _colour_gradient[COLOUR_GREY][1]);
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
template<class Twindow, uint Twidget_id>
/* static */ void LinkGraphOverlay<Twindow, Twidget_id>::DrawVertex(int x, int y, int size, int colour, int border_colour)
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

template class LinkGraphOverlay<SmallMapWindow, SM_WIDGET_MAP>;
/**
 * Colours for the various "load" states of links. Ordered from "empty" to
 * "overcrowded".
 */
template<> const uint8 LinkGraphOverlay<SmallMapWindow, SM_WIDGET_MAP>::LINK_COLOURS[] = {
	0x0f, 0xd1, 0xd0, 0x57,
	0x55, 0x53, 0xbf, 0xbd,
	0xba, 0xb9, 0xb7, 0xb5
};
