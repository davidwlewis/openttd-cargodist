/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file linkgraph_gui.cpp Implementation of linkgraph overlay GUI. */

#include "stdafx.h"
#include "window_func.h"
#include "window_gui.h"
#include "company_base.h"
#include "company_gui.h"
#include "date_func.h"
#include "viewport_func.h"
#include "linkgraph_gui.h"
#include "smallmap_gui.h"
#include "widgets/link_graph_legend_widget.h"

/**
 * Colours for the various "load" states of links. Ordered from "unused" to
 * "overloaded".
 */
const uint8 LinkGraphOverlay::LINK_COLOURS[] = {
	0x0f, 0xd1, 0xd0, 0x57,
	0x55, 0x53, 0xbf, 0xbd,
	0xba, 0xb9, 0xb7, 0xb5
};

/**
 * Get a DPI for the widget we will be drawing to.
 * @param dpi DrawPixelInfo to fill with the desired dimensions.
 */ 
void LinkGraphOverlay::GetWidgetDpi(DrawPixelInfo *dpi) const
{
	const NWidgetBase *wi = this->window->GetWidget<NWidgetBase>(this->widget_id);
	dpi->left = dpi->top = 0;
	dpi->width = wi->current_x;
	dpi->height = wi->current_y;
}

/**
 * Rebuild the cache and recalculate which links and stations to be shown.
 */
