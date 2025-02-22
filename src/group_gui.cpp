/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file group_gui.cpp GUI for the group window. */

#include "stdafx.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "vehicle_gui.h"
#include "vehicle_base.h"
#include "group.h"
#include "string_func.h"
#include "strings_func.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "autoreplace_gui.h"
#include "company_func.h"
#include "widgets/dropdown_func.h"
#include "tilehighlight_func.h"
#include "vehicle_gui_base.h"
#include "core/geometry_func.hpp"
#include "company_base.h"

#include "widgets/group_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

typedef GUIList<const Group*> GUIGroupList;

static const NWidgetPart _nested_group_widgets[] = {
	NWidget(NWID_HORIZONTAL), // Window header
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_GL_CAPTION),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		/* left part */
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalTextLines(1, WD_DROPDOWNTEXT_TOP + WD_DROPDOWNTEXT_BOTTOM), SetFill(1, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_GL_ALL_VEHICLES), SetFill(1, 0), EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_GL_DEFAULT_VEHICLES), SetFill(1, 0), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_GL_LIST_GROUP), SetDataTip(0x701, STR_GROUPS_CLICK_ON_GROUP_FOR_TOOLTIP),
						SetFill(1, 0), SetResize(0, 1), SetScrollbar(WID_GL_LIST_GROUP_SCROLLBAR),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_GL_LIST_GROUP_SCROLLBAR),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_GL_CREATE_GROUP), SetMinimalSize(24, 25), SetFill(0, 1),
						SetDataTip(SPR_GROUP_CREATE_TRAIN, STR_GROUP_CREATE_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_GL_DELETE_GROUP), SetMinimalSize(24, 25), SetFill(0, 1),
						SetDataTip(SPR_GROUP_DELETE_TRAIN, STR_GROUP_DELETE_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_GL_RENAME_GROUP), SetMinimalSize(24, 25), SetFill(0, 1),
						SetDataTip(SPR_GROUP_RENAME_TRAIN, STR_GROUP_RENAME_TOOLTIP),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(1, 1), EndContainer(),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_GL_REPLACE_PROTECTION), SetMinimalSize(24, 25), SetFill(0, 1),
						SetDataTip(SPR_GROUP_REPLACE_OFF_TRAIN, STR_GROUP_REPLACE_PROTECTION_TOOLTIP),
				NWidget(WWT_PANEL, COLOUR_GREY), SetFill(0, 1), EndContainer(),
			EndContainer(),
		EndContainer(),
		/* right part */
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GL_SORT_BY_ORDER), SetMinimalSize(81, 12), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GL_SORT_BY_DROPDOWN), SetMinimalSize(167, 12), SetDataTip(0x0, STR_TOOLTIP_SORT_CRITERIA),
				NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(12, 12), SetResize(1, 0), EndContainer(),
			EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_MATRIX, COLOUR_GREY, WID_GL_LIST_VEHICLE), SetMinimalSize(248, 0), SetDataTip(0x701, STR_NULL), SetResize(1, 1), SetFill(1, 0), SetScrollbar(WID_GL_LIST_VEHICLE_SCROLLBAR),
				NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_GL_LIST_VEHICLE_SCROLLBAR),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(1, 0), SetFill(1, 1), SetResize(1, 0), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_GL_AVAILABLE_VEHICLES), SetMinimalSize(106, 12), SetFill(0, 1),
						SetDataTip(STR_BLACK_STRING, STR_VEHICLE_LIST_AVAILABLE_ENGINES_TOOLTIP),
				NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_GL_MANAGE_VEHICLES_DROPDOWN), SetMinimalSize(118, 12), SetFill(0, 1),
						SetDataTip(STR_VEHICLE_LIST_MANAGE_LIST, STR_VEHICLE_LIST_MANAGE_LIST_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_GL_STOP_ALL), SetMinimalSize(12, 12), SetFill(0, 1),
						SetDataTip(SPR_FLAG_VEH_STOPPED, STR_VEHICLE_LIST_MASS_STOP_LIST_TOOLTIP),
				NWidget(WWT_PUSHIMGBTN, COLOUR_GREY, WID_GL_START_ALL), SetMinimalSize(12, 12), SetFill(0, 1),
						SetDataTip(SPR_FLAG_VEH_RUNNING, STR_VEHICLE_LIST_MASS_START_LIST_TOOLTIP),
				NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(0, 12), SetFill(1, 1), SetResize(1, 0), EndContainer(),
				NWidget(WWT_RESIZEBOX, COLOUR_GREY),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

