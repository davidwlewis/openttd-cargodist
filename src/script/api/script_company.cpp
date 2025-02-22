/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_company.cpp Implementation of ScriptCompany. */

#include "../../stdafx.h"
#include "script_company.hpp"
#include "script_error.hpp"
#include "../../command_func.h"
#include "../../company_func.h"
#include "../../company_base.h"
#include "../../company_manager_face.h"
#include "../../economy_func.h"
#include "../../object_type.h"
#include "../../strings_func.h"
#include "../../tile_map.h"
#include "../../string_func.h"
#include "../../settings_func.h"
#include "table/strings.h"

/* static */ ScriptCompany::CompanyID ScriptCompany::ResolveCompanyID(ScriptCompany::CompanyID company)
{
	if (company == COMPANY_SELF) {
		if (!::Company::IsValidID((::CompanyID)_current_company)) return COMPANY_INVALID;
		return (CompanyID)((byte)_current_company);
	}

	return ::Company::IsValidID((::CompanyID)company) ? company : COMPANY_INVALID;
}

/* static */ bool ScriptCompany::IsMine(ScriptCompany::CompanyID company)
{
	return ResolveCompanyID(company) == ResolveCompanyID(COMPANY_SELF);
}

/* static */ bool ScriptCompany::SetName(Text *name)
{
	CCountedPtr<Text> counter(name);

	EnforcePrecondition(false, name != NULL);
	const char *text = name->GetEncodedText();
	EnforcePrecondition(false, !::StrEmpty(text));
	EnforcePreconditionCustomError(false, ::Utf8StringLength(text) < MAX_LENGTH_COMPANY_NAME_CHARS, ScriptError::ERR_PRECONDITION_STRING_TOO_LONG);

	return ScriptObject::DoCommand(0, 0, 0, CMD_RENAME_COMPANY, text);
}

/* static */ char *ScriptCompany::GetName(ScriptCompany::CompanyID company)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return NULL;

	static const int len = 64;
	char *company_name = MallocT<char>(len);

	::SetDParam(0, company);
	::GetString(company_name, STR_COMPANY_NAME, &company_name[len - 1]);
	return company_name;
}

/* static */ bool ScriptCompany::SetPresidentName(Text *name)
{
	CCountedPtr<Text> counter(name);

	EnforcePrecondition(false, name != NULL);
	const char *text = name->GetEncodedText();
	EnforcePrecondition(false, !::StrEmpty(text));

	return ScriptObject::DoCommand(0, 0, 0, CMD_RENAME_PRESIDENT, text);
}

/* static */ char *ScriptCompany::GetPresidentName(ScriptCompany::CompanyID company)
{
	company = ResolveCompanyID(company);

	static const int len = 64;
	char *president_name = MallocT<char>(len);
	if (company != COMPANY_INVALID) {
		::SetDParam(0, company);
		::GetString(president_name, STR_PRESIDENT_NAME, &president_name[len - 1]);
	} else {
		*president_name = '\0';
	}

	return president_name;
}

/* static */ bool ScriptCompany::SetPresidentGender(Gender gender)
{
	EnforcePrecondition(false, gender == GENDER_MALE || gender == GENDER_FEMALE);
	EnforcePrecondition(false, GetPresidentGender(ScriptCompany::COMPANY_SELF) != gender);

	CompanyManagerFace cmf;
	GenderEthnicity ge = (GenderEthnicity)((gender == GENDER_FEMALE ? (1 << ::GENDER_FEMALE) : 0) | (::InteractiveRandom() & (1 << ETHNICITY_BLACK)));
	RandomCompanyManagerFaceBits(cmf, ge, false);

	return ScriptObject::DoCommand(0, 0, cmf, CMD_SET_COMPANY_MANAGER_FACE);
}

/* static */ ScriptCompany::Gender ScriptCompany::GetPresidentGender(CompanyID company)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return GENDER_INVALID;

	GenderEthnicity ge = (GenderEthnicity)GetCompanyManagerFaceBits(Company::Get(company)->face, CMFV_GEN_ETHN, GE_WM);
	return HasBit(ge, ::GENDER_FEMALE) ? GENDER_FEMALE : GENDER_MALE;
}

/* static */ Money ScriptCompany::GetQuarterlyIncome(ScriptCompany::CompanyID company, uint32 quarter)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return -1;
	if (quarter > EARLIEST_QUARTER) return -1;

	if (quarter == CURRENT_QUARTER) {
		return ::Company::Get((::CompanyID)company)->cur_economy.income;
	}
	return ::Company::Get((::CompanyID)company)->old_economy[quarter - 1].income;
}

/* static */ Money ScriptCompany::GetQuarterlyExpenses(ScriptCompany::CompanyID company, uint32 quarter)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return -1;
	if (quarter > EARLIEST_QUARTER) return -1;

	if (quarter == CURRENT_QUARTER) {
		return ::Company::Get((::CompanyID)company)->cur_economy.expenses;
	}
	return ::Company::Get((::CompanyID)company)->old_economy[quarter - 1].expenses;
}

