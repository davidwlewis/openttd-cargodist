/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file dock_gui.cpp GUI to create amazing water objects. */

#include "stdafx.h"
#include "terraform_gui.h"
#include "window_gui.h"
#include "station_gui.h"
#include "command_func.h"
#include "water.h"
#include "water_map.h"
#include "window_func.h"
#include "vehicle_func.h"
#include "sound_func.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "company_func.h"
#include "slope_func.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "station_type.h"
#include "hotkeys.h"
#include "bridge.h"

#include "widgets/dock_widget.h"

#include "table/sprites.h"
#include "table/strings.h"

static void ShowBuildDockStationPicker(Window *parent);
static void ShowBuildDocksDepotPicker(Window *parent);

static Axis _ship_depot_direction;

void CcBuildDocks(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Failed()) return;

	SndPlayTileFx(SND_02_SPLAT, tile);
	if (!_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
}

void CcBuildCanal(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Succeeded()) SndPlayTileFx(SND_02_SPLAT, tile);
}


/**
 * Gets the other end of the aqueduct, if possible.
 * @param tile_from     The begin tile for the aqueduct.
 * @param [out] tile_to The tile till where to show a selection for the aqueduct.
 * @return The other end of the aqueduct, or otherwise a tile in line with the aqueduct to cause the right error message.
 */
static TileIndex GetOtherAqueductEnd(TileIndex tile_from, TileIndex *tile_to = NULL)
{
	int z;
	DiagDirection dir = GetInclinedSlopeDirection(GetTileSlope(tile_from, &z));

	/* If the direction isn't right, just return the next tile so the command
	 * complains about the wrong slope instead of the ends not matching up.
	 * Make sure the coordinate is always a valid tile within the map, so we
	 * don't go "off" the map. That would cause the wrong error message. */
	if (!IsValidDiagDirection(dir)) return TILE_ADDXY(tile_from, TileX(tile_from) > 2 ? -1 : 1, 0);

	/* Direction the aqueduct is built to. */
	TileIndexDiff offset = TileOffsByDiagDir(ReverseDiagDir(dir));
	/* The maximum length of the aqueduct. */
	int max_length = min(_settings_game.construction.max_bridge_length, DistanceFromEdgeDir(tile_from, ReverseDiagDir(dir)) - 1);

	TileIndex endtile = tile_from;
	for (int length = 0; IsValidTile(endtile) && TileX(endtile) != 0 && TileY(endtile) != 0; length++) {
		endtile = TILE_ADD(endtile, offset);

		if (length > max_length) break;

		if (GetTileMaxZ(endtile) > z) {
			if (tile_to != NULL) *tile_to = endtile;
			break;
		}
	}

	return endtile;
}

/** Toolbar window for constructing water infra structure. */
struct BuildDocksToolbarWindow : Window {
	DockToolbarWidgets last_clicked_widget; ///< Contains the last widget that has been clicked on this toolbar.

	BuildDocksToolbarWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->last_clicked_widget = WID_DT_INVALID;
		this->InitNested(desc, window_number);
		this->OnInvalidateData();
		if (_settings_client.gui.link_terraform_toolbar) ShowTerraformToolbar(this);
	}

	~BuildDocksToolbarWindow()
	{
		if (_settings_client.gui.link_terraform_toolbar) DeleteWindowById(WC_SCEN_LAND_GEN, 0, false);
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		this->SetWidgetsDisabledState(!CanBuildVehicleInfrastructure(VEH_SHIP),
			WID_DT_DEPOT,
			WID_DT_STATION,
			WID_DT_BUOY,
			WIDGET_LIST_END);
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_DT_CANAL: // Build canal button
				HandlePlacePushButton(this, WID_DT_CANAL, SPR_CURSOR_CANAL, HT_RECT);
				break;

			case WID_DT_LOCK: // Build lock button
				HandlePlacePushButton(this, WID_DT_LOCK, SPR_CURSOR_LOCK, HT_SPECIAL);
				break;

			case WID_DT_DEMOLISH: // Demolish aka dynamite button
				HandlePlacePushButton(this, WID_DT_DEMOLISH, ANIMCURSOR_DEMOLISH, HT_RECT | HT_DIAGONAL);
				break;

			case WID_DT_DEPOT: // Build depot button
				if (!CanBuildVehicleInfrastructure(VEH_SHIP)) return;
				if (HandlePlacePushButton(this, WID_DT_DEPOT, SPR_CURSOR_SHIP_DEPOT, HT_RECT)) ShowBuildDocksDepotPicker(this);
				break;

			case WID_DT_STATION: // Build station button
				if (!CanBuildVehicleInfrastructure(VEH_SHIP)) return;
				if (HandlePlacePushButton(this, WID_DT_STATION, SPR_CURSOR_DOCK, HT_SPECIAL)) ShowBuildDockStationPicker(this);
				break;

			case WID_DT_BUOY: // Build buoy button
				if (!CanBuildVehicleInfrastructure(VEH_SHIP)) return;
				HandlePlacePushButton(this, WID_DT_BUOY, SPR_CURSOR_BUOY, HT_RECT);
				break;

			case WID_DT_RIVER: // Build river button (in scenario editor)
				if (_game_mode != GM_EDITOR) return;
				HandlePlacePushButton(this, WID_DT_RIVER, SPR_CURSOR_RIVER, HT_RECT);
				break;

			case WID_DT_BUILD_AQUEDUCT: // Build aqueduct button
				HandlePlacePushButton(this, WID_DT_BUILD_AQUEDUCT, SPR_CURSOR_AQUEDUCT, HT_SPECIAL);
				break;

			default: return;
		}
		this->last_clicked_widget = (DockToolbarWidgets)widget;
	}

	virtual EventState OnKeyPress(uint16 key, uint16 keycode)
	{
		int num = CheckHotkeyMatch(dockstoolbar_hotkeys, keycode, this);
		if (num == -1) return ES_NOT_HANDLED;
		this->OnClick(Point(), num, 1);
		return ES_HANDLED;
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		switch (this->last_clicked_widget) {
			case WID_DT_CANAL: // Build canal button
				VpStartPlaceSizing(tile, (_game_mode == GM_EDITOR) ? VPM_X_AND_Y : VPM_X_OR_Y, DDSP_CREATE_WATER);
				break;

			case WID_DT_LOCK: // Build lock button
				DoCommandP(tile, 0, 0, CMD_BUILD_LOCK | CMD_MSG(STR_ERROR_CAN_T_BUILD_LOCKS), CcBuildDocks);
				break;

			case WID_DT_DEMOLISH: // Demolish aka dynamite button
				PlaceProc_DemolishArea(tile);
				break;

			case WID_DT_DEPOT: // Build depot button
				DoCommandP(tile, _ship_depot_direction, 0, CMD_BUILD_SHIP_DEPOT | CMD_MSG(STR_ERROR_CAN_T_BUILD_SHIP_DEPOT), CcBuildDocks);
				break;

			case WID_DT_STATION: { // Build station button
				uint32 p2 = (uint32)INVALID_STATION << 16; // no station to join

				/* tile is always the land tile, so need to evaluate _thd.pos */
				CommandContainer cmdcont = { tile, _ctrl_pressed, p2, CMD_BUILD_DOCK | CMD_MSG(STR_ERROR_CAN_T_BUILD_DOCK_HERE), CcBuildDocks, "" };

				/* Determine the watery part of the dock. */
				DiagDirection dir = GetInclinedSlopeDirection(GetTileSlope(tile));
				TileIndex tile_to = (dir != INVALID_DIAGDIR ? TileAddByDiagDir(tile, ReverseDiagDir(dir)) : tile);

				ShowSelectStationIfNeeded(cmdcont, TileArea(tile, tile_to));
				break;
			}

			case WID_DT_BUOY: // Build buoy button
				DoCommandP(tile, 0, 0, CMD_BUILD_BUOY | CMD_MSG(STR_ERROR_CAN_T_POSITION_BUOY_HERE), CcBuildDocks);
				break;

			case WID_DT_RIVER: // Build river button (in scenario editor)
				VpStartPlaceSizing(tile, VPM_X_AND_Y, DDSP_CREATE_RIVER);
				break;

			case WID_DT_BUILD_AQUEDUCT: // Build aqueduct button
				DoCommandP(tile, GetOtherAqueductEnd(tile), TRANSPORT_WATER << 15, CMD_BUILD_BRIDGE | CMD_MSG(STR_ERROR_CAN_T_BUILD_AQUEDUCT_HERE), CcBuildBridge);
				break;

			default: NOT_REACHED();
		}
	}

	virtual void OnPlaceDrag(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt)
	{
		VpSelectTilesWithMethod(pt.x, pt.y, select_method);
	}

	virtual void OnPlaceMouseUp(ViewportPlaceMethod select_method, ViewportDragDropSelectionProcess select_proc, Point pt, TileIndex start_tile, TileIndex end_tile)
	{
		if (pt.x != -1) {
			switch (select_proc) {
				case DDSP_DEMOLISH_AREA:
					GUIPlaceProcDragXY(select_proc, start_tile, end_tile);
					break;
				case DDSP_CREATE_WATER:
					DoCommandP(end_tile, start_tile, (_game_mode == GM_EDITOR && _ctrl_pressed) ? WATER_CLASS_SEA : WATER_CLASS_CANAL, CMD_BUILD_CANAL | CMD_MSG(STR_ERROR_CAN_T_BUILD_CANALS), CcBuildCanal);
					break;
				case DDSP_CREATE_RIVER:
					DoCommandP(end_tile, start_tile, WATER_CLASS_RIVER, CMD_BUILD_CANAL | CMD_MSG(STR_ERROR_CAN_T_PLACE_RIVERS), CcBuildCanal);
					break;

				default: break;
			}
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();

		DeleteWindowById(WC_BUILD_STATION, TRANSPORT_WATER);
		DeleteWindowById(WC_BUILD_DEPOT, TRANSPORT_WATER);
		DeleteWindowById(WC_SELECT_STATION, 0);
		DeleteWindowByClass(WC_BUILD_BRIDGE);
	}

	virtual void OnPlacePresize(Point pt, TileIndex tile_from)
	{
		TileIndex tile_to = tile_from;

		if (this->last_clicked_widget == WID_DT_BUILD_AQUEDUCT) {
			GetOtherAqueductEnd(tile_from, &tile_to);
		} else {
			DiagDirection dir = GetInclinedSlopeDirection(GetTileSlope(tile_from));
			if (IsValidDiagDirection(dir)) {
				/* Locks and docks always select the tile "down" the slope. */
				tile_to = TileAddByDiagDir(tile_from, ReverseDiagDir(dir));
				/* Locks also select the tile "up" the slope. */
				if (this->last_clicked_widget == WID_DT_LOCK) tile_from = TileAddByDiagDir(tile_from, dir);
			}
		}

		VpSetPresizeRange(tile_from, tile_to);
	}

	static Hotkey<BuildDocksToolbarWindow> dockstoolbar_hotkeys[];
};

