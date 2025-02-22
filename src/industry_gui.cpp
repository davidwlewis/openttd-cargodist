/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file industry_gui.cpp GUIs related to industries. */

#include "stdafx.h"
#include "error.h"
#include "gui.h"
#include "sound_func.h"
#include "window_func.h"
#include "textbuf_gui.h"
#include "command_func.h"
#include "viewport_func.h"
#include "gfx_func.h"
#include "industry.h"
#include "town.h"
#include "cheat_type.h"
#include "newgrf_industries.h"
#include "newgrf_text.h"
#include "newgrf_debug.h"
#include "strings_func.h"
#include "company_func.h"
#include "tilehighlight_func.h"
#include "string_func.h"
#include "sortlist_type.h"
#include "widgets/dropdown_func.h"
#include "company_base.h"
#include "core/geometry_func.hpp"
#include "core/random_func.hpp"
#include "core/backup_type.hpp"
#include "genworld.h"
#include "smallmap_gui.h"

#include "widgets/industry_widget.h"

#include "table/strings.h"
#include "table/sprites.h"

bool _ignore_restrictions;
uint64 _displayed_industries; ///< Communication from the industry chain window to the smallmap window about what industries to display.

assert_compile(NUM_INDUSTRYTYPES <= 64); // Make sure all industry types fit in _displayed_industries.

/** Cargo suffix type (for which window is it requested) */
enum CargoSuffixType {
	CST_FUND,  ///< Fund-industry window
	CST_VIEW,  ///< View-industry window
	CST_DIR,   ///< Industry-directory window
};

static void ShowIndustryCargoesWindow(IndustryType id);

/**
 * Gets the string to display after the cargo name (using callback 37)
 * @param cargo the cargo for which the suffix is requested
 * - 00 - first accepted cargo type
 * - 01 - second accepted cargo type
 * - 02 - third accepted cargo type
 * - 03 - first produced cargo type
 * - 04 - second produced cargo type
 * @param cst the cargo suffix type (for which window is it requested). @see CargoSuffixType
 * @param ind the industry (NULL if in fund window)
 * @param ind_type the industry type
 * @param indspec the industry spec
 * @param suffix is filled with the string to display
 * @param suffix_last lastof(suffix)
 */
static void GetCargoSuffix(uint cargo, CargoSuffixType cst, const Industry *ind, IndustryType ind_type, const IndustrySpec *indspec, char *suffix, const char *suffix_last)
{
	suffix[0] = '\0';
	if (HasBit(indspec->callback_mask, CBM_IND_CARGO_SUFFIX)) {
		uint16 callback = GetIndustryCallback(CBID_INDUSTRY_CARGO_SUFFIX, 0, (cst << 8) | cargo, const_cast<Industry *>(ind), ind_type, (cst != CST_FUND) ? ind->location.tile : INVALID_TILE);
		if (callback == CALLBACK_FAILED || callback == 0x400) return;
		if (callback > 0x400) {
			ErrorUnknownCallbackResult(indspec->grf_prop.grffile->grfid, CBID_INDUSTRY_CARGO_SUFFIX, callback);
		} else if (indspec->grf_prop.grffile->grf_version >= 8 || GB(callback, 0, 8) != 0xFF) {
			StartTextRefStackUsage(6);
			GetString(suffix, GetGRFStringID(indspec->grf_prop.grffile->grfid, 0xD000 + callback), suffix_last);
			StopTextRefStackUsage();
		}
	}
}

/**
 * Gets all strings to display after the cargoes of industries (using callback 37)
 * @param cb_offset The offset for the cargo used in cb37, 0 for accepted cargoes, 3 for produced cargoes
 * @param cst the cargo suffix type (for which window is it requested). @see CargoSuffixType
 * @param ind the industry (NULL if in fund window)
 * @param ind_type the industry type
 * @param indspec the industry spec
 * @param cargoes array with cargotypes. for CT_INVALID no suffix will be determined
 * @param suffixes is filled with the suffixes
 */
template <typename TC, typename TS>
static inline void GetAllCargoSuffixes(uint cb_offset, CargoSuffixType cst, const Industry *ind, IndustryType ind_type, const IndustrySpec *indspec, const TC &cargoes, TS &suffixes)
{
	assert_compile(lengthof(cargoes) <= lengthof(suffixes));
	for (uint j = 0; j < lengthof(cargoes); j++) {
		if (cargoes[j] != CT_INVALID) {
			GetCargoSuffix(cb_offset + j, cst, ind, ind_type, indspec, suffixes[j], lastof(suffixes[j]));
		} else {
			suffixes[j][0] = '\0';
		}
	}
}

IndustryType _sorted_industry_types[NUM_INDUSTRYTYPES];

/** Sort industry types by their name. */
static int CDECL IndustryTypeNameSorter(const IndustryType *a, const IndustryType *b)
{
	static char industry_name[2][64];

	const IndustrySpec *indsp1 = GetIndustrySpec(*a);
	SetDParam(0, indsp1->name);
	GetString(industry_name[0], STR_JUST_STRING, lastof(industry_name[0]));

	const IndustrySpec *indsp2 = GetIndustrySpec(*b);
	SetDParam(0, indsp2->name);
	GetString(industry_name[1], STR_JUST_STRING, lastof(industry_name[1]));

	int r = strnatcmp(industry_name[0], industry_name[1]); // Sort by name (natural sorting).

	/* If the names are equal, sort by industry type. */
	return (r != 0) ? r : (*a - *b);
}

/**
 * Initialize the list of sorted industry types.
 */
void SortIndustryTypes()
{
	/* Add each industry type to the list. */
	for (IndustryType i = 0; i < NUM_INDUSTRYTYPES; i++) {
		_sorted_industry_types[i] = i;
	}

	/* Sort industry types by name. */
	QSortT(_sorted_industry_types, NUM_INDUSTRYTYPES, &IndustryTypeNameSorter);
}

/**
 * Command callback. In case of failure to build an industry, show an error message.
 * @param result Result of the command.
 * @param tile   Tile where the industry is placed.
 * @param p1     Additional data of the #CMD_BUILD_INDUSTRY command.
 * @param p2     Additional data of the #CMD_BUILD_INDUSTRY command.
 */
void CcBuildIndustry(const CommandCost &result, TileIndex tile, uint32 p1, uint32 p2)
{
	if (result.Succeeded()) return;

	uint8 indtype = GB(p1, 0, 8);
	if (indtype < NUM_INDUSTRYTYPES) {
		const IndustrySpec *indsp = GetIndustrySpec(indtype);
		if (indsp->enabled) {
			SetDParam(0, indsp->name);
			ShowErrorMessage(STR_ERROR_CAN_T_BUILD_HERE, result.GetErrorMessage(), WL_INFO, TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE);
		}
	}
}

static const NWidgetPart _nested_build_industry_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_CAPTION, COLOUR_DARK_GREEN), SetDataTip(STR_FUND_INDUSTRY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_DARK_GREEN),
		NWidget(WWT_STICKYBOX, COLOUR_DARK_GREEN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_DARK_GREEN, WID_DPI_MATRIX_WIDGET), SetDataTip(0x801, STR_FUND_INDUSTRY_SELECTION_TOOLTIP), SetFill(1, 0), SetResize(1, 1), SetScrollbar(WID_DPI_SCROLLBAR),
		NWidget(NWID_VSCROLLBAR, COLOUR_DARK_GREEN, WID_DPI_SCROLLBAR),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_DARK_GREEN, WID_DPI_INFOPANEL), SetResize(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_TEXTBTN, COLOUR_DARK_GREEN, WID_DPI_DISPLAY_WIDGET), SetFill(1, 0), SetResize(1, 0),
				SetDataTip(STR_INDUSTRY_DISPLAY_CHAIN, STR_INDUSTRY_DISPLAY_CHAIN_TOOLTIP),
		NWidget(WWT_TEXTBTN, COLOUR_DARK_GREEN, WID_DPI_FUND_WIDGET), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_JUST_STRING, STR_NULL),
		NWidget(WWT_RESIZEBOX, COLOUR_DARK_GREEN),
	EndContainer(),
};

/** Window definition of the dynamic place industries gui */
static const WindowDesc _build_industry_desc(
	WDP_AUTO, 170, 212,
	WC_BUILD_INDUSTRY, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_build_industry_widgets, lengthof(_nested_build_industry_widgets)
);

/** Build (fund or prospect) a new industry, */
class BuildIndustryWindow : public Window {
	int selected_index;                         ///< index of the element in the matrix
	IndustryType selected_type;                 ///< industry corresponding to the above index
	uint16 callback_timer;                      ///< timer counter for callback eventual verification
	bool timer_enabled;                         ///< timer can be used
	uint16 count;                               ///< How many industries are loaded
	IndustryType index[NUM_INDUSTRYTYPES + 1];  ///< Type of industry, in the order it was loaded
	bool enabled[NUM_INDUSTRYTYPES + 1];        ///< availability state, coming from CBID_INDUSTRY_PROBABILITY (if ever)
	Scrollbar *vscroll;

	/** The offset for the text in the matrix. */
	static const int MATRIX_TEXT_OFFSET = 17;

	void SetupArrays()
	{
		this->count = 0;

		for (uint i = 0; i < lengthof(this->index); i++) {
			this->index[i]   = INVALID_INDUSTRYTYPE;
			this->enabled[i] = false;
		}

		if (_game_mode == GM_EDITOR) { // give room for the Many Random "button"
			this->index[this->count] = INVALID_INDUSTRYTYPE;
			this->enabled[this->count] = true;
			this->count++;
			this->timer_enabled = false;
		}
		/* Fill the arrays with industries.
		 * The tests performed after the enabled allow to load the industries
		 * In the same way they are inserted by grf (if any)
		 */
		for (uint8 i = 0; i < NUM_INDUSTRYTYPES; i++) {
			IndustryType ind = _sorted_industry_types[i];
			const IndustrySpec *indsp = GetIndustrySpec(ind);
			if (indsp->enabled) {
				/* Rule is that editor mode loads all industries.
				 * In game mode, all non raw industries are loaded too
				 * and raw ones are loaded only when setting allows it */
				if (_game_mode != GM_EDITOR && indsp->IsRawIndustry() && _settings_game.construction.raw_industry_construction == 0) {
					/* Unselect if the industry is no longer in the list */
					if (this->selected_type == ind) this->selected_index = -1;
					continue;
				}
				this->index[this->count] = ind;
				this->enabled[this->count] = (_game_mode == GM_EDITOR) || GetIndustryProbabilityCallback(ind, IACT_USERCREATION, 1) > 0;
				/* Keep the selection to the correct line */
				if (this->selected_type == ind) this->selected_index = this->count;
				this->count++;
			}
		}

		/* first indutry type is selected if the current selection is invalid.
		 * I'll be damned if there are none available ;) */
		if (this->selected_index == -1) {
			this->selected_index = 0;
			this->selected_type = this->index[0];
		}

		this->vscroll->SetCount(this->count);
	}

	/** Update status of the fund and display-chain widgets. */
	void SetButtons()
	{
		this->SetWidgetDisabledState(WID_DPI_FUND_WIDGET, this->selected_type != INVALID_INDUSTRYTYPE && !this->enabled[this->selected_index]);
		this->SetWidgetDisabledState(WID_DPI_DISPLAY_WIDGET, this->selected_type == INVALID_INDUSTRYTYPE && this->enabled[this->selected_index]);
	}

public:
	BuildIndustryWindow() : Window()
	{
		this->timer_enabled = _loaded_newgrf_features.has_newindustries;

		this->selected_index = -1;
		this->selected_type = INVALID_INDUSTRYTYPE;

		this->callback_timer = DAY_TICKS;

		this->CreateNestedTree(&_build_industry_desc);
		this->vscroll = this->GetScrollbar(WID_DPI_SCROLLBAR);
		this->FinishInitNested(&_build_industry_desc, 0);

		this->SetButtons();
	}