class VehicleGroupWindow : public BaseVehicleListWindow {
private:
	/* Columns in the group list */
	enum ListColumns {
		VGC_NAME,          ///< Group name.
		VGC_PROTECT,       ///< Autoreplace protect icon.
		VGC_AUTOREPLACE,   ///< Autoreplace active icon.
		VGC_PROFIT,        ///< Profit icon.
		VGC_NUMBER,        ///< Number of vehicles in the group.

		VGC_END
	};

	VehicleID vehicle_sel; ///< Selected vehicle
	GroupID group_rename;  ///< Group being renamed, INVALID_GROUP if none
	GUIGroupList groups;   ///< List of groups
	uint tiny_step_height; ///< Step height for the group list
	Scrollbar *group_sb;

	Dimension column_size[VGC_END]; ///< Size of the columns in the group list.

	/**
	 * (Re)Build the group list.
	 *
	 * @param owner The owner of the window
	 */
	void BuildGroupList(Owner owner)
	{
		if (!this->groups.NeedRebuild()) return;

		this->groups.Clear();

		const Group *g;
		FOR_ALL_GROUPS(g) {
			if (g->owner == owner && g->vehicle_type == this->vli.vtype) {
				*this->groups.Append() = g;
			}
		}

		this->groups.Compact();
		this->groups.RebuildDone();
	}

	/** Sort the groups by their name */
	static int CDECL GroupNameSorter(const Group * const *a, const Group * const *b)
	{
		static const Group *last_group[2] = { NULL, NULL };
		static char         last_name[2][64] = { "", "" };

		if (*a != last_group[0]) {
			last_group[0] = *a;
			SetDParam(0, (*a)->index);
			GetString(last_name[0], STR_GROUP_NAME, lastof(last_name[0]));
		}

		if (*b != last_group[1]) {
			last_group[1] = *b;
			SetDParam(0, (*b)->index);
			GetString(last_name[1], STR_GROUP_NAME, lastof(last_name[1]));
		}

		int r = strnatcmp(last_name[0], last_name[1]); // Sort by name (natural sorting).
		if (r == 0) return (*a)->index - (*b)->index;
		return r;
	}

	/**
	 * Compute tiny_step_height and column_size
	 * @return Total width required for the group list.
	 */
	uint ComputeGroupInfoSize()
	{
		this->column_size[VGC_NAME] = maxdim(GetStringBoundingBox(STR_GROUP_DEFAULT_TRAINS + this->vli.vtype), GetStringBoundingBox(STR_GROUP_ALL_TRAINS + this->vli.vtype));
		this->column_size[VGC_NAME].width = max(170u, this->column_size[VGC_NAME].width);
		this->tiny_step_height = this->column_size[VGC_NAME].height;

		this->column_size[VGC_PROTECT] = GetSpriteSize(SPR_GROUP_REPLACE_PROTECT);
		this->tiny_step_height = max(this->tiny_step_height, this->column_size[VGC_PROTECT].height);

		this->column_size[VGC_AUTOREPLACE] = GetSpriteSize(SPR_GROUP_REPLACE_ACTIVE);
		this->tiny_step_height = max(this->tiny_step_height, this->column_size[VGC_AUTOREPLACE].height);

		this->column_size[VGC_PROFIT].width = 0;
		this->column_size[VGC_PROFIT].height = 0;
		static const SpriteID profit_sprites[] = {SPR_PROFIT_NA, SPR_PROFIT_NEGATIVE, SPR_PROFIT_SOME, SPR_PROFIT_LOT};
		for (uint i = 0; i < lengthof(profit_sprites); i++) {
			Dimension d = GetSpriteSize(profit_sprites[i]);
			this->column_size[VGC_PROFIT] = maxdim(this->column_size[VGC_PROFIT], d);
		}
		this->tiny_step_height = max(this->tiny_step_height, this->column_size[VGC_PROFIT].height);

		SetDParam(0, GroupStatistics::Get(this->vli.company, ALL_GROUP, this->vli.vtype).num_vehicle > 900 ? 9999 : 999);
		this->column_size[VGC_NUMBER] = GetStringBoundingBox(STR_TINY_COMMA);
		this->tiny_step_height = max(this->tiny_step_height, this->column_size[VGC_NUMBER].height);

		this->tiny_step_height += WD_MATRIX_TOP;

		return WD_FRAMERECT_LEFT + 8 +
			this->column_size[VGC_NAME].width + 8 +
			this->column_size[VGC_PROTECT].width + 2 +
			this->column_size[VGC_AUTOREPLACE].width + 2 +
			this->column_size[VGC_PROFIT].width + 2 +
			this->column_size[VGC_NUMBER].width + 2 +
			WD_FRAMERECT_RIGHT;
	}