const uint16 _dockstoolbar_aqueduct_keys[] = {'B', '8', 0};

Hotkey<BuildDocksToolbarWindow> BuildDocksToolbarWindow::dockstoolbar_hotkeys[] = {
	Hotkey<BuildDocksToolbarWindow>('1', "canal", WID_DT_CANAL),
	Hotkey<BuildDocksToolbarWindow>('2', "lock", WID_DT_LOCK),
	Hotkey<BuildDocksToolbarWindow>('3', "demolish", WID_DT_DEMOLISH),
	Hotkey<BuildDocksToolbarWindow>('4', "depot", WID_DT_DEPOT),
	Hotkey<BuildDocksToolbarWindow>('5', "dock", WID_DT_STATION),
	Hotkey<BuildDocksToolbarWindow>('6', "buoy", WID_DT_BUOY),
	Hotkey<BuildDocksToolbarWindow>('7', "river", WID_DT_RIVER),
	Hotkey<BuildDocksToolbarWindow>(_dockstoolbar_aqueduct_keys, "aqueduct", WID_DT_BUILD_AQUEDUCT),
	HOTKEY_LIST_END(BuildDocksToolbarWindow)
};
Hotkey<BuildDocksToolbarWindow> *_dockstoolbar_hotkeys = BuildDocksToolbarWindow::dockstoolbar_hotkeys;