	virtual void OnInit()
	{
		this->SetupArrays();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_DPI_MATRIX_WIDGET: {
				Dimension d = GetStringBoundingBox(STR_FUND_INDUSTRY_MANY_RANDOM_INDUSTRIES);
				for (byte i = 0; i < this->count; i++) {
					if (this->index[i] == INVALID_INDUSTRYTYPE) continue;
					d = maxdim(d, GetStringBoundingBox(GetIndustrySpec(this->index[i])->name));
				}
				resize->height = FONT_HEIGHT_NORMAL + WD_MATRIX_TOP + WD_MATRIX_BOTTOM;
				d.width += MATRIX_TEXT_OFFSET + padding.width;
				d.height = 5 * resize->height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_DPI_INFOPANEL: {
				/* Extra line for cost outside of editor + extra lines for 'extra' information for NewGRFs. */
				int height = 2 + (_game_mode == GM_EDITOR ? 0 : 1) + (_loaded_newgrf_features.has_newindustries ? 4 : 0);
				Dimension d = {0, 0};
				for (byte i = 0; i < this->count; i++) {
					if (this->index[i] == INVALID_INDUSTRYTYPE) continue;

					const IndustrySpec *indsp = GetIndustrySpec(this->index[i]);

					char cargo_suffix[3][512];
					GetAllCargoSuffixes(0, CST_FUND, NULL, this->index[i], indsp, indsp->accepts_cargo, cargo_suffix);
					StringID str = STR_INDUSTRY_VIEW_REQUIRES_CARGO;
					byte p = 0;
					SetDParam(0, STR_JUST_NOTHING);
					SetDParamStr(1, "");
					for (byte j = 0; j < lengthof(indsp->accepts_cargo); j++) {
						if (indsp->accepts_cargo[j] == CT_INVALID) continue;
						if (p > 0) str++;
						SetDParam(p++, CargoSpec::Get(indsp->accepts_cargo[j])->name);
						SetDParamStr(p++, cargo_suffix[j]);
					}
					d = maxdim(d, GetStringBoundingBox(str));

					/* Draw the produced cargoes, if any. Otherwise, will print "Nothing". */
					GetAllCargoSuffixes(3, CST_FUND, NULL, this->index[i], indsp, indsp->produced_cargo, cargo_suffix);
					str = STR_INDUSTRY_VIEW_PRODUCES_CARGO;
					p = 0;
					SetDParam(0, STR_JUST_NOTHING);
					SetDParamStr(1, "");
					for (byte j = 0; j < lengthof(indsp->produced_cargo); j++) {
						if (indsp->produced_cargo[j] == CT_INVALID) continue;
						if (p > 0) str++;
						SetDParam(p++, CargoSpec::Get(indsp->produced_cargo[j])->name);
						SetDParamStr(p++, cargo_suffix[j]);
					}
					d = maxdim(d, GetStringBoundingBox(str));
				}

				/* Set it to something more sane :) */
				size->height = height * FONT_HEIGHT_NORMAL + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				size->width  = d.width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				break;
			}

			case WID_DPI_FUND_WIDGET: {
				Dimension d = GetStringBoundingBox(STR_FUND_INDUSTRY_BUILD_NEW_INDUSTRY);
				d = maxdim(d, GetStringBoundingBox(STR_FUND_INDUSTRY_PROSPECT_NEW_INDUSTRY));
				d = maxdim(d, GetStringBoundingBox(STR_FUND_INDUSTRY_FUND_NEW_INDUSTRY));
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_DPI_FUND_WIDGET:
				/* Raw industries might be prospected. Show this fact by changing the string
				 * In Editor, you just build, while ingame, or you fund or you prospect */
				if (_game_mode == GM_EDITOR) {
					/* We've chosen many random industries but no industries have been specified */
					SetDParam(0, STR_FUND_INDUSTRY_BUILD_NEW_INDUSTRY);
				} else {
					const IndustrySpec *indsp = GetIndustrySpec(this->index[this->selected_index]);
					SetDParam(0, (_settings_game.construction.raw_industry_construction == 2 && indsp->IsRawIndustry()) ? STR_FUND_INDUSTRY_PROSPECT_NEW_INDUSTRY : STR_FUND_INDUSTRY_FUND_NEW_INDUSTRY);
				}
				break;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_DPI_MATRIX_WIDGET:
				for (byte i = 0; i < this->vscroll->GetCapacity() && i + this->vscroll->GetPosition() < this->count; i++) {
					int x = r.left + WD_MATRIX_LEFT;
					int y = r.top + WD_MATRIX_TOP + i * this->resize.step_height;
					bool selected = this->selected_index == i + this->vscroll->GetPosition();

					if (this->index[i + this->vscroll->GetPosition()] == INVALID_INDUSTRYTYPE) {
						DrawString(x + MATRIX_TEXT_OFFSET, r.right - WD_MATRIX_RIGHT, y, STR_FUND_INDUSTRY_MANY_RANDOM_INDUSTRIES, selected ? TC_WHITE : TC_ORANGE);
						continue;
					}
					const IndustrySpec *indsp = GetIndustrySpec(this->index[i + this->vscroll->GetPosition()]);

					/* Draw the name of the industry in white is selected, otherwise, in orange */
					DrawString(x + MATRIX_TEXT_OFFSET, r.right - WD_MATRIX_RIGHT, y, indsp->name, selected ? TC_WHITE : TC_ORANGE);
					GfxFillRect(x,     y + 1,  x + 10, y + 7, selected ? PC_WHITE : PC_BLACK);
					GfxFillRect(x + 1, y + 2,  x +  9, y + 6, indsp->map_colour);
				}
				break;

			case WID_DPI_INFOPANEL: {
				int y      = r.top    + WD_FRAMERECT_TOP;
				int bottom = r.bottom - WD_FRAMERECT_BOTTOM;
				int left   = r.left   + WD_FRAMERECT_LEFT;
				int right  = r.right  - WD_FRAMERECT_RIGHT;

				if (this->selected_type == INVALID_INDUSTRYTYPE) {
					DrawStringMultiLine(left, right, y,  bottom, STR_FUND_INDUSTRY_MANY_RANDOM_INDUSTRIES_TOOLTIP);
					break;
				}

				const IndustrySpec *indsp = GetIndustrySpec(this->selected_type);

				if (_game_mode != GM_EDITOR) {
					SetDParam(0, indsp->GetConstructionCost());
					DrawString(left, right, y, STR_FUND_INDUSTRY_INDUSTRY_BUILD_COST);
					y += FONT_HEIGHT_NORMAL;
				}

				/* Draw the accepted cargoes, if any. Otherwise, will print "Nothing". */
				char cargo_suffix[3][512];
				GetAllCargoSuffixes(0, CST_FUND, NULL, this->selected_type, indsp, indsp->accepts_cargo, cargo_suffix);
				StringID str = STR_INDUSTRY_VIEW_REQUIRES_CARGO;
				byte p = 0;
				SetDParam(0, STR_JUST_NOTHING);
				SetDParamStr(1, "");
				for (byte j = 0; j < lengthof(indsp->accepts_cargo); j++) {
					if (indsp->accepts_cargo[j] == CT_INVALID) continue;
					if (p > 0) str++;
					SetDParam(p++, CargoSpec::Get(indsp->accepts_cargo[j])->name);
					SetDParamStr(p++, cargo_suffix[j]);
				}
				DrawString(left, right, y, str);
				y += FONT_HEIGHT_NORMAL;

				/* Draw the produced cargoes, if any. Otherwise, will print "Nothing". */
				GetAllCargoSuffixes(3, CST_FUND, NULL, this->selected_type, indsp, indsp->produced_cargo, cargo_suffix);
				str = STR_INDUSTRY_VIEW_PRODUCES_CARGO;
				p = 0;
				SetDParam(0, STR_JUST_NOTHING);
				SetDParamStr(1, "");
				for (byte j = 0; j < lengthof(indsp->produced_cargo); j++) {
					if (indsp->produced_cargo[j] == CT_INVALID) continue;
					if (p > 0) str++;
					SetDParam(p++, CargoSpec::Get(indsp->produced_cargo[j])->name);
					SetDParamStr(p++, cargo_suffix[j]);
				}
				DrawString(left, right, y, str);
				y += FONT_HEIGHT_NORMAL;

				/* Get the additional purchase info text, if it has not already been queried. */
				str = STR_NULL;
				if (HasBit(indsp->callback_mask, CBM_IND_FUND_MORE_TEXT)) {
					uint16 callback_res = GetIndustryCallback(CBID_INDUSTRY_FUND_MORE_TEXT, 0, 0, NULL, this->selected_type, INVALID_TILE);
					if (callback_res != CALLBACK_FAILED && callback_res != 0x400) {
						if (callback_res > 0x400) {
							ErrorUnknownCallbackResult(indsp->grf_prop.grffile->grfid, CBID_INDUSTRY_FUND_MORE_TEXT, callback_res);
						} else {
							str = GetGRFStringID(indsp->grf_prop.grffile->grfid, 0xD000 + callback_res);  // No. here's the new string
							if (str != STR_UNDEFINED) {
								StartTextRefStackUsage(6);
								DrawStringMultiLine(left, right, y, bottom, str, TC_YELLOW);
								StopTextRefStackUsage();
							}
						}
					}
				}
				break;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_DPI_MATRIX_WIDGET: {
				int y = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_DPI_MATRIX_WIDGET);
				if (y < this->count) { // Is it within the boundaries of available data?
					this->selected_index = y;
					this->selected_type = this->index[y];
					const IndustrySpec *indsp = (this->selected_type == INVALID_INDUSTRYTYPE) ? NULL : GetIndustrySpec(this->selected_type);

					this->SetDirty();

					if (_thd.GetCallbackWnd() == this &&
							((_game_mode != GM_EDITOR && _settings_game.construction.raw_industry_construction == 2 && indsp != NULL && indsp->IsRawIndustry()) ||
							this->selected_type == INVALID_INDUSTRYTYPE ||
							!this->enabled[this->selected_index])) {
						/* Reset the button state if going to prospecting or "build many industries" */
						this->RaiseButtons();
						ResetObjectToPlace();
					}

					this->SetButtons();
					if (this->enabled[this->selected_index] && click_count > 1) this->OnClick(pt, WID_DPI_FUND_WIDGET, 1);
				}
				break;
			}

			case WID_DPI_DISPLAY_WIDGET:
				if (this->selected_type != INVALID_INDUSTRYTYPE) ShowIndustryCargoesWindow(this->selected_type);
				break;

			case WID_DPI_FUND_WIDGET: {
				if (this->selected_type == INVALID_INDUSTRYTYPE) {
					this->HandleButtonClick(WID_DPI_FUND_WIDGET);

					if (Town::GetNumItems() == 0) {
						ShowErrorMessage(STR_ERROR_CAN_T_GENERATE_INDUSTRIES, STR_ERROR_MUST_FOUND_TOWN_FIRST, WL_INFO);
					} else {
						extern void GenerateIndustries();
						_generating_world = true;
						GenerateIndustries();
						_generating_world = false;
					}
				} else if (_game_mode != GM_EDITOR && _settings_game.construction.raw_industry_construction == 2 && GetIndustrySpec(this->selected_type)->IsRawIndustry()) {
					DoCommandP(0, this->selected_type, InteractiveRandom(), CMD_BUILD_INDUSTRY | CMD_MSG(STR_ERROR_CAN_T_CONSTRUCT_THIS_INDUSTRY));
					this->HandleButtonClick(WID_DPI_FUND_WIDGET);
				} else {
					HandlePlacePushButton(this, WID_DPI_FUND_WIDGET, SPR_CURSOR_INDUSTRY, HT_RECT);
				}
				break;
			}
		}
	}

	virtual void OnResize()
	{
		/* Adjust the number of items in the matrix depending of the resize */
		this->vscroll->SetCapacityFromWidget(this, WID_DPI_MATRIX_WIDGET);
		this->GetWidget<NWidgetCore>(WID_DPI_MATRIX_WIDGET)->widget_data = (this->vscroll->GetCapacity() << MAT_ROW_START) + (1 << MAT_COL_START);
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		bool success = true;
		/* We do not need to protect ourselves against "Random Many Industries" in this mode */
		const IndustrySpec *indsp = GetIndustrySpec(this->selected_type);
		uint32 seed = InteractiveRandom();

		if (_game_mode == GM_EDITOR) {
			/* Show error if no town exists at all */
			if (Town::GetNumItems() == 0) {
				SetDParam(0, indsp->name);
				ShowErrorMessage(STR_ERROR_CAN_T_BUILD_HERE, STR_ERROR_MUST_FOUND_TOWN_FIRST, WL_INFO, pt.x, pt.y);
				return;
			}

			Backup<CompanyByte> cur_company(_current_company, OWNER_NONE, FILE_LINE);
			_generating_world = true;
			_ignore_restrictions = true;

			DoCommandP(tile, (InteractiveRandomRange(indsp->num_table) << 8) | this->selected_type, seed,
					CMD_BUILD_INDUSTRY | CMD_MSG(STR_ERROR_CAN_T_CONSTRUCT_THIS_INDUSTRY), &CcBuildIndustry);

			cur_company.Restore();
			_ignore_restrictions = false;
			_generating_world = false;
		} else {
			success = DoCommandP(tile, (InteractiveRandomRange(indsp->num_table) << 8) | this->selected_type, seed, CMD_BUILD_INDUSTRY | CMD_MSG(STR_ERROR_CAN_T_CONSTRUCT_THIS_INDUSTRY));
		}

		/* If an industry has been built, just reset the cursor and the system */
		if (success && !_settings_client.gui.persistent_buildingtools) ResetObjectToPlace();
	}

	virtual void OnTick()
	{
		if (_pause_mode != PM_UNPAUSED) return;
		if (!this->timer_enabled) return;
		if (--this->callback_timer == 0) {
			/* We have just passed another day.
			 * See if we need to update availability of currently selected industry */
			this->callback_timer = DAY_TICKS; // restart counter

			const IndustrySpec *indsp = GetIndustrySpec(this->selected_type);

			if (indsp->enabled) {
				bool call_back_result = GetIndustryProbabilityCallback(this->selected_type, IACT_USERCREATION, 1) > 0;

				/* Only if result does match the previous state would it require a redraw. */
				if (call_back_result != this->enabled[this->selected_index]) {
					this->enabled[this->selected_index] = call_back_result;
					this->SetButtons();
					this->SetDirty();
				}
			}
		}
	}

	virtual void OnTimeout()
	{
		this->RaiseButtons();
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		this->SetupArrays();

		const IndustrySpec *indsp = (this->selected_type == INVALID_INDUSTRYTYPE) ? NULL : GetIndustrySpec(this->selected_type);
		if (indsp == NULL) this->enabled[this->selected_index] = _settings_game.difficulty.industry_density != ID_FUND_ONLY;
		this->SetButtons();
	}
};

void ShowBuildIndustryWindow()
{
	if (_game_mode != GM_EDITOR && !Company::IsValidID(_local_company)) return;
	if (BringWindowToFrontById(WC_BUILD_INDUSTRY, 0)) return;
	new BuildIndustryWindow();
}

static void UpdateIndustryProduction(Industry *i);

static inline bool IsProductionAlterable(const Industry *i)
{
	const IndustrySpec *is = GetIndustrySpec(i->type);
	return ((_game_mode == GM_EDITOR || _cheats.setup_prod.value) &&
			(is->production_rate[0] != 0 || is->production_rate[1] != 0 || is->IsRawIndustry()));
}

class IndustryViewWindow : public Window
{
	/** Modes for changing production */
	enum Editability {
		EA_NONE,              ///< Not alterable
		EA_MULTIPLIER,        ///< Allow changing the production multiplier
		EA_RATE,              ///< Allow changing the production rates
	};

	/** Specific lines in the info panel */
	enum InfoLine {
		IL_NONE,              ///< No line
		IL_MULTIPLIER,        ///< Production multiplier
		IL_RATE1,             ///< Production rate of cargo 1
		IL_RATE2,             ///< Production rate of cargo 2
	};

	Editability editable;     ///< Mode for changing production
	InfoLine editbox_line;    ///< The line clicked to open the edit box
	InfoLine clicked_line;    ///< The line of the button that has been clicked
	byte clicked_button;      ///< The button that has been clicked (to raise)
	int production_offset_y;  ///< The offset of the production texts/buttons
	int info_height;          ///< Height needed for the #WID_IV_INFO panel

public:
	IndustryViewWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->flags |= WF_DISABLE_VP_SCROLL;
		this->editbox_line = IL_NONE;
		this->clicked_line = IL_NONE;
		this->clicked_button = 0;
		this->info_height = WD_FRAMERECT_TOP + 2 * FONT_HEIGHT_NORMAL + WD_FRAMERECT_BOTTOM + 1; // Info panel has at least two lines text.

