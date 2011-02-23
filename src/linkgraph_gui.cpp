/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_gui.cpp Implementation of linkgraph overlay GUI. */

#include "stdafx.h"
#include "window_gui.h"
#include "company_base.h"
#include "company_gui.h"
#include "date_func.h"
#include "linkgraph_gui.h"
#include "viewport_func.h"

/**
 * Colours for the various "load" states of links. Ordered from "empty" to
 * "overcrowded".
 */
const uint8 LinkGraphOverlay::LINK_COLOURS[] = {
	0x0f, 0xd1, 0xd0, 0x57,
	0x55, 0x53, 0xbf, 0xbd,
	0xba, 0xb9, 0xb7, 0xb5
};

void LinkGraphOverlay::GetWidgetDpi(DrawPixelInfo *dpi) const
{
	const NWidgetBase *wi = this->window->GetWidget<NWidgetBase>(this->widget_id);
	dpi->left = dpi->top = 0;
	dpi->width = wi->current_x;
	dpi->height = wi->current_y;
}

void LinkGraphOverlay::RebuildCache()
{
	this->cached_links.clear();
	this->cached_stations.clear();

	DrawPixelInfo dpi;
	this->GetWidgetDpi(&dpi);

	const Station *sta;
	FOR_ALL_STATIONS(sta) {
		/* Show links between own stations or "neutral" ones like oilrigs.*/
		if (sta->owner != INVALID_COMPANY && !HasBit(this->company_mask, sta->owner)) continue;
		if (sta->rect.IsEmpty()) continue;

		Point pta = this->GetStationMiddle(sta);

		StationID from = sta->index;
		StationLinkMap &seen_links = this->cached_links[from];

		uint supply = 0;
		CargoID c;
		FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
			if (!CargoSpec::Get(c)->IsValid()) continue;

			supply += sta->goods[c].supply;
			const LinkStatMap &links = sta->goods[c].link_stats;
			for (LinkStatMap::const_iterator i = links.begin(); i != links.end(); ++i) {
				StationID to = i->first;
				if (seen_links.find(to) != seen_links.end()) continue;

				if (!Station::IsValidID(to)) continue;
				const Station *stb = Station::Get(to);
				if (stb->owner != INVALID_COMPANY && !HasBit(this->company_mask, stb->owner)) continue;
				if (stb->rect.IsEmpty()) continue;

				if (!this->IsLinkVisible(pta, this->GetStationMiddle(stb), &dpi)) continue;

				this->AddLinks(sta, stb);
				this->AddLinks(stb, sta);
				seen_links[to]; // make sure it is created and marked as seen
			}
		}
		if (this->IsPointVisible(pta, &dpi)) {
			this->cached_stations.push_back(std::make_pair(from, supply));
		}
	}
}

FORCEINLINE bool LinkGraphOverlay::IsPointVisible(Point pt, const DrawPixelInfo *dpi, int padding) const
{
	return pt.x > dpi->left - padding && pt.y > dpi->top - padding &&
			pt.x < dpi->left + dpi->width + padding &&
			pt.y < dpi->top + dpi->height + padding;
}

FORCEINLINE bool LinkGraphOverlay::IsLinkVisible(Point pta, Point ptb, const DrawPixelInfo *dpi, int padding) const
{
	return !((pta.x < dpi->left - padding && ptb.x < dpi->left - padding) ||
			(pta.y < dpi->top - padding && ptb.y < dpi->top - padding) ||
			(pta.x > dpi->left + dpi->width + padding &&
					ptb.x > dpi->left + dpi->width + padding) ||
			(pta.y > dpi->top + dpi->height + padding &&
					ptb.y > dpi->top + dpi->height + padding));
}

void LinkGraphOverlay::AddLinks(const Station *from, const Station *to)
{
	CargoID c;
	FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
		if (!CargoSpec::Get(c)->IsValid()) continue;
		const GoodsEntry &ge = from->goods[c];
		FlowStat sum_flows = ge.GetSumFlowVia(to->index);
		const LinkStatMap &ls_map = ge.link_stats;
		LinkStatMap::const_iterator i = ls_map.find(to->index);
		if (i != ls_map.end()) {
			const LinkStat &link_stat = i->second;
			this->AddStats(link_stat, sum_flows, this->cached_links[from->index][to->index]);
		}
	}
}