/**
 * Nested widget parts of docks toolbar, game version.
 * Position of #WID_DT_RIVER widget has changed.
 */
static const NWidgetPart _nested_build_docks_toolbar_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_WATERWAYS_TOOLBAR_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL_LTR),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_CANAL), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_BUILD_CANAL, STR_WATERWAYS_TOOLBAR_BUILD_CANALS_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_LOCK), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_BUILD_LOCK, STR_WATERWAYS_TOOLBAR_BUILD_LOCKS_TOOLTIP),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetMinimalSize(5, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_DEMOLISH), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_DEPOT), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_SHIP_DEPOT, STR_WATERWAYS_TOOLBAR_BUILD_DEPOT_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_STATION), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_SHIP_DOCK, STR_WATERWAYS_TOOLBAR_BUILD_DOCK_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_BUOY), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_BUOY, STR_WATERWAYS_TOOLBAR_BUOY_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_BUILD_AQUEDUCT), SetMinimalSize(23, 22), SetFill(0, 1), SetDataTip(SPR_IMG_AQUEDUCT, STR_WATERWAYS_TOOLBAR_BUILD_AQUEDUCT_TOOLTIP),
	EndContainer(),
};

static const WindowDesc _build_docks_toolbar_desc(
	WDP_ALIGN_TOOLBAR, 0, 0,
	WC_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_docks_toolbar_widgets, lengthof(_nested_build_docks_toolbar_widgets)
);

/**
 * Open the build water toolbar window
 *
 * If the terraform toolbar is linked to the toolbar, that window is also opened.
 *
 * @return newly opened water toolbar, or NULL if the toolbar could not be opened.
 */
Window *ShowBuildDocksToolbar()
{
	if (!Company::IsValidID(_local_company)) return NULL;

	DeleteWindowByClass(WC_BUILD_TOOLBAR);
	return AllocateWindowDescFront<BuildDocksToolbarWindow>(&_build_docks_toolbar_desc, TRANSPORT_WATER);
}

EventState DockToolbarGlobalHotkeys(uint16 key, uint16 keycode)
{
	int num = CheckHotkeyMatch<BuildDocksToolbarWindow>(_dockstoolbar_hotkeys, keycode, NULL, true);
	if (num == -1) return ES_NOT_HANDLED;
	Window *w = ShowBuildDocksToolbar();
	if (w == NULL) return ES_NOT_HANDLED;
	return w->OnKeyPress(key, keycode);
}

/**
 * Nested widget parts of docks toolbar, scenario editor version.
 * Positions of #WID_DT_DEPOT, #WID_DT_STATION, and #WID_DT_BUOY widgets have changed.
 */
static const NWidgetPart _nested_build_docks_scen_toolbar_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_WATERWAYS_TOOLBAR_CAPTION_SE, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_CANAL), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_BUILD_CANAL, STR_WATERWAYS_TOOLBAR_CREATE_LAKE_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_LOCK), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_BUILD_LOCK, STR_WATERWAYS_TOOLBAR_BUILD_LOCKS_TOOLTIP),
		NWidget(WWT_PANEL, COLOUR_DARK_GREEN), SetMinimalSize(5, 22), SetFill(1, 1), EndContainer(),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_DEMOLISH), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_DYNAMITE, STR_TOOLTIP_DEMOLISH_BUILDINGS_ETC),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_RIVER), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_BUILD_RIVER, STR_WATERWAYS_TOOLBAR_CREATE_RIVER_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_DARK_GREEN, WID_DT_BUILD_AQUEDUCT), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_AQUEDUCT, STR_WATERWAYS_TOOLBAR_BUILD_AQUEDUCT_TOOLTIP),
	EndContainer(),
};

