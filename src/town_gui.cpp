/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file town_gui.cpp GUI for towns. */

#include "stdafx.h"
#include "town.h"
#include "viewport_func.h"
#include "error.h"
#include "gui.h"
#include "command_func.h"
#include "company_func.h"
#include "company_base.h"
#include "company_gui.h"
#include "network/network.h"
#include "string_func.h"
#include "strings_func.h"
#include "sound_func.h"
#include "economy_func.h"
#include "tilehighlight_func.h"
#include "sortlist_type.h"
#include "road_cmd.h"
#include "landscape.h"
#include "cargotype.h"
#include "querystring_gui.h"
#include "window_func.h"
#include "townname_func.h"
#include "townname_type.h"
#include "core/geometry_func.hpp"
#include "genworld.h"
#include "sprite.h"

#include "widgets/town_widget.h"

#include "table/strings.h"

typedef GUIList<const Town*> GUITownList;

static const NWidgetPart _nested_town_authority_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TA_CAPTION), SetDataTip(STR_LOCAL_AUTHORITY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TA_RATING_INFO), SetMinimalSize(317, 92), SetResize(1, 1), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_BROWN, WID_TA_COMMAND_LIST), SetMinimalSize(305, 52), SetResize(1, 0), SetDataTip(0x0, STR_LOCAL_AUTHORITY_ACTIONS_TOOLTIP), SetScrollbar(WID_TA_SCROLLBAR), EndContainer(),
		NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_TA_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TA_ACTION_INFO), SetMinimalSize(317, 52), SetResize(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TA_EXECUTE),  SetMinimalSize(317, 12), SetResize(1, 0), SetFill(1, 0), SetDataTip(STR_LOCAL_AUTHORITY_DO_IT_BUTTON, STR_LOCAL_AUTHORITY_DO_IT_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
	EndContainer()
};

/** Town authority window. */
struct TownAuthorityWindow : Window {
private:
	Town *town;    ///< Town being displayed.
	int sel_index; ///< Currently selected town action, \c 0 to \c TACT_COUNT-1, \c -1 means no action selected.
	Scrollbar *vscroll;
	uint displayed_actions_on_previous_painting; ///< Actions that were available on the previous call to OnPaint()

	/**
	 * Get the position of the Nth set bit.
	 *
	 * If there is no Nth bit set return -1
	 *
	 * @param bits The value to search in
	 * @param n The Nth set bit from which we want to know the position
	 * @return The position of the Nth set bit
	 */
	static int GetNthSetBit(uint32 bits, int n)
	{
		if (n >= 0) {
			uint i;
			FOR_EACH_SET_BIT(i, bits) {
				n--;
				if (n < 0) return i;
			}
		}
		return -1;
	}

public:
	TownAuthorityWindow(const WindowDesc *desc, WindowNumber window_number) : Window(), sel_index(-1), displayed_actions_on_previous_painting(0)
	{
		this->town = Town::Get(window_number);
		this->InitNested(desc, window_number);
		this->vscroll = this->GetScrollbar(WID_TA_SCROLLBAR);
		this->vscroll->SetCapacity((this->GetWidget<NWidgetBase>(WID_TA_COMMAND_LIST)->current_y - WD_FRAMERECT_TOP - WD_FRAMERECT_BOTTOM) / FONT_HEIGHT_NORMAL);
	}

	virtual void OnPaint()
	{
		int numact;
		uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);
		if (buttons != displayed_actions_on_previous_painting) this->SetDirty();
		displayed_actions_on_previous_painting = buttons;

		this->vscroll->SetCount(numact + 1);

		if (this->sel_index != -1 && !HasBit(buttons, this->sel_index)) {
			this->sel_index = -1;
		}

		this->SetWidgetDisabledState(WID_TA_EXECUTE, this->sel_index == -1);

