/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/X11DragDrop.h"

#include <cstddef>
#include <deque>
#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <dlfcn.h>

#include "platform/sdl3/DropPayload.h"

// after NewGUI's headers: Xlib defines Bool, Status, None and others as
// macros
#include <X11/Xatom.h>
#include <X11/Xlib.h>


namespace ngui::platform::sdl3 {


namespace {


// The functions of Xlib XDND needs, from the library SDL loaded too
// (libX11.so.6).
struct Xlib {
	void*		library = nullptr;
	Atom		(*InternAtom)(Display*, const char*, Bool) = nullptr;
	char*		(*GetAtomName)(Display*, Atom) = nullptr;
	int			(*Free)(void*) = nullptr;
	int			(*GetWindowProperty)(Display*, Window, Atom, long, long, Bool, Atom, Atom*,
					int*, unsigned long*, unsigned long*, unsigned char**) = nullptr;
	Status		(*SendEvent)(Display*, Window, Bool, long, XEvent*) = nullptr;
	int			(*ConvertSelection)(Display*, Atom, Atom, Atom, Window, Time) = nullptr;
	Bool		(*TranslateCoordinates)(Display*, Window, Window, int, int, int*, int*,
					Window*) = nullptr;
	Window		(*RootWindowOf)(Display*) = nullptr;
	int			(*Flush)(Display*) = nullptr;
	int			(*SetSelectionOwner)(Display*, Atom, Window, Time) = nullptr;
	Window		(*GetSelectionOwner)(Display*, Atom) = nullptr;
	Display*	(*OpenDisplay)(const char*) = nullptr;
	int			(*CloseDisplay)(Display*) = nullptr;
	int			(*ScreenOf)(Display*) = nullptr;
	int			(*ChangeProperty)(Display*, Window, Atom, Atom, int, int, const unsigned char*,
					int) = nullptr;
	Bool		(*QueryPointer)(Display*, Window, Window*, Window*, int*, int*, int*, int*,
					unsigned int*) = nullptr;

	bool Load()
	{
		library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
		if (library == nullptr)
			return false;
		bool complete = true;
		const auto bind = [&](auto& function, const char* name) {
			// POSIX: a function's address passes through dlsym()'s void*.
			void* symbol = dlsym(library, name);
			complete = complete && symbol != nullptr;
			function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(symbol);
		};
		bind(InternAtom, "XInternAtom");
		bind(GetAtomName, "XGetAtomName");
		bind(Free, "XFree");
		bind(GetWindowProperty, "XGetWindowProperty");
		bind(SendEvent, "XSendEvent");
		bind(ConvertSelection, "XConvertSelection");
		bind(TranslateCoordinates, "XTranslateCoordinates");
		bind(RootWindowOf, "XDefaultRootWindow");
		bind(Flush, "XFlush");
		bind(SetSelectionOwner, "XSetSelectionOwner");
		bind(GetSelectionOwner, "XGetSelectionOwner");
		bind(OpenDisplay, "XOpenDisplay");
		bind(CloseDisplay, "XCloseDisplay");
		bind(ScreenOf, "XDefaultScreen");
		bind(ChangeProperty, "XChangeProperty");
		bind(QueryPointer, "XQueryPointer");
		return complete;
	}
};


const Xlib*
Library()
{
	static const Xlib* const library = []() -> const Xlib* {
		static Xlib functions;
		return functions.Load() ? &functions : nullptr;
	}();
	return library;
}


// The atoms of XDND on a display.
struct Atoms {
	Atom	enter;
	Atom	position;
	Atom	status;
	Atom	leave;
	Atom	drop;
	Atom	finished;
	Atom	selection;
	Atom	typeList;
	Atom	actionCopy;
	Atom	utf8String;
	Atom	property;	// where the source puts the data for NewGUI
	Atom	incr;
	Atom	aware;
	Atom	targets;