/** Window definition for the build docks in scenario editor window. */
static const WindowDesc _build_docks_scen_toolbar_desc(
	WDP_AUTO, 0, 0,
	WC_SCEN_BUILD_TOOLBAR, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_docks_scen_toolbar_widgets, lengthof(_nested_build_docks_scen_toolbar_widgets)
);

/**
 * Open the build water toolbar window for the scenario editor.
 *
 * @return newly opened water toolbar, or NULL if the toolbar could not be opened.
 */
Window *ShowBuildDocksScenToolbar()
{
	return AllocateWindowDescFront<BuildDocksToolbarWindow>(&_build_docks_scen_toolbar_desc, TRANSPORT_WATER);
}

/** Widget numbers of the build-dock GUI. */
enum BuildDockStationWidgets {
	BDSW_BACKGROUND, ///< Background panel.
	BDSW_LT_OFF,     ///< 'Off' button of coverage high light.
	BDSW_LT_ON,      ///< 'On' button of coverage high light.
	BDSW_INFO,       ///< 'Coverage highlight' label.
};

struct BuildDocksStationWindow : public PickerWindowBase {
public:
	BuildDocksStationWindow(const WindowDesc *desc, Window *parent) : PickerWindowBase(parent)
	{
		this->InitNested(desc, TRANSPORT_WATER);
		this->LowerWidget(_settings_client.gui.station_show_coverage + BDSW_LT_OFF);
	}

	virtual ~BuildDocksStationWindow()
	{
		DeleteWindowById(WC_SELECT_STATION, 0);
	}

	virtual void OnPaint()
	{
		int rad = (_settings_game.station.modified_catchment) ? CA_DOCK : CA_UNMODIFIED;

		this->DrawWidgets();

		if (_settings_client.gui.station_show_coverage) {
			SetTileSelectBigSize(-rad, -rad, 2 * rad, 2 * rad);
		} else {
			SetTileSelectSize(1, 1);
		}

		/* strings such as 'Size' and 'Coverage Area' */
		int top = this->GetWidget<NWidgetBase>(BDSW_LT_OFF)->pos_y + this->GetWidget<NWidgetBase>(BDSW_LT_OFF)->current_y + WD_PAR_VSEP_NORMAL;
		NWidgetBase *back_nwi = this->GetWidget<NWidgetBase>(BDSW_BACKGROUND);
		int right  = back_nwi->pos_x + back_nwi->current_x;
		int bottom = back_nwi->pos_y + back_nwi->current_y;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, SCT_ALL, rad, false) + WD_PAR_VSEP_NORMAL;
		top = DrawStationCoverageAreaText(back_nwi->pos_x + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, top, SCT_ALL, rad, true) + WD_PAR_VSEP_NORMAL;
		/* Resize background if the text is not equally long as the window. */
		if (top > bottom || (top < bottom && back_nwi->current_y > back_nwi->smallest_y)) {
			ResizeWindow(this, 0, top - bottom);
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case BDSW_LT_OFF:
			case BDSW_LT_ON:
				this->RaiseWidget(_settings_client.gui.station_show_coverage + BDSW_LT_OFF);
				_settings_client.gui.station_show_coverage = (widget != BDSW_LT_OFF);
				this->LowerWidget(_settings_client.gui.station_show_coverage + BDSW_LT_OFF);
				SndPlayFx(SND_15_BEEP);
				this->SetDirty();
				break;
		}
	}

	virtual void OnTick()
	{
		CheckRedrawStationCoverage(this);
	}
};

/** Nested widget parts of a build dock station window. */
static const NWidgetPart _nested_build_dock_station_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_STATION_BUILD_DOCK_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, BDSW_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(WWT_LABEL, COLOUR_DARK_GREEN, BDSW_INFO), SetMinimalSize(148, 14), SetDataTip(STR_STATION_BUILD_COVERAGE_AREA_TITLE, STR_NULL),
		NWidget(NWID_HORIZONTAL), SetPIP(14, 0, 14),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BDSW_LT_OFF), SetMinimalSize(40, 12), SetFill(1, 0), SetDataTip(STR_STATION_BUILD_COVERAGE_OFF, STR_STATION_BUILD_COVERAGE_AREA_OFF_TOOLTIP),
			NWidget(WWT_TEXTBTN, COLOUR_GREY, BDSW_LT_ON), SetMinimalSize(40, 12), SetFill(1, 0), SetDataTip(STR_STATION_BUILD_COVERAGE_ON, STR_STATION_BUILD_COVERAGE_AREA_ON_TOOLTIP),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 20), SetResize(0, 1),
	EndContainer(),
};