		this->DrawWidgets();
		if (!this->IsShaded()) this->DrawRatings();
	}

	/** Draw the contents of the ratings panel. May request a resize of the window if the contents does not fit. */
	void DrawRatings()
	{
		NWidgetBase *nwid = this->GetWidget<NWidgetBase>(WID_TA_RATING_INFO);
		uint left = nwid->pos_x + WD_FRAMERECT_LEFT;
		uint right = nwid->pos_x + nwid->current_x - 1 - WD_FRAMERECT_RIGHT;

		uint y = nwid->pos_y + WD_FRAMERECT_TOP;

		DrawString(left, right, y, STR_LOCAL_AUTHORITY_COMPANY_RATINGS);
		y += FONT_HEIGHT_NORMAL;

		Dimension icon_size = GetSpriteSize(SPR_COMPANY_ICON);
		int icon_width      = icon_size.width;
		int icon_y_offset   = (FONT_HEIGHT_NORMAL - icon_size.height) / 2;

		Dimension exclusive_size = GetSpriteSize(SPR_EXCLUSIVE_TRANSPORT);
		int exclusive_width      = exclusive_size.width;
		int exclusive_y_offset   = (FONT_HEIGHT_NORMAL - exclusive_size.height) / 2;

		bool rtl = _current_text_dir == TD_RTL;
		uint text_left      = left  + (rtl ? 0 : icon_width + exclusive_width + 4);
		uint text_right     = right - (rtl ? icon_width + exclusive_width + 4 : 0);
		uint icon_left      = rtl ? right - icon_width : left;
		uint exclusive_left = rtl ? right - icon_width - exclusive_width - 2 : left + icon_width + 2;

		/* Draw list of companies */
		const Company *c;
		FOR_ALL_COMPANIES(c) {
			if ((HasBit(this->town->have_ratings, c->index) || this->town->exclusivity == c->index)) {
				DrawCompanyIcon(c->index, icon_left, y + icon_y_offset);

				SetDParam(0, c->index);
				SetDParam(1, c->index);

				int r = this->town->ratings[c->index];
				StringID str;
				(str = STR_CARGO_RATING_APPALLING, r <= RATING_APPALLING) || // Apalling
				(str++,                    r <= RATING_VERYPOOR)  || // Very Poor
				(str++,                    r <= RATING_POOR)      || // Poor
				(str++,                    r <= RATING_MEDIOCRE)  || // Mediocore
				(str++,                    r <= RATING_GOOD)      || // Good
				(str++,                    r <= RATING_VERYGOOD)  || // Very Good
				(str++,                    r <= RATING_EXCELLENT) || // Excellent
				(str++,                    true);                    // Outstanding

				SetDParam(2, str);
				if (this->town->exclusivity == c->index) {
					DrawSprite(SPR_EXCLUSIVE_TRANSPORT, COMPANY_SPRITE_COLOUR(c->index), exclusive_left, y + exclusive_y_offset);
				}

				DrawString(text_left, text_right, y, STR_LOCAL_AUTHORITY_COMPANY_RATING);
				y += FONT_HEIGHT_NORMAL;
			}
		}

		y = y + WD_FRAMERECT_BOTTOM - nwid->pos_y; // Compute needed size of the widget.
		if (y > nwid->current_y) {
			/* If the company list is too big to fit, mark ourself dirty and draw again. */
			ResizeWindow(this, 0, y - nwid->current_y);
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == WID_TA_CAPTION) SetDParam(0, this->window_number);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_TA_ACTION_INFO:
				if (this->sel_index != -1) {
					SetDParam(0, _price[PR_TOWN_ACTION] * _town_action_costs[this->sel_index] >> 8);
					DrawStringMultiLine(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, r.bottom - WD_FRAMERECT_BOTTOM,
								STR_LOCAL_AUTHORITY_ACTION_TOOLTIP_SMALL_ADVERTISING + this->sel_index);
				}
				break;
			case WID_TA_COMMAND_LIST: {
				int numact;
				uint buttons = GetMaskOfTownActions(&numact, _local_company, this->town);
				int y = r.top + WD_FRAMERECT_TOP;
				int pos = this->vscroll->GetPosition();

				if (--pos < 0) {
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_LOCAL_AUTHORITY_ACTIONS_TITLE);
					y += FONT_HEIGHT_NORMAL;
				}

				for (int i = 0; buttons; i++, buttons >>= 1) {
					if (pos <= -5) break; ///< Draw only the 5 fitting lines

					if ((buttons & 1) && --pos < 0) {
						DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y,
								STR_LOCAL_AUTHORITY_ACTION_SMALL_ADVERTISING_CAMPAIGN + i, this->sel_index == i ? TC_WHITE : TC_ORANGE);
						y += FONT_HEIGHT_NORMAL;
					}
				}
				break;
			}
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_TA_ACTION_INFO: {
				assert(size->width > padding.width && size->height > padding.height);
				size->width -= WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				size->height -= WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				Dimension d = {0, 0};
				for (int i = 0; i < TACT_COUNT; i++) {
					SetDParam(0, _price[PR_TOWN_ACTION] * _town_action_costs[i] >> 8);
					d = maxdim(d, GetStringMultiLineBoundingBox(STR_LOCAL_AUTHORITY_ACTION_TOOLTIP_SMALL_ADVERTISING + i, *size));
				}
				*size = maxdim(*size, d);
				size->width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				size->height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;
			}

			case WID_TA_COMMAND_LIST:
				size->height = WD_FRAMERECT_TOP + 5 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM;
				size->width = GetStringBoundingBox(STR_LOCAL_AUTHORITY_ACTIONS_TITLE).width;
				for (uint i = 0; i < TACT_COUNT; i++ ) {
					size->width = max(size->width, GetStringBoundingBox(STR_LOCAL_AUTHORITY_ACTION_SMALL_ADVERTISING_CAMPAIGN + i).width);
				}
				size->width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				break;

			case WID_TA_RATING_INFO:
				resize->height = FONT_HEIGHT_NORMAL;
				size->height = WD_FRAMERECT_TOP + 9 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM;
				break;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TA_COMMAND_LIST: {
				int y = this->GetRowFromWidget(pt.y, WID_TA_COMMAND_LIST, 1, FONT_HEIGHT_NORMAL);
				if (!IsInsideMM(y, 0, 5)) return;

				y = GetNthSetBit(GetMaskOfTownActions(NULL, _local_company, this->town), y + this->vscroll->GetPosition() - 1);
				if (y >= 0) {
					this->sel_index = y;
					this->SetDirty();
				}
				/* FALL THROUGH, when double-clicking. */
				if (click_count == 1 || y < 0) break;
			}

			case WID_TA_EXECUTE:
				DoCommandP(this->town->xy, this->window_number, this->sel_index, CMD_DO_TOWN_ACTION | CMD_MSG(STR_ERROR_CAN_T_DO_THIS));
				break;
		}
	}

	virtual void OnHundredthTick()
	{
		this->SetDirty();
	}
};