	/**
	 * Draw a row in the group list.
	 * @param y Top of the row.
	 * @param left Left of the row.
	 * @param right Right of the row.
	 * @param g_id Group to list.
	 * @param protection Whether autoreplace protection is set.
	 */
	void DrawGroupInfo(int y, int left, int right, GroupID g_id, bool protection = false) const
	{
		/* draw the selected group in white, else we draw it in black */
		TextColour colour = g_id == this->vli.index ? TC_WHITE : TC_BLACK;
		const GroupStatistics &stats = GroupStatistics::Get(this->vli.company, g_id, this->vli.vtype);
		bool rtl = _current_text_dir == TD_RTL;

		/* draw group name */
		StringID str;
		if (IsAllGroupID(g_id)) {
			str = STR_GROUP_ALL_TRAINS + this->vli.vtype;
		} else if (IsDefaultGroupID(g_id)) {
			str = STR_GROUP_DEFAULT_TRAINS + this->vli.vtype;
		} else {
			SetDParam(0, g_id);
			str = STR_GROUP_NAME;
		}
		int x = rtl ? right - WD_FRAMERECT_RIGHT - 8 - this->column_size[VGC_NAME].width + 1 : left + WD_FRAMERECT_LEFT + 8;
		DrawString(x, x + this->column_size[VGC_NAME].width - 1, y + (this->tiny_step_height - this->column_size[VGC_NAME].height) / 2, str, colour);

		/* draw autoreplace protection */
		x = rtl ? x - 8 - this->column_size[VGC_PROTECT].width : x + 8 + this->column_size[VGC_NAME].width;
		if (protection) DrawSprite(SPR_GROUP_REPLACE_PROTECT, PAL_NONE, x, y + (this->tiny_step_height - this->column_size[VGC_PROTECT].height) / 2);

		/* draw autoreplace status */
		x = rtl ? x - 2 - this->column_size[VGC_AUTOREPLACE].width : x + 2 + this->column_size[VGC_PROTECT].width;
		if (stats.autoreplace_defined) DrawSprite(SPR_GROUP_REPLACE_ACTIVE, stats.autoreplace_finished ? PALETTE_CRASH : PAL_NONE, x, y + (this->tiny_step_height - this->column_size[VGC_AUTOREPLACE].height) / 2);

		/* draw the profit icon */
		x = rtl ? x - 2 - this->column_size[VGC_PROFIT].width : x + 2 + this->column_size[VGC_AUTOREPLACE].width;
		SpriteID spr;
		if (stats.num_profit_vehicle == 0) {
			spr = SPR_PROFIT_NA;
		} else if (stats.profit_last_year < 0) {
			spr = SPR_PROFIT_NEGATIVE;
		} else if (stats.profit_last_year < 10000 * stats.num_profit_vehicle) { // TODO magic number
			spr = SPR_PROFIT_SOME;
		} else {
			spr = SPR_PROFIT_LOT;
		}
		DrawSprite(spr, PAL_NONE, x, y + (this->tiny_step_height - this->column_size[VGC_PROFIT].height) / 2);

		/* draw the number of vehicles of the group */
		x = rtl ? x - 2 - this->column_size[VGC_NUMBER].width : x + 2 + this->column_size[VGC_PROFIT].width;
		SetDParam(0, stats.num_vehicle);
		DrawString(x, x + this->column_size[VGC_NUMBER].width - 1, y + (this->tiny_step_height - this->column_size[VGC_NUMBER].height) / 2, STR_TINY_COMMA, colour, SA_RIGHT | SA_FORCE);
	}

public:
	VehicleGroupWindow(const WindowDesc *desc, WindowNumber window_number) : BaseVehicleListWindow(window_number)
	{
		this->CreateNestedTree(desc);

		this->vscroll = this->GetScrollbar(WID_GL_LIST_VEHICLE_SCROLLBAR);
		this->group_sb = this->GetScrollbar(WID_GL_LIST_GROUP_SCROLLBAR);

		switch (this->vli.vtype) {
			default: NOT_REACHED();
			case VEH_TRAIN:    this->sorting = &_sorting.train;    break;
			case VEH_ROAD:     this->sorting = &_sorting.roadveh;  break;
			case VEH_SHIP:     this->sorting = &_sorting.ship;     break;
			case VEH_AIRCRAFT: this->sorting = &_sorting.aircraft; break;
		}

		this->vli.index = ALL_GROUP;
		this->vehicle_sel = INVALID_VEHICLE;
		this->group_rename = INVALID_GROUP;

		this->vehicles.SetListing(*this->sorting);
		this->vehicles.ForceRebuild();
		this->vehicles.NeedResort();

		this->BuildVehicleList();
		this->SortVehicleList();

		this->groups.ForceRebuild();
		this->groups.NeedResort();
		this->BuildGroupList(vli.company);
		this->groups.Sort(&GroupNameSorter);

		this->GetWidget<NWidgetCore>(WID_GL_CAPTION)->widget_data = STR_VEHICLE_LIST_TRAIN_CAPTION + this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_GL_LIST_VEHICLE)->tool_tip = STR_VEHICLE_LIST_TRAIN_LIST_TOOLTIP + this->vli.vtype;