static const WindowDesc _build_dock_station_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_STATION, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_dock_station_widgets, lengthof(_nested_build_dock_station_widgets)
);

static void ShowBuildDockStationPicker(Window *parent)
{
	new BuildDocksStationWindow(&_build_dock_station_desc, parent);
}

struct BuildDocksDepotWindow : public PickerWindowBase {
private:
	static void UpdateDocksDirection()
	{
		if (_ship_depot_direction != AXIS_X) {
			SetTileSelectSize(1, 2);
		} else {
			SetTileSelectSize(2, 1);
		}
	}

public:
	BuildDocksDepotWindow(const WindowDesc *desc, Window *parent) : PickerWindowBase(parent)
	{
		this->InitNested(desc, TRANSPORT_WATER);
		this->LowerWidget(_ship_depot_direction + WID_BDD_X);
		UpdateDocksDirection();
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		DrawShipDepotSprite(this->GetWidget<NWidgetBase>(WID_BDD_X)->pos_x + 64, this->GetWidget<NWidgetBase>(WID_BDD_X)->pos_y + 18, AXIS_X, DEPOT_PART_NORTH);
		DrawShipDepotSprite(this->GetWidget<NWidgetBase>(WID_BDD_X)->pos_x + 32, this->GetWidget<NWidgetBase>(WID_BDD_X)->pos_y + 34, AXIS_X, DEPOT_PART_SOUTH);
		DrawShipDepotSprite(this->GetWidget<NWidgetBase>(WID_BDD_Y)->pos_x + 32, this->GetWidget<NWidgetBase>(WID_BDD_Y)->pos_y + 18, AXIS_Y, DEPOT_PART_NORTH);
		DrawShipDepotSprite(this->GetWidget<NWidgetBase>(WID_BDD_Y)->pos_x + 64, this->GetWidget<NWidgetBase>(WID_BDD_Y)->pos_y + 34, AXIS_Y, DEPOT_PART_SOUTH);
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_BDD_X:
			case WID_BDD_Y:
				this->RaiseWidget(_ship_depot_direction + WID_BDD_X);
				_ship_depot_direction = (widget == WID_BDD_X ? AXIS_X : AXIS_Y);
				this->LowerWidget(_ship_depot_direction + WID_BDD_X);
				SndPlayFx(SND_15_BEEP);
				UpdateDocksDirection();
				this->SetDirty();
				break;
		}
	}
};

static const NWidgetPart _nested_build_docks_depot_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_DEPOT_BUILD_SHIP_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_BDD_BACKGROUND),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
		NWidget(NWID_HORIZONTAL_LTR),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BDD_X), SetMinimalSize(98, 66), SetDataTip(0x0, STR_DEPOT_BUILD_SHIP_ORIENTATION_TOOLTIP),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(2, 0),
			NWidget(WWT_PANEL, COLOUR_GREY, WID_BDD_Y), SetMinimalSize(98, 66), SetDataTip(0x0, STR_DEPOT_BUILD_SHIP_ORIENTATION_TOOLTIP),
			EndContainer(),
			NWidget(NWID_SPACER), SetMinimalSize(3, 0),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 3),
	EndContainer(),
};

static const WindowDesc _build_docks_depot_desc(
	WDP_AUTO, 0, 0,
	WC_BUILD_DEPOT, WC_BUILD_TOOLBAR,
	WDF_CONSTRUCTION,
	_nested_build_docks_depot_widgets, lengthof(_nested_build_docks_depot_widgets)
);


static void ShowBuildDocksDepotPicker(Window *parent)
{
	new BuildDocksDepotWindow(&_build_docks_depot_desc, parent);
}


void InitializeDockGui()
{
	_ship_depot_direction = AXIS_X;
}
