/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file company_gui.cpp Company related GUIs. */

#include "stdafx.h"
#include "error.h"
#include "gui.h"
#include "window_gui.h"
#include "textbuf_gui.h"
#include "viewport_func.h"
#include "company_func.h"
#include "command_func.h"
#include "network/network.h"
#include "network/network_gui.h"
#include "network/network_func.h"
#include "vehicle_func.h"
#include "newgrf.h"
#include "company_manager_face.h"
#include "strings_func.h"
#include "date_func.h"
#include "widgets/dropdown_type.h"
#include "tilehighlight_func.h"
#include "sprite.h"
#include "company_base.h"
#include "core/geometry_func.hpp"
#include "economy_func.h"
#include "object_type.h"
#include "rail.h"
#include "engine_base.h"
#include "window_func.h"
#include "road_func.h"
#include "water.h"
#include "station_func.h"

#include "widgets/company_widget.h"

#include "table/strings.h"

/** Company GUI constants. */
static const uint EXP_LINESPACE  = 2;      ///< Amount of vertical space for a horizontal (sub-)total line.
static const uint EXP_BLOCKSPACE = 10;     ///< Amount of vertical space between two blocks of numbers.

static void DoSelectCompanyManagerFace(Window *parent);
static void ShowCompanyInfrastructure(CompanyID company);

/** Standard unsorted list of expenses. */
static ExpensesType _expenses_list_1[] = {
	EXPENSES_CONSTRUCTION,
	EXPENSES_NEW_VEHICLES,
	EXPENSES_TRAIN_RUN,
	EXPENSES_ROADVEH_RUN,
	EXPENSES_AIRCRAFT_RUN,
	EXPENSES_SHIP_RUN,
	EXPENSES_PROPERTY,
	EXPENSES_TRAIN_INC,
	EXPENSES_ROADVEH_INC,
	EXPENSES_AIRCRAFT_INC,
	EXPENSES_SHIP_INC,
	EXPENSES_LOAN_INT,
	EXPENSES_OTHER,
};

/** Grouped list of expenses. */
static ExpensesType _expenses_list_2[] = {
	EXPENSES_TRAIN_INC,
	EXPENSES_ROADVEH_INC,
	EXPENSES_AIRCRAFT_INC,
	EXPENSES_SHIP_INC,
	INVALID_EXPENSES,
	EXPENSES_TRAIN_RUN,
	EXPENSES_ROADVEH_RUN,
	EXPENSES_AIRCRAFT_RUN,
	EXPENSES_SHIP_RUN,
	EXPENSES_PROPERTY,
	EXPENSES_LOAN_INT,
	INVALID_EXPENSES,
	EXPENSES_CONSTRUCTION,
	EXPENSES_NEW_VEHICLES,
	EXPENSES_OTHER,
	INVALID_EXPENSES,
};

/** Expense list container. */
struct ExpensesList {
	const ExpensesType *et;   ///< Expenses items.
	const uint length;        ///< Number of items in list.
	const uint num_subtotals; ///< Number of sub-totals in the list.

	ExpensesList(ExpensesType *et, int length, int num_subtotals) : et(et), length(length), num_subtotals(num_subtotals)
	{
	}

	uint GetHeight() const
	{
		/* heading + line + texts of expenses + sub-totals + total line + total text */
		return FONT_HEIGHT_NORMAL + EXP_LINESPACE + this->length * FONT_HEIGHT_NORMAL + num_subtotals * (EXP_BLOCKSPACE + EXP_LINESPACE) + EXP_LINESPACE + FONT_HEIGHT_NORMAL;
	}

	/** Compute width of the expenses categories in pixels. */
	uint GetCategoriesWidth() const
	{
		uint width = 0;
		bool invalid_expenses_measured = false; // Measure 'Total' width only once.
		for (uint i = 0; i < this->length; i++) {
			ExpensesType et = this->et[i];
			if (et == INVALID_EXPENSES) {
				if (!invalid_expenses_measured) {
					width = max(width, GetStringBoundingBox(STR_FINANCES_TOTAL_CAPTION).width);
					invalid_expenses_measured = true;
				}
			} else {
				width = max(width, GetStringBoundingBox(STR_FINANCES_SECTION_CONSTRUCTION + et).width);
			}
		}
		return width;
	}
};

static const ExpensesList _expenses_list_types[] = {
	ExpensesList(_expenses_list_1, lengthof(_expenses_list_1), 0),
	ExpensesList(_expenses_list_2, lengthof(_expenses_list_2), 3),
};

/**
 * Draw the expenses categories.
 * @param r Available space for drawing.
 * @note The environment must provide padding at the left and right of \a r.
 */
static void DrawCategories(const Rect &r)
{
	int y = r.top;

	DrawString(r.left, r.right, y, STR_FINANCES_EXPENDITURE_INCOME_TITLE, TC_FROMSTRING, SA_HOR_CENTER, true);
	y += FONT_HEIGHT_NORMAL + EXP_LINESPACE;

	int type = _settings_client.gui.expenses_layout;
	for (uint i = 0; i < _expenses_list_types[type].length; i++) {
		const ExpensesType et = _expenses_list_types[type].et[i];
		if (et == INVALID_EXPENSES) {
			y += EXP_LINESPACE;
			DrawString(r.left, r.right, y, STR_FINANCES_TOTAL_CAPTION, TC_FROMSTRING, SA_RIGHT);
			y += FONT_HEIGHT_NORMAL + EXP_BLOCKSPACE;
		} else {
			DrawString(r.left, r.right, y, STR_FINANCES_SECTION_CONSTRUCTION + et);
			y += FONT_HEIGHT_NORMAL;
		}
	}

	DrawString(r.left, r.right, y + EXP_LINESPACE, STR_FINANCES_TOTAL_CAPTION, TC_FROMSTRING, SA_RIGHT);
}

/**
 * Draw an amount of money.
 * @param amount Amount of money to draw,
 * @param left   Left coordinate of the space to draw in.
 * @param right  Right coordinate of the space to draw in.
 * @param top    Top coordinate of the space to draw in.
 */
static void DrawPrice(Money amount, int left, int right, int top)
{
	StringID str = STR_FINANCES_NEGATIVE_INCOME;
	if (amount < 0) {
		amount = -amount;
		str++;
	}
	SetDParam(0, amount);
	DrawString(left, right, top, str, TC_FROMSTRING, SA_RIGHT);
}

/**
 * Draw a column with prices.
 * @param r    Available space for drawing.
 * @param year Year being drawn.
 * @param tbl  Pointer to table of amounts for \a year.
 * @note The environment must provide padding at the left and right of \a r.
 */
static void DrawYearColumn(const Rect &r, int year, const Money (*tbl)[EXPENSES_END])
{
	int y = r.top;

	SetDParam(0, year);
	DrawString(r.left, r.right, y, STR_FINANCES_YEAR, TC_FROMSTRING, SA_RIGHT, true);
	y += FONT_HEIGHT_NORMAL + EXP_LINESPACE;

	Money sum = 0;
	Money subtotal = 0;
	int type = _settings_client.gui.expenses_layout;
	for (uint i = 0; i < _expenses_list_types[type].length; i++) {
		const ExpensesType et = _expenses_list_types[type].et[i];
		if (et == INVALID_EXPENSES) {
			Money cost = subtotal;
			subtotal = 0;
			GfxFillRect(r.left, y, r.right, y, PC_BLACK);
			y += EXP_LINESPACE;
			DrawPrice(cost, r.left, r.right, y);
			y += FONT_HEIGHT_NORMAL + EXP_BLOCKSPACE;
		} else {
			Money cost = (*tbl)[et];
			subtotal += cost;
			sum += cost;
			if (cost != 0) DrawPrice(cost, r.left, r.right, y);
			y += FONT_HEIGHT_NORMAL;
		}
	}

	GfxFillRect(r.left, y, r.right, y, PC_BLACK);
	y += EXP_LINESPACE;
	DrawPrice(sum, r.left, r.right, y);
}

static const NWidgetPart _nested_company_finances_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_CF_CAPTION), SetDataTip(STR_FINANCES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_CF_TOGGLE_SIZE), SetDataTip(SPR_LARGE_SMALL_WINDOW, STR_TOOLTIP_TOGGLE_LARGE_SMALL_WINDOW),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_PANEL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(NWID_HORIZONTAL), SetPadding(WD_FRAMERECT_TOP, WD_FRAMERECT_RIGHT, WD_FRAMERECT_BOTTOM, WD_FRAMERECT_LEFT), SetPIP(0, 9, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_CATEGORY), SetMinimalSize(120, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE1), SetMinimalSize(86, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE2), SetMinimalSize(86, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE3), SetMinimalSize(86, 0), SetFill(0, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPadding(WD_FRAMERECT_TOP, WD_FRAMERECT_RIGHT, WD_FRAMERECT_BOTTOM, WD_FRAMERECT_LEFT),
			NWidget(NWID_VERTICAL), // Vertical column with 'bank balance', 'loan'
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_BANK_BALANCE_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_LOAN_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(NWID_SPACER), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_SPACER), SetFill(0, 0), SetMinimalSize(30, 0),
			NWidget(NWID_VERTICAL), // Vertical column with bank balance amount, loan amount, and total.
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_BALANCE_VALUE), SetDataTip(STR_NULL, STR_NULL),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_LOAN_VALUE), SetDataTip(STR_NULL, STR_NULL),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_LOAN_LINE), SetMinimalSize(0, 2), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_TOTAL_VALUE), SetDataTip(STR_NULL, STR_NULL),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_MAXLOAN),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(0, 1), SetMinimalSize(25, 0),
					NWidget(NWID_VERTICAL), // Max loan information
						NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_MAXLOAN_GAP), SetFill(0, 0),
						NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_MAXLOAN_VALUE), SetDataTip(STR_FINANCES_MAX_LOAN, STR_NULL),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_BUTTONS),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_INCREASE_LOAN), SetFill(1, 0), SetDataTip(STR_FINANCES_BORROW_BUTTON, STR_FINANCES_BORROW_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_REPAY_LOAN), SetFill(1, 0), SetDataTip(STR_FINANCES_REPAY_BUTTON, STR_FINANCES_REPAY_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_INFRASTRUCTURE), SetFill(1, 0), SetDataTip(STR_FINANCES_INFRASTRUCTURE_BUTTON, STR_COMPANY_VIEW_INFRASTRUCTURE_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

/**
 * Window class displaying the company finances.
 * @todo #money_width should be calculated dynamically.
 */
struct CompanyFinancesWindow : Window {
	static Money max_money; ///< The maximum amount of money a company has had this 'run'
	bool small;             ///< Window is toggled to 'small'.

	CompanyFinancesWindow(const WindowDesc *desc, CompanyID company) : Window()
	{
		this->small = false;
		this->CreateNestedTree(desc);
		this->SetupWidgets();
		this->FinishInitNested(desc, company);

		this->owner = (Owner)this->window_number;
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_CF_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				SetDParam(1, (CompanyID)this->window_number);
				break;

			case WID_CF_MAXLOAN_VALUE:
				SetDParam(0, _economy.max_loan);
				break;

			case WID_CF_INCREASE_LOAN:
			case WID_CF_REPAY_LOAN:
				SetDParam(0, LOAN_INTERVAL);
				break;
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		int type = _settings_client.gui.expenses_layout;
		switch (widget) {
			case WID_CF_EXPS_CATEGORY:
				size->width  = _expenses_list_types[type].GetCategoriesWidth();
				size->height = _expenses_list_types[type].GetHeight();
				break;

			case WID_CF_EXPS_PRICE1:
			case WID_CF_EXPS_PRICE2:
			case WID_CF_EXPS_PRICE3:
				size->height = _expenses_list_types[type].GetHeight();
				/* FALL THROUGH */
			case WID_CF_BALANCE_VALUE:
			case WID_CF_LOAN_VALUE:
			case WID_CF_TOTAL_VALUE:
				SetDParam(0, CompanyFinancesWindow::max_money);
				size->width = max(GetStringBoundingBox(STR_FINANCES_NEGATIVE_INCOME).width, GetStringBoundingBox(STR_FINANCES_POSITIVE_INCOME).width) + padding.width;
				break;

			case WID_CF_MAXLOAN_GAP:
				size->height = FONT_HEIGHT_NORMAL;
				break;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_CF_EXPS_CATEGORY:
				DrawCategories(r);
				break;

			case WID_CF_EXPS_PRICE1:
			case WID_CF_EXPS_PRICE2:
			case WID_CF_EXPS_PRICE3: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				int age = min(_cur_year - c->inaugurated_year, 2);
				int wid_offset = widget - WID_CF_EXPS_PRICE1;
				if (wid_offset <= age) {
					DrawYearColumn(r, _cur_year - (age - wid_offset), c->yearly_expenses + (age - wid_offset));
				}
				break;
			}

			case WID_CF_BALANCE_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->money);
				DrawString(r.left, r.right, r.top, STR_FINANCES_TOTAL_CURRENCY, TC_FROMSTRING, SA_RIGHT);
				break;
			}

			case WID_CF_LOAN_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->current_loan);
				DrawString(r.left, r.right, r.top, STR_FINANCES_TOTAL_CURRENCY, TC_FROMSTRING, SA_RIGHT);
				break;
			}

			case WID_CF_TOTAL_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->money - c->current_loan);
				DrawString(r.left, r.right, r.top, STR_FINANCES_TOTAL_CURRENCY, TC_FROMSTRING, SA_RIGHT);
				break;
			}

			case WID_CF_LOAN_LINE:
				GfxFillRect(r.left, r.top, r.right, r.top, PC_BLACK);
				break;
		}
	}

	/**
	 * Setup the widgets in the nested tree, such that the finances window is displayed properly.
	 * @note After setup, the window must be (re-)initialized.
	 */
	void SetupWidgets()
	{
		int plane = this->small ? SZSP_NONE : 0;
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_PANEL)->SetDisplayedPlane(plane);
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_MAXLOAN)->SetDisplayedPlane(plane);

		CompanyID company = (CompanyID)this->window_number;
		plane = (company != _local_company) ? SZSP_NONE : 0;
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_BUTTONS)->SetDisplayedPlane(plane);
	}

	virtual void OnPaint()
	{
		if (!this->IsShaded()) {
			if (!this->small) {
				/* Check that the expenses panel height matches the height needed for the layout. */
				int type = _settings_client.gui.expenses_layout;
				if (_expenses_list_types[type].GetHeight() != this->GetWidget<NWidgetBase>(WID_CF_EXPS_CATEGORY)->current_y) {
					this->SetupWidgets();
					this->ReInit();
					return;
				}
			}

			/* Check that the loan buttons are shown only when the user owns the company. */
			CompanyID company = (CompanyID)this->window_number;
			int req_plane = (company != _local_company) ? SZSP_NONE : 0;
			if (req_plane != this->GetWidget<NWidgetStacked>(WID_CF_SEL_BUTTONS)->shown_plane) {
				this->SetupWidgets();
				this->ReInit();
				return;
			}

			const Company *c = Company::Get(company);
			this->SetWidgetDisabledState(WID_CF_INCREASE_LOAN, c->current_loan == _economy.max_loan); // Borrow button only shows when there is any more money to loan.
			this->SetWidgetDisabledState(WID_CF_REPAY_LOAN, company != _local_company || c->current_loan == 0); // Repay button only shows when there is any more money to repay.
		}

		this->DrawWidgets();
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_CF_TOGGLE_SIZE: // toggle size
				this->small = !this->small;
				this->SetupWidgets();
				if (this->IsShaded()) {
					/* Finances window is not resizable, so size hints given during unshading have no effect
					 * on the changed appearance of the window. */
					this->SetShaded(false);
				} else {
					this->ReInit();
				}
				break;

			case WID_CF_INCREASE_LOAN: // increase loan
				DoCommandP(0, 0, _ctrl_pressed, CMD_INCREASE_LOAN | CMD_MSG(STR_ERROR_CAN_T_BORROW_ANY_MORE_MONEY));
				break;

			case WID_CF_REPAY_LOAN: // repay loan
				DoCommandP(0, 0, _ctrl_pressed, CMD_DECREASE_LOAN | CMD_MSG(STR_ERROR_CAN_T_REPAY_LOAN));
				break;

			case WID_CF_INFRASTRUCTURE: // show infrastructure details
				ShowCompanyInfrastructure((CompanyID)this->window_number);
				break;
		}
	}

	virtual void OnHundredthTick()
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		if (c->money > CompanyFinancesWindow::max_money) {
			CompanyFinancesWindow::max_money = max(c->money * 2, CompanyFinancesWindow::max_money * 4);
			this->SetupWidgets();
			this->ReInit();
		}
	}
};