		this->GetWidget<NWidgetCore>(WID_GL_CREATE_GROUP)->widget_data += this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_GL_RENAME_GROUP)->widget_data += this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_GL_DELETE_GROUP)->widget_data += this->vli.vtype;
		this->GetWidget<NWidgetCore>(WID_GL_REPLACE_PROTECTION)->widget_data += this->vli.vtype;

		this->FinishInitNested(desc, window_number);
		this->owner = vli.company;
	}

	~VehicleGroupWindow()
	{
		*this->sorting = this->vehicles.GetListing();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_GL_LIST_GROUP: {
				size->width = this->ComputeGroupInfoSize();
				resize->height = this->tiny_step_height;

				/* Minimum height is the height of the list widget minus all and default vehicles... */
				size->height =  4 * GetVehicleListHeight(this->vli.vtype, this->tiny_step_height) - 2 * this->tiny_step_height;

				/* ... minus the buttons at the bottom ... */
				uint max_icon_height = 25;
				max_icon_height = max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_GL_CREATE_GROUP)->widget_data).height);
				max_icon_height = max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_GL_RENAME_GROUP)->widget_data).height);
				max_icon_height = max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_GL_DELETE_GROUP)->widget_data).height);
				max_icon_height = max(max_icon_height, GetSpriteSize(this->GetWidget<NWidgetCore>(WID_GL_REPLACE_PROTECTION)->widget_data).height);

				/* ... plus the statusbar below the vehicle list */
				if (max_icon_height > FONT_HEIGHT_NORMAL) max_icon_height -= FONT_HEIGHT_NORMAL;

				/* The size must be a multiple of tiny_step_height for the resizing to work */
				size->height -= this->tiny_step_height * CeilDiv(max_icon_height, this->tiny_step_height);
				break;
			}

			case WID_GL_ALL_VEHICLES:
			case WID_GL_DEFAULT_VEHICLES:
				size->width = this->ComputeGroupInfoSize();
				size->height = this->tiny_step_height;
				break;

			case WID_GL_SORT_BY_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + WD_SORTBUTTON_ARROW_WIDTH * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_GL_LIST_VEHICLE:
				this->ComputeGroupInfoSize();
				resize->height = GetVehicleListHeight(this->vli.vtype, this->tiny_step_height);
				size->height = 4 * resize->height;
				break;

			case WID_GL_MANAGE_VEHICLES_DROPDOWN: {
				Dimension d = this->GetActionDropdownSize(true, true);
				d.height += padding.height;
				d.width  += padding.width;
				*size = maxdim(*size, d);
				break;
			}
		}
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
			this->vehicles.ForceRebuild();
			this->groups.ForceRebuild();
		} else {
			this->vehicles.ForceResort();
			this->groups.ForceResort();
		}

		/* Process ID-invalidation in command-scope as well */
		if (this->group_rename != INVALID_GROUP && !Group::IsValidID(this->group_rename)) {
			DeleteWindowByClass(WC_QUERY_STRING);
			this->group_rename = INVALID_GROUP;
		}

		if (!(IsAllGroupID(this->vli.index) || IsDefaultGroupID(this->vli.index) || Group::IsValidID(this->vli.index))) {
			this->vli.index = ALL_GROUP;
			HideDropDownMenu(this);
		}
		this->SetDirty();
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_GL_AVAILABLE_VEHICLES:
				SetDParam(0, STR_VEHICLE_LIST_AVAILABLE_TRAINS + this->vli.vtype);
				break;

			case WID_GL_CAPTION:
				/* If selected_group == DEFAULT_GROUP || ALL_GROUP, draw the standard caption
				 * We list all vehicles or ungrouped vehicles */
				if (IsDefaultGroupID(this->vli.index) || IsAllGroupID(this->vli.index)) {
					SetDParam(0, STR_COMPANY_NAME);
					SetDParam(1, this->vli.company);
					SetDParam(2, this->vehicles.Length());
					SetDParam(3, this->vehicles.Length());
				} else {
					const Group *g = Group::Get(this->vli.index);

					SetDParam(0, STR_GROUP_NAME);
					SetDParam(1, g->index);
					SetDParam(2, g->statistics.num_vehicle);
					SetDParam(3, g->statistics.num_vehicle);
				}
				break;
		}
	}

	virtual void OnPaint()
	{
		/* If we select the all vehicles, this->list will contain all vehicles of the owner
		 * else this->list will contain all vehicles which belong to the selected group */
		this->BuildVehicleList();
		this->SortVehicleList();

		this->BuildGroupList(this->owner);
		this->groups.Sort(&GroupNameSorter);

		this->group_sb->SetCount(this->groups.Length());
		this->vscroll->SetCount(this->vehicles.Length());

		/* The drop down menu is out, *but* it may not be used, retract it. */
		if (this->vehicles.Length() == 0 && this->IsWidgetLowered(WID_GL_MANAGE_VEHICLES_DROPDOWN)) {
			this->RaiseWidget(WID_GL_MANAGE_VEHICLES_DROPDOWN);
			HideDropDownMenu(this);
		}

		/* Disable all lists management button when the list is empty */
		this->SetWidgetsDisabledState(this->vehicles.Length() == 0 || _local_company != this->vli.company,
				WID_GL_STOP_ALL,
				WID_GL_START_ALL,
				WID_GL_MANAGE_VEHICLES_DROPDOWN,
				WIDGET_LIST_END);

		/* Disable the group specific function when we select the default group or all vehicles */
		this->SetWidgetsDisabledState(IsDefaultGroupID(this->vli.index) || IsAllGroupID(this->vli.index) || _local_company != this->vli.company,
				WID_GL_DELETE_GROUP,
				WID_GL_RENAME_GROUP,
				WID_GL_REPLACE_PROTECTION,
				WIDGET_LIST_END);

		/* Disable remaining buttons for non-local companies
		 * Needed while changing _local_company, eg. by cheats
		 * All procedures (eg. move vehicle to another group)
		 *  verify, whether you are the owner of the vehicle,
		 *  so it doesn't have to be disabled
		 */
		this->SetWidgetsDisabledState(_local_company != this->vli.company,
				WID_GL_CREATE_GROUP,
				WID_GL_AVAILABLE_VEHICLES,
				WIDGET_LIST_END);

		/* If not a default group and the group has replace protection, show an enabled replace sprite. */
		uint16 protect_sprite = SPR_GROUP_REPLACE_OFF_TRAIN;
		if (!IsDefaultGroupID(this->vli.index) && !IsAllGroupID(this->vli.index) && Group::Get(this->vli.index)->replace_protection) protect_sprite = SPR_GROUP_REPLACE_ON_TRAIN;
		this->GetWidget<NWidgetCore>(WID_GL_REPLACE_PROTECTION)->widget_data = protect_sprite + this->vli.vtype;

		/* Set text of sort by dropdown */
		this->GetWidget<NWidgetCore>(WID_GL_SORT_BY_DROPDOWN)->widget_data = this->vehicle_sorter_names[this->vehicles.SortType()];

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_GL_ALL_VEHICLES:
				DrawGroupInfo(r.top + WD_FRAMERECT_TOP, r.left, r.right, ALL_GROUP);
				break;

			case WID_GL_DEFAULT_VEHICLES:
				DrawGroupInfo(r.top + WD_FRAMERECT_TOP, r.left, r.right, DEFAULT_GROUP);
				break;

			case WID_GL_LIST_GROUP: {
				int y1 = r.top + WD_FRAMERECT_TOP;
				int max = min(this->group_sb->GetPosition() + this->group_sb->GetCapacity(), this->groups.Length());
				for (int i = this->group_sb->GetPosition(); i < max; ++i) {
					const Group *g = this->groups[i];

					assert(g->owner == this->owner);

					DrawGroupInfo(y1, r.left, r.right, g->index, g->replace_protection);

					y1 += this->tiny_step_height;
				}
				break;
			}

			case WID_GL_SORT_BY_ORDER:
				this->DrawSortButtonState(WID_GL_SORT_BY_ORDER, this->vehicles.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_GL_LIST_VEHICLE:
				this->DrawVehicleListItems(this->vehicle_sel, this->resize.step_height, r);
				break;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_GL_SORT_BY_ORDER: // Flip sorting method ascending/descending
				this->vehicles.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_GL_SORT_BY_DROPDOWN: // Select sorting criteria dropdown menu
				ShowDropDownMenu(this, this->vehicle_sorter_names, this->vehicles.SortType(),  WID_GL_SORT_BY_DROPDOWN, 0, (this->vli.vtype == VEH_TRAIN || this->vli.vtype == VEH_ROAD) ? 0 : (1 << 10));
				return;

			case WID_GL_ALL_VEHICLES: // All vehicles button
				if (!IsAllGroupID(this->vli.index)) {
					this->vli.index = ALL_GROUP;
					this->vehicles.ForceRebuild();
					this->SetDirty();
				}
				break;

			case WID_GL_DEFAULT_VEHICLES: // Ungrouped vehicles button
				if (!IsDefaultGroupID(this->vli.index)) {
					this->vli.index = DEFAULT_GROUP;
					this->vehicles.ForceRebuild();
					this->SetDirty();
				}
				break;

			case WID_GL_LIST_GROUP: { // Matrix Group
				uint id_g = this->group_sb->GetScrolledRowFromWidget(pt.y, this, WID_GL_LIST_GROUP, 0, this->tiny_step_height);
				if (id_g >= this->groups.Length()) return;

				this->vli.index = this->groups[id_g]->index;

				this->vehicles.ForceRebuild();
				this->SetDirty();
				break;
			}

			case WID_GL_LIST_VEHICLE: { // Matrix Vehicle
				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_GL_LIST_VEHICLE);
				if (id_v >= this->vehicles.Length()) return; // click out of list bound

				const Vehicle *v = this->vehicles[id_v];
				if (VehicleClicked(v)) break;

				this->vehicle_sel = v->index;

				int image = v->GetImage(_current_text_dir == TD_RTL ? DIR_E : DIR_W, EIT_IN_LIST);
				SetObjectToPlaceWnd(image, GetVehiclePalette(v), HT_DRAG, this);
				_cursor.vehchain = true;

				this->SetDirty();
				break;
			}

			case WID_GL_CREATE_GROUP: { // Create a new group
				DoCommandP(0, this->vli.vtype, 0, CMD_CREATE_GROUP | CMD_MSG(STR_ERROR_GROUP_CAN_T_CREATE), CcCreateGroup);
				break;
			}

			case WID_GL_DELETE_GROUP: { // Delete the selected group
				GroupID group = this->vli.index;
				this->vli.index = ALL_GROUP;

				DoCommandP(0, group, 0, CMD_DELETE_GROUP | CMD_MSG(STR_ERROR_GROUP_CAN_T_DELETE));
				break;
			}

			case WID_GL_RENAME_GROUP: // Rename the selected roup
				this->ShowRenameGroupWindow(this->vli.index, false);
				break;

			case WID_GL_AVAILABLE_VEHICLES:
				ShowBuildVehicleWindow(INVALID_TILE, this->vli.vtype);
				break;

			case WID_GL_MANAGE_VEHICLES_DROPDOWN: {
				DropDownList *list = this->BuildActionDropdownList(true, Group::IsValidID(this->vli.index));
				ShowDropDownList(this, list, 0, WID_GL_MANAGE_VEHICLES_DROPDOWN);
				break;
			}

			case WID_GL_START_ALL:
			case WID_GL_STOP_ALL: { // Start/stop all vehicles of the list
				DoCommandP(0, (1 << 1) | (widget == WID_GL_START_ALL ? (1 << 0) : 0), this->vli.Pack(), CMD_MASS_START_STOP);
				break;
			}

			case WID_GL_REPLACE_PROTECTION: {
				const Group *g = Group::GetIfValid(this->vli.index);
				if (g != NULL) {
					DoCommandP(0, this->vli.index, !g->replace_protection, CMD_SET_GROUP_REPLACE_PROTECTION);
				}
				break;
			}
		}
	}

	virtual void OnDragDrop(Point pt, int widget)
	{
		switch (widget) {
			case WID_GL_ALL_VEHICLES: // All vehicles
			case WID_GL_DEFAULT_VEHICLES: // Ungrouped vehicles
				DoCommandP(0, DEFAULT_GROUP, this->vehicle_sel, CMD_ADD_VEHICLE_GROUP | CMD_MSG(STR_ERROR_GROUP_CAN_T_ADD_VEHICLE));

				this->vehicle_sel = INVALID_VEHICLE;

				this->SetDirty();
				break;

			case WID_GL_LIST_GROUP: { // Matrix group
				const VehicleID vindex = this->vehicle_sel;
				this->vehicle_sel = INVALID_VEHICLE;
				this->SetDirty();

				uint id_g = this->group_sb->GetScrolledRowFromWidget(pt.y, this, WID_GL_LIST_GROUP, 0, this->tiny_step_height);
				if (id_g >= this->groups.Length()) return;

				DoCommandP(0, this->groups[id_g]->index, vindex, CMD_ADD_VEHICLE_GROUP | CMD_MSG(STR_ERROR_GROUP_CAN_T_ADD_VEHICLE));
				break;
			}

			case WID_GL_LIST_VEHICLE: { // Matrix vehicle
				const VehicleID vindex = this->vehicle_sel;
				this->vehicle_sel = INVALID_VEHICLE;
				this->SetDirty();

				uint id_v = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_GL_LIST_VEHICLE);
				if (id_v >= this->vehicles.Length()) return; // click out of list bound

				const Vehicle *v = this->vehicles[id_v];
				if (!VehicleClicked(v) && vindex == v->index) {
					ShowVehicleViewWindow(v);
				}
				break;
			}
		}
		_cursor.vehchain = false;
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str != NULL) DoCommandP(0, this->group_rename, 0, CMD_RENAME_GROUP | CMD_MSG(STR_ERROR_GROUP_CAN_T_RENAME), NULL, str);
		this->group_rename = INVALID_GROUP;
	}

	virtual void OnResize()
	{
		NWidgetCore *nwi = this->GetWidget<NWidgetCore>(WID_GL_LIST_GROUP);
		this->group_sb->SetCapacity(nwi->current_y / this->tiny_step_height);
		nwi->widget_data = (this->group_sb->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);

		nwi = this->GetWidget<NWidgetCore>(WID_GL_LIST_VEHICLE);
		this->vscroll->SetCapacityFromWidget(this, WID_GL_LIST_VEHICLE);
		nwi->widget_data = (this->vscroll->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		switch (widget) {
			case WID_GL_SORT_BY_DROPDOWN:
				this->vehicles.SetSortType(index);
				break;

			case WID_GL_MANAGE_VEHICLES_DROPDOWN:
				assert(this->vehicles.Length() != 0);

				switch (index) {
					case ADI_REPLACE: // Replace window
						ShowReplaceGroupVehicleWindow(this->vli.index, this->vli.vtype);
						break;
					case ADI_SERVICE: // Send for servicing
					case ADI_DEPOT: { // Send to Depots
						DoCommandP(0, DEPOT_MASS_SEND | (index == ADI_SERVICE ? DEPOT_SERVICE : 0U), this->vli.Pack(), GetCmdSendToDepot(this->vli.vtype));
						break;
					}

					case ADI_ADD_SHARED: // Add shared Vehicles
						assert(Group::IsValidID(this->vli.index));

						DoCommandP(0, this->vli.index, this->vli.vtype, CMD_ADD_SHARED_VEHICLE_GROUP | CMD_MSG(STR_ERROR_GROUP_CAN_T_ADD_SHARED_VEHICLE));
						break;
					case ADI_REMOVE_ALL: // Remove all Vehicles from the selected group
						assert(Group::IsValidID(this->vli.index));

						DoCommandP(0, this->vli.index, 0, CMD_REMOVE_ALL_VEHICLES_GROUP | CMD_MSG(STR_ERROR_GROUP_CAN_T_REMOVE_ALL_VEHICLES));
						break;
					default: NOT_REACHED();
				}
				break;

			default: NOT_REACHED();
		}

		this->SetDirty();
	}

	virtual void OnTick()
	{
		if (_pause_mode != PM_UNPAUSED) return;
		if (this->groups.NeedResort() || this->vehicles.NeedResort()) {
			this->SetDirty();
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		/* abort drag & drop */
		this->vehicle_sel = INVALID_VEHICLE;
		this->SetWidgetDirty(WID_GL_LIST_VEHICLE);
	}

	void ShowRenameGroupWindow(GroupID group, bool empty)
	{
		assert(Group::IsValidID(group));
		this->group_rename = group;
		/* Show empty query for new groups */
		StringID str = STR_EMPTY;
		if (!empty) {
			SetDParam(0, group);
			str = STR_GROUP_NAME;
		}
		ShowQueryString(str, STR_GROUP_RENAME_CAPTION, MAX_LENGTH_GROUP_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
	}

	/**
	 * Tests whether a given vehicle is selected in the window, and unselects it if necessary.
	 * Called when the vehicle is deleted.
	 * @param vehicle Vehicle that is going to be deleted
	 */
	void UnselectVehicle(VehicleID vehicle)
	{
		if (this->vehicle_sel == vehicle) ResetObjectToPlace();
	}
};


static WindowDesc _other_group_desc(
	WDP_AUTO, 460, 246,
	WC_INVALID, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_group_widgets, lengthof(_nested_group_widgets)
);

static const WindowDesc _train_group_desc(
	WDP_AUTO, 525, 246,
	WC_TRAINS_LIST, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_group_widgets, lengthof(_nested_group_widgets)
);

/**
 * Show the group window for the given company and vehicle type.
 * @param company The company to show the window for.
 * @param vehicle_type The type of vehicle to show it for.
 */
void ShowCompanyGroup(CompanyID company, VehicleType vehicle_type)
{
	if (!Company::IsValidID(company)) return;

	WindowNumber num = VehicleListIdentifier(VL_GROUP_LIST, vehicle_type, company).Pack();
	if (vehicle_type == VEH_TRAIN) {
		AllocateWindowDescFront<VehicleGroupWindow>(&_train_group_desc, num);
	} else {
		_other_group_desc.cls = GetWindowClassForVehicleType(vehicle_type);
		AllocateWindowDescFront<VehicleGroupWindow>(&_other_group_desc, num);
	}
}

/**
 * Finds a group list window determined by vehicle type and owner
 * @param vt vehicle type
 * @param owner owner of groups
 * @return pointer to VehicleGroupWindow, NULL if not found
 */
static inline VehicleGroupWindow *FindVehicleGroupWindow(VehicleType vt, Owner owner)
{
	return (VehicleGroupWindow *)FindWindowById(GetWindowClassForVehicleType(vt), VehicleListIdentifier(VL_GROUP_LIST, vt, owner).Pack());
}

/**
 * Opens a 'Rename group' window for newly created group
 * @param success did command succeed?
 * @param tile unused
 * @param p1 vehicle type
 * @param p2 unused
 * @see CmdCreateGroup
 */
void CcCreateGroup(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;
	assert(p1 <= VEH_AIRCRAFT);

	VehicleGroupWindow *w = FindVehicleGroupWindow((VehicleType)p1, _current_company);
	if (w != NULL) w->ShowRenameGroupWindow(_new_group_id, true);
}

/**
 * Removes the highlight of a vehicle in a group window
 * @param *v Vehicle to remove all highlights from
 */
void DeleteGroupHighlightOfVehicle(const Vehicle *v)
{
	/* If we haven't got any vehicles on the mouse pointer, we haven't got any highlighted in any group windows either
	 * If that is the case, we can skip looping though the windows and save time
	 */
	if (_special_mouse_mode != WSM_DRAGDROP) return;

	VehicleGroupWindow *w = FindVehicleGroupWindow(v->type, v->owner);
	if (w != NULL) w->UnselectVehicle(v->index);
}