		this->InitNested(desc, window_number);
		NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_IV_VIEWPORT);
		nvp->InitializeViewport(this, Industry::Get(window_number)->location.GetCenterTile(), ZOOM_LVL_INDUSTRY);

		this->InvalidateData();
	}

	virtual void OnPaint()
	{
		this->DrawWidgets();

		if (this->IsShaded()) return; // Don't draw anything when the window is shaded.

		NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_IV_INFO);
		uint expected = this->DrawInfo(nwi->pos_x, nwi->pos_x + nwi->current_x - 1, nwi->pos_y) - nwi->pos_y;
		if (expected > nwi->current_y - 1) {
			this->info_height = expected + 1;
			this->ReInit();
			return;
		}
	}

	/**
	 * Draw the text in the #WID_IV_INFO panel.
	 * @param left  Left edge of the panel.
	 * @param right Right edge of the panel.
	 * @param top   Top edge of the panel.
	 * @return Expected position of the bottom edge of the panel.
	 */
	int DrawInfo(uint left, uint right, uint top)
	{
		Industry *i = Industry::Get(this->window_number);
		const IndustrySpec *ind = GetIndustrySpec(i->type);
		int y = top + WD_FRAMERECT_TOP;
		bool first = true;
		bool has_accept = false;
		char cargo_suffix[3][512];

		if (HasBit(ind->callback_mask, CBM_IND_PRODUCTION_CARGO_ARRIVAL) || HasBit(ind->callback_mask, CBM_IND_PRODUCTION_256_TICKS)) {
			GetAllCargoSuffixes(0, CST_VIEW, i, i->type, ind, i->accepts_cargo, cargo_suffix);
			for (byte j = 0; j < lengthof(i->accepts_cargo); j++) {
				if (i->accepts_cargo[j] == CT_INVALID) continue;
				has_accept = true;
				if (first) {
					DrawString(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y, STR_INDUSTRY_VIEW_WAITING_FOR_PROCESSING);
					y += FONT_HEIGHT_NORMAL;
					first = false;
				}
				SetDParam(0, i->accepts_cargo[j]);
				SetDParam(1, i->incoming_cargo_waiting[j]);
				SetDParamStr(2, cargo_suffix[j]);
				DrawString(left + WD_FRAMETEXT_LEFT, right - WD_FRAMERECT_RIGHT, y, STR_INDUSTRY_VIEW_WAITING_STOCKPILE_CARGO);
				y += FONT_HEIGHT_NORMAL;
			}
		} else {
			GetAllCargoSuffixes(0, CST_VIEW, i, i->type, ind, i->accepts_cargo, cargo_suffix);
			StringID str = STR_INDUSTRY_VIEW_REQUIRES_CARGO;
			byte p = 0;
			for (byte j = 0; j < lengthof(i->accepts_cargo); j++) {
				if (i->accepts_cargo[j] == CT_INVALID) continue;
				has_accept = true;
				if (p > 0) str++;
				SetDParam(p++, CargoSpec::Get(i->accepts_cargo[j])->name);
				SetDParamStr(p++, cargo_suffix[j]);
			}
			if (has_accept) {
				DrawString(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y, str);
				y += FONT_HEIGHT_NORMAL;
			}
		}

		GetAllCargoSuffixes(3, CST_VIEW, i, i->type, ind, i->produced_cargo, cargo_suffix);
		first = true;
		for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
			if (i->produced_cargo[j] == CT_INVALID) continue;
			if (first) {
				if (has_accept) y += WD_PAR_VSEP_WIDE;
				DrawString(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y, STR_INDUSTRY_VIEW_PRODUCTION_LAST_MONTH_TITLE);
				y += FONT_HEIGHT_NORMAL;
				if (this->editable == EA_RATE) this->production_offset_y = y;
				first = false;
			}

			SetDParam(0, i->produced_cargo[j]);
			SetDParam(1, i->last_month_production[j]);
			SetDParamStr(2, cargo_suffix[j]);
			SetDParam(3, ToPercent8(i->last_month_pct_transported[j]));
			uint x = left + WD_FRAMETEXT_LEFT + (this->editable == EA_RATE ? 30 : 0);
			DrawString(x, right - WD_FRAMERECT_RIGHT, y, STR_INDUSTRY_VIEW_TRANSPORTED);
			/* Let's put out those buttons.. */
			if (this->editable == EA_RATE) {
				DrawArrowButtons(left + WD_FRAMETEXT_LEFT, y, COLOUR_YELLOW, (this->clicked_line == IL_RATE1 + j) ? this->clicked_button : 0,
						i->production_rate[j] > 0, i->production_rate[j] < 255);
			}
			y += FONT_HEIGHT_NORMAL;
		}

		/* Display production multiplier if editable */
		if (this->editable == EA_MULTIPLIER) {
			y += WD_PAR_VSEP_WIDE;
			this->production_offset_y = y;
			SetDParam(0, RoundDivSU(i->prod_level * 100, PRODLEVEL_DEFAULT));
			uint x = left + WD_FRAMETEXT_LEFT + 30;
			DrawString(x, right - WD_FRAMERECT_RIGHT, y, STR_INDUSTRY_VIEW_PRODUCTION_LEVEL);
			DrawArrowButtons(left + WD_FRAMETEXT_LEFT, y, COLOUR_YELLOW, (this->clicked_line == IL_MULTIPLIER) ? this->clicked_button : 0,
					i->prod_level > PRODLEVEL_MINIMUM, i->prod_level < PRODLEVEL_MAXIMUM);
			y += FONT_HEIGHT_NORMAL;
		}

		/* Get the extra message for the GUI */
		if (HasBit(ind->callback_mask, CBM_IND_WINDOW_MORE_TEXT)) {
			uint16 callback_res = GetIndustryCallback(CBID_INDUSTRY_WINDOW_MORE_TEXT, 0, 0, i, i->type, i->location.tile);
			if (callback_res != CALLBACK_FAILED && callback_res != 0x400) {
				if (callback_res > 0x400) {
					ErrorUnknownCallbackResult(ind->grf_prop.grffile->grfid, CBID_INDUSTRY_WINDOW_MORE_TEXT, callback_res);
				} else {
					StringID message = GetGRFStringID(ind->grf_prop.grffile->grfid, 0xD000 + callback_res);
					if (message != STR_NULL && message != STR_UNDEFINED) {
						y += WD_PAR_VSEP_WIDE;

						StartTextRefStackUsage(6);
						/* Use all the available space left from where we stand up to the
						 * end of the window. We ALSO enlarge the window if needed, so we
						 * can 'go' wild with the bottom of the window. */
						y = DrawStringMultiLine(left + WD_FRAMERECT_LEFT, right - WD_FRAMERECT_RIGHT, y, UINT16_MAX, message, TC_BLACK);
						StopTextRefStackUsage();
					}
				}
			}
		}
		return y + WD_FRAMERECT_BOTTOM;
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == WID_IV_CAPTION) SetDParam(0, this->window_number);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (widget == WID_IV_INFO) size->height = this->info_height;
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_IV_INFO: {
				Industry *i = Industry::Get(this->window_number);
				InfoLine line = IL_NONE;

				switch (this->editable) {
					case EA_NONE: break;

					case EA_MULTIPLIER:
						if (IsInsideBS(pt.y, this->production_offset_y, FONT_HEIGHT_NORMAL)) line = IL_MULTIPLIER;
						break;

					case EA_RATE:
						if (pt.y >= this->production_offset_y) {
							int row = (pt.y - this->production_offset_y) / FONT_HEIGHT_NORMAL;
							for (uint j = 0; j < lengthof(i->produced_cargo); j++) {
								if (i->produced_cargo[j] == CT_INVALID) continue;
								row--;
								if (row < 0) {
									line = (InfoLine)(IL_RATE1 + j);
									break;
								}
							}
						}
						break;
				}
				if (line == IL_NONE) return;

				NWidgetBase *nwi = this->GetWidget<NWidgetBase>(widget);
				int left = nwi->pos_x + WD_FRAMETEXT_LEFT;
				int right = nwi->pos_x + nwi->current_x - 1 - WD_FRAMERECT_RIGHT;
				if (IsInsideMM(pt.x, left, left + 20)) {
					/* Clicked buttons, decrease or increase production */
					byte button = (pt.x < left + 10) ? 1 : 2;
					switch (this->editable) {
						case EA_MULTIPLIER:
							if (button == 1) {
								if (i->prod_level <= PRODLEVEL_MINIMUM) return;
								i->prod_level = max<uint>(i->prod_level / 2, PRODLEVEL_MINIMUM);
							} else {
								if (i->prod_level >= PRODLEVEL_MAXIMUM) return;
								i->prod_level = minu(i->prod_level * 2, PRODLEVEL_MAXIMUM);
							}
							break;

						case EA_RATE:
							if (button == 1) {
								if (i->production_rate[line - IL_RATE1] <= 0) return;
								i->production_rate[line - IL_RATE1] = max(i->production_rate[line - IL_RATE1] / 2, 0);
							} else {
								if (i->production_rate[line - IL_RATE1] >= 255) return;
								/* a zero production industry is unlikely to give anything but zero, so push it a little bit */
								int new_prod = i->production_rate[line - IL_RATE1] == 0 ? 1 : i->production_rate[line - IL_RATE1] * 2;
								i->production_rate[line - IL_RATE1] = minu(new_prod, 255);
							}
							break;

						default: NOT_REACHED();
					}

					UpdateIndustryProduction(i);
					this->SetDirty();
					this->SetTimeout();
					this->clicked_line = line;
					this->clicked_button = button;
				} else if (IsInsideMM(pt.x, left + 30, right)) {
					/* clicked the text */
					this->editbox_line = line;
					switch (this->editable) {
						case EA_MULTIPLIER:
							SetDParam(0, RoundDivSU(i->prod_level * 100, PRODLEVEL_DEFAULT));
							ShowQueryString(STR_JUST_INT, STR_CONFIG_GAME_PRODUCTION_LEVEL, 10, this, CS_ALPHANUMERAL, QSF_NONE);
							break;

						case EA_RATE:
							SetDParam(0, i->production_rate[line - IL_RATE1] * 8);
							ShowQueryString(STR_JUST_INT, STR_CONFIG_GAME_PRODUCTION, 10, this, CS_ALPHANUMERAL, QSF_NONE);
							break;

						default: NOT_REACHED();
					}
				}
				break;
			}

			case WID_IV_GOTO: {
				Industry *i = Industry::Get(this->window_number);
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(i->location.GetCenterTile());
				} else {
					ScrollMainWindowToTile(i->location.GetCenterTile());
				}
				break;
			}

			case WID_IV_DISPLAY: {
				Industry *i = Industry::Get(this->window_number);
				ShowIndustryCargoesWindow(i->type);
				break;
			}
		}
	}

	virtual void OnTimeout()
	{
		this->clicked_line = IL_NONE;
		this->clicked_button = 0;
		this->SetDirty();
	}

	virtual void OnResize()
	{
		if (this->viewport != NULL) {
			NWidgetViewport *nvp = this->GetWidget<NWidgetViewport>(WID_IV_VIEWPORT);
			nvp->UpdateViewportCoordinates(this);

			ScrollWindowToTile(Industry::Get(this->window_number)->location.GetCenterTile(), this, true); // Re-center viewport.
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (StrEmpty(str)) return;

		Industry *i = Industry::Get(this->window_number);
		uint value = atoi(str);
		switch (this->editbox_line) {
			case IL_NONE: NOT_REACHED();

			case IL_MULTIPLIER:
				i->prod_level = ClampU(RoundDivSU(value * PRODLEVEL_DEFAULT, 100), PRODLEVEL_MINIMUM, PRODLEVEL_MAXIMUM);
				break;

			default:
				i->production_rate[this->editbox_line - IL_RATE1] = ClampU(RoundDivSU(value, 8), 0, 255);
				break;
		}
		UpdateIndustryProduction(i);
		this->SetDirty();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		const Industry *i = Industry::Get(this->window_number);
		if (IsProductionAlterable(i)) {
			const IndustrySpec *ind = GetIndustrySpec(i->type);
			this->editable = ind->UsesSmoothEconomy() ? EA_RATE : EA_MULTIPLIER;
		} else {
			this->editable = EA_NONE;
		}
	}

	virtual bool IsNewGRFInspectable() const
	{
		return ::IsNewGRFInspectable(GSF_INDUSTRIES, this->window_number);
	}

	virtual void ShowNewGRFInspectWindow() const
	{
		::ShowNewGRFInspectWindow(GSF_INDUSTRIES, this->window_number);
	}
};

static void UpdateIndustryProduction(Industry *i)
{
	const IndustrySpec *indspec = GetIndustrySpec(i->type);
	if (!indspec->UsesSmoothEconomy()) i->RecomputeProductionMultipliers();

	for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
		if (i->produced_cargo[j] != CT_INVALID) {
			i->last_month_production[j] = 8 * i->production_rate[j];
		}
	}
}

/** Widget definition of the view industry gui */
static const NWidgetPart _nested_industry_view_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_CREAM),
		NWidget(WWT_CAPTION, COLOUR_CREAM, WID_IV_CAPTION), SetDataTip(STR_INDUSTRY_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_DEBUGBOX, COLOUR_CREAM),
		NWidget(WWT_SHADEBOX, COLOUR_CREAM),
		NWidget(WWT_STICKYBOX, COLOUR_CREAM),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_CREAM),
		NWidget(WWT_INSET, COLOUR_CREAM), SetPadding(2, 2, 2, 2),
			NWidget(NWID_VIEWPORT, INVALID_COLOUR, WID_IV_VIEWPORT), SetMinimalSize(254, 86), SetFill(1, 0), SetPadding(1, 1, 1, 1), SetResize(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_CREAM, WID_IV_INFO), SetMinimalSize(260, 2), SetResize(1, 0),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PUSHTXTBTN, COLOUR_CREAM, WID_IV_GOTO), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_BUTTON_LOCATION, STR_INDUSTRY_VIEW_LOCATION_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_CREAM, WID_IV_DISPLAY), SetFill(1, 0), SetResize(1, 0), SetDataTip(STR_INDUSTRY_DISPLAY_CHAIN, STR_INDUSTRY_DISPLAY_CHAIN_TOOLTIP),
		NWidget(WWT_RESIZEBOX, COLOUR_CREAM),
	EndContainer(),
};