/* static */ int32 ScriptCompany::GetQuarterlyCargoDelivered(ScriptCompany::CompanyID company, uint32 quarter)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return -1;
	if (quarter > EARLIEST_QUARTER) return -1;

	if (quarter == CURRENT_QUARTER) {
		return ::Company::Get((::CompanyID)company)->cur_economy.delivered_cargo;
	}
	return ::Company::Get((::CompanyID)company)->old_economy[quarter - 1].delivered_cargo;
}

/* static */ int32 ScriptCompany::GetQuarterlyPerformanceRating(ScriptCompany::CompanyID company, uint32 quarter)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return -1;
	if (quarter > EARLIEST_QUARTER) return -1;
	if (quarter == CURRENT_QUARTER) return -1;

	return ::Company::Get((::CompanyID)company)->old_economy[quarter - 1].performance_history;
}

/* static */ Money ScriptCompany::GetQuarterlyCompanyValue(ScriptCompany::CompanyID company, uint32 quarter)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return -1;
	if (quarter > EARLIEST_QUARTER) return -1;

	if (quarter == CURRENT_QUARTER) {
		return ::CalculateCompanyValue(::Company::Get((::CompanyID)company));
	}
	return ::Company::Get((::CompanyID)company)->old_economy[quarter - 1].company_value;
}


/* static */ Money ScriptCompany::GetBankBalance(ScriptCompany::CompanyID company)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return -1;

	return ::Company::Get((CompanyID)company)->money;
}

/* static */ Money ScriptCompany::GetLoanAmount()
{
	ScriptCompany::CompanyID company = ResolveCompanyID(COMPANY_SELF);
	if (company == COMPANY_INVALID) return -1;

	return ::Company::Get(company)->current_loan;
}

/* static */ Money ScriptCompany::GetMaxLoanAmount()
{
	return _economy.max_loan;
}

/* static */ Money ScriptCompany::GetLoanInterval()
{
	return LOAN_INTERVAL;
}

/* static */ bool ScriptCompany::SetLoanAmount(int32 loan)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, loan >= 0);
	EnforcePrecondition(false, (loan % GetLoanInterval()) == 0);
	EnforcePrecondition(false, loan <= GetMaxLoanAmount());
	EnforcePrecondition(false, (loan - GetLoanAmount() + GetBankBalance(COMPANY_SELF)) >= 0);

	if (loan == GetLoanAmount()) return true;

	return ScriptObject::DoCommand(0,
			abs(loan - GetLoanAmount()), 2,
			(loan > GetLoanAmount()) ? CMD_INCREASE_LOAN : CMD_DECREASE_LOAN);
}

/* static */ bool ScriptCompany::SetMinimumLoanAmount(int32 loan)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, loan >= 0);

	int32 over_interval = loan % GetLoanInterval();
	if (over_interval != 0) loan += GetLoanInterval() - over_interval;

	EnforcePrecondition(false, loan <= GetMaxLoanAmount());

	SetLoanAmount(loan);

	return GetLoanAmount() == loan;
}

/* static */ bool ScriptCompany::BuildCompanyHQ(TileIndex tile)
{
	EnforcePrecondition(false, ScriptObject::GetCompany() != OWNER_DEITY);
	EnforcePrecondition(false, ::IsValidTile(tile));

	return ScriptObject::DoCommand(tile, OBJECT_HQ, 0, CMD_BUILD_OBJECT);
}

/* static */ TileIndex ScriptCompany::GetCompanyHQ(CompanyID company)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return INVALID_TILE;

	TileIndex loc = ::Company::Get((CompanyID)company)->location_of_HQ;
	return (loc == 0) ? INVALID_TILE : loc;
}

/* static */ bool ScriptCompany::SetAutoRenewStatus(bool autorenew)
{
	return ScriptObject::DoCommand(0, ::GetCompanySettingIndex("company.engine_renew"), autorenew ? 1 : 0, CMD_CHANGE_COMPANY_SETTING);
}

/* static */ bool ScriptCompany::GetAutoRenewStatus(CompanyID company)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return false;

	return ::Company::Get((CompanyID)company)->settings.engine_renew;
}

/* static */ bool ScriptCompany::SetAutoRenewMonths(int16 months)
{
	return ScriptObject::DoCommand(0, ::GetCompanySettingIndex("company.engine_renew_months"), months, CMD_CHANGE_COMPANY_SETTING);
}

/* static */ int16 ScriptCompany::GetAutoRenewMonths(CompanyID company)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return 0;

	return ::Company::Get((CompanyID)company)->settings.engine_renew_months;
}

/* static */ bool ScriptCompany::SetAutoRenewMoney(uint32 money)
{
	return ScriptObject::DoCommand(0, ::GetCompanySettingIndex("company.engine_renew_money"), money, CMD_CHANGE_COMPANY_SETTING);
}

/* static */ uint32 ScriptCompany::GetAutoRenewMoney(CompanyID company)
{
	company = ResolveCompanyID(company);
	if (company == COMPANY_INVALID) return 0;

	return ::Company::Get((CompanyID)company)->settings.engine_renew_money;
}