/** First conservative estimate of the maximum amount of money */
Money CompanyFinancesWindow::max_money = INT32_MAX;

static const WindowDesc _company_finances_desc(
	WDP_AUTO, 0, 0,
	WC_FINANCES, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_company_finances_widgets, lengthof(_nested_company_finances_widgets)
);

/**
 * Open the finances window of a company.
 * @param company Company to show finances of.
 * @pre is company a valid company.
 */
void ShowCompanyFinances(CompanyID company)
{
	if (!Company::IsValidID(company)) return;
	if (BringWindowToFrontById(WC_FINANCES, company)) return;

	new CompanyFinancesWindow(&_company_finances_desc, company);
}

/* List of colours for the livery window */
static const StringID _colour_dropdown[] = {
	STR_COLOUR_DARK_BLUE,
	STR_COLOUR_PALE_GREEN,
	STR_COLOUR_PINK,
	STR_COLOUR_YELLOW,
	STR_COLOUR_RED,
	STR_COLOUR_LIGHT_BLUE,
	STR_COLOUR_GREEN,
	STR_COLOUR_DARK_GREEN,
	STR_COLOUR_BLUE,
	STR_COLOUR_CREAM,
	STR_COLOUR_MAUVE,
	STR_COLOUR_PURPLE,
	STR_COLOUR_ORANGE,
	STR_COLOUR_BROWN,
	STR_COLOUR_GREY,
	STR_COLOUR_WHITE,
};

/* Association of liveries to livery classes */
static const LiveryClass _livery_class[LS_END] = {
	LC_OTHER,
	LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL,
	LC_ROAD, LC_ROAD,
	LC_SHIP, LC_SHIP,
	LC_AIRCRAFT, LC_AIRCRAFT, LC_AIRCRAFT,
	LC_ROAD, LC_ROAD,
};

class DropDownListColourItem : public DropDownListItem {
public:
	DropDownListColourItem(int result, bool masked) : DropDownListItem(result, masked) {}

	virtual ~DropDownListColourItem() {}

	StringID String() const
	{
		return _colour_dropdown[this->result];
	}

	uint Height(uint width) const
	{
		return max(FONT_HEIGHT_NORMAL, (byte)14);
	}

	bool Selectable() const
	{
		return true;
	}

	void Draw(int left, int right, int top, int bottom, bool sel, int bg_colour) const
	{
		bool rtl = _current_text_dir == TD_RTL;
		DrawSprite(SPR_VEH_BUS_SIDE_VIEW, PALETTE_RECOLOUR_START + this->result, rtl ? right - 16 : left + 16, top + 7);
		DrawString(rtl ? left + 2 : left + 32, rtl ? right - 32 : right - 2, top + max(0, 13 - FONT_HEIGHT_NORMAL), this->String(), sel ? TC_WHITE : TC_BLACK);
	}
};

/** Company livery colour scheme window. */
struct SelectCompanyLiveryWindow : public Window {
private:
	uint32 sel;
	LiveryClass livery_class;
	Dimension square;
	Dimension box;
	uint line_height;

	void ShowColourDropDownMenu(uint32 widget)
	{
		uint32 used_colours = 0;
		const Livery *livery;
		LiveryScheme scheme;

		/* Disallow other company colours for the primary colour */
		if (HasBit(this->sel, LS_DEFAULT) && widget == WID_SCL_PRI_COL_DROPDOWN) {
			const Company *c;
			FOR_ALL_COMPANIES(c) {
				if (c->index != _local_company) SetBit(used_colours, c->colour);
			}
		}

		/* Get the first selected livery to use as the default dropdown item */
		for (scheme = LS_BEGIN; scheme < LS_END; scheme++) {
			if (HasBit(this->sel, scheme)) break;
		}
		if (scheme == LS_END) scheme = LS_DEFAULT;
		livery = &Company::Get((CompanyID)this->window_number)->livery[scheme];

		DropDownList *list = new DropDownList();
		for (uint i = 0; i < lengthof(_colour_dropdown); i++) {
			list->push_back(new DropDownListColourItem(i, HasBit(used_colours, i)));
		}

		ShowDropDownList(this, list, widget == WID_SCL_PRI_COL_DROPDOWN ? livery->colour1 : livery->colour2, widget);
	}

public:
	SelectCompanyLiveryWindow(const WindowDesc *desc, CompanyID company) : Window()
	{
		this->livery_class = LC_OTHER;
		this->sel = 1;

		this->square = GetSpriteSize(SPR_SQUARE);
		this->box    = maxdim(GetSpriteSize(SPR_BOX_CHECKED), GetSpriteSize(SPR_BOX_EMPTY));
		this->line_height = max(max(this->square.height, this->box.height), (uint)FONT_HEIGHT_NORMAL) + 4;

		this->InitNested(desc, company);
		this->owner = company;
		this->LowerWidget(WID_SCL_CLASS_GENERAL);
		this->InvalidateData(1);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_SCL_SPACER_DROPDOWN: {
				/* The matrix widget below needs enough room to print all the schemes. */
				Dimension d = {0, 0};
				for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
					d = maxdim(d, GetStringBoundingBox(STR_LIVERY_DEFAULT + scheme));
				}
				size->width = max(size->width, 5 + this->box.width + d.width + WD_FRAMERECT_RIGHT);
				break;
			}