static const WindowDesc _town_authority_desc(
	WDP_AUTO, 317, 222,
	WC_TOWN_AUTHORITY, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_town_authority_widgets, lengthof(_nested_town_authority_widgets)
);

static void ShowTownAuthorityWindow(uint town)
{
	AllocateWindowDescFront<TownAuthorityWindow>(&_town_authority_desc, town);
}


/* Town view window. */
struct TownViewWindow : Window {
private:
	Town *town; ///< Town displayed by the window.

public:
	static const int WID_TV_HEIGHT_NORMAL = 150;

	TownViewWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->CreateNestedTree(desc);

		this->town = Town::Get(window_number);
		if (this->town->larger_town) this->GetWidget<NWidgetCore>(WID_TV_CAPTION)->widget_data = STR_TOWN_VIEW_CITY_CAPTION;

		this->FinishInitNested(desc, window_number);

		this->flags|= WF_DISABLE_VP_SCROLL;
		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_TV_VIEWPORT);
		nvp->InitializeViewport(this, this->town->xy, ZOOM_LVL_NEWS);

		/* disable renaming town in network games if you are not the server */
		this->SetWidgetDisabledState(WID_TV_CHANGE_NAME, _networking && !_network_server);
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == WID_TV_CAPTION) SetDParam(0, this->town->index);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != WID_TV_INFO) return;

		uint y = r.top + WD_FRAMERECT_TOP;

		SetDParam(0, this->town->population);
		SetDParam(1, this->town->num_houses);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y, STR_TOWN_VIEW_POPULATION_HOUSES);

		SetDParam(0, this->town->supplied[CT_PASSENGERS].old_act);
		SetDParam(1, this->town->supplied[CT_PASSENGERS].old_max);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_PASSENGERS_LAST_MONTH_MAX);

		SetDParam(0, this->town->supplied[CT_MAIL].old_act);
		SetDParam(1, this->town->supplied[CT_MAIL].old_max);
		DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_MAIL_LAST_MONTH_MAX);

		bool first = true;
		for (int i = TE_BEGIN; i < TE_END; i++) {
			if (this->town->goal[i] == 0) continue;
			if (this->town->goal[i] == TOWN_GROWTH_WINTER && (TileHeight(this->town->xy) < LowestSnowLine() || this->town->population <= 90)) continue;
			if (this->town->goal[i] == TOWN_GROWTH_DESERT && (GetTropicZone(this->town->xy) != TROPICZONE_DESERT || this->town->population <= 60)) continue;

			if (first) {
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH);
				first = false;
			}

			bool rtl = _current_text_dir == TD_RTL;
			uint cargo_text_left = r.left + WD_FRAMERECT_LEFT + (rtl ? 0 : 20);
			uint cargo_text_right = r.right - WD_FRAMERECT_RIGHT - (rtl ? 20 : 0);

			const CargoSpec *cargo = FindFirstCargoWithTownEffect((TownEffect)i);
			assert(cargo != NULL);

			StringID string;

			if (this->town->goal[i] == TOWN_GROWTH_DESERT || this->town->goal[i] == TOWN_GROWTH_WINTER) {
				/* For 'original' gameplay, don't show the amount required (you need 1 or more ..) */
				string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_DELIVERED_GENERAL;
				if (this->town->received[i].old_act == 0) {
					string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_REQUIRED_GENERAL;

					if (this->town->goal[i] == TOWN_GROWTH_WINTER && TileHeight(this->town->xy) < GetSnowLine()) {
						string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_REQUIRED_WINTER;
					}
				}

				SetDParam(0, cargo->name);
			} else {
				string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_DELIVERED;
				if (this->town->received[i].old_act < this->town->goal[i]) {
					string = STR_TOWN_VIEW_CARGO_FOR_TOWNGROWTH_REQUIRED;
				}

				SetDParam(0, cargo->Index());
				SetDParam(1, this->town->received[i].old_act);
				SetDParam(2, cargo->Index());
				SetDParam(3, this->town->goal[i]);
			}
			DrawString(cargo_text_left, cargo_text_right, y += FONT_HEIGHT_NORMAL, string);
		}

		if (HasBit(this->town->flags, TOWN_IS_FUNDED)) {
			SetDParam(0, ((this->town->growth_rate & (~TOWN_GROW_RATE_CUSTOM)) * TOWN_GROWTH_TICKS + DAY_TICKS) / DAY_TICKS);
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, this->town->fund_buildings_months == 0 ? STR_TOWN_VIEW_TOWN_GROWS_EVERY : STR_TOWN_VIEW_TOWN_GROWS_EVERY_FUNDED);
		} else {
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_TOWN_GROW_STOPPED);
		}

		/* only show the town noise, if the noise option is activated. */
		if (_settings_game.economy.station_noise_level) {
			SetDParam(0, this->town->noise_reached);
			SetDParam(1, this->town->MaxTownNoise());
			DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, STR_TOWN_VIEW_NOISE_IN_TOWN);
		}

		if (this->town->text != NULL) {
			SetDParamStr(0, this->town->text);
			DrawStringMultiLine(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_LEFT, y += FONT_HEIGHT_NORMAL, UINT16_MAX, STR_JUST_RAW_STRING, TC_BLACK);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TV_CENTER_VIEW: // scroll to location
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(this->town->xy);
				} else {
					ScrollMainWindowToTile(this->town->xy);
				}
				break;

			case WID_TV_SHOW_AUTHORITY: // town authority
				ShowTownAuthorityWindow(this->window_number);
				break;

			case WID_TV_CHANGE_NAME: // rename
				SetDParam(0, this->window_number);
				ShowQueryString(STR_TOWN_NAME, STR_TOWN_VIEW_RENAME_TOWN_BUTTON, MAX_LENGTH_TOWN_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_TV_EXPAND: { // expand town - only available on Scenario editor
				/* Warn the user if towns are not allowed to build roads, but do this only once per OpenTTD run. */
				static bool _warn_town_no_roads = false;

				if (!_settings_game.economy.allow_town_roads && !_warn_town_no_roads) {
					ShowErrorMessage(STR_ERROR_TOWN_EXPAND_WARN_NO_ROADS, INVALID_STRING_ID, WL_WARNING);
					_warn_town_no_roads = true;
				}

				DoCommandP(0, this->window_number, 0, CMD_EXPAND_TOWN | CMD_MSG(STR_ERROR_CAN_T_EXPAND_TOWN));
				break;
			}

			case WID_TV_DELETE: // delete town - only available on Scenario editor
				DoCommandP(0, this->window_number, 0, CMD_DELETE_TOWN | CMD_MSG(STR_ERROR_TOWN_CAN_T_DELETE));
				break;
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_TV_INFO:
				size->height = GetDesiredInfoHeight(size->width);
				break;
		}
	}

	/**
	 * Gets the desired height for the information panel.
	 * @return the desired height in pixels.
	 */
	uint GetDesiredInfoHeight(int width) const
	{
		uint aimed_height = 3 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;

		bool first = true;
		for (int i = TE_BEGIN; i < TE_END; i++) {
			if (this->town->goal[i] == 0) continue;
			if (this->town->goal[i] == TOWN_GROWTH_WINTER && (TileHeight(this->town->xy) < LowestSnowLine() || this->town->population <= 90)) continue;
			if (this->town->goal[i] == TOWN_GROWTH_DESERT && (GetTropicZone(this->town->xy) != TROPICZONE_DESERT || this->town->population <= 60)) continue;

			if (first) {
				aimed_height += FONT_HEIGHT_NORMAL;
				first = false;
			}
			aimed_height += FONT_HEIGHT_NORMAL;
		}
		aimed_height += FONT_HEIGHT_NORMAL;

		if (_settings_game.economy.station_noise_level) aimed_height += FONT_HEIGHT_NORMAL;

		if (this->town->text != NULL) {
			SetDParamStr(0, this->town->text);
			aimed_height += GetStringHeight(STR_JUST_RAW_STRING, width);
		}

		return aimed_height;
	}

	void ResizeWindowAsNeeded()
	{
		const NWidgetBase *nwid_info = this->GetWidget<NWidgetBase>(WID_TV_INFO);
		uint aimed_height = GetDesiredInfoHeight(nwid_info->current_x);
		if (aimed_height > nwid_info->current_y || (aimed_height < nwid_info->current_y && nwid_info->current_y > nwid_info->smallest_y)) {
			this->ReInit();
		}
	}

	virtual void OnResize()
	{
		if (this->viewport != NULL) {
			NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_TV_VIEWPORT);
			nvp->UpdateViewportCoordinates(this);

			ScrollWindowToTile(this->town->xy, this, true); // Re-center viewport.
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		/* Called when setting station noise or required cargoes have changed, in order to resize the window */
		this->SetDirty(); // refresh display for current size. This will allow to avoid glitches when downgrading
		this->ResizeWindowAsNeeded();
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;

		DoCommandP(0, this->window_number, 0, CMD_RENAME_TOWN | CMD_MSG(STR_ERROR_CAN_T_RENAME_TOWN), NULL, str);
	}
};