/* static */ void LinkGraphOverlay::AddStats(const LinkStat &orig_link, const FlowStat &orig_flow, LinkProperties &cargo)
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


void LinkGraphOverlay::Draw(const DrawPixelInfo *dpi) const
{
	this->DrawLinks(dpi);
	this->DrawStationDots(dpi);
}

void LinkGraphOverlay::DrawLinks(const DrawPixelInfo *dpi) const
{
	for (LinkMap::const_iterator i(this->cached_links.begin()); i != this->cached_links.end(); ++i) {
		if (!Station::IsValidID(i->first)) continue;
		Point pta = this->GetStationMiddle(Station::Get(i->first));
		for (StationLinkMap::const_iterator j(i->second.begin()); j != i->second.end(); ++j) {
			if (!Station::IsValidID(j->first)) continue;
			Point ptb = this->GetStationMiddle(Station::Get(j->first));
			if (!this->IsLinkVisible(pta, ptb, dpi)) continue;
			if (pta.x > ptb.x || (pta.x == ptb.x && pta.y > ptb.y)) {
				GfxDrawLine(pta.x, pta.y, ptb.x, ptb.y, _colour_gradient[COLOUR_GREY][1]);
			}
			LinkGraphOverlay::DrawContent(pta, ptb, j->second);
		}
	}
}

/* static */ void LinkGraphOverlay::DrawContent(Point pta, Point ptb, const LinkProperties &cargo)
{
	if (cargo.capacity <= 0) return;
	int direction_y = (pta.x < ptb.x ? 1 : -1);
	int direction_x = (pta.y > ptb.y ? 1 : -1);;

	uint usage_or_plan = min(cargo.capacity * 2, max(cargo.usage, cargo.planned));
	int colour = LinkGraphOverlay::LINK_COLOURS[usage_or_plan * lengthof(LinkGraphOverlay::LINK_COLOURS) / (cargo.capacity * 2 + 1)];
	GfxDrawLine(pta.x + direction_x, pta.y, ptb.x + direction_x, ptb.y, colour);
	GfxDrawLine(pta.x, pta.y + direction_y, ptb.x, ptb.y + direction_y, colour);
}

/**
 * Draw dots for stations into the smallmap. The dots' sizes are determined by the amount of
 * cargo produced there, their colours by the type of cargo produced.
 */