			case WID_SCL_MATRIX: {
				uint livery_height = 0;
				for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
					if (_livery_class[scheme] == this->livery_class && HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
						livery_height++;
					}
				}
				size->height = livery_height * this->line_height;
				this->GetWidget<NWidgetCore>(WID_SCL_MATRIX)->widget_data = (livery_height << MAT_ROW_START) | (1 << MAT_COL_START);
				break;
			}

			case WID_SCL_SEC_COL_DROPDOWN:
				if (!_loaded_newgrf_features.has_2CC) {
					size->width = 0;
					break;
				}
				/* FALL THROUGH */
			case WID_SCL_PRI_COL_DROPDOWN: {
				int padding = this->square.width + NWidgetScrollbar::GetVerticalDimension().width + 10;
				for (const StringID *id = _colour_dropdown; id != endof(_colour_dropdown); id++) {
					size->width = max(size->width, GetStringBoundingBox(*id).width + padding);
				}
				break;
			}
		}
	}

	virtual void OnPaint()
	{
		/* Disable dropdown controls if no scheme is selected */
		this->SetWidgetDisabledState(WID_SCL_PRI_COL_DROPDOWN, this->sel == 0);
		this->SetWidgetDisabledState(WID_SCL_SEC_COL_DROPDOWN, this->sel == 0);

		this->DrawWidgets();
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_SCL_PRI_COL_DROPDOWN:
			case WID_SCL_SEC_COL_DROPDOWN: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				LiveryScheme scheme = LS_DEFAULT;

				if (this->sel != 0) {
					for (scheme = LS_BEGIN; scheme < LS_END; scheme++) {
						if (HasBit(this->sel, scheme)) break;
					}
					if (scheme == LS_END) scheme = LS_DEFAULT;
				}
				SetDParam(0, STR_COLOUR_DARK_BLUE + ((widget == WID_SCL_PRI_COL_DROPDOWN) ? c->livery[scheme].colour1 : c->livery[scheme].colour2));
				break;
			}
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		if (widget != WID_SCL_MATRIX) return;

		bool rtl = _current_text_dir == TD_RTL;

		/* Horizontal coordinates of scheme name column. */
		const NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_SCL_SPACER_DROPDOWN);
		int sch_left = nwi->pos_x;
		int sch_right = sch_left + nwi->current_x - 1;
		/* Horizontal coordinates of first dropdown. */
		nwi = this->GetWidget<NWidgetBase>(WID_SCL_PRI_COL_DROPDOWN);
		int pri_left = nwi->pos_x;
		int pri_right = pri_left + nwi->current_x - 1;
		/* Horizontal coordinates of second dropdown. */
		nwi = this->GetWidget<NWidgetBase>(WID_SCL_SEC_COL_DROPDOWN);
		int sec_left = nwi->pos_x;
		int sec_right = sec_left + nwi->current_x - 1;

		int text_left  = (rtl ? (uint)WD_FRAMERECT_LEFT : (this->box.width + 5));
		int text_right = (rtl ? (this->box.width + 5) : (uint)WD_FRAMERECT_RIGHT);

		int box_offs    = (this->line_height - this->box.height) / 2;
		int square_offs = (this->line_height - this->square.height) / 2 + 1;
		int text_offs   = (this->line_height - FONT_HEIGHT_NORMAL) / 2 + 1;

		int y = r.top;
		const Company *c = Company::Get((CompanyID)this->window_number);
		for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
			if (_livery_class[scheme] == this->livery_class && HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
				bool sel = HasBit(this->sel, scheme) != 0;

				/* Optional check box + scheme name. */
				if (scheme != LS_DEFAULT) {
					DrawSprite(c->livery[scheme].in_use ? SPR_BOX_CHECKED : SPR_BOX_EMPTY, PAL_NONE, (rtl ? sch_right - (this->box.width + 5) + WD_FRAMERECT_RIGHT : sch_left) + WD_FRAMERECT_LEFT, y + box_offs);
				}
				DrawString(sch_left + text_left, sch_right - text_right, y + text_offs, STR_LIVERY_DEFAULT + scheme, sel ? TC_WHITE : TC_BLACK);

				/* Text below the first dropdown. */
				DrawSprite(SPR_SQUARE, GENERAL_SPRITE_COLOUR(c->livery[scheme].colour1), (rtl ? pri_right - (this->box.width + 5) + WD_FRAMERECT_RIGHT : pri_left) + WD_FRAMERECT_LEFT, y + square_offs);
				DrawString(pri_left + text_left, pri_right - text_right, y + text_offs, STR_COLOUR_DARK_BLUE + c->livery[scheme].colour1, sel ? TC_WHITE : TC_GOLD);

				/* Text below the second dropdown. */
				if (sec_right > sec_left) { // Second dropdown has non-zero size.
					DrawSprite(SPR_SQUARE, GENERAL_SPRITE_COLOUR(c->livery[scheme].colour2), (rtl ? sec_right - (this->box.width + 5) + WD_FRAMERECT_RIGHT : sec_left) + WD_FRAMERECT_LEFT, y + square_offs);
					DrawString(sec_left + text_left, sec_right - text_right, y + text_offs, STR_COLOUR_DARK_BLUE + c->livery[scheme].colour2, sel ? TC_WHITE : TC_GOLD);
				}

				y += this->line_height;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			/* Livery Class buttons */
			case WID_SCL_CLASS_GENERAL:
			case WID_SCL_CLASS_RAIL:
			case WID_SCL_CLASS_ROAD:
			case WID_SCL_CLASS_SHIP:
			case WID_SCL_CLASS_AIRCRAFT:
				this->RaiseWidget(this->livery_class + WID_SCL_CLASS_GENERAL);
				this->livery_class = (LiveryClass)(widget - WID_SCL_CLASS_GENERAL);
				this->LowerWidget(this->livery_class + WID_SCL_CLASS_GENERAL);

				/* Select the first item in the list */
				this->sel = 0;
				for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
					if (_livery_class[scheme] == this->livery_class && HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
						this->sel = 1 << scheme;
						break;
					}
				}

				this->ReInit();
				break;

			case WID_SCL_PRI_COL_DROPDOWN: // First colour dropdown
				ShowColourDropDownMenu(WID_SCL_PRI_COL_DROPDOWN);
				break;

			case WID_SCL_SEC_COL_DROPDOWN: // Second colour dropdown
				ShowColourDropDownMenu(WID_SCL_SEC_COL_DROPDOWN);
				break;

			case WID_SCL_MATRIX: {
				const NWidgetBase *wid = this->GetWidget<NWidgetBase>(WID_SCL_MATRIX);
				LiveryScheme j = (LiveryScheme)((pt.y - wid->pos_y) / this->line_height);

				for (LiveryScheme scheme = LS_BEGIN; scheme <= j; scheme++) {
					if (_livery_class[scheme] != this->livery_class || !HasBit(_loaded_newgrf_features.used_liveries, scheme)) j++;
					if (scheme >= LS_END) return;
				}
				if (j >= LS_END) return;

				/* If clicking on the left edge, toggle using the livery */
				if (_current_text_dir == TD_RTL ? pt.x - wid->pos_x > wid->current_x - (this->box.width + 5) : pt.x - wid->pos_x < (this->box.width + 5)) {
					DoCommandP(0, j | (2 << 8), !Company::Get((CompanyID)this->window_number)->livery[j].in_use, CMD_SET_COMPANY_COLOUR);
				}

				if (_ctrl_pressed) {
					ToggleBit(this->sel, j);
				} else {
					this->sel = 1 << j;
				}
				this->SetDirty();
				break;
			}
		}
	}

	virtual void OnDropdownSelect(int widget, int index)
	{
		for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
			if (HasBit(this->sel, scheme)) {
				DoCommandP(0, scheme | (widget == WID_SCL_PRI_COL_DROPDOWN ? 0 : 256), index, CMD_SET_COMPANY_COLOUR);
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
		if (!gui_scope) return;
		this->SetWidgetsDisabledState(true, WID_SCL_CLASS_RAIL, WID_SCL_CLASS_ROAD, WID_SCL_CLASS_SHIP, WID_SCL_CLASS_AIRCRAFT, WIDGET_LIST_END);

		bool current_class_valid = this->livery_class == LC_OTHER;
		if (_settings_client.gui.liveries == LIT_ALL || (_settings_client.gui.liveries == LIT_COMPANY && this->window_number == _local_company)) {
			for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
				if (HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
					if (_livery_class[scheme] == this->livery_class) current_class_valid = true;
					this->EnableWidget(WID_SCL_CLASS_GENERAL + _livery_class[scheme]);
				} else {
					ClrBit(this->sel, scheme);
				}
			}
		}

		if (!current_class_valid) {
			Point pt = {0, 0};
			this->OnClick(pt, WID_SCL_CLASS_GENERAL, 1);
		} else if (data == 0) {
			this->ReInit();
		}
	}
};

static const NWidgetPart _nested_select_company_livery_widgets [] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SCL_CAPTION), SetDataTip(STR_LIVERY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_GENERAL), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_COMPANY_GENERAL, STR_LIVERY_GENERAL_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_RAIL), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_TRAINLIST, STR_LIVERY_TRAIN_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_ROAD), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_TRUCKLIST, STR_LIVERY_ROAD_VEHICLE_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_SHIP), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_SHIPLIST, STR_LIVERY_SHIP_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_AIRCRAFT), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_AIRPLANESLIST, STR_LIVERY_AIRCRAFT_TOOLTIP),
		NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(90, 22), SetFill(1, 1), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_SCL_SPACER_DROPDOWN), SetMinimalSize(150, 12), SetFill(1, 1), EndContainer(),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_SCL_PRI_COL_DROPDOWN), SetMinimalSize(125, 12), SetFill(0, 1), SetDataTip(STR_BLACK_STRING, STR_LIVERY_PRIMARY_TOOLTIP),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_SCL_SEC_COL_DROPDOWN), SetMinimalSize(125, 12), SetFill(0, 1),
				SetDataTip(STR_BLACK_STRING, STR_LIVERY_SECONDARY_TOOLTIP),
	EndContainer(),
	NWidget(WWT_MATRIX, COLOUR_GREY, WID_SCL_MATRIX), SetMinimalSize(275, 15), SetFill(1, 0), SetDataTip((1 << MAT_ROW_START) | (1 << MAT_COL_START), STR_LIVERY_PANEL_TOOLTIP),
};

static const WindowDesc _select_company_livery_desc(
	WDP_AUTO, 0, 0,
	WC_COMPANY_COLOUR, WC_NONE,
	0,
	_nested_select_company_livery_widgets, lengthof(_nested_select_company_livery_widgets)
);

/**
 * Draws the face of a company manager's face.
 * @param cmf   the company manager's face
 * @param colour the (background) colour of the gradient
 * @param x     x-position to draw the face
 * @param y     y-position to draw the face
 */
void DrawCompanyManagerFace(CompanyManagerFace cmf, int colour, int x, int y)
{
	GenderEthnicity ge = (GenderEthnicity)GetCompanyManagerFaceBits(cmf, CMFV_GEN_ETHN, GE_WM);

	bool has_moustache   = !HasBit(ge, GENDER_FEMALE) && GetCompanyManagerFaceBits(cmf, CMFV_HAS_MOUSTACHE,   ge) != 0;
	bool has_tie_earring = !HasBit(ge, GENDER_FEMALE) || GetCompanyManagerFaceBits(cmf, CMFV_HAS_TIE_EARRING, ge) != 0;
	bool has_glasses     = GetCompanyManagerFaceBits(cmf, CMFV_HAS_GLASSES, ge) != 0;
	PaletteID pal;

	/* Modify eye colour palette only if 2 or more valid values exist */
	if (_cmf_info[CMFV_EYE_COLOUR].valid_values[ge] < 2) {
		pal = PAL_NONE;
	} else {
		switch (GetCompanyManagerFaceBits(cmf, CMFV_EYE_COLOUR, ge)) {
			default: NOT_REACHED();
			case 0: pal = PALETTE_TO_BROWN; break;
			case 1: pal = PALETTE_TO_BLUE;  break;
			case 2: pal = PALETTE_TO_GREEN; break;
		}
	}

	/* Draw the gradient (background) */
	DrawSprite(SPR_GRADIENT, GENERAL_SPRITE_COLOUR(colour), x, y);

	for (CompanyManagerFaceVariable cmfv = CMFV_CHEEKS; cmfv < CMFV_END; cmfv++) {
		switch (cmfv) {
			case CMFV_MOUSTACHE:   if (!has_moustache)   continue; break;
			case CMFV_LIPS:        // FALL THROUGH
			case CMFV_NOSE:        if (has_moustache)    continue; break;
			case CMFV_TIE_EARRING: if (!has_tie_earring) continue; break;
			case CMFV_GLASSES:     if (!has_glasses)     continue; break;
			default: break;
		}
		DrawSprite(GetCompanyManagerFaceSprite(cmf, cmfv, ge), (cmfv == CMFV_EYEBROWS) ? pal : PAL_NONE, x, y);
	}
}

/** Nested widget description for the company manager face selection dialog */
static const NWidgetPart _nested_select_company_manager_face_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SCMF_CAPTION), SetDataTip(STR_FACE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCMF_TOGGLE_LARGE_SMALL), SetDataTip(SPR_LARGE_SMALL_WINDOW, STR_FACE_ADVANCED_TOOLTIP),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_SCMF_SELECT_FACE),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 2, 2),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_EMPTY, COLOUR_GREY, WID_SCMF_FACE), SetMinimalSize(92, 119),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_RANDOM_NEW_FACE), SetFill(1, 0), SetDataTip(STR_FACE_NEW_FACE_BUTTON, STR_FACE_NEW_FACE_TOOLTIP),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCMF_SEL_LOADSAVE), // Load/number/save buttons under the portrait in the advanced view.
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetMinimalSize(0, 5), SetFill(0, 1),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_LOAD), SetFill(1, 0), SetDataTip(STR_FACE_LOAD, STR_FACE_LOAD_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_FACECODE), SetFill(1, 0), SetDataTip(STR_FACE_FACECODE, STR_FACE_FACECODE_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_SAVE), SetFill(1, 0), SetDataTip(STR_FACE_SAVE, STR_FACE_SAVE_TOOLTIP),
						NWidget(NWID_SPACER), SetMinimalSize(0, 5), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_TOGGLE_LARGE_SMALL_BUTTON), SetFill(1, 0), SetDataTip(STR_FACE_ADVANCED, STR_FACE_ADVANCED_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCMF_SEL_MALEFEMALE), // Simple male/female face setting.
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetFill(0, 1),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_MALE), SetFill(1, 0), SetDataTip(STR_FACE_MALE_BUTTON, STR_FACE_MALE_TOOLTIP),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_FEMALE), SetFill(1, 0), SetDataTip(STR_FACE_FEMALE_BUTTON, STR_FACE_FEMALE_TOOLTIP),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCMF_SEL_PARTS), // Advanced face parts setting.
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetMinimalSize(0, 2),
						NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_MALE2), SetFill(1, 0), SetDataTip(STR_FACE_MALE_BUTTON, STR_FACE_MALE_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_FEMALE2), SetFill(1, 0), SetDataTip(STR_FACE_FEMALE_BUTTON, STR_FACE_FEMALE_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SPACER), SetMinimalSize(0, 2),
						NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_ETHNICITY_EUR), SetFill(1, 0), SetDataTip(STR_FACE_EUROPEAN, STR_FACE_SELECT_EUROPEAN),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_ETHNICITY_AFR), SetFill(1, 0), SetDataTip(STR_FACE_AFRICAN, STR_FACE_SELECT_AFRICAN),
						EndContainer(),
						NWidget(NWID_SPACER), SetMinimalSize(0, 4),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_HAS_MOUSTACHE_EARRING), SetDataTip(STR_EMPTY, STR_FACE_MOUSTACHE_EARRING_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_HAS_GLASSES_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_HAS_GLASSES), SetDataTip(STR_EMPTY, STR_FACE_GLASSES_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetFill(1, 0),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_HAIR_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_HAIR_L), SetDataTip(AWV_DECREASE, STR_FACE_HAIR_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_HAIR), SetDataTip(STR_EMPTY, STR_FACE_HAIR_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_HAIR_R), SetDataTip(AWV_INCREASE, STR_FACE_HAIR_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_EYEBROWS_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYEBROWS_L), SetDataTip(AWV_DECREASE, STR_FACE_EYEBROWS_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_EYEBROWS), SetDataTip(STR_EMPTY, STR_FACE_EYEBROWS_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYEBROWS_R), SetDataTip(AWV_INCREASE, STR_FACE_EYEBROWS_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_EYECOLOUR_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYECOLOUR_L), SetDataTip(AWV_DECREASE, STR_FACE_EYECOLOUR_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_EYECOLOUR), SetDataTip(STR_EMPTY, STR_FACE_EYECOLOUR_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYECOLOUR_R), SetDataTip(AWV_INCREASE, STR_FACE_EYECOLOUR_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_GLASSES_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_GLASSES_L), SetDataTip(AWV_DECREASE, STR_FACE_GLASSES_TOOLTIP_2),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_GLASSES), SetDataTip(STR_EMPTY, STR_FACE_GLASSES_TOOLTIP_2),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_GLASSES_R), SetDataTip(AWV_INCREASE, STR_FACE_GLASSES_TOOLTIP_2),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_NOSE_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_NOSE_L), SetDataTip(AWV_DECREASE, STR_FACE_NOSE_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_NOSE), SetDataTip(STR_EMPTY, STR_FACE_NOSE_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_NOSE_R), SetDataTip(AWV_INCREASE, STR_FACE_NOSE_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_LIPS_MOUSTACHE_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_LIPS_MOUSTACHE_L), SetDataTip(AWV_DECREASE, STR_FACE_LIPS_MOUSTACHE_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_LIPS_MOUSTACHE), SetDataTip(STR_EMPTY, STR_FACE_LIPS_MOUSTACHE_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_LIPS_MOUSTACHE_R), SetDataTip(AWV_INCREASE, STR_FACE_LIPS_MOUSTACHE_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_CHIN_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_CHIN_L), SetDataTip(AWV_DECREASE, STR_FACE_CHIN_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_CHIN), SetDataTip(STR_EMPTY, STR_FACE_CHIN_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_CHIN_R), SetDataTip(AWV_INCREASE, STR_FACE_CHIN_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_JACKET_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_JACKET_L), SetDataTip(AWV_DECREASE, STR_FACE_JACKET_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_JACKET), SetDataTip(STR_EMPTY, STR_FACE_JACKET_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_JACKET_R), SetDataTip(AWV_INCREASE, STR_FACE_JACKET_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_COLLAR_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_COLLAR_L), SetDataTip(AWV_DECREASE, STR_FACE_COLLAR_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_COLLAR), SetDataTip(STR_EMPTY, STR_FACE_COLLAR_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_COLLAR_R), SetDataTip(AWV_INCREASE, STR_FACE_COLLAR_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_SCMF_TIE_EARRING_TEXT), SetFill(1, 0),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_TIE_EARRING_L), SetDataTip(AWV_DECREASE, STR_FACE_TIE_EARRING_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_TIE_EARRING), SetDataTip(STR_EMPTY, STR_FACE_TIE_EARRING_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_TIE_EARRING_R), SetDataTip(AWV_INCREASE, STR_FACE_TIE_EARRING_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_CANCEL), SetFill(1, 0), SetDataTip(STR_BUTTON_CANCEL, STR_FACE_CANCEL_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_ACCEPT), SetFill(1, 0), SetDataTip(STR_BUTTON_OK, STR_FACE_OK_TOOLTIP),
	EndContainer(),
};