/** Window definition of the view industry gui */
static const WindowDesc _industry_view_desc(
	WDP_AUTO, 260, 120,
	WC_INDUSTRY_VIEW, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_industry_view_widgets, lengthof(_nested_industry_view_widgets)
);

void ShowIndustryViewWindow(int industry)
{
	AllocateWindowDescFront<IndustryViewWindow>(&_industry_view_desc, industry);
}

/** Widget definition of the industry directory gui */
static const NWidgetPart _nested_industry_directory_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN), SetDataTip(STR_INDUSTRY_DIRECTORY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_ID_DROPDOWN_ORDER), SetDataTip(STR_BUTTON_SORT_BY, STR_TOOLTIP_SORT_ORDER),
				NWidget(WWT_DROPDOWN, COLOUR_BROWN, WID_ID_DROPDOWN_CRITERIA), SetDataTip(STR_JUST_STRING, STR_TOOLTIP_SORT_CRITERIA),
				NWidget(WWT_PANEL, COLOUR_BROWN), SetResize(1, 0), EndContainer(),
			EndContainer(),
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_ID_INDUSTRY_LIST), SetDataTip(0x0, STR_INDUSTRY_DIRECTORY_LIST_CAPTION), SetResize(1, 1), SetScrollbar(WID_ID_SCROLLBAR), EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_ID_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
		EndContainer(),
	EndContainer(),
};

typedef GUIList<const Industry*> GUIIndustryList;


/**
 * The list of industries.
 */
class IndustryDirectoryWindow : public Window {
protected:
	/* Runtime saved values */
	static Listing last_sorting;
	static const Industry *last_industry;

	/* Constants for sorting stations */
	static const StringID sorter_names[];
	static GUIIndustryList::SortFunction * const sorter_funcs[];

	GUIIndustryList industries;
	Scrollbar *vscroll;

	/** (Re)Build industries list */
	void BuildSortIndustriesList()
	{
		if (this->industries.NeedRebuild()) {
			this->industries.Clear();

			const Industry *i;
			FOR_ALL_INDUSTRIES(i) {
				*this->industries.Append() = i;
			}

			this->industries.Compact();
			this->industries.RebuildDone();
			this->vscroll->SetCount(this->industries.Length()); // Update scrollbar as well.
		}

		if (!this->industries.Sort()) return;
		IndustryDirectoryWindow::last_industry = NULL; // Reset name sorter sort cache
		this->SetWidgetDirty(WID_ID_INDUSTRY_LIST); // Set the modified widget dirty
	}

	/**
	 * Returns percents of cargo transported if industry produces this cargo, else -1
	 *
	 * @param i industry to check
	 * @param id cargo slot
	 * @return percents of cargo transported, or -1 if industry doesn't use this cargo slot
	 */
	static inline int GetCargoTransportedPercentsIfValid(const Industry *i, uint id)
	{
		assert(id < lengthof(i->produced_cargo));

		if (i->produced_cargo[id] == CT_INVALID) return 101;
		return ToPercent8(i->last_month_pct_transported[id]);
	}

	/**
	 * Returns value representing industry's transported cargo
	 *  percentage for industry sorting
	 *
	 * @param i industry to check
	 * @return value used for sorting
	 */
	static int GetCargoTransportedSortValue(const Industry *i)
	{
		int p1 = GetCargoTransportedPercentsIfValid(i, 0);
		int p2 = GetCargoTransportedPercentsIfValid(i, 1);

		if (p1 > p2) Swap(p1, p2); // lower value has higher priority

		return (p1 << 8) + p2;
	}

	/** Sort industries by name */
	static int CDECL IndustryNameSorter(const Industry * const *a, const Industry * const *b)
	{
		static char buf_cache[96];
		static char buf[96];

		SetDParam(0, (*a)->index);
		GetString(buf, STR_INDUSTRY_NAME, lastof(buf));

		if (*b != last_industry) {
			last_industry = *b;
			SetDParam(0, (*b)->index);
			GetString(buf_cache, STR_INDUSTRY_NAME, lastof(buf_cache));
		}

		return strnatcmp(buf, buf_cache); // Sort by name (natural sorting).
	}

	/** Sort industries by type and name */
	static int CDECL IndustryTypeSorter(const Industry * const *a, const Industry * const *b)
	{
		int it_a = 0;
		while (it_a != NUM_INDUSTRYTYPES && (*a)->type != _sorted_industry_types[it_a]) it_a++;
		int it_b = 0;
		while (it_b != NUM_INDUSTRYTYPES && (*b)->type != _sorted_industry_types[it_b]) it_b++;
		int r = it_a - it_b;
		return (r == 0) ? IndustryNameSorter(a, b) : r;
	}

	/** Sort industries by production and name */
	static int CDECL IndustryProductionSorter(const Industry * const *a, const Industry * const *b)
	{
		uint prod_a = 0, prod_b = 0;
		for (uint i = 0; i < lengthof((*a)->produced_cargo); i++) {
			if ((*a)->produced_cargo[i] != CT_INVALID) prod_a += (*a)->last_month_production[i];
			if ((*b)->produced_cargo[i] != CT_INVALID) prod_b += (*b)->last_month_production[i];
		}
		int r = prod_a - prod_b;

		return (r == 0) ? IndustryTypeSorter(a, b) : r;
	}

	/** Sort industries by transported cargo and name */
	static int CDECL IndustryTransportedCargoSorter(const Industry * const *a, const Industry * const *b)
	{
		int r = GetCargoTransportedSortValue(*a) - GetCargoTransportedSortValue(*b);
		return (r == 0) ? IndustryNameSorter(a, b) : r;
	}

	/**
	 * Get the StringID to draw and set the appropriate DParams.
	 * @param i the industry to get the StringID of.
	 * @return the StringID.
	 */
	StringID GetIndustryString(const Industry *i) const
	{
		const IndustrySpec *indsp = GetIndustrySpec(i->type);
		byte p = 0;

		/* Industry name */
		SetDParam(p++, i->index);

		static char cargo_suffix[lengthof(i->produced_cargo)][512];
		GetAllCargoSuffixes(3, CST_DIR, i, i->type, indsp, i->produced_cargo, cargo_suffix);

		/* Industry productions */
		for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
			if (i->produced_cargo[j] == CT_INVALID) continue;
			SetDParam(p++, i->produced_cargo[j]);
			SetDParam(p++, i->last_month_production[j]);
			SetDParamStr(p++, cargo_suffix[j]);
		}

		/* Transported productions */
		for (byte j = 0; j < lengthof(i->produced_cargo); j++) {
			if (i->produced_cargo[j] == CT_INVALID) continue;
			SetDParam(p++, ToPercent8(i->last_month_pct_transported[j]));
		}

		/* Drawing the right string */
		switch (p) {
			case 1:  return STR_INDUSTRY_DIRECTORY_ITEM_NOPROD;
			case 5:  return STR_INDUSTRY_DIRECTORY_ITEM;
			default: return STR_INDUSTRY_DIRECTORY_ITEM_TWO;
		}
	}

public:
	IndustryDirectoryWindow(const WindowDesc *desc, WindowNumber number) : Window()
	{
		this->CreateNestedTree(desc);
		this->vscroll = this->GetScrollbar(WID_ID_SCROLLBAR);

		this->industries.SetListing(this->last_sorting);
		this->industries.SetSortFuncs(IndustryDirectoryWindow::sorter_funcs);
		this->industries.ForceRebuild();
		this->BuildSortIndustriesList();

		this->FinishInitNested(desc, 0);
	}

	~IndustryDirectoryWindow()
	{
		this->last_sorting = this->industries.GetListing();
	}

	virtual void SetStringParameters(int widget) const
	{
		if (widget == WID_ID_DROPDOWN_CRITERIA) SetDParam(0, IndustryDirectoryWindow::sorter_names[this->industries.SortType()]);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_ID_DROPDOWN_ORDER:
				this->DrawSortButtonState(widget, this->industries.IsDescSortOrder() ? SBS_DOWN : SBS_UP);
				break;

			case WID_ID_INDUSTRY_LIST: {
				int n = 0;
				int y = r.top + WD_FRAMERECT_TOP;
				if (this->industries.Length() == 0) {
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, STR_INDUSTRY_DIRECTORY_NONE);
					break;
				}
				for (uint i = this->vscroll->GetPosition(); i < this->industries.Length(); i++) {
					DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, y, this->GetIndustryString(this->industries[i]));

					y += this->resize.step_height;
					if (++n == this->vscroll->GetCapacity()) break; // max number of industries in 1 window
				}
				break;
			}
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_ID_DROPDOWN_ORDER: {
				Dimension d = GetStringBoundingBox(this->GetWidget<NWidgetCore>(widget)->widget_data);
				d.width += padding.width + WD_SORTBUTTON_ARROW_WIDTH * 2; // Doubled since the string is centred and it also looks better.
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_ID_DROPDOWN_CRITERIA: {
				Dimension d = {0, 0};
				for (uint i = 0; IndustryDirectoryWindow::sorter_names[i] != INVALID_STRING_ID; i++) {
					d = maxdim(d, GetStringBoundingBox(IndustryDirectoryWindow::sorter_names[i]));
				}
				d.width += padding.width;
				d.height += padding.height;
				*size = maxdim(*size, d);
				break;
			}

			case WID_ID_INDUSTRY_LIST: {
				Dimension d = GetStringBoundingBox(STR_INDUSTRY_DIRECTORY_NONE);
				for (uint i = 0; i < this->industries.Length(); i++) {
					d = maxdim(d, GetStringBoundingBox(this->GetIndustryString(this->industries[i])));
				}
				resize->height = d.height;
				d.height *= 5;
				d.width += padding.width + WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				d.height += padding.height + WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				*size = maxdim(*size, d);
				break;
			}
		}
	}


	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_ID_DROPDOWN_ORDER:
				this->industries.ToggleSortOrder();
				this->SetDirty();
				break;

			case WID_ID_DROPDOWN_CRITERIA:
				ShowDropDownMenu(this, IndustryDirectoryWindow::sorter_names, this->industries.SortType(), WID_ID_DROPDOWN_CRITERIA, 0, 0);
				break;

			case WID_ID_INDUSTRY_LIST: {
				uint p = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_ID_INDUSTRY_LIST, WD_FRAMERECT_TOP);
				if (p < this->industries.Length()) {
					if (_ctrl_pressed) {
						ShowExtraViewPortWindow(this->industries[p]->location.tile);
					} else {
						ScrollMainWindowToTile(this->industries[p]->location.tile);
					}
				}
				break;
			}
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		if (this->industries.SortType() != index) {
			this->industries.SetSortType(index);
			this->BuildSortIndustriesList();
		}
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, WID_ID_INDUSTRY_LIST);
	}

	virtual void OnPaint()
	{
		if (this->industries.NeedRebuild()) this->BuildSortIndustriesList();
		this->DrawWidgets();
	}

	virtual void OnHundredthTick()
	{
		this->industries.ForceResort();
		this->BuildSortIndustriesList();
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
			this->industries.ForceRebuild();
		} else {
			this->industries.ForceResort();
		}
	}
};

Listing IndustryDirectoryWindow::last_sorting = {false, 0};
const Industry *IndustryDirectoryWindow::last_industry = NULL;

/* Available station sorting functions. */
GUIIndustryList::SortFunction * const IndustryDirectoryWindow::sorter_funcs[] = {
	&IndustryNameSorter,
	&IndustryTypeSorter,
	&IndustryProductionSorter,
	&IndustryTransportedCargoSorter
};

/* Names of the sorting functions */
const StringID IndustryDirectoryWindow::sorter_names[] = {
	STR_SORT_BY_NAME,
	STR_SORT_BY_TYPE,
	STR_SORT_BY_PRODUCTION,
	STR_SORT_BY_TRANSPORTED,
	INVALID_STRING_ID
};


/** Window definition of the industry directory gui */
static const WindowDesc _industry_directory_desc(
	WDP_AUTO, 428, 190,
	WC_INDUSTRY_DIRECTORY, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_industry_directory_widgets, lengthof(_nested_industry_directory_widgets)
);

void ShowIndustryDirectory()
{
	AllocateWindowDescFront<IndustryDirectoryWindow>(&_industry_directory_desc, 0);
}

/** Widgets of the industry cargoes window. */
static const NWidgetPart _nested_industry_cargoes_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_BROWN),
		NWidget(WWT_CAPTION, COLOUR_BROWN, WID_IC_CAPTION), SetDataTip(STR_INDUSTRY_CARGOES_INDUSTRY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_BROWN),
		NWidget(WWT_STICKYBOX, COLOUR_BROWN),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(NWID_VERTICAL),
			NWidget(WWT_PANEL, COLOUR_BROWN, WID_IC_PANEL), SetResize(1, 10), SetMinimalSize(200, 90), SetScrollbar(WID_IC_SCROLLBAR), EndContainer(),
			NWidget(NWID_HORIZONTAL),
				NWidget(WWT_TEXTBTN, COLOUR_BROWN, WID_IC_NOTIFY),
					SetDataTip(STR_INDUSTRY_CARGOES_NOTIFY_SMALLMAP, STR_INDUSTRY_CARGOES_NOTIFY_SMALLMAP_TOOLTIP),
				NWidget(WWT_PANEL, COLOUR_BROWN), SetFill(1, 0), SetResize(1, 0), EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_BROWN, WID_IC_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_BROWN),
		EndContainer(),
	EndContainer(),
};

/** Window description for the industry cargoes window. */
static const WindowDesc _industry_cargoes_desc(
	WDP_AUTO, 300, 210,
	WC_INDUSTRY_CARGOES, WC_NONE,
	0,
	_nested_industry_cargoes_widgets, lengthof(_nested_industry_cargoes_widgets)
);

/** Available types of field. */
enum CargoesFieldType {
	CFT_EMPTY,       ///< Empty field.
	CFT_SMALL_EMPTY, ///< Empty small field (for the header).
	CFT_INDUSTRY,    ///< Display industry.
	CFT_CARGO,       ///< Display cargo connections.
	CFT_CARGO_LABEL, ///< Display cargo labels.
	CFT_HEADER,      ///< Header text.
};

