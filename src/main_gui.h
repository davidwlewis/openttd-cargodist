/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file main_gui.cpp Declaration of class for handling of the main viewport. */

#ifndef MAIN_GUI_H_
#define MAIN_GUI_H_

#include "linkgraph_gui.h"
#include "hotkeys.h"
#include "core/geometry_type.hpp"

/** Widgets of the main window. */
enum MainWindowWidgets {
	MW_VIEWPORT, ///< Main window viewport.
};

struct MainWindow : Window
{
	LinkGraphOverlay<MainWindow, MW_VIEWPORT> overlay;

	MainWindow();

	virtual void OnPaint();
	virtual EventState OnKeyPress(uint16 key, uint16 keycode);
	virtual void OnScroll(Point delta);
	virtual void OnMouseWheel(int wheel);
	virtual void OnResize();
	virtual void OnInvalidateData(int data);

	Point GetStationMiddle(const Station *st) const;

	static Hotkey<MainWindow> global_hotkeys[];
};

#endif /* MAIN_GUI_H_ */