void LinkGraphOverlay::RebuildCache()
{
	this->cached_links.clear();
	this->cached_stations.clear();

	DrawPixelInfo dpi;
	this->GetWidgetDpi(&dpi);

	const Station *sta;
	FOR_ALL_STATIONS(sta) {
		/* Show links between stations of selected companies or "neutral" ones like oilrigs. */
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

/**
 * Determine if a certain point is inside the given DPI, with some lee way.
 * @param pt Point we are looking for.
 * @param dpi Visible area.
 * @param padding Extent of the point.
 * @return If the point or any of its 'extent' is inside the dpi.
 */
inline bool LinkGraphOverlay::IsPointVisible(Point pt, const DrawPixelInfo *dpi, int padding) const
{
	return pt.x > dpi->left - padding && pt.y > dpi->top - padding &&
			pt.x < dpi->left + dpi->width + padding &&
			pt.y < dpi->top + dpi->height + padding;
}

/**
 * Determine if a certain link crosses through the area given by the dpi with some lee way.
 * @param pta First end of the link.
 * @param ptb Second end of the link.
 * @param dpi Visible area.
 * @param padding Width or thickness of the link.
 * @return If the link or any of its "thickness" is visible. This may return false positives.
 */
inline bool LinkGraphOverlay::IsLinkVisible(Point pta, Point ptb, const DrawPixelInfo *dpi, int padding) const
{
	return !((pta.x < dpi->left - padding && ptb.x < dpi->left - padding) ||
			(pta.y < dpi->top - padding && ptb.y < dpi->top - padding) ||
			(pta.x > dpi->left + dpi->width + padding &&
					ptb.x > dpi->left + dpi->width + padding) ||
			(pta.y > dpi->top + dpi->height + padding &&
					ptb.y > dpi->top + dpi->height + padding));
}

/**
 * Add all "interesting" links between the given stations to the cache.
 * @param from The source station.
 * @param to The destination station.
 */
void LinkGraphOverlay::AddLinks(const Station *from, const Station *to)
{
	CargoID c;
	FOR_EACH_SET_CARGO_ID(c, this->cargo_mask) {
		if (!CargoSpec::Get(c)->IsValid()) continue;
		const GoodsEntry &ge = from->goods[c];
		uint sum_flows = ge.GetSumFlowVia(to->index);
		const LinkStatMap &ls_map = ge.link_stats;
		LinkStatMap::const_iterator i = ls_map.find(to->index);
		if (i != ls_map.end()) {
			const LinkStat &link_stat = i->second;
			this->AddStats(link_stat, sum_flows, this->cached_links[from->index][to->index]);
		}
	}
}

/**
 * Add information from a given pair of link stat and flow stat to the given link properties.
 * @param orig_link Link stat to read the information from.
 * @param new_plan Planned flow for the link.
 * @param cargo LinkProperties to write the information to.
 */
/* static */ void LinkGraphOverlay::AddStats(const LinkStat &orig_link, uint new_plan, LinkProperties &cargo)
{
	uint new_cap = orig_link.Capacity();
	uint new_usg = orig_link.Usage();

	/* multiply the numbers by 32 in order to avoid comparing to 0 too often. */
	if (cargo.capacity == 0 ||
			max(cargo.usage, cargo.planned) * 32 / (cargo.capacity + 1) < max(new_usg, new_plan) * 32 / (new_cap + 1)) {
		cargo.capacity = new_cap;
		cargo.usage = new_usg;
		cargo.planned = new_plan;
	}
}

/**
 * Draw the linkgraph overlay or some part of it, in the area given.
 * @param dpi Area to be drawn to.
 */
void LinkGraphOverlay::Draw(const DrawPixelInfo *dpi) const
{
	this->DrawLinks(dpi);
	this->DrawStationDots(dpi);
}

/**
 * Draw the cached links or part of them into the given area.
 * @param dpi Area to be drawn to.
 */
void LinkGraphOverlay::DrawLinks(const DrawPixelInfo *dpi) const
{
	for (LinkMap::const_iterator i(this->cached_links.begin()); i != this->cached_links.end(); ++i) {
		if (!Station::IsValidID(i->first)) continue;
		Point pta = this->GetStationMiddle(Station::Get(i->first));
		for (StationLinkMap::const_iterator j(i->second.begin()); j != i->second.end(); ++j) {
			if (!Station::IsValidID(j->first)) continue;
			Point ptb = this->GetStationMiddle(Station::Get(j->first));
			if (!this->IsLinkVisible(pta, ptb, dpi, this->scale + 2)) continue;
			this->DrawContent(pta, ptb, j->second);
		}
	}
}

/**
 * Draw one specific link.
 * @param pta Source of the link.
 * @param ptb Destination of the link.
 * @param cargo Properties of the link.
 */
void LinkGraphOverlay::DrawContent(Point pta, Point ptb, const LinkProperties &cargo) const
{
	int offset_y = (pta.x < ptb.x ? 1 : -1) * this->scale;
	int offset_x = (pta.y > ptb.y ? 1 : -1) * this->scale;

	uint usage_or_plan = min(cargo.capacity * 2 + 1, max(cargo.usage, cargo.planned));
	int colour = LinkGraphOverlay::LINK_COLOURS[usage_or_plan * lengthof(LinkGraphOverlay::LINK_COLOURS) / (cargo.capacity * 2 + 2)];

	GfxDrawLine(pta.x + offset_x, pta.y, ptb.x + offset_x, ptb.y, colour, scale);
	GfxDrawLine(pta.x, pta.y + offset_y, ptb.x, ptb.y + offset_y, colour, scale);

	GfxDrawLine(pta.x, pta.y, ptb.x, ptb.y, _colour_gradient[COLOUR_GREY][1], this->scale);
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
		if (!this->IsPointVisible(pt, dpi, 3 * this->scale)) continue;

		uint r = this->scale * 2 + this->scale * 2 * min(200, i->second) / 200;

		LinkGraphOverlay::DrawVertex(pt.x, pt.y, r,
				_colour_gradient[Company::Get(st->owner)->colour][5],
				_colour_gradient[COLOUR_GREY][1]);
	}
}

/**
 * Draw a square symbolizing a producer of cargo.
 * @param x X coordinate of the middle of the vertex.
 * @param y Y coordinate of the middle of the vertex.
 * @param size Y and y extend of the vertex.
 * @param colour Colour with which the vertex will be filled.
 * @param border_colour Colour for the border of the vertex.
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

/**
 * Determine the middle of a station in the current window.
 * @param st The station we're looking for.
 * @return Middle point of the station in the current window.
 */
Point LinkGraphOverlay::GetStationMiddle(const Station *st) const {
	if (this->window->viewport != NULL) {
		return GetViewportStationMiddle(this->window->viewport, st);
	} else {
		/* assume this is a smallmap */
		return static_cast<const SmallMapWindow *>(this->window)->GetStationMiddle(st);
	}
}

/**
 * Set a new cargo mask and rebuild the cache.
 * @param cargo_mask New cargo mask.
 */
void LinkGraphOverlay::SetCargoMask(uint32 cargo_mask)
{
	this->cargo_mask = cargo_mask;
	this->RebuildCache();
	this->window->GetWidget<NWidgetBase>(this->widget_id)->SetDirty(this->window);
}

/**
 * Set a new company mask and rebuild the cache.
 * @param company_mask New company mask.
 */
void LinkGraphOverlay::SetCompanyMask(uint32 company_mask)
{
	this->company_mask = company_mask;
	this->RebuildCache();
	this->window->GetWidget<NWidgetBase>(this->widget_id)->SetDirty(this->window);
}

/** Make a number of rows with buttons for each company for the linkgraph legend window. */
NWidgetBase *MakeCompanyButtonRowsLinkGraphGUI(int *biggest_index)
{
	return MakeCompanyButtonRows(biggest_index, WID_LGL_COMPANY_FIRST, WID_LGL_COMPANY_LAST, 3, STR_LINKGRAPH_LEGEND_SELECT_COMPANIES);
}

NWidgetBase *MakeSaturationLegendLinkGraphGUI(int *biggest_index)
{
	NWidgetVertical *panel = new NWidgetVertical();
	for (uint i = 0; i < lengthof(LinkGraphOverlay::LINK_COLOURS); ++i) {
		NWidgetBackground * wid = new NWidgetBackground(WWT_PANEL, COLOUR_DARK_GREEN, i + WID_LGL_SATURATION_FIRST);
		wid->SetMinimalSize(50, FONT_HEIGHT_SMALL);
		wid->SetFill(0, 1);
		wid->SetResize(0, 1);
		panel->Add(wid);
	}
	*biggest_index = WID_LGL_SATURATION_LAST;
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
		NWidgetBackground * wid = new NWidgetBackground(WWT_PANEL, COLOUR_GREY, i + WID_LGL_CARGO_FIRST);
		wid->SetMinimalSize(25, FONT_HEIGHT_SMALL);
		wid->SetFill(0, 1);
		wid->SetResize(0, 1);
		row->Add(wid);
	}
	panel->Add(row);
	*biggest_index = WID_LGL_CARGO_LAST;
	return panel;
}