static const NWidgetPart _nested_town_game_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TV_CAPTION), SetDataTip(STR_TOWN_VIEW_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(WWT_INSET, COLOUR_BROWN), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_TV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 0), SetResize(1, 1), SetPadding(1, 1, 1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TV_INFO), SetMinimalSize(260, 32), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CENTER_VIEW), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_SHOW_AUTHORITY), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_LOCAL_AUTHORITY_BUTTON, STR_TOWN_VIEW_LOCAL_AUTHORITY_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CHANGE_NAME), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_RENAME, STR_TOWN_VIEW_RENAME_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
	EndContainer(),
};

static const WindowDesc _town_game_view_desc(
	WDP_AUTO, 260, TownViewWindow::WID_TV_HEIGHT_NORMAL,
	WC_TOWN_VIEW, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_town_game_view_widgets, lengthof(_nested_town_game_view_widgets)
);

static const NWidgetPart _nested_town_editor_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_TV_CAPTION), SetDataTip(STR_TOWN_VIEW_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CHANGE_NAME), SetMinimalSize(76, 14), SetDataTip(STR_BUTTON_RENAME, STR_TOWN_VIEW_RENAME_TOOLTIP),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN),
		NWidget(WWT_INSET, COLOUR_BROWN), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_TV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 1), SetResize(1, 1), SetPadding(1, 1, 1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_BROWN, WID_TV_INFO), SetMinimalSize(260, 32), SetResize(1, 0), SetFill(1, 0), EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_CENTER_VIEW), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_BUTTON_LOCATION, STR_TOWN_VIEW_CENTER_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_EXPAND), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_EXPAND_BUTTON, STR_TOWN_VIEW_EXPAND_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TV_DELETE), SetMinimalSize(80, 12), SetFill(1, 1), SetResize(1, 0), SetDataTip(STR_TOWN_VIEW_DELETE_BUTTON, STR_TOWN_VIEW_DELETE_TOOLTIP),
		EndContainer(),
		NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
	EndContainer(),
};