/** Management class for customizing the face of the company manager. */
class SelectCompanyManagerFaceWindow : public Window
{
	CompanyManagerFace face; ///< company manager face bits
	bool advanced; ///< advanced company manager face selection window

	GenderEthnicity ge; ///< Gender and ethnicity.
	bool is_female;     ///< Female face.
	bool is_moust_male; ///< Male face with a moustache.

	Dimension yesno_dim;  ///< Dimension of a yes/no button of a part in the advanced face window.
	Dimension number_dim; ///< Dimension of a number widget of a part in the advanced face window.

	static const StringID PART_TEXTS_IS_FEMALE[]; ///< Strings depending on #is_female, used to describe parts (2 entries for a part).
	static const StringID PART_TEXTS[];           ///< Fixed strings to describe parts of the face.

	/**
	 * Draw dynamic a label to the left of the button and a value in the button
	 *
	 * @param widget_index   index of this widget in the window
	 * @param val            the value which will be draw
	 * @param is_bool_widget is it a bool button
	 */
	void DrawFaceStringLabel(byte widget_index, uint8 val, bool is_bool_widget) const
	{
		StringID str;
		const NWidgetCore *nwi_widget = this->GetWidget<NWidgetCore>(widget_index);
		if (!nwi_widget->IsDisabled()) {
			if (is_bool_widget) {
				/* if it a bool button write yes or no */
				str = (val != 0) ? STR_FACE_YES : STR_FACE_NO;
			} else {
				/* else write the value + 1 */
				SetDParam(0, val + 1);
				str = STR_JUST_INT;
			}

			/* Draw the value/bool in white (0xC). If the button clicked adds 1px to x and y text coordinates (IsWindowWidgetLowered()). */
			DrawString(nwi_widget->pos_x + nwi_widget->IsLowered(), nwi_widget->pos_x + nwi_widget->current_x - 1 - nwi_widget->IsLowered(),
					nwi_widget->pos_y + 1 + nwi_widget->IsLowered(), str, TC_WHITE, SA_HOR_CENTER);
		}
	}

	void UpdateData()
	{
		this->ge = (GenderEthnicity)GB(this->face, _cmf_info[CMFV_GEN_ETHN].offset, _cmf_info[CMFV_GEN_ETHN].length); // get the gender and ethnicity
		this->is_female = HasBit(this->ge, GENDER_FEMALE); // get the gender: 0 == male and 1 == female
		this->is_moust_male = !is_female && GetCompanyManagerFaceBits(this->face, CMFV_HAS_MOUSTACHE, this->ge) != 0; // is a male face with moustache
	}

public:
	SelectCompanyManagerFaceWindow(const WindowDesc *desc, Window *parent) : Window()
	{
		this->advanced = false;
		this->CreateNestedTree(desc);
		this->SelectDisplayPlanes(this->advanced);
		this->FinishInitNested(desc, parent->window_number);
		this->parent = parent;
		this->owner = (Owner)this->window_number;
		this->face = Company::Get((CompanyID)this->window_number)->face;

		this->UpdateData();
	}

	/**
	 * Select planes to display to the user with the #NWID_SELECTION widgets #WID_SCMF_SEL_LOADSAVE, #WID_SCMF_SEL_MALEFEMALE, and #WID_SCMF_SEL_PARTS.
	 * @param advanced Display advanced face management window.
	 */
	void SelectDisplayPlanes(bool advanced)
	{
		this->GetWidget<NWidgetStacked>(WID_SCMF_SEL_LOADSAVE)->SetDisplayedPlane(advanced ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_SCMF_SEL_PARTS)->SetDisplayedPlane(advanced ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_SCMF_SEL_MALEFEMALE)->SetDisplayedPlane(advanced ? SZSP_NONE : 0);
		this->GetWidget<NWidgetCore>(WID_SCMF_RANDOM_NEW_FACE)->widget_data = advanced ? STR_MAPGEN_RANDOM : STR_FACE_NEW_FACE_BUTTON;

		NWidgetCore *wi = this->GetWidget<NWidgetCore>(WID_SCMF_TOGGLE_LARGE_SMALL_BUTTON);
		if (advanced) {
			wi->SetDataTip(STR_FACE_SIMPLE, STR_FACE_SIMPLE_TOOLTIP);
		} else {
			wi->SetDataTip(STR_FACE_ADVANCED, STR_FACE_ADVANCED_TOOLTIP);
		}
	}

