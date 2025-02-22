/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file script_config.cpp Implementation of ScriptConfig. */

#include "../stdafx.h"
#include "../settings_type.h"
#include "../core/random_func.hpp"
#include "script_info.hpp"
#include "script_config.hpp"

void ScriptConfig::Change(const char *name, int version, bool force_exact_match, bool is_random)
{
	free(this->name);
	this->name = (name == NULL) ? NULL : strdup(name);
	this->info = (name == NULL) ? NULL : this->FindInfo(this->name, version, force_exact_match);
	this->version = (info == NULL) ? -1 : info->GetVersion();
	this->is_random = is_random;
	if (this->config_list != NULL) delete this->config_list;
	this->config_list = (info == NULL) ? NULL : new ScriptConfigItemList();
	if (this->config_list != NULL) this->PushExtraConfigList();

	this->ClearConfigList();

	if (_game_mode == GM_NORMAL && this->info != NULL) {
		/* If we're in an existing game and the Script is changed, set all settings
		 *  for the Script that have the random flag to a random value. */
		for (ScriptConfigItemList::const_iterator it = this->info->GetConfigList()->begin(); it != this->info->GetConfigList()->end(); it++) {
			if ((*it).flags & SCRIPTCONFIG_RANDOM) {
				this->SetSetting((*it).name, InteractiveRandomRange((*it).max_value - (*it).min_value) + (*it).min_value);
			}
		}
		this->AddRandomDeviation();
	}
}

ScriptConfig::ScriptConfig(const ScriptConfig *config)
{
	this->name = (config->name == NULL) ? NULL : strdup(config->name);
	this->info = config->info;
	this->version = config->version;
	this->config_list = NULL;
	this->is_random = config->is_random;

	for (SettingValueList::const_iterator it = config->settings.begin(); it != config->settings.end(); it++) {
		this->settings[strdup((*it).first)] = (*it).second;
	}
	this->AddRandomDeviation();
}

ScriptConfig::~ScriptConfig()
{
	free(this->name);
	this->ResetSettings();
	if (this->config_list != NULL) delete this->config_list;
}

ScriptInfo *ScriptConfig::GetInfo() const
{
	return this->info;
}

const ScriptConfigItemList *ScriptConfig::GetConfigList()
{
	if (this->info != NULL) return this->info->GetConfigList();
	if (this->config_list == NULL) {
		this->config_list = new ScriptConfigItemList();
		this->PushExtraConfigList();
	}
	return this->config_list;
}

void ScriptConfig::ClearConfigList()
{
	for (SettingValueList::iterator it = this->settings.begin(); it != this->settings.end(); it++) {
		free((*it).first);
	}
	this->settings.clear();
}

int ScriptConfig::GetSetting(const char *name) const
{
	/* Return default values if the difficulty is not set to Custom */
	if (GetGameSettings().difficulty.diff_level != 3) {
		return this->info->GetSettingDefaultValue(name);
	}

	SettingValueList::const_iterator it = this->settings.find(name);
	if (it == this->settings.end()) return this->info->GetSettingDefaultValue(name);
	return (*it).second;
}

void ScriptConfig::SetSetting(const char *name, int value)
{
	/* You can only set Script specific settings if an Script is selected. */
	if (this->info == NULL) return;

	const ScriptConfigItem *config_item = this->info->GetConfigItem(name);
	if (config_item == NULL) return;

	value = Clamp(value, config_item->min_value, config_item->max_value);

	SettingValueList::iterator it = this->settings.find(name);
	if (it != this->settings.end()) {
		(*it).second = value;
	} else {
		this->settings[strdup(name)] = value;
	}
}

void ScriptConfig::ResetSettings()
{
	for (SettingValueList::iterator it = this->settings.begin(); it != this->settings.end(); it++) {
		free((*it).first);
	}
	this->settings.clear();
}

void ScriptConfig::AddRandomDeviation()
{
	for (ScriptConfigItemList::const_iterator it = this->GetConfigList()->begin(); it != this->GetConfigList()->end(); it++) {
		if ((*it).random_deviation != 0) {
			this->SetSetting((*it).name, InteractiveRandomRange((*it).random_deviation * 2) - (*it).random_deviation + this->GetSetting((*it).name));
		}
	}
}

bool ScriptConfig::HasScript() const
{
	return this->info != NULL;
}

bool ScriptConfig::IsRandom() const
{
	return this->is_random;
}

const char *ScriptConfig::GetName() const
{
	return this->name;
}

int ScriptConfig::GetVersion() const
{
	return this->version;
}

void ScriptConfig::StringToSettings(const char *value)
{
	char *value_copy = strdup(value);
	char *s = value_copy;

	while (s != NULL) {
		/* Analyze the string ('name=value,name=value\0') */
		char *item_name = s;
		s = strchr(s, '=');
		if (s == NULL) break;
		if (*s == '\0') break;
		*s = '\0';
		s++;

		char *item_value = s;
		s = strchr(s, ',');
		if (s != NULL) {
			*s = '\0';
			s++;
		}

		this->SetSetting(item_name, atoi(item_value));
	}
	free(value_copy);
}

void ScriptConfig::SettingsToString(char *string, size_t size) const
{
	string[0] = '\0';
	for (SettingValueList::const_iterator it = this->settings.begin(); it != this->settings.end(); it++) {
		char no[10];
		snprintf(no, sizeof(no), "%d", (*it).second);

		/* Check if the string would fit in the destination */
		size_t needed_size = strlen((*it).first) + 1 + strlen(no) + 1;
		/* If it doesn't fit, skip the next settings */
		if (size <= needed_size) break;
		size -= needed_size;

		strcat(string, (*it).first);
		strcat(string, "=");
		strcat(string, no);
		strcat(string, ",");
	}
	/* Remove the last ',', but only if at least one setting was saved. */
	size_t len = strlen(string);
	if (len > 0) string[len - 1] = '\0';
}