static const WindowDesc _town_editor_view_desc(
	WDP_AUTO, 260, TownViewWindow::WID_TV_HEIGHT_NORMAL,
	WC_TOWN_VIEW, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_town_editor_view_widgets, lengthof(_nested_town_editor_view_widgets)
);

void ShowTownViewWindow(TownID town)
{
	if (_game_mode == GM_EDITOR) {
		AllocateWindowDescFront<TownViewWindow>(&_town_editor_view_desc, town);
	} else {
		AllocateWindowDescFront<TownViewWindow>(&_town_game_view_desc, town);
	}
}

static const NWidgetPart _nested_town_directory_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_TOWN_DIRECTORY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TD_SORT_NAME), SetMinimalSize(99, 12), SetDataTip(STR_SORT_BY_CAPTION_NAME, STR_TOOLTIP_SORT_ORDER), SetFill(1, 0),
				NWidget(WWT_PUSHTXTBTN, COLOUR_BROWN, WID_TD_SORT_POPULATION), SetMinimalSize(97, 12), SetDataTip(STR_SORT_BY_CAPTION_POPULATION, STR_TOOLTIP_SORT_ORDER), SetFill(1, 0),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_TD_LIST), SetMinimalSize(196, 0), SetDataTip(0x0, STR_TOWN_DIRECTORY_LIST_TOOLTIP),
							SetFill(1, 0), SetResize(0, 10), SetScrollbar(WID_TD_SCROLLBAR), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN),
				NWidget(WWT_TEXT, COLOUR_BROWN, WID_TD_WORLD_POPULATION), SetPadding(2, 0, 0, 2), SetMinimalSize(196, 12), SetFill(1, 0), SetDataTip(STR_TOWN_POPULATION, STR_NULL),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_TD_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
		EndContainer(),
	EndContainer(),
};

/** Town directory window class. */
struct TownDirectoryWindow : public Window {
private:
	/* Runtime saved values */
	static Listing last_sorting;
	static const Town *last_town;

	/* Constants for sorting towns */
	static GUITownList::SortFunction * const sorter_funcs[];

	GUITownList towns;

	Scrollbar *vscroll;

	void BuildSortTownList()
	{
		if (this->towns.NeedRebuild()) {
			this->towns.Clear();

			const Town *t;
			FOR_ALL_TOWNS(t) {
				*this->towns.Append() = t;
			}

			this->towns.Compact();
			this->towns.RebuildDone();
			this->vscroll->SetCount(this->towns.Length()); // Update scrollbar as well.
		}
		/* Always sort the towns. */
		this->last_town = NULL;
		this->towns.Sort();
	}

	/** Sort by town name */
	static int CDECL TownNameSorter(const Town * const *a, const Town * const *b)
	{
		static char buf_cache[64];
		const Town *ta = *a;
		const Town *tb = *b;
		char buf[64];

		SetDParam(0, ta->index);
		GetString(buf, STR_TOWN_NAME, lastof(buf));

		/* If 'b' is the same town as in the last round, use the cached value
		 * We do this to speed stuff up ('b' is called with the same value a lot of
		 * times after eachother) */
		if (tb != last_town) {
			last_town = tb;
			SetDParam(0, tb->index);
			GetString(buf_cache, STR_TOWN_NAME, lastof(buf_cache));
		}

		return strnatcmp(buf, buf_cache); // Sort by name (natural sorting).
	}

	/** Sort by population */
	static int CDECL TownPopulationSorter(const Town * const *a, const Town * const *b)
	{
		return (*a)->population - (*b)->population;
	}

public:
	TownDirectoryWindow(const WindowDesc *desc) : Window()
	{
		this->CreateNestedTree(desc);

		this->vscroll = this->GetScrollbar(WID_TD_SCROLLBAR);

		this->towns.SetListing(this->last_sorting);
		this->towns.SetSortFuncs(TownDirectoryWindow::sorter_funcs);
		this->towns.ForceRebuild();
		this->BuildSortTownList();

		this->FinishInitNested(desc, 0);
	}