	Atoms(const Xlib& x, Display* display)
		:
		enter(x.InternAtom(display, "XdndEnter", False)),
		position(x.InternAtom(display, "XdndPosition", False)),
		status(x.InternAtom(display, "XdndStatus", False)),
		leave(x.InternAtom(display, "XdndLeave", False)),
		drop(x.InternAtom(display, "XdndDrop", False)),
		finished(x.InternAtom(display, "XdndFinished", False)),
		selection(x.InternAtom(display, "XdndSelection", False)),
		typeList(x.InternAtom(display, "XdndTypeList", False)),
		actionCopy(x.InternAtom(display, "XdndActionCopy", False)),
		utf8String(x.InternAtom(display, "UTF8_STRING", False)),
		property(x.InternAtom(display, "NGUI_XDND", False)),
		incr(x.InternAtom(display, "INCR", False)),
		aware(x.InternAtom(display, "XdndAware", False)),
		targets(x.InternAtom(display, "TARGETS", False))
	{
	}
};


// A window that takes drops, and the drop it is taking.
struct Target {
	Target(SDL_Window* window, Display* connection, const Atoms& interned)
		:
		sdl(window),
		id(SDL_GetWindowID(window)),
		display(connection),
		atoms(interned)
	{
	}

	SDL_Window*				sdl;
	SDL_WindowID			id;
	Display*				display;
	// interned when the window comes: a new connection of SDL may have the
	// old one's address, after the X server forgot the old atoms
	Atoms					atoms;
	Window					source = 0;
	std::vector<Atom>		offered;
	bool					begun = false;
	gfx::Point				where;
	// while the data comes: the types still to ask for, with their MIME
	// types, and what came
	std::deque<std::pair<Atom, std::string>> pending;
	std::vector<DragItem>	items;
	Time					time = CurrentTime;
};


std::map<Window, Target> sTargets;
std::uint32_t sDropEvent = 0;
bool sHooked = false;


// A drag out of the application: NewGUI's window is the XDND source and
// owns XdndSelection; the drop target under the pointer, as far as known.
struct Drag {
	Drag(Display* connection, Window window, const Atoms& interned)
		:
		display(connection),
		source(window),
		atoms(interned)
	{
	}