	virtual void OnInit()
	{
		/* Size of the boolean yes/no button. */
		Dimension yesno_dim = maxdim(GetStringBoundingBox(STR_FACE_YES), GetStringBoundingBox(STR_FACE_NO));
		yesno_dim.width  += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
		yesno_dim.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
		/* Size of the number button + arrows. */
		Dimension number_dim = {0, 0};
		for (int val = 1; val <= 12; val++) {
			SetDParam(0, val);
			number_dim = maxdim(number_dim, GetStringBoundingBox(STR_JUST_INT));
		}
		uint arrows_width = GetSpriteSize(SPR_ARROW_LEFT).width + GetSpriteSize(SPR_ARROW_RIGHT).width + 2 * (WD_IMGBTN_LEFT + WD_IMGBTN_RIGHT);
		number_dim.width += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT + arrows_width;
		number_dim.height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
		/* Compute width of both buttons. */
		yesno_dim.width = max(yesno_dim.width, number_dim.width);
		number_dim.width = yesno_dim.width - arrows_width;

		this->yesno_dim = yesno_dim;
		this->number_dim = number_dim;
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT:
			case WID_SCMF_TIE_EARRING_TEXT: {
				int offset = (widget - WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT) * 2;
				*size = maxdim(GetStringBoundingBox(PART_TEXTS_IS_FEMALE[offset]), GetStringBoundingBox(PART_TEXTS_IS_FEMALE[offset + 1]));
				size->width  += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				size->height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;
			}

			case WID_SCMF_LIPS_MOUSTACHE_TEXT:
				*size = maxdim(GetStringBoundingBox(STR_FACE_LIPS), GetStringBoundingBox(STR_FACE_MOUSTACHE));
				size->width  += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				size->height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;

			case WID_SCMF_HAS_GLASSES_TEXT:
			case WID_SCMF_HAIR_TEXT:
			case WID_SCMF_EYEBROWS_TEXT:
			case WID_SCMF_EYECOLOUR_TEXT:
			case WID_SCMF_GLASSES_TEXT:
			case WID_SCMF_NOSE_TEXT:
			case WID_SCMF_CHIN_TEXT:
			case WID_SCMF_JACKET_TEXT:
			case WID_SCMF_COLLAR_TEXT:
				*size = GetStringBoundingBox(PART_TEXTS[widget - WID_SCMF_HAS_GLASSES_TEXT]);
				size->width  += WD_FRAMERECT_LEFT + WD_FRAMERECT_RIGHT;
				size->height += WD_FRAMERECT_TOP + WD_FRAMERECT_BOTTOM;
				break;

			case WID_SCMF_HAS_MOUSTACHE_EARRING:
			case WID_SCMF_HAS_GLASSES:
				*size = this->yesno_dim;
				break;

			case WID_SCMF_EYECOLOUR:
			case WID_SCMF_CHIN:
			case WID_SCMF_EYEBROWS:
			case WID_SCMF_LIPS_MOUSTACHE:
			case WID_SCMF_NOSE:
			case WID_SCMF_HAIR:
			case WID_SCMF_JACKET:
			case WID_SCMF_COLLAR:
			case WID_SCMF_TIE_EARRING:
			case WID_SCMF_GLASSES:
				*size = this->number_dim;
				break;
		}
	}

	virtual void OnPaint()
	{
		/* lower the non-selected gender button */
		this->SetWidgetsLoweredState(!this->is_female, WID_SCMF_MALE, WID_SCMF_MALE2, WIDGET_LIST_END);
		this->SetWidgetsLoweredState( this->is_female, WID_SCMF_FEMALE, WID_SCMF_FEMALE2, WIDGET_LIST_END);

		/* advanced company manager face selection window */

		/* lower the non-selected ethnicity button */
		this->SetWidgetLoweredState(WID_SCMF_ETHNICITY_EUR, !HasBit(this->ge, ETHNICITY_BLACK));
		this->SetWidgetLoweredState(WID_SCMF_ETHNICITY_AFR,  HasBit(this->ge, ETHNICITY_BLACK));


		/* Disable dynamically the widgets which CompanyManagerFaceVariable has less than 2 options
		 * (or in other words you haven't any choice).
		 * If the widgets depend on a HAS-variable and this is false the widgets will be disabled, too. */

		/* Eye colour buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_EYE_COLOUR].valid_values[this->ge] < 2,
				WID_SCMF_EYECOLOUR, WID_SCMF_EYECOLOUR_L, WID_SCMF_EYECOLOUR_R, WIDGET_LIST_END);

		/* Chin buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_CHIN].valid_values[this->ge] < 2,
				WID_SCMF_CHIN, WID_SCMF_CHIN_L, WID_SCMF_CHIN_R, WIDGET_LIST_END);

		/* Eyebrows buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_EYEBROWS].valid_values[this->ge] < 2,
				WID_SCMF_EYEBROWS, WID_SCMF_EYEBROWS_L, WID_SCMF_EYEBROWS_R, WIDGET_LIST_END);

		/* Lips or (if it a male face with a moustache) moustache buttons */
		this->SetWidgetsDisabledState(_cmf_info[this->is_moust_male ? CMFV_MOUSTACHE : CMFV_LIPS].valid_values[this->ge] < 2,
				WID_SCMF_LIPS_MOUSTACHE, WID_SCMF_LIPS_MOUSTACHE_L, WID_SCMF_LIPS_MOUSTACHE_R, WIDGET_LIST_END);

		/* Nose buttons | male faces with moustache haven't any nose options */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_NOSE].valid_values[this->ge] < 2 || this->is_moust_male,
				WID_SCMF_NOSE, WID_SCMF_NOSE_L, WID_SCMF_NOSE_R, WIDGET_LIST_END);

		/* Hair buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_HAIR].valid_values[this->ge] < 2,
				WID_SCMF_HAIR, WID_SCMF_HAIR_L, WID_SCMF_HAIR_R, WIDGET_LIST_END);

		/* Jacket buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_JACKET].valid_values[this->ge] < 2,
				WID_SCMF_JACKET, WID_SCMF_JACKET_L, WID_SCMF_JACKET_R, WIDGET_LIST_END);

		/* Collar buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_COLLAR].valid_values[this->ge] < 2,
				WID_SCMF_COLLAR, WID_SCMF_COLLAR_L, WID_SCMF_COLLAR_R, WIDGET_LIST_END);

		/* Tie/earring buttons | female faces without earring haven't any earring options */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_TIE_EARRING].valid_values[this->ge] < 2 ||
					(this->is_female && GetCompanyManagerFaceBits(this->face, CMFV_HAS_TIE_EARRING, this->ge) == 0),
				WID_SCMF_TIE_EARRING, WID_SCMF_TIE_EARRING_L, WID_SCMF_TIE_EARRING_R, WIDGET_LIST_END);

		/* Glasses buttons | faces without glasses haven't any glasses options */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_GLASSES].valid_values[this->ge] < 2 || GetCompanyManagerFaceBits(this->face, CMFV_HAS_GLASSES, this->ge) == 0,
				WID_SCMF_GLASSES, WID_SCMF_GLASSES_L, WID_SCMF_GLASSES_R, WIDGET_LIST_END);

		this->DrawWidgets();
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT:
			case WID_SCMF_TIE_EARRING_TEXT: {
				StringID str = PART_TEXTS_IS_FEMALE[(widget - WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT) * 2 + this->is_female];
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, str, TC_GOLD, SA_RIGHT);
				break;
			}

			case WID_SCMF_LIPS_MOUSTACHE_TEXT:
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, (this->is_moust_male) ? STR_FACE_MOUSTACHE : STR_FACE_LIPS, TC_GOLD, SA_RIGHT);
				break;

			case WID_SCMF_HAS_GLASSES_TEXT:
			case WID_SCMF_HAIR_TEXT:
			case WID_SCMF_EYEBROWS_TEXT:
			case WID_SCMF_EYECOLOUR_TEXT:
			case WID_SCMF_GLASSES_TEXT:
			case WID_SCMF_NOSE_TEXT:
			case WID_SCMF_CHIN_TEXT:
			case WID_SCMF_JACKET_TEXT:
			case WID_SCMF_COLLAR_TEXT:
				DrawString(r.left + WD_FRAMERECT_LEFT, r.right - WD_FRAMERECT_RIGHT, r.top + WD_FRAMERECT_TOP, PART_TEXTS[widget - WID_SCMF_HAS_GLASSES_TEXT], TC_GOLD, SA_RIGHT);
				break;


			case WID_SCMF_HAS_MOUSTACHE_EARRING:
				if (this->is_female) { // Only for female faces
					this->DrawFaceStringLabel(WID_SCMF_HAS_MOUSTACHE_EARRING, GetCompanyManagerFaceBits(this->face, CMFV_HAS_TIE_EARRING, this->ge), true);
				} else { // Only for male faces
					this->DrawFaceStringLabel(WID_SCMF_HAS_MOUSTACHE_EARRING, GetCompanyManagerFaceBits(this->face, CMFV_HAS_MOUSTACHE,   this->ge), true);
				}
				break;

			case WID_SCMF_TIE_EARRING:
				if (this->is_female) { // Only for female faces
					this->DrawFaceStringLabel(WID_SCMF_TIE_EARRING, GetCompanyManagerFaceBits(this->face, CMFV_TIE_EARRING, this->ge), false);
				} else { // Only for male faces
					this->DrawFaceStringLabel(WID_SCMF_TIE_EARRING, GetCompanyManagerFaceBits(this->face, CMFV_TIE_EARRING, this->ge), false);
				}
				break;

			case WID_SCMF_LIPS_MOUSTACHE:
				if (this->is_moust_male) { // Only for male faces with moustache
					this->DrawFaceStringLabel(WID_SCMF_LIPS_MOUSTACHE, GetCompanyManagerFaceBits(this->face, CMFV_MOUSTACHE, this->ge), false);
				} else { // Only for female faces or male faces without moustache
					this->DrawFaceStringLabel(WID_SCMF_LIPS_MOUSTACHE, GetCompanyManagerFaceBits(this->face, CMFV_LIPS,      this->ge), false);
				}
				break;

			case WID_SCMF_HAS_GLASSES:
				this->DrawFaceStringLabel(WID_SCMF_HAS_GLASSES, GetCompanyManagerFaceBits(this->face, CMFV_HAS_GLASSES, this->ge), true );
				break;

			case WID_SCMF_HAIR:
				this->DrawFaceStringLabel(WID_SCMF_HAIR,        GetCompanyManagerFaceBits(this->face, CMFV_HAIR,        this->ge), false);
				break;

			case WID_SCMF_EYEBROWS:
				this->DrawFaceStringLabel(WID_SCMF_EYEBROWS,    GetCompanyManagerFaceBits(this->face, CMFV_EYEBROWS,    this->ge), false);
				break;

			case WID_SCMF_EYECOLOUR:
				this->DrawFaceStringLabel(WID_SCMF_EYECOLOUR,   GetCompanyManagerFaceBits(this->face, CMFV_EYE_COLOUR,  this->ge), false);
				break;

			case WID_SCMF_GLASSES:
				this->DrawFaceStringLabel(WID_SCMF_GLASSES,     GetCompanyManagerFaceBits(this->face, CMFV_GLASSES,     this->ge), false);
				break;

			case WID_SCMF_NOSE:
				this->DrawFaceStringLabel(WID_SCMF_NOSE,        GetCompanyManagerFaceBits(this->face, CMFV_NOSE,        this->ge), false);
				break;

			case WID_SCMF_CHIN:
				this->DrawFaceStringLabel(WID_SCMF_CHIN,        GetCompanyManagerFaceBits(this->face, CMFV_CHIN,        this->ge), false);
				break;

			case WID_SCMF_JACKET:
				this->DrawFaceStringLabel(WID_SCMF_JACKET,      GetCompanyManagerFaceBits(this->face, CMFV_JACKET,      this->ge), false);
				break;

			case WID_SCMF_COLLAR:
				this->DrawFaceStringLabel(WID_SCMF_COLLAR,      GetCompanyManagerFaceBits(this->face, CMFV_COLLAR,      this->ge), false);
				break;

			case WID_SCMF_FACE:
				DrawCompanyManagerFace(this->face, Company::Get((CompanyID)this->window_number)->colour, r.left, r.top);
				break;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			/* Toggle size, advanced/simple face selection */
			case WID_SCMF_TOGGLE_LARGE_SMALL:
			case WID_SCMF_TOGGLE_LARGE_SMALL_BUTTON:
				this->advanced = !this->advanced;
				this->SelectDisplayPlanes(this->advanced);
				this->ReInit();
				break;

			/* OK button */
			case WID_SCMF_ACCEPT:
				DoCommandP(0, 0, this->face, CMD_SET_COMPANY_MANAGER_FACE);
				/* FALL THROUGH */

			/* Cancel button */
			case WID_SCMF_CANCEL:
				delete this;
				break;

			/* Load button */
			case WID_SCMF_LOAD:
				this->face = _company_manager_face;
				ScaleAllCompanyManagerFaceBits(this->face);
				ShowErrorMessage(STR_FACE_LOAD_DONE, INVALID_STRING_ID, WL_INFO);
				this->UpdateData();
				this->SetDirty();
				break;

			/* 'Company manager face number' button, view and/or set company manager face number */
			case WID_SCMF_FACECODE:
				SetDParam(0, this->face);
				ShowQueryString(STR_JUST_INT, STR_FACE_FACECODE_CAPTION, 10 + 1, this, CS_NUMERAL, QSF_NONE);
				break;

			/* Save button */
			case WID_SCMF_SAVE:
				_company_manager_face = this->face;
				ShowErrorMessage(STR_FACE_SAVE_DONE, INVALID_STRING_ID, WL_INFO);
				break;

			/* Toggle gender (male/female) button */
			case WID_SCMF_MALE:
			case WID_SCMF_FEMALE:
			case WID_SCMF_MALE2:
			case WID_SCMF_FEMALE2:
				SetCompanyManagerFaceBits(this->face, CMFV_GENDER, this->ge, (widget == WID_SCMF_FEMALE || widget == WID_SCMF_FEMALE2));
				ScaleAllCompanyManagerFaceBits(this->face);
				this->UpdateData();
				this->SetDirty();
				break;

			/* Randomize face button */
			case WID_SCMF_RANDOM_NEW_FACE:
				RandomCompanyManagerFaceBits(this->face, this->ge, this->advanced);
				this->UpdateData();
				this->SetDirty();
				break;

			/* Toggle ethnicity (european/african) button */
			case WID_SCMF_ETHNICITY_EUR:
			case WID_SCMF_ETHNICITY_AFR:
				SetCompanyManagerFaceBits(this->face, CMFV_ETHNICITY, this->ge, widget - WID_SCMF_ETHNICITY_EUR);
				ScaleAllCompanyManagerFaceBits(this->face);
				this->UpdateData();
				this->SetDirty();
				break;

			default:
				/* Here all buttons from WID_SCMF_HAS_MOUSTACHE_EARRING to WID_SCMF_GLASSES_R are handled.
				 * First it checks which CompanyManagerFaceVariable is being changed, and then either
				 * a: invert the value for boolean variables, or
				 * b: it checks inside of IncreaseCompanyManagerFaceBits() if a left (_L) butten is pressed and then decrease else increase the variable */
				if (widget >= WID_SCMF_HAS_MOUSTACHE_EARRING && widget <= WID_SCMF_GLASSES_R) {
					CompanyManagerFaceVariable cmfv; // which CompanyManagerFaceVariable shall be edited

					if (widget < WID_SCMF_EYECOLOUR_L) { // Bool buttons
						switch (widget - WID_SCMF_HAS_MOUSTACHE_EARRING) {
							default: NOT_REACHED();
							case 0: cmfv = this->is_female ? CMFV_HAS_TIE_EARRING : CMFV_HAS_MOUSTACHE; break; // Has earring/moustache button
							case 1: cmfv = CMFV_HAS_GLASSES; break; // Has glasses button
						}
						SetCompanyManagerFaceBits(this->face, cmfv, this->ge, !GetCompanyManagerFaceBits(this->face, cmfv, this->ge));
						ScaleAllCompanyManagerFaceBits(this->face);
					} else { // Value buttons
						switch ((widget - WID_SCMF_EYECOLOUR_L) / 3) {
							default: NOT_REACHED();
							case 0: cmfv = CMFV_EYE_COLOUR; break;  // Eye colour buttons
							case 1: cmfv = CMFV_CHIN; break;        // Chin buttons
							case 2: cmfv = CMFV_EYEBROWS; break;    // Eyebrows buttons
							case 3: cmfv = this->is_moust_male ? CMFV_MOUSTACHE : CMFV_LIPS; break; // Moustache or lips buttons
							case 4: cmfv = CMFV_NOSE; break;        // Nose buttons
							case 5: cmfv = CMFV_HAIR; break;        // Hair buttons
							case 6: cmfv = CMFV_JACKET; break;      // Jacket buttons
							case 7: cmfv = CMFV_COLLAR; break;      // Collar buttons
							case 8: cmfv = CMFV_TIE_EARRING; break; // Tie/earring buttons
							case 9: cmfv = CMFV_GLASSES; break;     // Glasses buttons
						}
						/* 0 == left (_L), 1 == middle or 2 == right (_R) - button click */
						IncreaseCompanyManagerFaceBits(this->face, cmfv, this->ge, (((widget - WID_SCMF_EYECOLOUR_L) % 3) != 0) ? 1 : -1);
					}
					this->UpdateData();
					this->SetDirty();
				}
				break;
		}
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;
		/* Set a new company manager face number */
		if (!StrEmpty(str)) {
			this->face = strtoul(str, NULL, 10);
			ScaleAllCompanyManagerFaceBits(this->face);
			ShowErrorMessage(STR_FACE_FACECODE_SET, INVALID_STRING_ID, WL_INFO);
			this->UpdateData();
			this->SetDirty();
		} else {
			ShowErrorMessage(STR_FACE_FACECODE_ERR, INVALID_STRING_ID, WL_INFO);
		}
	}
};