	~TownDirectoryWindow()
	{
		this->last_sorting = this->towns.GetListing();
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == WID_TD_WORLD_POPULATION) SetDParam(0, GetWorldPopulation());
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_TD_SORT_NAME:
				if (this->towns.SortType() == 0) this->DrawSortButtonState(widget, this->towns.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_TD_SORT_POPULATION:
				if (this->towns.SortType() != 0) this->DrawSortButtonState(widget, this->towns.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_TD_LIST: {
				int n = 0;
				int y = r.top + WD_FRAMERECT_TOP;
				if (this->towns.Length() == 0) { // No towns available.
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right, y, STR_TOWN_DIRECTORY_NONE);
					break;
				}
				/* At least one town available. */
				for (uint i = this->vscroll->GetPosition(); i < this->towns.Length(); i++) {
					const Town *t = this->towns[i];

					assert(t->xy != INVALID_TILE);

					SetDParam(0, t->index);
					SetDParam(1, t->population);
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_TOWN_DIRECTORY_TOWN);

					y += this->resize.step_height;
					if (++n == this->vscroll->GetCapacity()) break; // max number of towns in 1 window
				}
				break;
			}
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_TD_SORT_NAME:
			case WID_TD_SORT_POPULATION: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + WD_SORTBUTTON_ARROW_WIDTH * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
			case WID_TD_LIST: {
				Dimension d = GetStringBoundingBox(STR_TOWN_DIRECTORY_NONE);
				for (uint i = 0; i < this->towns.Length(); i++) {
					const Town *t = this->towns[i];

					assert(t != NULL);

					SetDParam(0, t->index);
					SetDParam(1, 10000000); // 10^7
					d = maxdim(d, GetStringBoundingBox(STR_TOWN_DIRECTORY_TOWN));
				}
				resize->height = d.height;
				d.height *= 5;
				d.width += padding.width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				d.height += padding.height + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				*size = maxdim(*size, d);
				break;
			}
			case WID_TD_WORLD_POPULATION: {
				SetDParam(0, 1000000000); // 10^9
				Dimension d = GetStringBoundingBox(STR_TOWN_POPULATION);
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TD_SORT_NAME: // Sort by Name ascending/descending
				if (this->towns.SortType() == 0) {
					this->towns.ToggleSortOrder();
				} else {
					this->towns.SetSortType(0);
				}
				this->BuildSortTownList();
				this->SetDirty();
				break;

			case WID_TD_SORT_POPULATION: // Sort by Population ascending/descending
				if (this->towns.SortType() == 1) {
					this->towns.ToggleSortOrder();
				} else {
					this->towns.SetSortType(1);
				}
				this->BuildSortTownList();
				this->SetDirty();
				break;

			case WID_TD_LIST: { // Click on Town Matrix
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_TD_LIST, WD_FRAMERECT_TOP);
				if (id_v >= this->towns.Length()) return; // click out of town bounds

				const Town *t = this->towns[id_v];
				assert(t != NULL);
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(t->xy);
				} else {
					ScrollMainWindowToTile(t->xy);
				}
				break;
			}
		}
	}

	virtual void OnPaint()
	{
		if (this->towns.NeedRebuild()) this->BuildSortTownList();
		this->DrawWidgets();
	}

	virtual void OnHundredthTick()
	{
		this->BuildSortTownList();
		this->SetDirty();
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, WID_TD_LIST);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (data == 0) {
			/* This needs to be done in command-scope to enforce rebuilding before resorting invalid data */
			this->towns.ForceRebuild();
		} else {
			this->towns.ForceResort();
		}
	}
};

Listing TownDirectoryWindow::last_sorting = {false, 0};
const Town *TownDirectoryWindow::last_town = NULL;

/* Available town directory sorting functions */
GUITownList::SortFunction * const TownDirectoryWindow::sorter_funcs[] = {
	&TownNameSorter,
	&TownPopulationSorter,
};

static const WindowDesc _town_directory_desc(
	WDP_AUTO, 208, 202,
	WC_TOWN_DIRECTORY, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_town_directory_widgets, lengthof(_nested_town_directory_widgets)
);

void ShowTownDirectory()
{
	if (BringWindowToFrontById(WC_TOWN_DIRECTORY, 0)) return;
	new TownDirectoryWindow(&_town_directory_desc);
}

void CcFoundTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	SndPlayTileFx(SND_1F_SPLAT, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

void CcFoundRandomTown(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Succeeded()) ScrollMainWindowToTile(Town::Get(_new_town_id)->xy);
}