void LinkGraphOverlay::DrawStationDots(const DrawPixelInfo *dpi) const
{
	for (StationSupplyList::const_iterator i(this->cached_stations.begin()); i != this->cached_stations.end(); ++i) {
		const Station *st = Station::GetIfValid(i->first);
		if (st == NULL) continue;
		Point pt = this->GetStationMiddle(st);
		if (!this->IsPointVisible(pt, dpi, 10)) continue;

		uint r = 1;
		if (i->second >= 20) r++;
		if (i->second >= 90) r++;
		if (i->second >= 160) r++;

		LinkGraphOverlay::DrawVertex(pt.x, pt.y, r,
				_colour_gradient[Company::Get(st->owner)->colour][5],
				_colour_gradient[COLOUR_GREY][1]);
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
/* static */ void LinkGraphOverlay::DrawVertex(int x, int y, int size, int colour, int border_colour)
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

Point LinkGraphOverlay::GetStationMiddle(const Station *st) const {
	//if (this->window->viewport != NULL) {
	//	return GetViewportStationMiddle(this->window->viewport, st);
	//} else {
		/* assume this is a smallmap */
		//return GetSmallmapStationMiddle(this->window, st);
		Point dummy;
		dummy.x = dummy.y = 0;
		return dummy;
	//}
}

void LinkGraphOverlay::SetCargoMask(uint32 cargo_mask)
{
	this->cargo_mask = cargo_mask;
	this->RebuildCache();
	this->window->GetWidget<NWidgetBase>(this->widget_id)->SetDirty(this->window);
}

void LinkGraphOverlay::SetCompanyMask(uint32 company_mask)
{
	this->company_mask = company_mask;
	this->RebuildCache();
	this->window->GetWidget<NWidgetBase>(this->widget_id)->SetDirty(this->window);
}

enum LinkGraphLegendWindowWidgets {
	LGL_CAPTION,           ///< Caption widget.
	LGL_SATURATION,        ///< Saturation legend.
	LGL_SATURATION_FIRST,
	LGL_SATURATION_LAST = LGL_SATURATION_FIRST + lengthof(LinkGraphOverlay::LINK_COLOURS) - 1,
	LGL_COMPANIES,         ///< Company selection widget.
	LGL_COMPANY_FIRST,
	LGL_COMPANY_LAST = LGL_COMPANY_FIRST + MAX_COMPANIES - 1,
	LGL_COMPANIES_ALL,
	LGL_COMPANIES_NONE,
	LGL_CARGOES,            ///< Cargo selection widget.
	LGL_CARGO_FIRST,
	LGL_CARGO_LAST = LGL_CARGO_FIRST + NUM_CARGO - 1,
	LGL_CARGOES_ALL,
	LGL_CARGOES_NONE,s
};

/** Make a number of rows with buttons for each company for the linkgraph legend window. */
NWidgetBase *MakeCompanyButtonRowsLinkGraphGUI(int *biggest_index)
{
	return MakeCompanyButtonRows(biggest_index, LGL_COMPANY_FIRST, LGL_COMPANY_LAST, 3, STR_LINKGRAPH_LEGEND_SELECT_COMPANIES);
}

NWidgetBase *MakeSaturationLegendLinkGraphGUI(int *biggest_index)
{
	NWidgetVertical *panel = new NWidgetVertical();
	for (uint i = 0; i < lengthof(LinkGraphOverlay::LINK_COLOURS); ++i) {
		NWidgetBackground * wid = new NWidgetBackground(WWT_PANEL, COLOUR_DARK_GREEN, i + LGL_SATURATION_FIRST);
		wid->SetMinimalSize(50, FONT_HEIGHT_SMALL);
		wid->SetFill(0, 1);
		wid->SetResize(0, 1);
		panel->Add(wid);
	}
	*biggest_index = LGL_SATURATION_LAST;
	return panel;
}

NWidgetBase *MakeCargoesLegendLinkGraphGUI(int *biggest_index)
{
	static const uint ENTRIES_PER_ROW = CeilDiv(NUM_CARGO, 5);
	NWidgetVertical *panel = new NWidgetVertical();
	NWidgetHorizontal *row = NULL;
	for (uint i = 0; i < NUM_CARGO; ++i) {
		if (i % ENTRIES_PER_ROW == 0) {
			if (row) panel->Add(row);
			row = new NWidgetHorizontal();
		}
		NWidgetBackground * wid = new NWidgetBackground(WWT_PANEL, COLOUR_GREY, i + LGL_CARGO_FIRST);
		wid->SetMinimalSize(25, FONT_HEIGHT_SMALL);
		wid->SetFill(0, 1);
		wid->SetResize(0, 1);
		row->Add(wid);
	}
	panel->Add(row);
	*biggest_index = LGL_CARGO_LAST;
	return panel;
}


static const NWidgetPart _nested_linkgraph_legend_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, LGL_CAPTION), SetDataTip(STR_LINKGRAPH_LEGEND_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, LGL_SATURATION),
				SetPadding(WD_FRAMERECT_TOP, 0, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				SetMinimalSize(50, 100),
				NWidgetFunction(MakeSaturationLegendLinkGraphGUI),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, LGL_COMPANIES),
				SetPadding(WD_FRAMERECT_TOP, 0, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				NWidget(NWID_VERTICAL, NC_EQUALSIZE),
					SetMinimalSize(100, 100),
					NWidgetFunction(MakeCompanyButtonRowsLinkGraphGUI),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, LGL_COMPANIES_ALL), SetDataTip(STR_LINKGRAPH_LEGEND_ALL, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, LGL_COMPANIES_NONE), SetDataTip(STR_LINKGRAPH_LEGEND_NONE, STR_NULL),
				EndContainer(),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, LGL_CARGOES),
				SetPadding(WD_FRAMERECT_TOP, WD_FRAMERECT_RIGHT, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				NWidget(NWID_VERTICAL, NC_EQUALSIZE),
					SetMinimalSize(150, 100),
					NWidgetFunction(MakeCargoesLegendLinkGraphGUI),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, LGL_CARGOES_ALL), SetDataTip(STR_LINKGRAPH_LEGEND_ALL, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, LGL_CARGOES_NONE), SetDataTip(STR_LINKGRAPH_LEGEND_NONE, STR_NULL),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static const WindowDesc _linkgraph_legend_desc(
	WDP_MANUAL, 300, 314,
	WC_LINKGRAPH_LEGEND, WC_NONE,
	0,
	_nested_linkgraph_legend_widgets, lengthof(_nested_linkgraph_legend_widgets)
);