/** Both text values of parts of the face that depend on the #is_female boolean value. */
const StringID SelectCompanyManagerFaceWindow::PART_TEXTS_IS_FEMALE[] = {
	STR_FACE_MOUSTACHE, STR_FACE_EARRING, // WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT
	STR_FACE_TIE,       STR_FACE_EARRING, // WID_SCMF_TIE_EARRING_TEXT
};

/** Textual names for parts of the face. */
const StringID SelectCompanyManagerFaceWindow::PART_TEXTS[] = {
	STR_FACE_GLASSES,   // WID_SCMF_HAS_GLASSES_TEXT
	STR_FACE_HAIR,      // WID_SCMF_HAIR_TEXT
	STR_FACE_EYEBROWS,  // WID_SCMF_EYEBROWS_TEXT
	STR_FACE_EYECOLOUR, // WID_SCMF_EYECOLOUR_TEXT
	STR_FACE_GLASSES,   // WID_SCMF_GLASSES_TEXT
	STR_FACE_NOSE,      // WID_SCMF_NOSE_TEXT
	STR_FACE_CHIN,      // WID_SCMF_CHIN_TEXT
	STR_FACE_JACKET,    // WID_SCMF_JACKET_TEXT
	STR_FACE_COLLAR,    // WID_SCMF_COLLAR_TEXT
};

/** Company manager face selection window description */
static const WindowDesc _select_company_manager_face_desc(
	WDP_AUTO, 0, 0,
	WC_COMPANY_MANAGER_FACE, WC_NONE,
	WDF_UNCLICK_BUTTONS | WDF_CONSTRUCTION,
	_nested_select_company_manager_face_widgets, lengthof(_nested_select_company_manager_face_widgets)
);

/**
 * Open the simple/advanced company manager face selection window
 *
 * @param parent the parent company window
 * @param adv    simple or advanced face selection window
 * @param top    previous top position of the window
 * @param left   previous left position of the window
 */
static void DoSelectCompanyManagerFace(Window *parent)
{
	if (!Company::IsValidID((CompanyID)parent->window_number)) return;

	if (BringWindowToFrontById(WC_COMPANY_MANAGER_FACE, parent->window_number)) return;
	new SelectCompanyManagerFaceWindow(&_select_company_manager_face_desc, parent);
}

static const NWidgetPart _nested_company_infrastructure_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_CI_CAPTION), SetDataTip(STR_COMPANY_INFRASTRUCTURE_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VERTICAL), SetPIP(WD_FRAMERECT_TOP, 4, WD_FRAMETEXT_BOTTOM),
			NWidget(NWID_HORIZONTAL), SetPIP(2, 4, 2),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_RAIL_DESC), SetMinimalTextLines(2, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_RAIL_COUNT), SetMinimalTextLines(2, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(2, 4, 2),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_ROAD_DESC), SetMinimalTextLines(2, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_ROAD_COUNT), SetMinimalTextLines(2, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(2, 4, 2),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_WATER_DESC), SetMinimalTextLines(2, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_WATER_COUNT), SetMinimalTextLines(2, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(2, 4, 2),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_STATION_DESC), SetMinimalTextLines(3, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_STATION_COUNT), SetMinimalTextLines(3, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(2, 4, 2),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_TOTAL_DESC), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_TOTAL), SetFill(0, 1),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/**
 * Window with detailed information about the company's infrastructure.
 */
struct CompanyInfrastructureWindow : Window
{
	RailTypes railtypes; ///< Valid railtypes.
	RoadTypes roadtypes; ///< Valid roadtypes.

	uint total_width; ///< String width of the total cost line.

	CompanyInfrastructureWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->UpdateRailRoadTypes();

		this->InitNested(desc, window_number);
		this->owner = (Owner)this->window_number;
	}

	void UpdateRailRoadTypes()
	{
		this->railtypes = RAILTYPES_NONE;
		this->roadtypes = ROADTYPES_ROAD; // Road is always available.

		/* Find the used railtypes. */
		Engine *e;
		FOR_ALL_ENGINES_OF_TYPE(e, VEH_TRAIN) {
			if (!HasBit(e->info.climates, _settings_game.game_creation.landscape)) continue;

			this->railtypes |= GetRailTypeInfo(e->u.rail.railtype)->introduces_railtypes;
		}

		/* Get the date introduced railtypes as well. */
		this->railtypes = AddDateIntroducedRailTypes(this->railtypes, MAX_DAY);

		/* Tram is only visible when there will be a tram. */
		FOR_ALL_ENGINES_OF_TYPE(e, VEH_ROAD) {
			if (!HasBit(e->info.climates, _settings_game.game_creation.landscape)) continue;
			if (!HasBit(e->info.misc_flags, EF_ROAD_TRAM)) continue;

			this->roadtypes |= ROADTYPES_TRAM;
			break;
		}
	}

	/** Get total infrastructure maintenance cost. */
	Money GetTotalMaintenanceCost() const
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		Money total;

		for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
			if (HasBit(this->railtypes, rt)) total += RailMaintenanceCost(rt, c->infrastructure.rail[rt]);
		}
		total += SignalMaintenanceCost(c->infrastructure.signal);

		if (HasBit(this->roadtypes, ROADTYPE_ROAD)) total += RoadMaintenanceCost(ROADTYPE_ROAD, c->infrastructure.road[ROADTYPE_ROAD]);
		if (HasBit(this->roadtypes, ROADTYPE_TRAM)) total += RoadMaintenanceCost(ROADTYPE_TRAM, c->infrastructure.road[ROADTYPE_TRAM]);

		total += CanalMaintenanceCost(c->infrastructure.water);
		total += StationMaintenanceCost(c->infrastructure.station);
		total += AirportMaintenanceCost(c->index);

		return total;
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_CI_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				break;
		}
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		const Company *c = Company::Get((CompanyID)this->window_number);

		switch (widget) {
			case WID_CI_RAIL_DESC: {
				uint lines = 1;

				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_RAIL_SECT).width);

				for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
					if (HasBit(this->railtypes, rt)) {
						lines++;
						SetDParam(0, GetRailTypeInfo(rt)->strings.name);
						size->width = max(size->width, GetStringBoundingBox(STR_WHITE_STRING).width + WD_FRAMERECT_LEFT);
					}
				}
				if (this->railtypes != RAILTYPES_NONE) {
					lines++;
					size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_SIGNALS).width + WD_FRAMERECT_LEFT);
				}

				size->height = max(size->height, lines * FONT_HEIGHT_NORMAL);
				break;
			}

			case WID_CI_ROAD_DESC: {
				uint lines = 1;

				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_ROAD_SECT).width);

				if (HasBit(this->roadtypes, ROADTYPE_ROAD)) {
					lines++;
					size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_ROAD).width + WD_FRAMERECT_LEFT);
				}
				if (HasBit(this->roadtypes, ROADTYPE_TRAM)) {
					lines++;
					size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_TRAMWAY).width + WD_FRAMERECT_LEFT);
				}

				size->height = max(size->height, lines * FONT_HEIGHT_NORMAL);
				break;
			}

			case WID_CI_WATER_DESC:
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_WATER_SECT).width);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_CANALS).width + WD_FRAMERECT_LEFT);
				break;

			case WID_CI_STATION_DESC:
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_STATION_SECT).width);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_STATIONS).width + WD_FRAMERECT_LEFT);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_AIRPORTS).width + WD_FRAMERECT_LEFT);
				break;

			case WID_CI_RAIL_COUNT:
			case WID_CI_ROAD_COUNT:
			case WID_CI_WATER_COUNT:
			case WID_CI_STATION_COUNT:
			case WID_CI_TOTAL: {
				/* Find the maximum count that is displayed. */
				uint32 max_val = 1000;  // Some random number to reserve enough space.
				Money max_cost = 10000; // Some random number to reserve enough space.
				for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
					max_val = max(max_val, c->infrastructure.rail[rt]);
					max_cost = max(max_cost, RailMaintenanceCost(rt, c->infrastructure.rail[rt]));
				}
				max_val = max(max_val, c->infrastructure.signal);
				max_cost = max(max_cost, SignalMaintenanceCost(c->infrastructure.signal));
				for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
					max_val = max(max_val, c->infrastructure.road[rt]);
					max_cost = max(max_cost, RoadMaintenanceCost(rt, c->infrastructure.road[rt]));
				}
				max_val = max(max_val, c->infrastructure.water);
				max_cost = max(max_cost, CanalMaintenanceCost(c->infrastructure.water));
				max_val = max(max_val, c->infrastructure.station);
				max_cost = max(max_cost, StationMaintenanceCost(c->infrastructure.station));
				max_val = max(max_val, c->infrastructure.airport);
				max_cost = max(max_cost, AirportMaintenanceCost(c->index));

				SetDParam(0, max_val);
				SetDParam(1, max_cost * 12); // Convert to per year
				size->width = max(size->width, GetStringBoundingBox(_settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA).width + 20); // Reserve some wiggle room.

				if (_settings_game.economy.infrastructure_maintenance) {
					SetDParam(0, this->GetTotalMaintenanceCost() * 12); // Convert to per year
					this->total_width = GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_TOTAL).width + 20;
					size->width = max(size->width, this->total_width);
				}

				/* Set height of the total line. */
				if (widget == WID_CI_TOTAL) {
					size->height = _settings_game.economy.infrastructure_maintenance ? max(size->height, EXP_LINESPACE + FONT_HEIGHT_NORMAL) : 0;
				}
				break;
			}
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		int y = r.top;

		int offs_left = _current_text_dir == TD_LTR ? WD_FRAMERECT_LEFT : 0;
		int offs_right = _current_text_dir == TD_LTR ? 0 : WD_FRAMERECT_LEFT;

		switch (widget) {
			case WID_CI_RAIL_DESC:
				DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_RAIL_SECT);

				if (this->railtypes != RAILTYPES_NONE) {
					/* Draw name of each valid railtype. */
					for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
						if (HasBit(this->railtypes, rt)) {
							SetDParam(0, GetRailTypeInfo(rt)->strings.name);
							DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_WHITE_STRING);
						}
					}
					DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_SIGNALS);
				} else {
					/* No valid railtype. */
					DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_VIEW_INFRASTRUCTURE_NONE);
				}

				break;

			case WID_CI_RAIL_COUNT:
				/* Draw infrastructure count for each valid railtype. */
				for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
					if (HasBit(this->railtypes, rt)) {
						SetDParam(0, c->infrastructure.rail[rt]);
						SetDParam(1, RailMaintenanceCost(rt, c->infrastructure.rail[rt]) * 12); // Convert to per year
						DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, _settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA);
					}
				}
				if (this->railtypes != RAILTYPES_NONE) {
					SetDParam(0, c->infrastructure.signal);
					SetDParam(1, SignalMaintenanceCost(c->infrastructure.signal) * 12); // Convert to per year
					DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, _settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA);
				}
				break;

			case WID_CI_ROAD_DESC:
				DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_ROAD_SECT);

				if (this->roadtypes != ROADTYPES_NONE) {
					if (HasBit(this->roadtypes, ROADTYPE_ROAD)) DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_ROAD);
					if (HasBit(this->roadtypes, ROADTYPE_TRAM)) DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_TRAMWAY);
				} else {
					/* No valid roadtypes. */
					DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_VIEW_INFRASTRUCTURE_NONE);
				}

				break;

			case WID_CI_ROAD_COUNT:
				if (HasBit(this->roadtypes, ROADTYPE_ROAD)) {
					SetDParam(0, c->infrastructure.road[ROADTYPE_ROAD]);
					SetDParam(1, RoadMaintenanceCost(ROADTYPE_ROAD, c->infrastructure.road[ROADTYPE_ROAD]) * 12); // Convert to per year
					DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, _settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA);
				}
				if (HasBit(this->roadtypes, ROADTYPE_TRAM)) {
					SetDParam(0, c->infrastructure.road[ROADTYPE_TRAM]);
					SetDParam(1, RoadMaintenanceCost(ROADTYPE_TRAM, c->infrastructure.road[ROADTYPE_TRAM]) * 12); // Convert to per year
					DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, _settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA);
				}
				break;

			case WID_CI_WATER_DESC:
				DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_WATER_SECT);
				DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_CANALS);
				break;

			case WID_CI_WATER_COUNT:
				SetDParam(0, c->infrastructure.water);
				SetDParam(1, CanalMaintenanceCost(c->infrastructure.water) * 12); // Convert to per year
				DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, _settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA);
				break;

			case WID_CI_TOTAL:
				if (_settings_game.economy.infrastructure_maintenance) {
					GfxFillRect(r.left, y, r.left + this->total_width, y, PC_WHITE);
					y += EXP_LINESPACE;
					SetDParam(0, this->GetTotalMaintenanceCost() * 12); // Convert to per year
					DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_TOTAL);
				}
				break;

			case WID_CI_STATION_DESC:
				DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_STATION_SECT);
				DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_STATIONS);
				DrawString(r.left + offs_left, r.right - offs_right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_AIRPORTS);
				break;

			case WID_CI_STATION_COUNT:
				SetDParam(0, c->infrastructure.station);
				SetDParam(1, StationMaintenanceCost(c->infrastructure.station) * 12); // Convert to per year
				DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, _settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA);
				SetDParam(0, c->infrastructure.airport);
				SetDParam(1, AirportMaintenanceCost(c->index) * 12); // Convert to per year
				DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, _settings_game.economy.infrastructure_maintenance ? STR_COMPANY_INFRASTRUCTURE_VIEW_COST : STR_WHITE_COMMA);
				break;
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

		this->UpdateRailRoadTypes();
		this->ReInit();
	}
};