static const NWidgetPart _nested_found_town_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_FOUND_TOWN_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	/* Construct new town(s) buttons. */
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_NEW_TOWN), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_NEW_TOWN_BUTTON, STR_FOUND_TOWN_NEW_TOWN_TOOLTIP), SetPadding(0, 2, 1, 2),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_RANDOM_TOWN), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_RANDOM_TOWN_BUTTON, STR_FOUND_TOWN_RANDOM_TOWN_TOOLTIP), SetPadding(0, 2, 1, 2),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_MANY_RANDOM_TOWNS), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_MANY_RANDOM_TOWNS, STR_FOUND_TOWN_RANDOM_TOWNS_TOOLTIP), SetPadding(0, 2, 0, 2),
		/* Town name selection. */
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(156, 14), SetPadding(0, 2, 0, 2), SetDataTip(STR_FOUND_TOWN_NAME_TITLE, STR_NULL),
		NWidget(WWT_EDITBOX, COLOUR_WHITE, WID_TF_TOWN_NAME_EDITBOX), SetMinimalSize(156, 12), SetPadding(0, 2, 3, 2),
										SetDataTip(STR_FOUND_TOWN_NAME_EDITOR_TITLE, STR_FOUND_TOWN_NAME_EDITOR_HELP),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_TOWN_NAME_RANDOM), SetMinimalSize(78, 12), SetPadding(0, 2, 0, 2), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_NAME_RANDOM_BUTTON, STR_FOUND_TOWN_NAME_RANDOM_TOOLTIP),
		/* Town size selection. */
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_TITLE, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_SMALL), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_SMALL_BUTTON, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_MEDIUM), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_MEDIUM_BUTTON, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_LARGE), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_INITIAL_SIZE_LARGE_BUTTON, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_SIZE_RANDOM), SetMinimalSize(78, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_SIZE_RANDOM, STR_FOUND_TOWN_INITIAL_SIZE_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_CITY), SetPadding(0, 2, 0, 2), SetMinimalSize(156, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_CITY, STR_FOUND_TOWN_CITY_TOOLTIP), SetFill(1, 0),
		/* Town roads selection. */
		NWidget(NWID_HORIZONTAL), SetPIP(2, 0, 2),
			NWidget(NWID_SPACER), SetFill(1, 0),
			NWidget(WWT_LABEL, COLOUR_DARK_GREEN), SetMinimalSize(148, 14), SetDataTip(STR_FOUND_TOWN_ROAD_LAYOUT, STR_NULL),
			NWidget(NWID_SPACER), SetFill(1, 0),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_ORIGINAL), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_ORIGINAL, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_BETTER), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_BETTER_ROADS, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(2, 0, 2),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_GRID2), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_2X2_GRID, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_GRID3), SetMinimalSize(78, 12), SetFill(1, 0), SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_3X3_GRID, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 1),
		NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_TF_LAYOUT_RANDOM), SetPadding(0, 2, 0, 2), SetMinimalSize(0, 12), SetFill(1, 0),
										SetDataTip(STR_FOUND_TOWN_SELECT_LAYOUT_RANDOM, STR_FOUND_TOWN_SELECT_TOWN_ROAD_LAYOUT), SetFill(1, 0),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
	EndContainer(),
};

/** Found a town window class. */
struct FoundTownWindow : QueryStringBaseWindow {
private:
	TownSize town_size;     ///< Selected town size
	TownLayout town_layout; ///< Selected town layout
	bool city;              ///< Are we building a city?
	bool townnamevalid;     ///< Is generated town name valid?
	uint32 townnameparts;   ///< Generated town name
	TownNameParams params;  ///< Town name parameters

public:
	FoundTownWindow(const WindowDesc *desc, WindowNumber window_number) :
			QueryStringBaseWindow(MAX_LENGTH_TOWN_NAME_CHARS * MAX_CHAR_LENGTH, MAX_LENGTH_TOWN_NAME_CHARS),
			town_size(TSZ_MEDIUM),
			town_layout(_settings_game.economy.town_layout),
			params(_settings_game.game_creation.town_name)
	{
		this->InitNested(desc, window_number);
		InitializeTextBuffer(&this->text, this->edit_str_buf, this->edit_str_size, this->max_chars);
		this->RandomTownName();
		this->UpdateButtons(true);
	}

	void RandomTownName()
	{
		this->townnamevalid = GenerateTownName(&this->townnameparts);

		if (!this->townnamevalid) {
			this->edit_str_buf[0] = '\0';
		} else {
			GetTownName(this->edit_str_buf, &this->params, this->townnameparts, &this->edit_str_buf[this->edit_str_size - 1]);
		}
		UpdateTextBufferSize(&this->text);
		UpdateOSKOriginalText(this, WID_TF_TOWN_NAME_EDITBOX);

		this->SetWidgetDirty(WID_TF_TOWN_NAME_EDITBOX);
	}