static const NWidgetPart _nested_linkgraph_legend_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN, WID_LGL_CAPTION), SetDataTip(STR_LINKGRAPH_LEGEND_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_HORIZONTAL),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_LGL_SATURATION),
				SetPadding(WD_FRAMERECT_TOP, 0, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				SetMinimalSize(50, 100),
				NWidgetFunction(MakeSaturationLegendLinkGraphGUI),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_LGL_COMPANIES),
				SetPadding(WD_FRAMERECT_TOP, 0, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				NWidget(NWID_VERTICAL, NC_EQUALSIZE),
					SetMinimalSize(100, 100),
					NWidgetFunction(MakeCompanyButtonRowsLinkGraphGUI),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_COMPANIES_ALL), SetDataTip(STR_LINKGRAPH_LEGEND_ALL, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_COMPANIES_NONE), SetDataTip(STR_LINKGRAPH_LEGEND_NONE, STR_NULL),
				EndContainer(),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_LGL_CARGOES),
				SetPadding(WD_FRAMERECT_TOP, WD_FRAMERECT_RIGHT, WD_FRAMERECT_BOTTOM, WD_CAPTIONTEXT_LEFT),
				NWidget(NWID_VERTICAL, NC_EQUALSIZE),
					SetMinimalSize(150, 100),
					NWidgetFunction(MakeCargoesLegendLinkGraphGUI),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_CARGOES_ALL), SetDataTip(STR_LINKGRAPH_LEGEND_ALL, STR_NULL),
					NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_LGL_CARGOES_NONE), SetDataTip(STR_LINKGRAPH_LEGEND_NONE, STR_NULL),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer()
};