static const WindowDesc _company_infrastructure_desc(
	WDP_AUTO, 0, 0,
	WC_COMPANY_INFRASTRUCTURE, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_company_infrastructure_widgets, lengthof(_nested_company_infrastructure_widgets)
);

/**
 * Open the infrastructure window of a company.
 * @param company Company to show infrastructure of.
 */
static void ShowCompanyInfrastructure(CompanyID company)
{
	if (!Company::IsValidID(company)) return;
	AllocateWindowDescFront<CompanyInfrastructureWindow>(&_company_infrastructure_desc, company);
}

static const NWidgetPart _nested_company_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_C_CAPTION), SetDataTip(STR_COMPANY_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPIP(4, 6, 4),
			NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_FACE), SetMinimalSize(92, 119), SetFill(1, 0),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_FACE_TITLE), SetFill(1, 1), SetMinimalTextLines(2, 0),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_VERTICAL), SetPIP(4, 5, 5),
						NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_INAUGURATION), SetDataTip(STR_COMPANY_VIEW_INAUGURATED_TITLE, STR_NULL), SetFill(1, 0),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 5, 0),
							NWidget(WWT_LABEL, COLOUR_GREY, WID_C_DESC_COLOUR_SCHEME), SetDataTip(STR_COMPANY_VIEW_COLOUR_SCHEME_TITLE, STR_NULL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_COLOUR_SCHEME_EXAMPLE), SetMinimalSize(30, 0), SetFill(0, 1),
							NWidget(NWID_SPACER), SetFill(1, 0),
						EndContainer(),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(NWID_VERTICAL),
								NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_VEHICLE), SetDataTip(STR_COMPANY_VIEW_VEHICLES_TITLE, STR_NULL),
								NWidget(NWID_SPACER), SetFill(0, 1),
							EndContainer(),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_VEHICLE_COUNTS), SetMinimalTextLines(4, 0),
							NWidget(NWID_SPACER), SetFill(1, 0),
						EndContainer(),
					EndContainer(),
					NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_VIEW_BUILD_HQ),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_VIEW_HQ), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_VIEW_HQ_BUTTON, STR_COMPANY_VIEW_VIEW_HQ_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_C_BUILD_HQ), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_BUILD_HQ_BUTTON, STR_COMPANY_VIEW_BUILD_HQ_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_RELOCATE),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_C_RELOCATE_HQ), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_RELOCATE_HQ, STR_COMPANY_VIEW_RELOCATE_COMPANY_HEADQUARTERS),
							NWidget(NWID_SPACER), SetMinimalSize(90, 0),
						EndContainer(),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_COMPANY_VALUE), SetDataTip(STR_COMPANY_VIEW_COMPANY_VALUE, STR_NULL), SetFill(1, 0),
					NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(NWID_VERTICAL),
								NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_INFRASTRUCTURE), SetDataTip(STR_COMPANY_VIEW_INFRASTRUCTURE, STR_NULL),
								NWidget(NWID_SPACER), SetFill(0, 1),
							EndContainer(),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_INFRASTRUCTURE_COUNTS), SetMinimalTextLines(5, 0), SetFill(1, 0),
							NWidget(NWID_VERTICAL),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_VIEW_INFRASTRUCTURE), SetDataTip(STR_COMPANY_VIEW_INFRASTRUCTURE_BUTTON, STR_COMPANY_VIEW_INFRASTRUCTURE_TOOLTIP),
								NWidget(NWID_SPACER), SetFill(0, 1), SetMinimalSize(90, 0),
							EndContainer(),
						EndContainer(),
					EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_DESC_OWNERS),
						NWidget(NWID_VERTICAL), SetPIP(5, 5, 4),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_OWNERS), SetMinimalTextLines(3, 0),
							NWidget(NWID_SPACER), SetFill(0, 1),
						EndContainer(),
					EndContainer(),
					NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
						NWidget(NWID_SPACER), SetMinimalSize(90, 0), SetFill(0, 1),
						/* Multi player buttons. */
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_EMPTY, COLOUR_GREY, WID_C_HAS_PASSWORD),
							NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_MULTIPLAYER),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_PASSWORD), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_PASSWORD, STR_COMPANY_VIEW_PASSWORD_TOOLTIP),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_JOIN), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_JOIN, STR_COMPANY_VIEW_JOIN_TOOLTIP),
							EndContainer(),
						EndContainer(),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	/* Button bars at the bottom. */
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_BUTTONS),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_NEW_FACE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_NEW_FACE_BUTTON, STR_COMPANY_VIEW_NEW_FACE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COLOUR_SCHEME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_COLOUR_SCHEME_BUTTON, STR_COMPANY_VIEW_COLOUR_SCHEME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_PRESIDENT_NAME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_PRESIDENT_NAME_BUTTON, STR_COMPANY_VIEW_PRESIDENT_NAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_NAME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_COMPANY_NAME_BUTTON, STR_COMPANY_VIEW_COMPANY_NAME_TOOLTIP),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_BUY_SHARE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_BUY_SHARE_BUTTON, STR_COMPANY_VIEW_BUY_SHARE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_SELL_SHARE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_SELL_SHARE_BUTTON, STR_COMPANY_VIEW_SELL_SHARE_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

int GetAmountOwnedBy(const Company *c, Owner owner)
{
	return (c->share_owners[0] == owner) +
				 (c->share_owners[1] == owner) +
				 (c->share_owners[2] == owner) +
				 (c->share_owners[3] == owner);
}

/** Strings for the company vehicle counts */
static const StringID _company_view_vehicle_count_strings[] = {
	STR_COMPANY_VIEW_TRAINS, STR_COMPANY_VIEW_ROAD_VEHICLES, STR_COMPANY_VIEW_SHIPS, STR_COMPANY_VIEW_AIRCRAFT
};

/**
 * Window with general information about a company
 */
struct CompanyWindow : Window
{
	CompanyWidgets query_widget;

	/** Display planes in the company window. */
	enum CompanyWindowPlanes {
		/* Display planes of the #WID_C_SELECT_MULTIPLAYER selection widget. */
		CWP_MP_C_PWD = 0, ///< Display the company password button.
		CWP_MP_C_JOIN,    ///< Display the join company button.

		/* Display planes of the #WID_C_SELECT_VIEW_BUILD_HQ selection widget. */
		CWP_VB_VIEW = 0,  ///< Display the view button
		CWP_VB_BUILD,     ///< Display the build button

		/* Display planes of the #WID_C_SELECT_RELOCATE selection widget. */
		CWP_RELOCATE_SHOW = 0, ///< Show the relocate HQ button.
		CWP_RELOCATE_HIDE,     ///< Hide the relocate HQ button.

		/* Display planes of the #WID_C_SELECT_BUTTONS selection widget. */
		CWP_BUTTONS_LOCAL = 0, ///< Buttons of the local company.
		CWP_BUTTONS_OTHER,     ///< Buttons of the other companies.
	};

	CompanyWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->InitNested(desc, window_number);
		this->owner = (Owner)this->window_number;
	}

	virtual void OnPaint()
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		bool local = this->window_number == _local_company;

		if (!this->IsShaded()) {
			bool reinit = false;

			/* Button bar selection. */
			int plane = local ? CWP_BUTTONS_LOCAL : CWP_BUTTONS_OTHER;
			NWidgetStacked *wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_BUTTONS);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				this->SetDirty();
				return;
			}

			/* Build HQ button handling. */
			plane = (local && c->location_of_HQ == INVALID_TILE) ? CWP_VB_BUILD : CWP_VB_VIEW;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_VIEW_BUILD_HQ);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				this->SetDirty();
				return;
			}

			this->SetWidgetDisabledState(WID_C_VIEW_HQ, c->location_of_HQ == INVALID_TILE);

			/* Enable/disable 'Relocate HQ' button. */
			plane = (!local || c->location_of_HQ == INVALID_TILE) ? CWP_RELOCATE_HIDE : CWP_RELOCATE_SHOW;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_RELOCATE);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				this->SetDirty();
				return;
			}

			/* Owners of company */
			plane = SZSP_HORIZONTAL;
			for (uint i = 0; i < lengthof(c->share_owners); i++) {
				if (c->share_owners[i] != INVALID_COMPANY) {
					plane = 0;
					break;
				}
			}
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_DESC_OWNERS);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			/* Multiplayer buttons. */
			plane = ((!_networking) ? (int)SZSP_NONE : (int)(local ? CWP_MP_C_PWD : CWP_MP_C_JOIN));
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_MULTIPLAYER);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}
			this->SetWidgetDisabledState(WID_C_COMPANY_JOIN,   c->is_ai);

			if (reinit) {
				this->ReInit();
				return;
			}
		}

		if (!local) {
			if (_settings_game.economy.allow_shares) { // Shares are allowed
				/* If all shares are owned by someone (none by nobody), disable buy button */
				this->SetWidgetDisabledState(WID_C_BUY_SHARE, GetAmountOwnedBy(c, INVALID_OWNER) == 0 ||
						/* Only 25% left to buy. If the company is human, disable buying it up.. TODO issues! */
						(GetAmountOwnedBy(c, INVALID_OWNER) == 1 && !c->is_ai) ||
						/* Spectators cannot do anything of course */
						_local_company == COMPANY_SPECTATOR);

				/* If the company doesn't own any shares, disable sell button */
				this->SetWidgetDisabledState(WID_C_SELL_SHARE, (GetAmountOwnedBy(c, _local_company) == 0) ||
						/* Spectators cannot do anything of course */
						_local_company == COMPANY_SPECTATOR);
			} else { // Shares are not allowed, disable buy/sell buttons
				this->DisableWidget(WID_C_BUY_SHARE);
				this->DisableWidget(WID_C_SELL_SHARE);
			}
		}

		this->DrawWidgets();
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_C_DESC_COLOUR_SCHEME_EXAMPLE: {
				Point offset;
				Dimension d = GetSpriteSize(SPR_VEH_BUS_SW_VIEW, &offset);
				d.width -= offset.x;
				d.height -= offset.y;
				*size = maxdim(*size, d);
				break;
			}

			case WID_C_DESC_COMPANY_VALUE:
				SetDParam(0, INT64_MAX); // Arguably the maximum company value
				size->width = GetStringBoundingBox(STR_COMPANY_VIEW_COMPANY_VALUE).width;
				break;

			case WID_C_DESC_VEHICLE_COUNTS:
				SetDParam(0, 5000); // Maximum number of vehicles
				for (uint i = 0; i < lengthof(_company_view_vehicle_count_strings); i++) {
					size->width = max(size->width, GetStringBoundingBox(_company_view_vehicle_count_strings[i]).width);
				}
				break;

			case WID_C_DESC_INFRASTRUCTURE_COUNTS:
				SetDParam(0, UINT_MAX);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_RAIL).width);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_ROAD).width);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_WATER).width);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_STATION).width);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_AIRPORT).width);
				size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_NONE).width);
				break;

			case WID_C_DESC_OWNERS: {
				const Company *c2;

				FOR_ALL_COMPANIES(c2) {
					SetDParam(0, 25);
					SetDParam(1, c2->index);

					size->width = max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_SHARES_OWNED_BY).width);
				}
				break;
			}

#ifdef ENABLE_NETWORK
			case WID_C_HAS_PASSWORD:
				*size = maxdim(*size, GetSpriteSize(SPR_LOCK));
				break;