	Display*				display;
	Window					source;
	Atoms					atoms;
	// the data by type offered (several text types for text/plain)
	std::vector<std::pair<Atom, std::vector<std::byte>>> offered;
	Window					target = 0;
	long					version = 0;
	bool					accepted = false;
	// a position sent, its XdndStatus not here yet
	bool					waiting = false;
	bool					moved = false;
	bool					released = false;
	bool					dropped = false;
	int						rootX = 0;
	int						rootY = 0;
};


std::optional<Drag> sDrag;


std::string
NameOf(const Xlib& x, Display* display, Atom atom)
{
	char* name = x.GetAtomName(display, atom);
	std::string result = name != nullptr ? name : "";
	if (name != nullptr)
		x.Free(name);
	return result;
}


// The MIME type of an offered type, empty for one without.
std::string
MimeOf(const std::string& name)
{
	if (name == "UTF8_STRING" || name.starts_with("text/plain"))
		return "text/plain";
	if (name.find('/') != std::string::npos)
		return name.substr(0, name.find(';'));
	return {};
}


void
Push(std::uint32_t type, const Target& target)
{
	SDL_Event event{};
	event.type = type;
	event.drop.windowID = target.id;
	event.drop.x = target.where.x;
	event.drop.y = target.where.y;
	(void)SDL_PushEvent(&event);
}


void
Send(const Xlib& x, const Target& target, Window ours, Atom type, long l1, long l4,
	long l2 = 0)
{
	XEvent event{};
	event.xclient.type = ClientMessage;
	event.xclient.display = target.display;
	event.xclient.window = target.source;
	event.xclient.message_type = type;
	event.xclient.format = 32;
	event.xclient.data.l[0] = static_cast<long>(ours);
	event.xclient.data.l[1] = l1;
	event.xclient.data.l[2] = l2;
	event.xclient.data.l[3] = 0;
	event.xclient.data.l[4] = l4;
	(void)x.SendEvent(target.display, target.source, False, NoEventMask, &event);
	(void)x.Flush(target.display);
}


// The drop is over: the data in SDL's queue, the source told.
void
Finish(const Xlib& x, Window ours, Target& target)
{
	const Atoms& atoms = target.atoms;
	const bool any = !target.items.empty();
	SDL_Event event{};
	event.type = sDropEvent;
	event.user.windowID = target.id;
	event.user.data1 = new DropPayload{std::move(target.items), target.where};
	if (!SDL_PushEvent(&event))
		delete static_cast<DropPayload*>(event.user.data1);
	// XdndFinished: accepted in l[1], the action in l[2]
	Send(x, target, ours, atoms.finished, any ? 1 : 0, 0,
		any ? static_cast<long>(atoms.actionCopy) : 0);
	target.source = 0;
	target.begun = false;
	target.items.clear();
	target.pending.clear();
}


// Asks the source for the next type, or finishes.
void
RequestNext(const Xlib& x, Window ours, Target& target)
{
	if (target.pending.empty()) {
		Finish(x, ours, target);
		return;
	}
	const Atoms& atoms = target.atoms;
	(void)x.ConvertSelection(target.display, atoms.selection, target.pending.front().first,
		atoms.property, ours, target.time);
	(void)x.Flush(target.display);
}


void
ClientMessageOf(const Xlib& x, const XClientMessageEvent& message, Target& target)
{
	const Atoms& atoms = target.atoms;
	const Window ours = message.window;
	if (message.message_type == atoms.enter) {
		target.source = static_cast<Window>(message.data.l[0]);
		target.offered.clear();
		target.begun = false;
		if ((message.data.l[1] & 1) != 0) {
			// more than three types: in XdndTypeList of the source
			Atom type = None;
			int format = 0;
			unsigned long count = 0;
			unsigned long after = 0;
			unsigned char* data = nullptr;
			if (x.GetWindowProperty(target.display, target.source, atoms.typeList, 0, 1024,
					False, XA_ATOM, &type, &format, &count, &after, &data) == Success
				&& data != nullptr) {
				const auto* list = reinterpret_cast<const Atom*>(data);
				target.offered.assign(list, list + count);
			}
			if (data != nullptr)
				x.Free(data);
		} else {
			for (int i = 2; i <= 4; i++) {
				if (message.data.l[i] != 0)
					target.offered.push_back(static_cast<Atom>(message.data.l[i]));
			}
		}
		return;
	}
	if (static_cast<Window>(message.data.l[0]) != target.source)
		return;
	if (message.message_type == atoms.position) {
		const int rootX = static_cast<int>((message.data.l[2] >> 16) & 0xffff);
		const int rootY = static_cast<int>(message.data.l[2] & 0xffff);
		int x0 = 0;
		int y0 = 0;
		Window child = 0;
		(void)x.TranslateCoordinates(target.display, x.RootWindowOf(target.display), ours,
			rootX, rootY, &x0, &y0, &child);
		const float density = SDL_GetWindowPixelDensity(target.sdl);
		const float scale = density > 0 ? density : 1;
		target.where = {static_cast<float>(x0) / scale, static_cast<float>(y0) / scale};
		if (!target.begun) {
			target.begun = true;
			Push(SDL_EVENT_DROP_BEGIN, target);
		}
		Push(SDL_EVENT_DROP_POSITION, target);
		// accepted, as a copy; positions wanted anywhere in the window
		Send(x, target, ours, atoms.status, 1 | 2, static_cast<long>(atoms.actionCopy));
		return;
	}
	if (message.message_type == atoms.leave) {
		Push(SDL_EVENT_DROP_COMPLETE, target);
		target.source = 0;
		target.begun = false;
		return;
	}
	if (message.message_type == atoms.drop) {
		target.time = static_cast<Time>(message.data.l[2]);
		target.items.clear();
		target.pending.clear();
		// each MIME type once, in the source's order; text as UTF-8 where
		// the source offers it
		for (const Atom atom : target.offered) {
			const std::string name = NameOf(x, target.display, atom);
			const std::string mime = MimeOf(name);
			if (mime.empty())
				continue;
			const bool utf8 = atom == atoms.utf8String
				|| name.find("utf-8") != std::string::npos;
			bool known = false;
			for (auto& [other, otherMime] : target.pending) {
				if (otherMime != mime)
					continue;
				known = true;
				if (utf8)
					other = atom;
			}
			if (!known)
				target.pending.emplace_back(atom, mime);
		}
		RequestNext(x, ours, target);
	}
}


void
SelectionOf(const Xlib& x, const XSelectionEvent& selection, Target& target)
{
	if (target.source == 0 || target.pending.empty())
		return;
	const Atoms& atoms = target.atoms;
	if (selection.property != None) {
		Atom type = None;
		int format = 0;
		unsigned long count = 0;
		unsigned long after = 0;
		unsigned char* data = nullptr;
		if (x.GetWindowProperty(target.display, selection.requestor, atoms.property, 0,
				64 * 1024 * 1024 / 4, True, AnyPropertyType, &type, &format, &count, &after,
				&data) == Success
			&& data != nullptr && format == 8 && type != atoms.incr) {
			// INCR (very large data in pieces) is left out
			const auto* bytes = reinterpret_cast<const std::byte*>(data);
			target.items.push_back({target.pending.front().second,
				std::vector<std::byte>(bytes, bytes + count)});
		}
		if (data != nullptr)
			x.Free(data);
	}
	target.pending.pop_front();
	RequestNext(x, selection.requestor, target);
}


void
SendTo(const Xlib& x, const Drag& drag, Atom type, long l1, long l2, long l3, long l4)
{
	XEvent event{};
	event.xclient.type = ClientMessage;
	event.xclient.display = drag.display;
	event.xclient.window = drag.target;
	event.xclient.message_type = type;
	event.xclient.format = 32;
	event.xclient.data.l[0] = static_cast<long>(drag.source);
	event.xclient.data.l[1] = l1;
	event.xclient.data.l[2] = l2;
	event.xclient.data.l[3] = l3;
	event.xclient.data.l[4] = l4;
	(void)x.SendEvent(drag.display, drag.target, False, NoEventMask, &event);
	(void)x.Flush(drag.display);
}


// The XDND version of \a window, 0 for a window that takes no drops.
long
AwareOf(const Xlib& x, const Drag& drag, Window window)
{
	Atom type = None;
	int format = 0;
	unsigned long count = 0;
	unsigned long after = 0;
	unsigned char* data = nullptr;
	long version = 0;
	if (x.GetWindowProperty(drag.display, window, drag.atoms.aware, 0, 1, False, XA_ATOM,
			&type, &format, &count, &after, &data) == Success
		&& data != nullptr && count == 1) {
		version = static_cast<long>(*reinterpret_cast<const Atom*>(data));
	}
	if (data != nullptr)
		x.Free(data);
	return version;
}


// The window under the point of the root window that takes drops (with
// XdndAware, the application's window inside the window manager's frame),
// and its version.
std::pair<Window, long>
TargetAt(const Xlib& x, const Drag& drag, int rootX, int rootY)
{
	const Window root = x.RootWindowOf(drag.display);
	Window window = root;
	for (int depth = 0; depth < 16; depth++) {
		int childX = 0;
		int childY = 0;
		Window child = 0;
		if (!x.TranslateCoordinates(drag.display, root, window, rootX, rootY, &childX, &childY,
				&child) || child == 0) {
			break;
		}
		window = child;
		if (const long version = AwareOf(x, drag, window); version >= 3)
			return {window, version};
	}
	return {0, 0};
}


void
SendPosition(const Xlib& x, Drag& drag)
{
	SendTo(x, drag, drag.atoms.position, 0, (static_cast<long>(drag.rootX) << 16)
		| (drag.rootY & 0xffff), CurrentTime, static_cast<long>(drag.atoms.actionCopy));
	drag.waiting = true;
	drag.moved = false;
}


// The release: the drop where the target took it, else no drop.
void
Release(const Xlib& x, Drag& drag)
{
	if (drag.target != 0 && drag.accepted) {
		SendTo(x, drag, drag.atoms.drop, 0, CurrentTime, 0, 0);
		drag.dropped = true;
		return;
	}
	if (drag.target != 0)
		SendTo(x, drag, drag.atoms.leave, 0, 0, 0, 0);
	sDrag.reset();
}


// Follows the pointer: the target under it hears of the drag, the release
// ends it.
void
Track(const Xlib& x, Drag& drag)
{
	if (drag.dropped || drag.released)
		return;
	Window root = 0;
	Window child = 0;
	int windowX = 0;
	int windowY = 0;
	unsigned int buttons = 0;
	if (!x.QueryPointer(drag.display, x.RootWindowOf(drag.display), &root, &child, &drag.rootX,
			&drag.rootY, &windowX, &windowY, &buttons)) {
		return;
	}
	const auto [target, version] = TargetAt(x, drag, drag.rootX, drag.rootY);
	if (target != drag.target) {
		if (drag.target != 0)
			SendTo(x, drag, drag.atoms.leave, 0, 0, 0, 0);
		drag.target = target;
		drag.version = std::min(version, 5L);
		drag.accepted = false;
		drag.waiting = false;
		if (target != 0) {
			// the types in XdndTypeList, so any number of them
			SendTo(x, drag, drag.atoms.enter, (drag.version << 24) | 1, 0, 0, 0);
		}
	}
	if ((buttons & (Button1Mask | Button2Mask | Button3Mask)) == 0) {
		drag.released = true;
		// with a position on its way, its status decides
		if (!drag.waiting)
			Release(x, drag);
		return;
	}
	if (drag.target == 0)
		return;
	if (drag.waiting)
		drag.moved = true;
	else
		SendPosition(x, drag);
}


// The target's answers: XdndStatus and XdndFinished.
bool
SourceMessageOf(const Xlib& x, const XClientMessageEvent& message)
{
	if (!sDrag.has_value() || message.window != sDrag->source)
		return false;
	Drag& drag = *sDrag;
	if (message.message_type == drag.atoms.status) {
		if (static_cast<Window>(message.data.l[0]) == drag.target) {
			drag.accepted = (message.data.l[1] & 1) != 0;
			drag.waiting = false;
			if (drag.released)
				Release(x, drag);
			else if (drag.moved)
				SendPosition(x, drag);
		}
		return true;
	}
	if (message.message_type == drag.atoms.finished) {
		if (drag.dropped && static_cast<Window>(message.data.l[0]) == drag.target)
			sDrag.reset();
		return true;
	}
	return false;
}


// A request of the target for the data of a type.
bool
RequestOf(const Xlib& x, const XSelectionRequestEvent& request)
{
	if (!sDrag.has_value() || request.owner != sDrag->source
		|| request.selection != sDrag->atoms.selection) {
		return false;
	}
	const Drag& drag = *sDrag;
	const Atom property = request.property != None ? request.property : request.target;
	XEvent reply{};
	reply.xselection.type = SelectionNotify;
	reply.xselection.display = drag.display;
	reply.xselection.requestor = request.requestor;
	reply.xselection.selection = request.selection;
	reply.xselection.target = request.target;
	reply.xselection.time = request.time;
	reply.xselection.property = None;
	if (request.target == drag.atoms.targets) {
		std::vector<Atom> types = {drag.atoms.targets};
		for (const auto& [type, bytes] : drag.offered)
			types.push_back(type);
		(void)x.ChangeProperty(drag.display, request.requestor, property, XA_ATOM, 32,
			PropModeReplace, reinterpret_cast<const unsigned char*>(types.data()),
			static_cast<int>(types.size()));
		reply.xselection.property = property;
	}
	for (const auto& [type, bytes] : drag.offered) {
		if (type != request.target)
			continue;
		(void)x.ChangeProperty(drag.display, request.requestor, property, type, 8,
			PropModeReplace, reinterpret_cast<const unsigned char*>(bytes.data()),
			static_cast<int>(bytes.size()));
		reply.xselection.property = property;
		break;
	}
	(void)x.SendEvent(drag.display, request.requestor, False, NoEventMask, &reply);
	(void)x.Flush(drag.display);
	return true;
}


// SDL's X11 event hook: the XDND messages of the windows that take drops,
// the data they asked for, and the drag out of the application; everything
// else goes on to SDL.
bool SDLCALL
Hook(void* /*userdata*/, XEvent* event)
{
	const Xlib* x = Library();
	if (x == nullptr)
		return true;
	// the pointer of a drag out, whichever event moved or released it
	// (core or XInput 2)
	if (sDrag.has_value()
		&& (event->type == MotionNotify || event->type == ButtonRelease
			|| event->type == GenericEvent)) {
		Track(*x, *sDrag);
	}
	if (event->type == SelectionRequest)
		return !RequestOf(*x, event->xselectionrequest);
	if (event->type == ClientMessage) {
		if (SourceMessageOf(*x, event->xclient))
			return false;
		const auto found = sTargets.find(event->xclient.window);
		if (found == sTargets.end())
			return true;
		const Atoms& atoms = found->second.atoms;
		const Atom type = event->xclient.message_type;
		if (type != atoms.enter && type != atoms.position && type != atoms.leave
			&& type != atoms.drop) {
			return true;
		}
		ClientMessageOf(*x, event->xclient, found->second);
		return false;
	}
	if (event->type == SelectionNotify) {
		// only the answers to the requests of a drop being taken
		const auto found = sTargets.find(event->xselection.requestor);
		if (found == sTargets.end() || found->second.pending.empty())
			return true;
		if (event->xselection.property != None
			&& event->xselection.property != found->second.atoms.property) {
			return true;
		}
		SelectionOf(*x, event->xselection, found->second);
		return false;
	}
	return true;
}


} // namespace


bool
AcceptX11Drops(SDL_Window* window, std::uint32_t eventType)
{
	if (Library() == nullptr)
		return false;
	const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
	auto* display = static_cast<Display*>(SDL_GetPointerProperty(properties,
		SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
	const auto handle = static_cast<Window>(SDL_GetNumberProperty(properties,
		SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
	if (display == nullptr || handle == 0)
		return false;
	sDropEvent = eventType;
	sTargets.insert_or_assign(handle, Target(window, display, Atoms(*Library(), display)));
	if (!sHooked) {
		sHooked = true;
		SDL_SetX11EventHook(&Hook, nullptr);
	}
	return true;
}


void
ForgetX11Drops(SDL_Window* window)
{
	const auto handle = static_cast<Window>(SDL_GetNumberProperty(
		SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
	sTargets.erase(handle);
	if (sDrag.has_value() && sDrag->source == handle)
		sDrag.reset();
}


bool
X11HasCompositor()
{
	const Xlib* x = Library();
	if (x == nullptr)
		return false;
	// a connection of its own: SDL's may not be open yet
	Display* display = x->OpenDisplay(nullptr);
	if (display == nullptr)
		return false;
	const std::string name = "_NET_WM_CM_S" + std::to_string(x->ScreenOf(display));
	const bool composited = x->GetSelectionOwner(display,
		x->InternAtom(display, name.c_str(), False)) != None;
	(void)x->CloseDisplay(display);
	return composited;
}


bool
StartX11Drag(SDL_Window* window, const std::vector<DragItem>& items)
{
	const Xlib* x = Library();
	const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
	auto* display = static_cast<Display*>(SDL_GetPointerProperty(properties,
		SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
	const auto handle = static_cast<Window>(SDL_GetNumberProperty(properties,
		SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
	const auto found = sTargets.find(handle);
	if (x == nullptr || display == nullptr || found == sTargets.end())
		return false;
	// a drag before it that never finished is given up
	if (sDrag.has_value() && sDrag->target != 0 && !sDrag->dropped)
		SendTo(*x, *sDrag, sDrag->atoms.leave, 0, 0, 0, 0);
	Drag drag(display, handle, found->second.atoms);
	for (const DragItem& item : items) {
		if (item.type == "text/plain") {
			// UTF-8, under the names programs ask for
			for (const char* name : {"text/plain;charset=utf-8", "UTF8_STRING", "text/plain"})
				drag.offered.emplace_back(x->InternAtom(display, name, False), item.data);
			continue;
		}
		drag.offered.emplace_back(x->InternAtom(display, item.type.c_str(), False), item.data);
	}
	if (drag.offered.empty())
		return false;
	std::vector<Atom> types;
	for (const auto& [type, bytes] : drag.offered)
		types.push_back(type);
	(void)x->ChangeProperty(display, handle, drag.atoms.typeList, XA_ATOM, 32, PropModeReplace,
		reinterpret_cast<const unsigned char*>(types.data()), static_cast<int>(types.size()));
	(void)x->SetSelectionOwner(display, drag.atoms.selection, handle, CurrentTime);
	sDrag.emplace(std::move(drag));
	Track(*x, *sDrag);
	return true;
}


} // namespace ngui::platform::sdl3