static const WindowDesc _linkgraph_legend_desc(
	WDP_MANUAL, 300, 314,
	WC_LINKGRAPH_LEGEND, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_linkgraph_legend_widgets, lengthof(_nested_linkgraph_legend_widgets)
);

/**
 * Open a link graph legend window.
*/
void ShowLinkGraphLegend()
{
	AllocateWindowDescFront<LinkGraphLegendWindow>(&_linkgraph_legend_desc, 0);
}

LinkGraphLegendWindow::LinkGraphLegendWindow(const WindowDesc *desc, int window_number)
{
	this->InitNested(desc, window_number);
	this->InvalidateData(0);
	this->SetOverlay(FindWindowById(WC_MAIN_WINDOW, 0)->viewport->overlay);
}

/**
 * Set the overlay belonging to this menu and import its company/cargo settings.
 * @params overlay New overlay for this menu.
 */
void LinkGraphLegendWindow::SetOverlay(LinkGraphOverlay *overlay) {
	this->overlay = overlay;
	uint32 companies = this->overlay->GetCompanyMask();
	for (uint c = 0; c < MAX_COMPANIES; c++) {
		if (!this->IsWidgetDisabled(WID_LGL_COMPANY_FIRST + c)) {
			this->SetWidgetLoweredState(WID_LGL_COMPANY_FIRST + c, HasBit(companies, c));
		}
	}
	uint32 cargoes = this->overlay->GetCargoMask();
	for (uint c = 0; c < NUM_CARGO; c++) {
		if (!this->IsWidgetDisabled(WID_LGL_CARGO_FIRST + c)) {
			this->SetWidgetLoweredState(WID_LGL_CARGO_FIRST + c, HasBit(cargoes, c));
		}
	}
}

void LinkGraphLegendWindow::DrawWidget(const Rect &r, int widget) const
{
	const NWidgetBase *wid = this->GetWidget<NWidgetBase>(widget);
	if (IsInsideMM(widget, WID_LGL_COMPANY_FIRST, WID_LGL_COMPANY_LAST + 1)) {
		if (this->IsWidgetDisabled(widget)) return;
		CompanyID cid = (CompanyID)(widget - WID_LGL_COMPANY_FIRST);
		Dimension sprite_size = GetSpriteSize(SPR_COMPANY_ICON);
		DrawCompanyIcon(cid, (r.left + r.right - sprite_size.width) / 2, (r.top + r.bottom - sprite_size.height) / 2);
		return;
	}
	if (IsInsideMM(widget, WID_LGL_SATURATION_FIRST, WID_LGL_SATURATION_LAST + 1)) {
		GfxFillRect(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, LinkGraphOverlay::LINK_COLOURS[widget - WID_LGL_SATURATION_FIRST]);
		if (widget == WID_LGL_SATURATION_FIRST) {
			DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y, STR_LINKGRAPH_LEGEND_UNUSED, TC_FROMSTRING, SA_HOR_CENTER);
		} else if (widget == WID_LGL_SATURATION_LAST) {
			DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y, STR_LINKGRAPH_LEGEND_OVERLOADED, TC_FROMSTRING, SA_HOR_CENTER);
		} else if (widget == (WID_LGL_SATURATION_LAST + WID_LGL_SATURATION_FIRST) / 2) {
			DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y, STR_LINKGRAPH_LEGEND_SATURATED, TC_FROMSTRING, SA_HOR_CENTER);
		}
	}
	if (IsInsideMM(widget, WID_LGL_CARGO_FIRST, WID_LGL_CARGO_LAST + 1)) {
		if (this->IsWidgetDisabled(widget)) return;
		CargoSpec *cargo = CargoSpec::Get(widget - WID_LGL_CARGO_FIRST);
		GfxFillRect(r.left + 2, r.top + 2, r.right - 2, r.bottom - 2, cargo->legend_colour);
		DrawString(wid->pos_x, wid->current_x + wid->pos_x, wid->pos_y + 2, cargo->abbrev, TC_BLACK, SA_HOR_CENTER);
	}
}