#endif /* ENABLE_NETWORK */
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		switch (widget) {
			case WID_C_FACE:
				DrawCompanyManagerFace(c->face, c->colour, r.left, r.top);
				break;

			case WID_C_FACE_TITLE:
				SetDParam(0, c->index);
				DrawStringMultiLine(r.left, r.right, r.top, r.bottom, STR_COMPANY_VIEW_PRESIDENT_MANAGER_TITLE, TC_FROMSTRING, SA_HOR_CENTER);
				break;

			case WID_C_DESC_COLOUR_SCHEME_EXAMPLE: {
				Point offset;
				Dimension d = GetSpriteSize(SPR_VEH_BUS_SW_VIEW, &offset);
				d.height -= offset.y;
				DrawSprite(SPR_VEH_BUS_SW_VIEW, COMPANY_SPRITE_COLOUR(c->index), r.left - offset.x, (r.top + r.bottom - d.height) / 2 - offset.y);
				break;
			}

			case WID_C_DESC_VEHICLE_COUNTS: {
				uint amounts[4];
				amounts[0] = c->group_all[VEH_TRAIN].num_vehicle;
				amounts[1] = c->group_all[VEH_ROAD].num_vehicle;
				amounts[2] = c->group_all[VEH_SHIP].num_vehicle;
				amounts[3] = c->group_all[VEH_AIRCRAFT].num_vehicle;

				int y = r.top;
				if (amounts[0] + amounts[1] + amounts[2] + amounts[3] == 0) {
					DrawString(r.left, r.right, y, STR_COMPANY_VIEW_VEHICLES_NONE);
				} else {
					assert_compile(lengthof(amounts) == lengthof(_company_view_vehicle_count_strings));

					for (uint i = 0; i < lengthof(amounts); i++) {
						if (amounts[i] != 0) {
							SetDParam(0, amounts[i]);
							DrawString(r.left, r.right, y, _company_view_vehicle_count_strings[i]);
							y += FONT_HEIGHT_NORMAL;
						}
					}
				}
				break;
			}

			case WID_C_DESC_INFRASTRUCTURE_COUNTS: {
				uint y = r.top;

				/* Collect rail and road counts. */
				uint rail_pices = c->infrastructure.signal;
				uint road_pieces = 0;
				for (uint i = 0; i < lengthof(c->infrastructure.rail); i++) rail_pices += c->infrastructure.rail[i];
				for (uint i = 0; i < lengthof(c->infrastructure.road); i++) road_pieces += c->infrastructure.road[i];

				if (rail_pices == 0 && road_pieces == 0 && c->infrastructure.water == 0 && c->infrastructure.station == 0 && c->infrastructure.airport == 0) {
					DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_NONE);
				} else {
					if (rail_pices != 0) {
						SetDParam(0, rail_pices);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_RAIL);
						y += FONT_HEIGHT_NORMAL;
					}
					if (road_pieces != 0) {
						SetDParam(0, road_pieces);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_ROAD);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.water != 0) {
						SetDParam(0, c->infrastructure.water);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_WATER);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.station != 0) {
						SetDParam(0, c->infrastructure.station);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_STATION);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.airport != 0) {
						SetDParam(0, c->infrastructure.airport);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_AIRPORT);
					}
				}

				break;
			}

			case WID_C_DESC_OWNERS: {
				const Company *c2;
				uint y = r.top;

				FOR_ALL_COMPANIES(c2) {
					uint amt = GetAmountOwnedBy(c, c2->index);
					if (amt != 0) {
						SetDParam(0, amt * 25);
						SetDParam(1, c2->index);

						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_SHARES_OWNED_BY);
						y += FONT_HEIGHT_NORMAL;
					}
				}
				break;
			}

#ifdef ENABLE_NETWORK
			case WID_C_HAS_PASSWORD:
				if (_networking && NetworkCompanyIsPassworded(c->index)) {
					DrawSprite(SPR_LOCK, PAL_NONE, r.left, r.top);
				}
				break;
#endif /* ENABLE_NETWORK */
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_C_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				SetDParam(1, (CompanyID)this->window_number);
				break;

			case WID_C_DESC_INAUGURATION:
				SetDParam(0, Company::Get((CompanyID)this->window_number)->inaugurated_year);
				break;

			case WID_C_DESC_COMPANY_VALUE:
				SetDParam(0, CalculateCompanyValue(Company::Get((CompanyID)this->window_number)));
				break;
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_C_NEW_FACE: DoSelectCompanyManagerFace(this); break;

			case WID_C_COLOUR_SCHEME:
				if (BringWindowToFrontById(WC_COMPANY_COLOUR, this->window_number)) break;
				new SelectCompanyLiveryWindow(&_select_company_livery_desc, (CompanyID)this->window_number);
				break;

			case WID_C_PRESIDENT_NAME:
				this->query_widget = WID_C_PRESIDENT_NAME;
				SetDParam(0, this->window_number);
				ShowQueryString(STR_PRESIDENT_NAME, STR_COMPANY_VIEW_PRESIDENT_S_NAME_QUERY_CAPTION, MAX_LENGTH_PRESIDENT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_C_COMPANY_NAME:
				this->query_widget = WID_C_COMPANY_NAME;
				SetDParam(0, this->window_number);
				ShowQueryString(STR_COMPANY_NAME, STR_COMPANY_VIEW_COMPANY_NAME_QUERY_CAPTION, MAX_LENGTH_COMPANY_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_C_VIEW_HQ: {
				TileIndex tile = Company::Get((CompanyID)this->window_number)->location_of_HQ;
				if (_ctrl_pressed) {
					ShowExtraViewPortWindow(tile);
				} else {
					ScrollMainWindowToTile(tile);
				}
				break;
			}

			case WID_C_BUILD_HQ:
				if ((byte)this->window_number != _local_company) return;
				if (this->IsWidgetLowered(WID_C_BUILD_HQ)) {
					ResetObjectToPlace();
					this->RaiseButtons();
					break;
				}
				SetObjectToPlaceWnd(SPR_CURSOR_HQ, PAL_NONE, HT_RECT, this);
				SetTileSelectSize(2, 2);
				this->LowerWidget(WID_C_BUILD_HQ);
				this->SetWidgetDirty(WID_C_BUILD_HQ);
				break;

			case WID_C_RELOCATE_HQ:
				if (this->IsWidgetLowered(WID_C_RELOCATE_HQ)) {
					ResetObjectToPlace();
					this->RaiseButtons();
					break;
				}
				SetObjectToPlaceWnd(SPR_CURSOR_HQ, PAL_NONE, HT_RECT, this);
				SetTileSelectSize(2, 2);
				this->LowerWidget(WID_C_RELOCATE_HQ);
				this->SetWidgetDirty(WID_C_RELOCATE_HQ);
				break;

			case WID_C_VIEW_INFRASTRUCTURE:
				ShowCompanyInfrastructure((CompanyID)this->window_number);
				break;

			case WID_C_BUY_SHARE:
				DoCommandP(0, this->window_number, 0, CMD_BUY_SHARE_IN_COMPANY | CMD_MSG(STR_ERROR_CAN_T_BUY_25_SHARE_IN_THIS));
				break;

			case WID_C_SELL_SHARE:
				DoCommandP(0, this->window_number, 0, CMD_SELL_SHARE_IN_COMPANY | CMD_MSG(STR_ERROR_CAN_T_SELL_25_SHARE_IN));
				break;

#ifdef ENABLE_NETWORK
			case WID_C_COMPANY_PASSWORD:
				if (this->window_number == _local_company) ShowNetworkCompanyPasswordWindow(this);
				break;

			case WID_C_COMPANY_JOIN: {
				this->query_widget = WID_C_COMPANY_JOIN;
				CompanyID company = (CompanyID)this->window_number;
				if (_network_server) {
					NetworkServerDoMove(CLIENT_ID_SERVER, company);
					MarkWholeScreenDirty();
				} else if (NetworkCompanyIsPassworded(company)) {
					/* ask for the password */
					ShowQueryString(STR_EMPTY, STR_NETWORK_NEED_COMPANY_PASSWORD_CAPTION, NETWORK_PASSWORD_LENGTH, this, CS_ALPHANUMERAL, QSF_NONE);
				} else {
					/* just send the join command */
					NetworkClientRequestMove(company);
				}
				break;
			}
#endif /* ENABLE_NETWORK */
		}
	}

	virtual void OnHundredthTick()
	{
		/* redraw the window every now and then */
		this->SetDirty();
	}

	virtual void OnPlaceObject(Point pt, TileIndex tile)
	{
		if (DoCommandP(tile, OBJECT_HQ, 0, CMD_BUILD_OBJECT | CMD_MSG(STR_ERROR_CAN_T_BUILD_COMPANY_HEADQUARTERS))) {
			ResetObjectToPlace();
			this->RaiseButtons();
		}
	}

	virtual void OnPlaceObjectAbort()
	{
		this->RaiseButtons();
	}

	virtual void OnQueryTextFinished(char *str)
	{
		if (str == NULL) return;

		switch (this->query_widget) {
			default: NOT_REACHED();

			case WID_C_PRESIDENT_NAME:
				DoCommandP(0, 0, 0, CMD_RENAME_PRESIDENT | CMD_MSG(STR_ERROR_CAN_T_CHANGE_PRESIDENT), NULL, str);
				break;

			case WID_C_COMPANY_NAME:
				DoCommandP(0, 0, 0, CMD_RENAME_COMPANY | CMD_MSG(STR_ERROR_CAN_T_CHANGE_COMPANY_NAME), NULL, str);
				break;

#ifdef ENABLE_NETWORK
			case WID_C_COMPANY_JOIN:
				NetworkClientRequestMove((CompanyID)this->window_number, str);
				break;
#endif /* ENABLE_NETWORK */
		}
	}
};

static const WindowDesc _company_desc(
	WDP_AUTO, 0, 0,
	WC_COMPANY, WC_NONE,
	WDF_UNCLICK_BUTTONS,
	_nested_company_widgets, lengthof(_nested_company_widgets)
);

/**
 * Show the window with the overview of the company.
 * @param company The company to show the window for.
 */
void ShowCompany(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	AllocateWindowDescFront<CompanyWindow>(&_company_desc, company);
}

/**
 * Redraw all windows with company infrastructure counts.
 * @param company The company to redraw the windows of.
 */
void DirtyCompanyInfrastructureWindows(CompanyID company)
{
	SetWindowDirty(WC_COMPANY, company);
	SetWindowDirty(WC_COMPANY_INFRASTRUCTURE, company);
}

struct BuyCompanyWindow : Window {
	BuyCompanyWindow(const WindowDesc *desc, WindowNumber window_number) : Window()
	{
		this->InitNested(desc, window_number);
	}

	virtual void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize)
	{
		switch (widget) {
			case WID_BC_FACE:
				*size = GetSpriteSize(SPR_GRADIENT);
				break;

			case WID_BC_QUESTION:
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->index);
				SetDParam(1, c->bankrupt_value);
				size->height = GetStringHeight(STR_BUY_COMPANY_MESSAGE, size->width);
				break;
		}
	}

	virtual void SetStringParameters(int widget) const
	{
		switch (widget) {
			case WID_BC_CAPTION:
				SetDParam(0, STR_COMPANY_NAME);
				SetDParam(1, Company::Get((CompanyID)this->window_number)->index);
				break;
		}
	}

	virtual void DrawWidget(const Rect &r, int widget) const
	{
		switch (widget) {
			case WID_BC_FACE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				DrawCompanyManagerFace(c->face, c->colour, r.left, r.top);
				break;
			}

			case WID_BC_QUESTION: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->index);
				SetDParam(1, c->bankrupt_value);
				DrawStringMultiLine(r.left, r.right, r.top, r.bottom, STR_BUY_COMPANY_MESSAGE, TC_FROMSTRING, SA_CENTER);
				break;
			}
		}
	}

	virtual void OnClick(Point pt, int widget, int click_count)
	{
		switch (widget) {
			case WID_BC_NO:
				delete this;
				break;

			case WID_BC_YES:
				DoCommandP(0, this->window_number, 0, CMD_BUY_COMPANY | CMD_MSG(STR_ERROR_CAN_T_BUY_COMPANY));
				break;
		}
	}
};

static const NWidgetPart _nested_buy_company_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE, WID_BC_CAPTION), SetDataTip(STR_ERROR_MESSAGE_CAPTION_OTHER_COMPANY, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE),
		NWidget(NWID_VERTICAL), SetPIP(8, 8, 8),
			NWidget(NWID_HORIZONTAL), SetPIP(8, 10, 8),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BC_FACE), SetFill(0, 1),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BC_QUESTION), SetMinimalSize(240, 0), SetFill(1, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(100, 10, 100),
				NWidget(WWT_TEXTBTN, COLOUR_LIGHT_BLUE, WID_BC_NO), SetMinimalSize(60, 12), SetDataTip(STR_QUIT_NO, STR_NULL), SetFill(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_LIGHT_BLUE, WID_BC_YES), SetMinimalSize(60, 12), SetDataTip(STR_QUIT_YES, STR_NULL), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static const WindowDesc _buy_company_desc(
	WDP_AUTO, 0, 0,
	WC_BUY_COMPANY, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_buy_company_widgets, lengthof(_nested_buy_company_widgets)
);

/**
 * Show the query to buy another company.
 * @param company The company to buy.
 */
void ShowBuyCompanyDialog(CompanyID company)
{
	AllocateWindowDescFront<BuyCompanyWindow>(&_buy_company_desc, company);
}