static const uint MAX_CARGOES = 3; ///< Maximum number of cargoes carried in a #CFT_CARGO field in #CargoesField.

/** Data about a single field in the #IndustryCargoesWindow panel. */
struct CargoesField {
	static const int VERT_INTER_INDUSTRY_SPACE;
	static const int HOR_CARGO_BORDER_SPACE;
	static const int CARGO_STUB_WIDTH;
	static const int HOR_CARGO_WIDTH, HOR_CARGO_SPACE;
	static const int CARGO_FIELD_WIDTH;
	static const int VERT_CARGO_SPACE, VERT_CARGO_EDGE;
	static const int BLOB_DISTANCE, BLOB_WIDTH, BLOB_HEIGHT;

	static const int INDUSTRY_LINE_COLOUR;
	static const int CARGO_LINE_COLOUR;

	static int small_height, normal_height;
	static int industry_width;

	CargoesFieldType type; ///< Type of field.
	union {
		struct {
			IndustryType ind_type;                 ///< Industry type (#NUM_INDUSTRYTYPES means 'houses').
			CargoID other_produced[MAX_CARGOES];   ///< Cargoes produced but not used in this figure.
			CargoID other_accepted[MAX_CARGOES];   ///< Cargoes accepted but not used in this figure.
		} industry; ///< Industry data (for #CFT_INDUSTRY).
		struct {
			CargoID vertical_cargoes[MAX_CARGOES]; ///< Cargoes running from top to bottom (cargo ID or #INVALID_CARGO).
			byte num_cargoes;                      ///< Number of cargoes.
			CargoID supp_cargoes[MAX_CARGOES];     ///< Cargoes entering from the left (index in #vertical_cargoes, or #INVALID_CARGO).
			byte top_end;                          ///< Stop at the top of the vertical cargoes.
			CargoID cust_cargoes[MAX_CARGOES];     ///< Cargoes leaving to the right (index in #vertical_cargoes, or #INVALID_CARGO).
			byte bottom_end;                       ///< Stop at the bottom of the vertical cargoes.
		} cargo; ///< Cargo data (for #CFT_CARGO).
		struct {
			CargoID cargoes[MAX_CARGOES];          ///< Cargoes to display (or #INVALID_CARGO).
			bool left_align;                       ///< Align all cargo texts to the left (else align to the right).
		} cargo_label;   ///< Label data (for #CFT_CARGO_LABEL).
		StringID header; ///< Header text (for #CFT_HEADER).
	} u; // Data for each type.

	/**
	 * Make one of the empty fields (#CFT_EMPTY or #CFT_SMALL_EMPTY).
	 * @param type Type of empty field.
	 */
	void MakeEmpty(CargoesFieldType type)
	{
		this->type = type;
	}

	/**
	 * Make an industry type field.
	 * @param ind_type Industry type (#NUM_INDUSTRYTYPES means 'houses').
	 * @note #other_accepted and #other_produced should be filled later.
	 */
	void MakeIndustry(IndustryType ind_type)
	{
		this->type = CFT_INDUSTRY;
		this->u.industry.ind_type = ind_type;
		MemSetT(this->u.industry.other_accepted, INVALID_CARGO, MAX_CARGOES);
		MemSetT(this->u.industry.other_produced, INVALID_CARGO, MAX_CARGOES);
	}

	/**
	 * Connect a cargo from an industry to the #CFT_CARGO column.
	 * @param cargo Cargo to connect.
	 * @param produced Cargo is produced (if \c false, cargo is assumed to be accepted).
	 * @return Horizontal connection index, or \c -1 if not accepted at all.
	 */
	int ConnectCargo(CargoID cargo, bool producer)
	{
		assert(this->type == CFT_CARGO);
		if (cargo == INVALID_CARGO) return -1;

		/* Find the vertical cargo column carrying the cargo. */
		int column = -1;
		for (int i = 0; i < this->u.cargo.num_cargoes; i++) {
			if (cargo == this->u.cargo.vertical_cargoes[i]) {
				column = i;
				break;
			}
		}
		if (column < 0) return -1;

		if (producer) {
			assert(this->u.cargo.supp_cargoes[column] == INVALID_CARGO);
			this->u.cargo.supp_cargoes[column]  = column;
		} else {
			assert(this->u.cargo.cust_cargoes[column] == INVALID_CARGO);
			this->u.cargo.cust_cargoes[column] = column;
		}
		return column;
	}

	/**
	 * Does this #CFT_CARGO field have a horizontal connection?
	 * @return \c true if a horizontal connection exists, \c false otherwise.
	 */
	bool HasConnection()
	{
		assert(this->type == CFT_CARGO);

		for (uint i = 0; i < MAX_CARGOES; i++) {
			if (this->u.cargo.supp_cargoes[i] != INVALID_CARGO) return true;
			if (this->u.cargo.cust_cargoes[i] != INVALID_CARGO) return true;
		}
		return false;
	}

	/**
	 * Make a piece of cargo column.
	 * @param cargoes    Array of #CargoID (may contain #INVALID_CARGO).
	 * @param length     Number of cargoes in #cargoes.
	 * @param count      Number of cargoes to display (should be at least the number of valid cargoes, or \c -1 to let the method compute it).
	 * @param top_end    This is the first cargo field of this column.
	 * @param bottom_end This is the last cargo field of this column.
	 * @note #supp_cargoes and #cust_cargoes should be filled in later.
	 */
	void MakeCargo(const CargoID *cargoes, uint length, int count = -1, bool top_end = false, bool bottom_end = false)
	{
		this->type = CFT_CARGO;
		uint i;
		uint num = 0;
		for (i = 0; i < MAX_CARGOES && i < length; i++) {
			if (cargoes[i] != INVALID_CARGO) {
				this->u.cargo.vertical_cargoes[num] = cargoes[i];
				num++;
			}
		}
		this->u.cargo.num_cargoes = (count < 0) ? num : count;
		for (; num < MAX_CARGOES; num++) this->u.cargo.vertical_cargoes[num] = INVALID_CARGO;
		this->u.cargo.top_end = top_end;
		this->u.cargo.bottom_end = bottom_end;
		MemSetT(this->u.cargo.supp_cargoes, INVALID_CARGO, MAX_CARGOES);
		MemSetT(this->u.cargo.cust_cargoes, INVALID_CARGO, MAX_CARGOES);
	}

	/**
	 * Make a field displaying cargo type names.
	 * @param cargoes    Array of #CargoID (may contain #INVALID_CARGO).
	 * @param length     Number of cargoes in #cargoes.
	 * @param left_align ALign texts to the left (else to the right).
	 */
	void MakeCargoLabel(const CargoID *cargoes, uint length, bool left_align)
	{
		this->type = CFT_CARGO_LABEL;
		uint i;
		for (i = 0; i < MAX_CARGOES && i < length; i++) this->u.cargo_label.cargoes[i] = cargoes[i];
		for (; i < MAX_CARGOES; i++) this->u.cargo_label.cargoes[i] = INVALID_CARGO;
		this->u.cargo_label.left_align = left_align;
	}

	/**
	 * Make a header above an industry column.
	 * @param textid Text to display.
	 */
	void MakeHeader(StringID textid)
	{
		this->type = CFT_HEADER;
		this->u.header = textid;
	}

	/**
	 * For a #CFT_CARGO, compute the left position of the left-most vertical cargo connection.
	 * @param xpos Left position of the field.
	 * @return Left position of the left-most vertical cargo column.
	 */
	int GetCargoBase(int xpos) const
	{
		assert(this->type == CFT_CARGO);

		switch (this->u.cargo.num_cargoes) {
			case 0: return xpos + CARGO_FIELD_WIDTH / 2;
			case 1: return xpos + CARGO_FIELD_WIDTH / 2 - HOR_CARGO_WIDTH / 2;
			case 2: return xpos + CARGO_FIELD_WIDTH / 2 - HOR_CARGO_WIDTH - HOR_CARGO_SPACE / 2;
			case 3: return xpos + CARGO_FIELD_WIDTH / 2 - HOR_CARGO_WIDTH - HOR_CARGO_SPACE - HOR_CARGO_WIDTH / 2;
			default: NOT_REACHED();
		}
	}

	/**
	 * Draw the field.
	 * @param xpos Position of the left edge.
	 * @param vpos Position of the top edge.
	 */
	void Draw(int xpos, int ypos) const
	{
		switch (this->type) {
			case CFT_EMPTY:
			case CFT_SMALL_EMPTY:
				break;

			case CFT_HEADER:
				ypos += (small_height - FONT_HEIGHT_NORMAL) / 2;
				DrawString(xpos, xpos + industry_width, ypos, this->u.header, TC_WHITE, SA_HOR_CENTER);
				break;

			case CFT_INDUSTRY: {
				int ypos1 = ypos + VERT_INTER_INDUSTRY_SPACE / 2;
				int ypos2 = ypos + normal_height - 1 - VERT_INTER_INDUSTRY_SPACE / 2;
				int xpos2 = xpos + industry_width - 1;
				GfxDrawLine(xpos,  ypos1, xpos2, ypos1, INDUSTRY_LINE_COLOUR);
				GfxDrawLine(xpos,  ypos1, xpos,  ypos2, INDUSTRY_LINE_COLOUR);
				GfxDrawLine(xpos,  ypos2, xpos2, ypos2, INDUSTRY_LINE_COLOUR);
				GfxDrawLine(xpos2, ypos1, xpos2, ypos2, INDUSTRY_LINE_COLOUR);
				ypos += (normal_height - FONT_HEIGHT_NORMAL) / 2;
				if (this->u.industry.ind_type < NUM_INDUSTRYTYPES) {
					const IndustrySpec *indsp = GetIndustrySpec(this->u.industry.ind_type);
					SetDParam(0, indsp->name);
					DrawString(xpos, xpos2, ypos, STR_JUST_STRING, TC_WHITE, SA_HOR_CENTER);

					/* Draw the industry legend. */
					int blob_left, blob_right;
					if (_current_text_dir == TD_RTL) {
						blob_right = xpos2 - BLOB_DISTANCE;
						blob_left  = blob_right - BLOB_WIDTH;
					} else {
						blob_left  = xpos + BLOB_DISTANCE;
						blob_right = blob_left + BLOB_WIDTH;
					}
					GfxFillRect(blob_left,     ypos2 - BLOB_DISTANCE - BLOB_HEIGHT,     blob_right,     ypos2 - BLOB_DISTANCE,     PC_BLACK); // Border
					GfxFillRect(blob_left + 1, ypos2 - BLOB_DISTANCE - BLOB_HEIGHT + 1, blob_right - 1, ypos2 - BLOB_DISTANCE - 1, indsp->map_colour);
				} else {
					DrawString(xpos, xpos2, ypos, STR_INDUSTRY_CARGOES_HOUSES, TC_FROMSTRING, SA_HOR_CENTER);
				}

				/* Draw the other_produced/other_accepted cargoes. */
				const CargoID *other_right, *other_left;
				if (_current_text_dir == TD_RTL) {
					other_right = this->u.industry.other_accepted;
					other_left  = this->u.industry.other_produced;
				} else {
					other_right = this->u.industry.other_produced;
					other_left  = this->u.industry.other_accepted;
				}
				ypos1 += VERT_CARGO_EDGE;
				for (uint i = 0; i < MAX_CARGOES; i++) {
					if (other_right[i] != INVALID_CARGO) {
						const CargoSpec *csp = CargoSpec::Get(other_right[i]);
						int xp = xpos + industry_width + CARGO_STUB_WIDTH;
						DrawHorConnection(xpos + industry_width, xp - 1, ypos1, csp);
						GfxDrawLine(xp, ypos1, xp, ypos1 + FONT_HEIGHT_NORMAL - 1, CARGO_LINE_COLOUR);
					}
					if (other_left[i] != INVALID_CARGO) {
						const CargoSpec *csp = CargoSpec::Get(other_left[i]);
						int xp = xpos - CARGO_STUB_WIDTH;
						DrawHorConnection(xp + 1, xpos - 1, ypos1, csp);
						GfxDrawLine(xp, ypos1, xp, ypos1 + FONT_HEIGHT_NORMAL - 1, CARGO_LINE_COLOUR);
					}
					ypos1 += FONT_HEIGHT_NORMAL + VERT_CARGO_SPACE;
				}
				break;
			}

			case CFT_CARGO: {
				int cargo_base = this->GetCargoBase(xpos);
				int top = ypos + (this->u.cargo.top_end ? VERT_INTER_INDUSTRY_SPACE / 2 + 1 : 0);
				int bot = ypos - (this->u.cargo.bottom_end ? VERT_INTER_INDUSTRY_SPACE / 2 + 1 : 0) + normal_height - 1;
				int colpos = cargo_base;
				for (int i = 0; i < this->u.cargo.num_cargoes; i++) {
					if (this->u.cargo.top_end) GfxDrawLine(colpos, top - 1, colpos + HOR_CARGO_WIDTH - 1, top - 1, CARGO_LINE_COLOUR);
					if (this->u.cargo.bottom_end) GfxDrawLine(colpos, bot + 1, colpos + HOR_CARGO_WIDTH - 1, bot + 1, CARGO_LINE_COLOUR);
					GfxDrawLine(colpos, top, colpos, bot, CARGO_LINE_COLOUR);
					colpos++;
					const CargoSpec *csp = CargoSpec::Get(this->u.cargo.vertical_cargoes[i]);
					GfxFillRect(colpos, top, colpos + HOR_CARGO_WIDTH - 2, bot, csp->legend_colour, FILLRECT_OPAQUE);
					colpos += HOR_CARGO_WIDTH - 2;
					GfxDrawLine(colpos, top, colpos, bot, CARGO_LINE_COLOUR);
					colpos += 1 + HOR_CARGO_SPACE;
				}

				const CargoID *hor_left, *hor_right;
				if (_current_text_dir == TD_RTL) {
					hor_left  = this->u.cargo.cust_cargoes;
					hor_right = this->u.cargo.supp_cargoes;
				} else {
					hor_left  = this->u.cargo.supp_cargoes;
					hor_right = this->u.cargo.cust_cargoes;
				}
				ypos += VERT_CARGO_EDGE + VERT_INTER_INDUSTRY_SPACE / 2;
				for (uint i = 0; i < MAX_CARGOES; i++) {
					if (hor_left[i] != INVALID_CARGO) {
						int col = hor_left[i];
						int dx = 0;
						const CargoSpec *csp = CargoSpec::Get(this->u.cargo.vertical_cargoes[col]);
						for (; col > 0; col--) {
							int lf = cargo_base + col * HOR_CARGO_WIDTH + (col - 1) * HOR_CARGO_SPACE;
							DrawHorConnection(lf, lf + HOR_CARGO_SPACE - dx, ypos, csp);
							dx = 1;
						}
						DrawHorConnection(xpos, cargo_base - dx, ypos, csp);
					}
					if (hor_right[i] != INVALID_CARGO) {
						int col = hor_right[i];
						int dx = 0;
						const CargoSpec *csp = CargoSpec::Get(this->u.cargo.vertical_cargoes[col]);
						for (; col < this->u.cargo.num_cargoes - 1; col++) {
							int lf = cargo_base + (col + 1) * HOR_CARGO_WIDTH + col * HOR_CARGO_SPACE;
							DrawHorConnection(lf + dx - 1, lf + HOR_CARGO_SPACE - 1, ypos, csp);
							dx = 1;
						}
						DrawHorConnection(cargo_base + col * HOR_CARGO_SPACE + (col + 1) * HOR_CARGO_WIDTH - 1 + dx, xpos + CARGO_FIELD_WIDTH - 1, ypos, csp);
					}
					ypos += FONT_HEIGHT_NORMAL + VERT_CARGO_SPACE;
				}
				break;
			}

			case CFT_CARGO_LABEL:
				ypos += VERT_CARGO_EDGE + VERT_INTER_INDUSTRY_SPACE / 2;
				for (uint i = 0; i < MAX_CARGOES; i++) {
					if (this->u.cargo_label.cargoes[i] != INVALID_CARGO) {
						const CargoSpec *csp = CargoSpec::Get(this->u.cargo_label.cargoes[i]);
						DrawString(xpos + WD_FRAMERECT_LEFT, xpos + industry_width - 1 - WD_FRAMERECT_RIGHT, ypos, csp->name, TC_WHITE,
								(this->u.cargo_label.left_align) ? SA_LEFT : SA_RIGHT);
					}
					ypos += FONT_HEIGHT_NORMAL + VERT_CARGO_SPACE;
				}
				break;

			default:
				NOT_REACHED();
		}
	}