/**
 * Update the overlay with the new company selection.
 */
void LinkGraphLegendWindow::UpdateOverlayCompanies()
{
	uint32 mask = 0;
	for (uint c = 0; c < MAX_COMPANIES; c++) {
		if (this->IsWidgetDisabled(c + WID_LGL_COMPANY_FIRST)) continue;
		if (!this->IsWidgetLowered(c + WID_LGL_COMPANY_FIRST)) continue;
		SetBit(mask, c);
	}
	this->overlay->SetCompanyMask(mask);
}

/**
 * Update the overlay with the new cargo selection.
 */
void LinkGraphLegendWindow::UpdateOverlayCargoes()
{
	uint32 mask = 0;
	for (uint c = 0; c < NUM_CARGO; c++) {
		if (this->IsWidgetDisabled(c + WID_LGL_CARGO_FIRST)) continue;
		if (!this->IsWidgetLowered(c + WID_LGL_CARGO_FIRST)) continue;
		SetBit(mask, c);
	}
	this->overlay->SetCargoMask(mask);
}

void LinkGraphLegendWindow::OnClick(Point pt, int widget, int click_count)
{
	/* Check which button is clicked */
	if (IsInsideMM(widget, WID_LGL_COMPANY_FIRST, WID_LGL_COMPANY_LAST + 1)) {
		/* Is it no on disable? */
		if (!this->IsWidgetDisabled(widget)) {
			this->ToggleWidgetLoweredState(widget);
			this->UpdateOverlayCompanies();
		}
	} else if (widget == WID_LGL_COMPANIES_ALL || widget == WID_LGL_COMPANIES_NONE) {
		for (uint c = 0; c < MAX_COMPANIES; c++) {
			if (this->IsWidgetDisabled(c + WID_LGL_COMPANY_FIRST)) continue;
			this->SetWidgetLoweredState(WID_LGL_COMPANY_FIRST + c, widget == WID_LGL_COMPANIES_ALL);
		}
		this->UpdateOverlayCompanies();
		this->SetDirty();
	} else if (IsInsideMM(widget, WID_LGL_CARGO_FIRST, WID_LGL_CARGO_LAST + 1)) {
		/* Is it no on disable? */
		if (!this->IsWidgetDisabled(widget)) {
			this->ToggleWidgetLoweredState(widget);
			this->UpdateOverlayCargoes();

		}
	} else if (widget == WID_LGL_CARGOES_ALL || widget == WID_LGL_CARGOES_NONE) {
		for (uint c = 0; c < NUM_CARGO; c++) {
			if (this->IsWidgetDisabled(c + WID_LGL_CARGO_FIRST)) continue;
			this->SetWidgetLoweredState(WID_LGL_CARGO_FIRST + c, widget == WID_LGL_CARGOES_ALL);
		}
		this->UpdateOverlayCargoes();
	}
	this->SetDirty();
}

/**
 * Invalidate the data of this window if the cargoes or companies have changed.
 * @param data ignored
 * @param gui_scope ignored
 */
void LinkGraphLegendWindow::OnInvalidateData(int data, bool gui_scope)
{
	/* Disable the companies who are not active */
	for (CompanyID i = COMPANY_FIRST; i < MAX_COMPANIES; i++) {
		this->SetWidgetDisabledState(i + WID_LGL_COMPANY_FIRST, !Company::IsValidID(i));
	}
	for (CargoID i = 0; i < NUM_CARGO; i++) {
		this->SetWidgetDisabledState(i + WID_LGL_CARGO_FIRST, !CargoSpec::Get(i)->IsValid());
	}
}
