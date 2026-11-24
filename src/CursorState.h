/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <mutex>
#include <optional>

#include <ngui/platform/Platform.h>


namespace ngui::detail {


// The mouse cursor of the process (Haiku's ServerApp: its application
// cursor, the cursor of the view under the mouse, the hide level and the
// obscured state). Shows the result on platform::Current(), only when it
// changes. Thread-safe.
class CursorState {
public:
	static	CursorState&		Default();

			void				SetAppCursor(const platform::CursorShape& cursor);
			// The cursor of the view under the mouse; std::nullopt for none
			// (the application cursor is shown).
			void				SetViewCursor(const std::optional<platform::CursorShape>& cursor);
			void				Hide();
			void				Show();
			void				Obscure();
			// The mouse moved: an obscured cursor shows again.
			void				MouseMoved();
			bool				IsHidden() const;
			// platform::Current() changed: the cursor is shown on the new one
			// (a new platform may have the old one's address).
			void				PlatformChanged();

			platform::CursorShape Shown() const;
			bool				IsVisible() const;

private:
			void				_Apply();

	mutable	std::mutex			fLock;
			platform::CursorShape fApp;
			std::optional<platform::CursorShape> fView;
			int					fHideLevel = 0;
			bool				fObscured = false;
			// what the platform shows
			platform::Platform*	fShownOn = nullptr;
			std::optional<platform::CursorShape> fShown;
			std::optional<bool>	fVisible;
};


} // namespace ngui::detail