	/**
	 * Decide which cargo was clicked at in a #CFT_CARGO field.
	 * @param left  Left industry neighbour if available (else \c NULL should be supplied).
	 * @param right Right industry neighbour if available (else \c NULL should be supplied).
	 * @param pt    Click position in the cargo field.
	 * @return Cargo clicked at, or #INVALID_CARGO if none.
	 */
	CargoID CargoClickedAt(const CargoesField *left, const CargoesField *right, Point pt) const
	{
		assert(this->type == CFT_CARGO);

		/* Vertical matching. */
		int cpos = this->GetCargoBase(0);
		uint col;
		for (col = 0; col < this->u.cargo.num_cargoes; col++) {
			if (pt.x < cpos) break;
			if (pt.x < cpos + CargoesField::HOR_CARGO_WIDTH) return this->u.cargo.vertical_cargoes[col];
			cpos += CargoesField::HOR_CARGO_WIDTH + CargoesField::HOR_CARGO_SPACE;
		}
		/* col = 0 -> left of first col, 1 -> left of 2nd col, ... this->u.cargo.num_cargoes right of last-col. */

		int vpos = VERT_INTER_INDUSTRY_SPACE / 2 + VERT_CARGO_EDGE;
		uint row;
		for (row = 0; row < MAX_CARGOES; row++) {
			if (pt.y < vpos) return INVALID_CARGO;
			if (pt.y < vpos + FONT_HEIGHT_NORMAL) break;
			vpos += FONT_HEIGHT_NORMAL + VERT_CARGO_SPACE;
		}
		if (row == MAX_CARGOES) return INVALID_CARGO;

		/* row = 0 -> at first horizontal row, row = 1 -> second horizontal row, 2 = 3rd horizontal row. */
		if (col == 0) {
			if (this->u.cargo.supp_cargoes[row] != INVALID_CARGO) return this->u.cargo.vertical_cargoes[this->u.cargo.supp_cargoes[row]];
			if (left != NULL) {
				if (left->type == CFT_INDUSTRY) return left->u.industry.other_produced[row];
				if (left->type == CFT_CARGO_LABEL && !left->u.cargo_label.left_align) return left->u.cargo_label.cargoes[row];
			}
			return INVALID_CARGO;
		}
		if (col == this->u.cargo.num_cargoes) {
			if (this->u.cargo.cust_cargoes[row] != INVALID_CARGO) return this->u.cargo.vertical_cargoes[this->u.cargo.cust_cargoes[row]];
			if (right != NULL) {
				if (right->type == CFT_INDUSTRY) return right->u.industry.other_accepted[row];
				if (right->type == CFT_CARGO_LABEL && right->u.cargo_label.left_align) return right->u.cargo_label.cargoes[row];
			}
			return INVALID_CARGO;
		}
		if (row >= col) {
			/* Clicked somewhere in-between vertical cargo connection.
			 * Since the horizontal connection is made in the same order as the vertical list, the above condition
			 * ensures we are left-below the main diagonal, thus at the supplying side.
			 */
			return (this->u.cargo.supp_cargoes[row] != INVALID_CARGO) ? this->u.cargo.vertical_cargoes[this->u.cargo.supp_cargoes[row]] : INVALID_CARGO;
		} else {
			/* Clicked at a customer connection. */
			return (this->u.cargo.cust_cargoes[row] != INVALID_CARGO) ? this->u.cargo.vertical_cargoes[this->u.cargo.cust_cargoes[row]] : INVALID_CARGO;
		}
	}

	/**
	 * Decide what cargo the user clicked in the cargo label field.
	 * @param pt Click position in the cargo label field.
	 * @return Cargo clicked at, or #INVALID_CARGO if none.
	 */
	CargoID CargoLabelClickedAt(Point pt) const
	{
		assert(this->type == CFT_CARGO_LABEL);

		int vpos = VERT_INTER_INDUSTRY_SPACE / 2 + VERT_CARGO_EDGE;
		uint row;
		for (row = 0; row < MAX_CARGOES; row++) {
			if (pt.y < vpos) return INVALID_CARGO;
			if (pt.y < vpos + FONT_HEIGHT_NORMAL) break;
			vpos += FONT_HEIGHT_NORMAL + VERT_CARGO_SPACE;
		}
		if (row == MAX_CARGOES) return INVALID_CARGO;
		return this->u.cargo_label.cargoes[row];
	}

private:
	/**
	 * Draw a horizontal cargo connection.
	 * @param left  Left-most coordinate to draw.
	 * @param right Right-most coordinate to draw.
	 * @param top   Top coordinate of the cargo connection.
	 * @csp         Cargo to draw.
	 */
	static void DrawHorConnection(int left, int right, int top, const CargoSpec *csp)
	{
		GfxDrawLine(left, top, right, top, CARGO_LINE_COLOUR);
		GfxFillRect(left, top + 1, right, top + FONT_HEIGHT_NORMAL - 2, csp->legend_colour, FILLRECT_OPAQUE);
		GfxDrawLine(left, top + FONT_HEIGHT_NORMAL - 1, right, top + FONT_HEIGHT_NORMAL - 1, CARGO_LINE_COLOUR);
	}
};

assert_compile(MAX_CARGOES >= cpp_lengthof(IndustrySpec, produced_cargo));
assert_compile(MAX_CARGOES >= cpp_lengthof(IndustrySpec, accepts_cargo));

int CargoesField::small_height;   ///< Height of the header row.
int CargoesField::normal_height;  ///< Height of the non-header rows.
int CargoesField::industry_width; ///< Width of an industry field.
const int CargoesField::VERT_INTER_INDUSTRY_SPACE = 6; ///< Amount of space between two industries in a column.

const int CargoesField::HOR_CARGO_BORDER_SPACE = 15; ///< Amount of space between the left/right edge of a #CFT_CARGO field, and the left/right most vertical cargo.
const int CargoesField::CARGO_STUB_WIDTH       = 10; ///< Width of a cargo not carried in the column (should be less than #HOR_CARGO_BORDER_SPACE).
const int CargoesField::HOR_CARGO_WIDTH        = 15; ///< Width of a vertical cargo column (inclusive the border line).
const int CargoesField::HOR_CARGO_SPACE        =  5; ///< Amount of horizontal space between two vertical cargoes.
const int CargoesField::VERT_CARGO_EDGE        =  4; ///< Amount of vertical space between top/bottom and the top/bottom connected cargo at an industry.
const int CargoesField::VERT_CARGO_SPACE       =  4; ///< Amount of vertical space between two connected cargoes at an industry.

const int CargoesField::BLOB_DISTANCE =  5; ///< Distance of the industry legend colour from the edge of the industry box.
const int CargoesField::BLOB_WIDTH    = 12; ///< Width of the industry legend colour, including border.
const int CargoesField::BLOB_HEIGHT   =  9; ///< Height of the industry legend colour, including border

/** Width of a #CFT_CARGO field. */
const int CargoesField::CARGO_FIELD_WIDTH = HOR_CARGO_BORDER_SPACE * 2 + HOR_CARGO_WIDTH * MAX_CARGOES + HOR_CARGO_SPACE * (MAX_CARGOES - 1);

const int CargoesField::INDUSTRY_LINE_COLOUR = PC_YELLOW; ///< Line colour of the industry type box.
const int CargoesField::CARGO_LINE_COLOUR    = PC_YELLOW; ///< Line colour around the cargo.

/** A single row of #CargoesField. */
struct CargoesRow {
	CargoesField columns[5]; ///< One row of fields.

	/**
	 * Connect industry production cargoes to the cargo column after it.
	 * @param column Column of the industry.
	 */
	void ConnectIndustryProduced(int column)
	{
		CargoesField *ind_fld   = this->columns + column;
		CargoesField *cargo_fld = this->columns + column + 1;
		assert(ind_fld->type == CFT_INDUSTRY && cargo_fld->type == CFT_CARGO);

		MemSetT(ind_fld->u.industry.other_produced, INVALID_CARGO, MAX_CARGOES);

		if (ind_fld->u.industry.ind_type < NUM_INDUSTRYTYPES) {
			CargoID others[MAX_CARGOES]; // Produced cargoes not carried in the cargo column.
			int other_count = 0;

			const IndustrySpec *indsp = GetIndustrySpec(ind_fld->u.industry.ind_type);
			for (uint i = 0; i < lengthof(indsp->produced_cargo); i++) {
				int col = cargo_fld->ConnectCargo(indsp->produced_cargo[i], true);
				if (col < 0) others[other_count++] = indsp->produced_cargo[i];
			}

			/* Allocate other cargoes in the empty holes of the horizontal cargo connections. */
			for (uint i = 0; i < MAX_CARGOES && other_count > 0; i++) {
				if (cargo_fld->u.cargo.supp_cargoes[i] == INVALID_CARGO) ind_fld->u.industry.other_produced[i] = others[--other_count];
			}
		} else {
			/* Houses only display what is demanded. */
			for (uint i = 0; i < cargo_fld->u.cargo.num_cargoes; i++) {
				CargoID cid = cargo_fld->u.cargo.vertical_cargoes[i];
				if (cid == CT_PASSENGERS || cid == CT_MAIL) cargo_fld->ConnectCargo(cid, true);
			}
		}
	}

	/**
	 * Construct a #CFT_CARGO_LABEL field.
	 * @param column    Column to create the new field.
	 * @param accepting Display accepted cargo (if \c false, display produced cargo).
	 */
	void MakeCargoLabel(int column, bool accepting)
	{
		CargoID cargoes[MAX_CARGOES];
		MemSetT(cargoes, INVALID_CARGO, lengthof(cargoes));

		CargoesField *label_fld = this->columns + column;
		CargoesField *cargo_fld = this->columns + (accepting ? column - 1 : column + 1);

		assert(cargo_fld->type == CFT_CARGO && label_fld->type == CFT_EMPTY);
		for (uint i = 0; i < cargo_fld->u.cargo.num_cargoes; i++) {
			int col = cargo_fld->ConnectCargo(cargo_fld->u.cargo.vertical_cargoes[i], !accepting);
			cargoes[col] = cargo_fld->u.cargo.vertical_cargoes[i];
		}
		label_fld->MakeCargoLabel(cargoes, lengthof(cargoes), accepting);
	}


	/**
	 * Connect industry accepted cargoes to the cargo column before it.
	 * @param column Column of the industry.
	 */
	void ConnectIndustryAccepted(int column)
	{
		CargoesField *ind_fld   = this->columns + column;
		CargoesField *cargo_fld = this->columns + column - 1;
		assert(ind_fld->type == CFT_INDUSTRY && cargo_fld->type == CFT_CARGO);

		MemSetT(ind_fld->u.industry.other_accepted, INVALID_CARGO, MAX_CARGOES);

		if (ind_fld->u.industry.ind_type < NUM_INDUSTRYTYPES) {
			CargoID others[MAX_CARGOES]; // Accepted cargoes not carried in the cargo column.
			int other_count = 0;

			const IndustrySpec *indsp = GetIndustrySpec(ind_fld->u.industry.ind_type);
			for (uint i = 0; i < lengthof(indsp->accepts_cargo); i++) {
				int col = cargo_fld->ConnectCargo(indsp->accepts_cargo[i], false);
				if (col < 0) others[other_count++] = indsp->accepts_cargo[i];
			}

			/* Allocate other cargoes in the empty holes of the horizontal cargo connections. */
			for (uint i = 0; i < MAX_CARGOES && other_count > 0; i++) {
				if (cargo_fld->u.cargo.cust_cargoes[i] == INVALID_CARGO) ind_fld->u.industry.other_accepted[i] = others[--other_count];
			}
		} else {
			/* Houses only display what is demanded. */
			for (uint i = 0; i < cargo_fld->u.cargo.num_cargoes; i++) {
				for (uint h = 0; h < HOUSE_MAX; h++) {
					HouseSpec *hs = HouseSpec::Get(h);
					if (!hs->enabled) continue;

					for (uint j = 0; j < lengthof(hs->accepts_cargo); j++) {
						if (cargo_fld->u.cargo.vertical_cargoes[i] == hs->accepts_cargo[j]) {
							cargo_fld->ConnectCargo(cargo_fld->u.cargo.vertical_cargoes[i], false);
							goto next_cargo;
						}
					}
				}
next_cargo: ;
			}
		}
	}
};