	void UpdateButtons(bool check_availability)
	{
		if (check_availability && _game_mode != GM_EDITOR) {
			this->SetWidgetsDisabledState(true, WID_TF_RANDOM_TOWN, WID_TF_MANY_RANDOM_TOWNS, WID_TF_SIZE_LARGE, WIDGET_LIST_END);
			this->SetWidgetsDisabledState(_settings_game.economy.found_town != TF_CUSTOM_LAYOUT,
					WID_TF_LAYOUT_ORIGINAL, WID_TF_LAYOUT_BETTER, WID_TF_LAYOUT_GRID2, WID_TF_LAYOUT_GRID3, WID_TF_LAYOUT_RANDOM, WIDGET_LIST_END);
			if (_settings_game.economy.found_town != TF_CUSTOM_LAYOUT) town_layout = _settings_game.economy.town_layout;
		}

		for (int i = WID_TF_SIZE_SMALL; i <= WID_TF_SIZE_RANDOM; i++) {
			this->SetWidgetLoweredState(i, i == WID_TF_SIZE_SMALL + this->town_size);
		}

		this->SetWidgetLoweredState(WID_TF_CITY, this->city);

		for (int i = WID_TF_LAYOUT_ORIGINAL; i <= WID_TF_LAYOUT_RANDOM; i++) {
			this->SetWidgetLoweredState(i, i == WID_TF_LAYOUT_ORIGINAL + this->town_layout);
		}

		this->SetDirty();
	}

	void ExecuteFoundTownCommand(TileIndex tile, bool random, StringID errstr, CommandCallback cc)
	{
		const char *name = NULL;

		if (!this->townnamevalid) {
			name = this->edit_str_buf;
		} else {
			/* If user changed the name, send it */
			char buf[MAX_LENGTH_TOWN_NAME_CHARS * MAX_CHAR_LENGTH];
			GetTownName(buf, &this->params, this->townnameparts, lastof(buf));
			if (strcmp(buf, this->edit_str_buf) != 0) name = this->edit_str_buf;
		}

		bool success = DoCommandP(tile, this->town_size | this->city << 2 | this->town_layout << 3 | random << 6,
				townnameparts, CMD_FOUND_TOWN | CMD_MSG(errstr), cc, name);

		if (success) this->RandomTownName();
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();
		if (!this->IsShaded()) this->DrawEditBox(WID_TF_TOWN_NAME_EDITBOX);
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_TF_NEW_TOWN:
				HandlePlacePushButton(this, WID_TF_NEW_TOWN, SPR_CURSOR_TOWN, HT_RECT);
				break;

			case WID_TF_RANDOM_TOWN:
				this->HandleButtonClick(WID_TF_RANDOM_TOWN);
				this->ExecuteFoundTownCommand(0, true, STR_ERROR_CAN_T_GENERATE_TOWN, CcFoundRandomTown);
				break;

			case WID_TF_TOWN_NAME_RANDOM:
				this->RandomTownName();
				this->SetFocusedWidget(WID_TF_TOWN_NAME_EDITBOX);
				break;

			case WID_TF_MANY_RANDOM_TOWNS:
				this->HandleButtonClick(WID_TF_MANY_RANDOM_TOWNS);

				_generating_world = true;
				UpdateNearestTownForRoadTiles(true);
				if (!GenerateTowns(this->town_layout)) {
					ShowErrorMessage(STR_ERROR_CAN_T_GENERATE_TOWN, STR_ERROR_NO_SPACE_FOR_TOWN, WL_INFO);
				}
				UpdateNearestTownForRoadTiles(false);
				_generating_world = false;
				break;

			case WID_TF_SIZE_SMALL: case WID_TF_SIZE_MEDIUM: case WID_TF_SIZE_LARGE: case WID_TF_SIZE_RANDOM:
				this->town_size = (TownSize)(widget - WID_TF_SIZE_SMALL);
				this->UpdateButtons(false);
				break;

			case WID_TF_CITY:
				this->city ^= true;
				this->SetWidgetLoweredState(WID_TF_CITY, this->city);
				this->SetDirty();
				break;

			case WID_TF_LAYOUT_ORIGINAL: case WID_TF_LAYOUT_BETTER: case WID_TF_LAYOUT_GRID2:
			case WID_TF_LAYOUT_GRID3: case WID_TF_LAYOUT_RANDOM:
				this->town_layout = (TownLayout)(widget - WID_TF_LAYOUT_ORIGINAL);
				this->UpdateButtons(false);
				break;
		}
	}

	virtual void OnTimeout()
	{
		this->RaiseWidget(WID_TF_RANDOM_TOWN);
		this->RaiseWidget(WID_TF_MANY_RANDOM_TOWNS);
		this->SetDirty();
	}

	virtual void OnMouseLoop()
	{
		this->HandleEditBox(WID_TF_TOWN_NAME_EDITBOX);
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		EventState state = ES_NOT_HANDLED;
		if (this->HandleEditBoxKey(WID_TF_TOWN_NAME_EDITBOX, key, keycode, state) == HEBR_CANCEL) {
			this->UnfocusFocusedWidget();
		}
		return state;
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		this->ExecuteFoundTownCommand(tile, false, STR_ERROR_CAN_T_FOUND_TOWN_HERE, CcFoundTown);
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
		this->UpdateButtons(false);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		this->UpdateButtons(true);
	}
};

static const WindowDesc _found_town_desc(
	WDP_AUTO, 160, 162,
	WC_FOUND_TOWN, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_found_town_widgets, lengthof(_nested_found_town_widgets)
);

void ShowFoundTownWindow()
{
	if (_game_mode != GM_EDITOR && !Company::IsValidID(_local_company)) return;
	AllocateWindowDescFront<FoundTownWindow>(&_found_town_desc, 0);
}