void ShowLinkGraphLegend()
{
	AllocateWindowDescFront<LinkGraphLegendWindow>(&_linkgraph_legend_desc, 0);
}

LinkGraphLegendWindow::LinkGraphLegendWindow(const WindowDesc *desc, int window_number)
{
	this->InitNested(desc, window_number);
	this->InvalidateData(0);
}


void LinkGraphLegendWindow::DrawWidget(const Rect &r, int widget) const
{
	const NWidgetBase *wid = this->GetWidget<NWidgetBase>(widget);
	if (IsInsideMM(widget, LGL_COMPANY_FIRST, LGL_COMPANY_LAST + 1)) {
		if (this->IsWidgetDisabled(widget)) return;
		CompanyID cid = (CompanyID)(widget - LGL_COMPANY_FIRST);
		Dimension sprite_size = GetSpriteSize(SPR_COMPANY_ICON);
		DrawCompanyIcon(cid, (r.left + r.right - sprite_size.width) / 2, (r.top + r.bottom - sprite_size.height) / 2);
		return;
	}
	if (IsInsideMM(widget, LGL_SATURATION_FIRST, LGL_SATURATION_LAST + 1)) {
		GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, LinkGraphOverlay::LINK_COLOURS[widget - LGL_SATURATION_FIRST]);
		if (widget == LGL_SATURATION_FIRST) {
			DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y, STR_LINKGRAPH_LEGEND_UNUSED, TC_FROMSTRING, SA_HOR_CENTER);
		} else if (widget == LGL_SATURATION_LAST) {
			DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y, STR_LINKGRAPH_LEGEND_OVERLOADED, TC_FROMSTRING, SA_HOR_CENTER);
		} else if (widget == (LGL_SATURATION_LAST + LGL_SATURATION_FIRST) / 2) {
			DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y, STR_LINKGRAPH_LEGEND_SATURATED, TC_FROMSTRING, SA_HOR_CENTER);
		}
	}
	if (IsInsideMM(widget, LGL_CARGO_FIRST, LGL_CARGO_LAST + 1)) {
		if (this->IsWidgetDisabled(widget)) return;
		CargoSpec *cargo = CargoSpec::Get(widget - LGL_CARGO_FIRST);
		GfxFillRect(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2, cargo->legend_colour);
		DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y + 2, cargo->abbrev, TC_BLACK, SA_HOR_CENTER);
	}
}

/**
 * Invalidate the data of this window.
 * @param data ignored
 */
void LinkGraphLegendWindow::OnInvalidateData(int data)
{
	/* Disable the companies who are not active */
	for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
		this->SetWidgetDisabledState(i + LGL_COMPANY_FIRST, !Company::IsValidID(i));
	}
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		this->SetWidgetDisabledState(i + LGL_CARGO_FIRST, !CargoSpec::Get(i)->IsValid());
	}
}