/**
 * Window displaying the cargo connections around an industry (or cargo).
 *
 * The main display is constructed from 'fields', rectangles that contain an industry, piece of the cargo connection, cargo labels, or headers.
 * For a nice display, the following should be kept in mind:
 * - A #CFT_HEADER is always at the top of an column of #CFT_INDUSTRY fields.
 * - A #CFT_CARGO_LABEL field is also always put in a column of #CFT_INDUSTRY fields.
 * - The top row contains #CFT_HEADER and #CFT_SMALL_EMPTY fields.
 * - Cargo connections have a column of their own (#CFT_CARGO fields).
 * - Cargo accepted or produced by an industry, but not carried in a cargo connection, is drawn in the space of a cargo column attached to the industry.
 *   The information however is part of the industry.
 *
 * This results in the following invariants:
 * - Width of a #CFT_INDUSTRY column is large enough to hold all industry type labels, all cargo labels, and all header texts.
 * - Height of a #CFT_INDUSTRY is large enough to hold a header line, or a industry type line, \c N cargo labels
 *   (where \c N is the maximum number of cargoes connected between industries), \c N connections of cargo types, and space
 *   between two industry types (1/2 above it, and 1/2 underneath it).
 * - Width of a cargo field (#CFT_CARGO) is large enough to hold \c N vertical columns (one for each type of cargo).
 *   Also, space is needed between an industry and the leftmost/rightmost column to draw the non-carried cargoes.
 * - Height of a #CFT_CARGO field is equally high as the height of the #CFT_INDUSTRY.
 * - A field at the top (#CFT_HEADER or #CFT_SMALL_EMPTY) match the width of the fields below them (#CFT_INDUSTRY respectively
 *   #CFT_CARGO), the height should be sufficient to display the header text.
 *
 * When displaying the cargoes around an industry type, five columns are needed (supplying industries, accepted cargoes, the industry,
 * produced cargoes, customer industries). Displaying the industries around a cargo needs three columns (supplying industries, the cargo,
 * customer industries). The remaining two columns are set to #CFT_EMPTY with a width equal to the average of a cargo and an industry column.
 */
struct IndustryCargoesWindow : public Window {
	static const int HOR_TEXT_PADDING, VERT_TEXT_PADDING;

	typedef SmallVector<CargoesRow, 4> Fields;

	Fields fields;  ///< Fields to display in the #WID_IC_PANEL.
	uint ind_cargo; ///< If less than #NUM_INDUSTRYTYPES, an industry type, else a cargo id + NUM_INDUSTRYTYPES.

	Scrollbar *vscroll;

	IndustryCargoesWindow(int id) : Window()
	{
		this->OnInit();
		this->CreateNestedTree(&_industry_cargoes_desc);
		this->vscroll = this->GetScrollbar(WID_IC_SCROLLBAR);
		this->FinishInitNested(&_industry_cargoes_desc, 0);
		this->OnInvalidateData(id);
	}

	virtual void OnInit()
	{
		/* Initialize static CargoesField size variables. */
		Dimension d = GetStringBoundingBox(STR_INDUSTRY_CARGOES_PRODUCERS);
		d = maxdim(d, GetStringBoundingBox(STR_INDUSTRY_CARGOES_CUSTOMERS));
		d.width  += WD_FRAMETEXT_LEFT + WD_FRAMETEXT_RIGHT;
		d.height += WD_FRAMETEXT_TOP + WD_FRAMETEXT_BOTTOM;
		CargoesField::small_height = d.height;

		/* Decide about the size of the box holding the text of an industry type. */
		d.height = 0;
		for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
			const IndustrySpec *indsp = GetIndustrySpec(it);
			if (!indsp->enabled) continue;
			SetDParam(0, indsp->name);
			d = maxdim(d, GetStringBoundingBox(STR_JUST_STRING));
		}
		/* Box must also be wide enough to hold any cargo label. */
		for (uint i = 0; i < NUM_CARGO; i++) {
			const CargoSpec *csp = CargoSpec::Get(i);
			if (!csp->IsValid()) continue;
			d = maxdim(d, GetStringBoundingBox(csp->name));
		}
		d.width  += 2 * HOR_TEXT_PADDING;
		/* Ensure the height is enough for the industry type text, for the horizontal connections, and for the cargo labels. */
		uint min_ind_height = CargoesField::VERT_CARGO_EDGE * 2 + MAX_CARGOES * FONT_HEIGHT_NORMAL + (MAX_CARGOES - 1) *  CargoesField::VERT_CARGO_SPACE;
		d.height = max(d.height + 2 * VERT_TEXT_PADDING, min_ind_height);

		CargoesField::industry_width = d.width;
		CargoesField::normal_height = d.height + CargoesField::VERT_INTER_INDUSTRY_SPACE;
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		if (widget != WID_IC_PANEL) return;

		size->width = WD_FRAMETEXT_LEFT + CargoesField::industry_width * 3 + CargoesField::CARGO_FIELD_WIDTH * 2 + WD_FRAMETEXT_RIGHT;
	}


	CargoesFieldType type; ///< Type of field.
	virtual void SetStringParameters  (int widget) const
	{
		if (widget != WID_IC_CAPTION) return;

		if (this->ind_cargo < NUM_INDUSTRYTYPES) {
			const IndustrySpec *indsp = GetIndustrySpec(this->ind_cargo);
			SetDParam(0, indsp->name);
		} else {
			const CargoSpec *csp = CargoSpec::Get(this->ind_cargo - NUM_INDUSTRYTYPES);
			SetDParam(0, csp->name);
		}
	}

	/**
	 * Do the two sets of cargoes have a valid cargo in common?
	 * @param cargoes1 Base address of the first cargo array.
	 * @param length1  Number of cargoes in the first cargo array.
	 * @param cargoes2 Base address of the second cargo array.
	 * @param length2  Number of cargoes in the second cargo array.
	 * @return Arrays have at least one valid cargo in common.
	 */
	static bool HasCommonValidCargo(const CargoID *cargoes1, uint length1, const CargoID *cargoes2, uint length2)
	{
		while (length1 > 0) {
			if (*cargoes1 != INVALID_CARGO) {
				for (uint i = 0; i < length2; i++) if (*cargoes1 == cargoes2[i]) return true;
			}
			cargoes1++;
			length1--;
		}
		return false;
	}

	/**
	 * Can houses be used to supply one of the cargoes?
	 * @param cargoes Base address of the cargo array.
	 * @param length  Number of cargoes in the array.
	 * @return Houses can supply at least one of the cargoes.
	 */
	static bool HousesCanSupply(const CargoID *cargoes, uint length)
	{
		for (uint i = 0; i < length; i++) {
			if (cargoes[i] == INVALID_CARGO) continue;
			if (cargoes[i] == CT_PASSENGERS || cargoes[i] == CT_MAIL) return true;
		}
		return false;
	}

	/**
	 * Can houses be used as customers of the produced cargoes?
	 * @param cargoes Base address of the cargo array.
	 * @param length  Number of cargoes in the array.
	 * @return Houses can accept at least one of the cargoes.
	 */
	static bool HousesCanAccept(const CargoID *cargoes, uint length)
	{
		HouseZones climate_mask;
		switch (_settings_game.game_creation.landscape) {
			case LT_TEMPERATE: climate_mask = HZ_TEMP; break;
			case LT_ARCTIC:    climate_mask = HZ_SUBARTC_ABOVE | HZ_SUBARTC_BELOW; break;
			case LT_TROPIC:    climate_mask = HZ_SUBTROPIC; break;
			case LT_TOYLAND:   climate_mask = HZ_TOYLND; break;
			default: NOT_REACHED();
		}
		for (uint i = 0; i < length; i++) {
			if (cargoes[i] == INVALID_CARGO) continue;

			for (uint h = 0; h < HOUSE_MAX; h++) {
				HouseSpec *hs = HouseSpec::Get(h);
				if (!hs->enabled || !(hs->building_availability & climate_mask)) continue;

				for (uint j = 0; j < lengthof(hs->accepts_cargo); j++) {
					if (cargoes[i] == hs->accepts_cargo[j]) return true;
				}
			}
		}
		return false;
	}

	/**
	 * Count how many industries have accepted cargoes in common with one of the supplied set.
	 * @param cargoes Cargoes to search.
	 * @param length  Number of cargoes in #cargoes.
	 * @return Number of industries that have an accepted cargo in common with the supplied set.
	 */
	static int CountMatchingAcceptingIndustries(const CargoID *cargoes, uint length)
	{
		int count = 0;
		for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
			const IndustrySpec *indsp = GetIndustrySpec(it);
			if (!indsp->enabled) continue;

			if (HasCommonValidCargo(cargoes, length, indsp->accepts_cargo, lengthof(indsp->accepts_cargo))) count++;
		}
		return count;
	}

	/**
	 * Count how many industries have produced cargoes in common with one of the supplied set.
	 * @param cargoes Cargoes to search.
	 * @param length  Number of cargoes in #cargoes.
	 * @return Number of industries that have a produced cargo in common with the supplied set.
	 */
	static int CountMatchingProducingIndustries(const CargoID *cargoes, uint length)
	{
		int count = 0;
		for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
			const IndustrySpec *indsp = GetIndustrySpec(it);
			if (!indsp->enabled) continue;

			if (HasCommonValidCargo(cargoes, length, indsp->produced_cargo, lengthof(indsp->produced_cargo))) count++;
		}
		return count;
	}

	/**
	 * Shorten the cargo column to just the part between industries.
	 * @param column Column number of the cargo column.
	 * @param top    Current top row.
	 * @param bottom Current bottom row.
	 */
	void ShortenCargoColumn(int column, int top, int bottom)
	{
		while (top < bottom && !this->fields[top].columns[column].HasConnection()) {
			this->fields[top].columns[column].MakeEmpty(CFT_EMPTY);
			top++;
		}
		this->fields[top].columns[column].u.cargo.top_end = true;

		while (bottom > top && !this->fields[bottom].columns[column].HasConnection()) {
			this->fields[bottom].columns[column].MakeEmpty(CFT_EMPTY);
			bottom--;
		}
		this->fields[bottom].columns[column].u.cargo.bottom_end = true;
	}

	/**
	 * Place an industry in the fields.
	 * @param row Row of the new industry.
	 * @param col Column of the new industry.
	 * @param it  Industry to place.
	 */
	void PlaceIndustry(int row, int col, IndustryType it)
	{
		assert(this->fields[row].columns[col].type == CFT_EMPTY);
		this->fields[row].columns[col].MakeIndustry(it);
		if (col == 0) {
			this->fields[row].ConnectIndustryProduced(col);
		} else {
			this->fields[row].ConnectIndustryAccepted(col);
		}
	}

	/**
	 * Notify smallmap that new displayed industries have been selected (in #_displayed_industries).
	 */
	void NotifySmallmap()
	{
		if (!this->IsWidgetLowered(WID_IC_NOTIFY)) return;

		/* Only notify the smallmap window if it exists. In particular, do not
		 * bring it to the front to prevent messing up any nice layout of the user. */
		InvalidateWindowClassesData(WC_SMALLMAP, 0);
	}

	/**
	 * Compute what and where to display for industry type \a it.
	 * @param it Industry type to display.
	 */
	void ComputeIndustryDisplay(IndustryType it)
	{
		this->GetWidget<NWidgetCore>(WID_IC_CAPTION)->widget_data = STR_INDUSTRY_CARGOES_INDUSTRY_CAPTION;
		this->ind_cargo = it;
		_displayed_industries = 1ULL << it;

		this->fields.Clear();
		CargoesRow *row = this->fields.Append();
		row->columns[0].MakeHeader(STR_INDUSTRY_CARGOES_PRODUCERS);
		row->columns[1].MakeEmpty(CFT_SMALL_EMPTY);
		row->columns[2].MakeEmpty(CFT_SMALL_EMPTY);
		row->columns[3].MakeEmpty(CFT_SMALL_EMPTY);
		row->columns[4].MakeHeader(STR_INDUSTRY_CARGOES_CUSTOMERS);

		const IndustrySpec *central_sp = GetIndustrySpec(it);
		bool houses_supply = HousesCanSupply(central_sp->accepts_cargo, lengthof(central_sp->accepts_cargo));
		bool houses_accept = HousesCanAccept(central_sp->produced_cargo, lengthof(central_sp->produced_cargo));
		/* Make a field consisting of two cargo columns. */
		int num_supp = CountMatchingProducingIndustries(central_sp->accepts_cargo, lengthof(central_sp->accepts_cargo)) + houses_supply;
		int num_cust = CountMatchingAcceptingIndustries(central_sp->produced_cargo, lengthof(central_sp->produced_cargo)) + houses_accept;
		int num_indrows = max(3, max(num_supp, num_cust)); // One is needed for the 'it' industry, and 2 for the cargo labels.
		for (int i = 0; i < num_indrows; i++) {
			CargoesRow *row = this->fields.Append();
			row->columns[0].MakeEmpty(CFT_EMPTY);
			row->columns[1].MakeCargo(central_sp->accepts_cargo, lengthof(central_sp->accepts_cargo));
			row->columns[2].MakeEmpty(CFT_EMPTY);
			row->columns[3].MakeCargo(central_sp->produced_cargo, lengthof(central_sp->produced_cargo));
			row->columns[4].MakeEmpty(CFT_EMPTY);
		}
		/* Add central industry. */
		int central_row = 1 + num_indrows / 2;
		this->fields[central_row].columns[2].MakeIndustry(it);
		this->fields[central_row].ConnectIndustryProduced(2);
		this->fields[central_row].ConnectIndustryAccepted(2);

		/* Add cargo labels. */
		this->fields[central_row - 1].MakeCargoLabel(2, true);
		this->fields[central_row + 1].MakeCargoLabel(2, false);

		/* Add suppliers and customers of the 'it' industry. */
		int supp_count = 0;
		int cust_count = 0;
		for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
			const IndustrySpec *indsp = GetIndustrySpec(it);
			if (!indsp->enabled) continue;

			if (HasCommonValidCargo(central_sp->accepts_cargo, lengthof(central_sp->accepts_cargo), indsp->produced_cargo, lengthof(indsp->produced_cargo))) {
				this->PlaceIndustry(1 + supp_count * num_indrows / num_supp, 0, it);
				SetBit(_displayed_industries, it);
				supp_count++;
			}
			if (HasCommonValidCargo(central_sp->produced_cargo, lengthof(central_sp->produced_cargo), indsp->accepts_cargo, lengthof(indsp->accepts_cargo))) {
				this->PlaceIndustry(1 + cust_count * num_indrows / num_cust, 4, it);
				SetBit(_displayed_industries, it);
				cust_count++;
			}
		}
		if (houses_supply) {
			this->PlaceIndustry(1 + supp_count * num_indrows / num_supp, 0, NUM_INDUSTRYTYPES);
			supp_count++;
		}
		if (houses_accept) {
			this->PlaceIndustry(1 + cust_count * num_indrows / num_cust, 4, NUM_INDUSTRYTYPES);
			cust_count++;
		}

		this->ShortenCargoColumn(1, 1, num_indrows);
		this->ShortenCargoColumn(3, 1, num_indrows);
		const NWidgetBase *nwp = this->GetWidget<NWidgetBase>(WID_IC_PANEL);
		this->vscroll->SetCount(CeilDiv(WD_FRAMETEXT_TOP + WD_FRAMETEXT_BOTTOM + CargoesField::small_height + num_indrows * CargoesField::normal_height, nwp->resize_y));
		this->SetDirty();
		this->NotifySmallmap();
	}

	/**
	 * Compute what and where to display for cargo id \a cid.
	 * @param cid Cargo id to display.
	 */
	void ComputeCargoDisplay(CargoID cid)
	{
		this->GetWidget<NWidgetCore>(WID_IC_CAPTION)->widget_data = STR_INDUSTRY_CARGOES_CARGO_CAPTION;
		this->ind_cargo = cid + NUM_INDUSTRYTYPES;
		_displayed_industries = 0;

		this->fields.Clear();
		CargoesRow *row = this->fields.Append();
		row->columns[0].MakeHeader(STR_INDUSTRY_CARGOES_PRODUCERS);
		row->columns[1].MakeEmpty(CFT_SMALL_EMPTY);
		row->columns[2].MakeHeader(STR_INDUSTRY_CARGOES_CUSTOMERS);
		row->columns[3].MakeEmpty(CFT_SMALL_EMPTY);
		row->columns[4].MakeEmpty(CFT_SMALL_EMPTY);

		bool houses_supply = HousesCanSupply(&cid, 1);
		bool houses_accept = HousesCanAccept(&cid, 1);
		int num_supp = CountMatchingProducingIndustries(&cid, 1) + houses_supply + 1; // Ensure room for the cargo label.
		int num_cust = CountMatchingAcceptingIndustries(&cid, 1) + houses_accept;
		int num_indrows = max(num_supp, num_cust);
		for (int i = 0; i < num_indrows; i++) {
			CargoesRow *row = this->fields.Append();
			row->columns[0].MakeEmpty(CFT_EMPTY);
			row->columns[1].MakeCargo(&cid, 1);
			row->columns[2].MakeEmpty(CFT_EMPTY);
			row->columns[3].MakeEmpty(CFT_EMPTY);
			row->columns[4].MakeEmpty(CFT_EMPTY);
		}

		this->fields[num_indrows].MakeCargoLabel(0, false); // Add cargo labels at the left bottom.

		/* Add suppliers and customers of the cargo. */
		int supp_count = 0;
		int cust_count = 0;
		for (IndustryType it = 0; it < NUM_INDUSTRYTYPES; it++) {
			const IndustrySpec *indsp = GetIndustrySpec(it);
			if (!indsp->enabled) continue;

			if (HasCommonValidCargo(&cid, 1, indsp->produced_cargo, lengthof(indsp->produced_cargo))) {
				this->PlaceIndustry(1 + supp_count * num_indrows / num_supp, 0, it);
				SetBit(_displayed_industries, it);
				supp_count++;
			}
			if (HasCommonValidCargo(&cid, 1, indsp->accepts_cargo, lengthof(indsp->accepts_cargo))) {
				this->PlaceIndustry(1 + cust_count * num_indrows / num_cust, 2, it);
				SetBit(_displayed_industries, it);
				cust_count++;
			}
		}
		if (houses_supply) {
			this->PlaceIndustry(1 + supp_count * num_indrows / num_supp, 0, NUM_INDUSTRYTYPES);
			supp_count++;
		}
		if (houses_accept) {
			this->PlaceIndustry(1 + cust_count * num_indrows / num_cust, 2, NUM_INDUSTRYTYPES);
			cust_count++;
		}

		this->ShortenCargoColumn(1, 1, num_indrows);
		const NWidgetBase *nwp = this->GetWidget<NWidgetBase>(WID_IC_PANEL);
		this->vscroll->SetCount(CeilDiv(WD_FRAMETEXT_TOP + WD_FRAMETEXT_BOTTOM + CargoesField::small_height + num_indrows * CargoesField::normal_height, nwp->resize_y));
		this->SetDirty();
		this->NotifySmallmap();
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * - data = 0 .. NUM_INDUSTRYTYPES - 1: Display the chain around the given industry.
	 * - data = NUM_INDUSTRYTYPES: Stop sending updates to the smallmap window.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	virtual void OnInvalidateData(int data = 0, bool gui_scope = true)
	{
		if (!gui_scope) return;
		if (data == NUM_INDUSTRYTYPES) {
			if (this->IsWidgetLowered(WID_IC_NOTIFY)) {
				this->RaiseWidget(WID_IC_NOTIFY);
				this->SetWidgetDirty(WID_IC_NOTIFY);
			}
			return;
		}

		assert(data >= 0 && data < NUM_INDUSTRYTYPES);
		this->ComputeIndustryDisplay(data);
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != WID_IC_PANEL) return;

		DrawPixelInfo tmp_dpi, *old_dpi;
		int width = r.right - r.left + 1;
		int height = r.bottom - r.top + 1 - WD_FRAMERECT_TOP - WD_FRAMERECT_BOTTOM;
		if (!FillDrawPixelInfo(&tmp_dpi, r.left + WD_FRAMERECT_LEFT, r.top + WD_FRAMERECT_TOP, width, height)) return;
		old_dpi = _cur_dpi;
		_cur_dpi = &tmp_dpi;

		int left_pos = WD_FRAMERECT_LEFT;
		if (this->ind_cargo >= NUM_INDUSTRYTYPES) left_pos += (CargoesField::industry_width + CargoesField::CARGO_FIELD_WIDTH) / 2;
		int last_column = (this->ind_cargo < NUM_INDUSTRYTYPES) ? 4 : 2;

		const NWidgetBase *nwp = this->GetWidget<NWidgetBase>(WID_IC_PANEL);
		int vpos = -this->vscroll->GetPosition() * nwp->resize_y;
		for (uint i = 0; i < this->fields.Length(); i++) {
			int row_height = (i == 0) ? CargoesField::small_height : CargoesField::normal_height;
			if (vpos + row_height >= 0) {
				int xpos = left_pos;
				int col, dir;
				if (_current_text_dir == TD_RTL) {
					col = last_column;
					dir = -1;
				} else {
					col = 0;
					dir = 1;
				}
				while (col >= 0 && col <= last_column) {
					this->fields[i].columns[col].Draw(xpos, vpos);
					xpos += (col & 1) ? CargoesField::CARGO_FIELD_WIDTH : CargoesField::industry_width;
					col += dir;
				}
			}
			vpos += row_height;
			if (vpos >= height) break;
		}

		_cur_dpi = old_dpi;
	}

	/**
	 * Calculate in which field was clicked, and within the field, at what position.
	 * @param pt Clicked position in the #WID_IC_PANEL widget.
	 * @param fieldxy If \c true is returned, field x/y coordinate of \a pt.
	 * @param xy      If \c true is returned, x/y coordinate with in the field.
	 * @return Clicked at a valid position.
	 */
	bool CalculatePositionInWidget(Point pt, Point *fieldxy, Point *xy)
	{
		const NWidgetBase *nw = this->GetWidget<NWidgetBase>(WID_IC_PANEL);
		pt.x -= nw->pos_x;
		pt.y -= nw->pos_y;

		int vpos = WD_FRAMERECT_TOP + CargoesField::small_height - this->vscroll->GetPosition() * nw->resize_y;
		if (pt.y < vpos) return false;

		int row = (pt.y - vpos) / CargoesField::normal_height; // row is relative to row 1.
		if (row + 1 >= (int)this->fields.Length()) return false;
		vpos = pt.y - vpos - row * CargoesField::normal_height; // Position in the row + 1 field
		row++; // rebase row to match index of this->fields.

		int xpos = 2 * WD_FRAMERECT_LEFT + ((this->ind_cargo < NUM_INDUSTRYTYPES) ? 0 :  (CargoesField::industry_width + CargoesField::CARGO_FIELD_WIDTH) / 2);
		if (pt.x < xpos) return false;
		int column;
		for (column = 0; column <= 5; column++) {
			int width = (column & 1) ? CargoesField::CARGO_FIELD_WIDTH : CargoesField::industry_width;
			if (pt.x < xpos + width) break;
			xpos += width;
		}
		int num_columns = (this->ind_cargo < NUM_INDUSTRYTYPES) ? 4 : 2;
		if (column > num_columns) return false;
		xpos = pt.x - xpos;

		/* Return both positions, compensating for RTL languages (which works due to the equal symmetry in both displays). */
		fieldxy->y = row;
		xy->y = vpos;
		if (_current_text_dir == TD_RTL) {
			fieldxy->x = num_columns - column;
			xy->x = ((column & 1) ? CargoesField::CARGO_FIELD_WIDTH : CargoesField::industry_width) - xpos;
		} else {
			fieldxy->x = column;
			xy->x = xpos;
		}
		return true;
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_IC_PANEL: {
				Point fieldxy, xy;
				if (!CalculatePositionInWidget(pt, &fieldxy, &xy)) return;

				const CargoesField *fld = this->fields[fieldxy.y].columns + fieldxy.x;
				switch (fld->type) {
					case CFT_INDUSTRY:
						if (fld->u.industry.ind_type < NUM_INDUSTRYTYPES) this->ComputeIndustryDisplay(fld->u.industry.ind_type);
						break;

					case CFT_CARGO: {
						CargoesField *lft = (fieldxy.x > 0) ? this->fields[fieldxy.y].columns + fieldxy.x - 1 : NULL;
						CargoesField *rgt = (fieldxy.x < 4) ? this->fields[fieldxy.y].columns + fieldxy.x + 1 : NULL;
						CargoID cid = fld->CargoClickedAt(lft, rgt, xy);
						if (cid != INVALID_CARGO) this->ComputeCargoDisplay(cid);
						break;
					}

					case CFT_CARGO_LABEL: {
						CargoID cid = fld->CargoLabelClickedAt(xy);
						if (cid != INVALID_CARGO) this->ComputeCargoDisplay(cid);
						break;
					}

					default:
						break;
				}
				break;
			}

			case WID_IC_NOTIFY:
				this->ToggleWidgetLoweredState(WID_IC_NOTIFY);
				this->SetWidgetDirty(WID_IC_NOTIFY);
				SndPlayFx(SND_15_BEEP);

				if (this->IsWidgetLowered(WID_IC_NOTIFY)) {
					if (FindWindowByClass(WC_SMALLMAP) == NULL) ShowSmallMap();
					this->NotifySmallmap();
				}
				break;
		}
	}

	virtual void OnHover(Point pt, int widget)
	{
		if (widget != WID_IC_PANEL) return;

		Point fieldxy, xy;
		if (!CalculatePositionInWidget(pt, &fieldxy, &xy)) return;

		const CargoesField *fld = this->fields[fieldxy.y].columns + fieldxy.x;
		CargoID cid = INVALID_CARGO;
		switch (fld->type) {
			case CFT_CARGO: {
				CargoesField *lft = (fieldxy.x > 0) ? this->fields[fieldxy.y].columns + fieldxy.x - 1 : NULL;
				CargoesField *rgt = (fieldxy.x < 4) ? this->fields[fieldxy.y].columns + fieldxy.x + 1 : NULL;
				cid = fld->CargoClickedAt(lft, rgt, xy);
				break;
			}

			case CFT_CARGO_LABEL: {
				cid = fld->CargoLabelClickedAt(xy);
				break;
			}

			case CFT_INDUSTRY:
				if (fld->u.industry.ind_type < NUM_INDUSTRYTYPES && (this->ind_cargo >= NUM_INDUSTRYTYPES || fieldxy.x != 2)) {
					GuiShowTooltips(this, STR_INDUSTRY_CARGOES_INDUSTRY_TOOLTIP, 0, NULL, TCC_HOVER);
				}
				return;

			default:
				break;
		}
		if (cid != INVALID_CARGO && (this->ind_cargo < NUM_INDUSTRYTYPES || cid != this->ind_cargo - NUM_INDUSTRYTYPES)) {
			const CargoSpec *csp = CargoSpec::Get(cid);
			uint64 params[5];
			params[0] = csp->name;
			GuiShowTooltips(this, STR_INDUSTRY_CARGOES_CARGO_TOOLTIP, 1, params, TCC_HOVER);
		}
	}

	virtual void OnResize()
	{
		this->vscroll->SetCapacityFromWidget(this, WID_IC_PANEL);
	}
};

const int IndustryCargoesWindow::HOR_TEXT_PADDING  = 5; ///< Horizontal padding around the industry type text.
const int IndustryCargoesWindow::VERT_TEXT_PADDING = 5; ///< Vertical padding around the industry type text.

/**
 * Open the industry and cargoes window.
 * @param id Industry type to display.
 */
static void ShowIndustryCargoesWindow(IndustryType id)
{
	assert(id < NUM_INDUSTRYTYPES);

	Window *w = BringWindowToFrontById(WC_INDUSTRY_CARGOES, 0);
	if (w != NULL) {
		w->InvalidateData(id);
		return;
	}
	new IndustryCargoesWindow(id);
}
