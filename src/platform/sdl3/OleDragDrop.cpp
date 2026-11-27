/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/OleDragDrop.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
// windows.h's macro would turn BMessenger::SendMessage() into SendMessageA()
#ifdef SendMessage
#undef SendMessage
#endif

#include <ngui/AppDefs.h>
#include <ngui/Message.h>

#include "DragData.h"
#include "PlatformEvent.h"


namespace ngui::platform::sdl3 {


namespace {


std::wstring
Wide(std::string_view text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
	(void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		wide.data(), length);
	return wide;
}


std::string
Utf8(std::wstring_view text)
{
	if (text.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8(static_cast<std::size_t>(std::max(length, 0)), '\0');
	(void)WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		utf8.data(), length, nullptr, nullptr);
	return utf8;
}


std::string_view
Text(const std::vector<std::byte>& bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}


std::vector<std::byte>
Bytes(const void* data, std::size_t size)
{
	const auto* begin = static_cast<const std::byte*>(data);
	return std::vector<std::byte>(begin, begin + size);
}


CLIPFORMAT
Registered(const wchar_t* name)
{
	return static_cast<CLIPFORMAT>(RegisterClipboardFormatW(name));
}


// One format of a data object and its bytes as an HGLOBAL wants them.
struct Format {
	CLIPFORMAT				format;
	std::vector<std::byte>	bytes;
};


// The formats of \a items: each MIME type as a registered format of its
// name; text also as CF_UNICODETEXT and "text/plain;charset=utf-8" (SDL's),
// files also as CF_HDROP, PNG also as "PNG" (browsers, Office).
std::vector<Format>
FormatsOf(const std::vector<DragItem>& items)
{
	std::vector<Format> formats;
	for (const DragItem& item : items) {
		formats.push_back({Registered(Wide(item.type).c_str()), item.data});
		if (item.type == "text/plain") {
			std::wstring text = Wide(Text(item.data));
			text.push_back(L'\0');
			formats.push_back({CF_UNICODETEXT,
				Bytes(text.data(), text.size() * sizeof(wchar_t))});
			formats.push_back({Registered(L"text/plain;charset=utf-8"), item.data});
		} else if (item.type == "text/uri-list") {
			std::wstring files;
			for (const std::string& path : detail::PathsOf(Text(item.data))) {
				std::wstring wide = Wide(path);
				std::replace(wide.begin(), wide.end(), L'/', L'\\');
				files += wide;
				files.push_back(L'\0');
			}
			if (!files.empty()) {
				files.push_back(L'\0');
				DROPFILES header{};
				header.pFiles = sizeof(DROPFILES);
				header.fWide = TRUE;
				std::vector<std::byte> drop = Bytes(&header, sizeof(header));
				const std::vector<std::byte> names = Bytes(files.data(),
					files.size() * sizeof(wchar_t));
				drop.insert(drop.end(), names.begin(), names.end());
				formats.push_back({CF_HDROP, std::move(drop)});
			}
		} else if (item.type == "image/png") {
			formats.push_back({Registered(L"PNG"), item.data});
		}
	}
	return formats;
}


// The IDataObject of a drag: its formats, in HGLOBALs.
class DataObject final : public IDataObject {
public:
	explicit DataObject(std::vector<Format> formats)
		:
		fFormats(std::move(formats))
	{
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
	{
		if (object == nullptr)
			return E_POINTER;
		if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_IDataObject)) {
			*object = static_cast<IDataObject*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++fReferences;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG left = --fReferences;
		if (left == 0)
			delete this;
		return left;
	}

	HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override
	{
		if (format == nullptr || medium == nullptr)
			return E_INVALIDARG;
		const Format* found = _Find(*format);
		if (found == nullptr)
			return DV_E_FORMATETC;
		HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, std::max<std::size_t>(found->bytes.size(),
			1));
		if (global == nullptr)
			return E_OUTOFMEMORY;
		void* bytes = GlobalLock(global);
		if (!found->bytes.empty())
			std::memcpy(bytes, found->bytes.data(), found->bytes.size());
		GlobalUnlock(global);
		medium->tymed = TYMED_HGLOBAL;
		medium->hGlobal = global;
		medium->pUnkForRelease = nullptr;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override
	{
		return format != nullptr && _Find(*format) != nullptr ? S_OK : DV_E_FORMATETC;
	}

	HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override
	{
		if (out != nullptr)
			out->ptd = nullptr;
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override
	{
		return E_NOTIMPL;
	}

	HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** formats) override
	{
		if (formats == nullptr)
			return E_POINTER;
		if (direction != DATADIR_GET)
			return E_NOTIMPL;
		std::vector<FORMATETC> list;
		for (const Format& format : fFormats)
			list.push_back({format.format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL});
		return SHCreateStdEnumFmtEtc(static_cast<UINT>(list.size()), list.data(), formats);
	}

	HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override
	{
		return OLE_E_ADVISENOTSUPPORTED;
	}

	HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override
	{
		return OLE_E_ADVISENOTSUPPORTED;
	}

	HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override
	{
		return OLE_E_ADVISENOTSUPPORTED;
	}

private:
	const Format* _Find(const FORMATETC& format) const
	{
		if ((format.tymed & TYMED_HGLOBAL) == 0 || format.dwAspect != DVASPECT_CONTENT)
			return nullptr;
		for (const Format& candidate : fFormats) {
			if (candidate.format == format.cfFormat)
				return &candidate;
		}
		return nullptr;
	}

	std::atomic<ULONG> fReferences = 1;
	std::vector<Format> fFormats;
};


// The IDropSource of a drag: dropped when the buttons are up, cancelled by
// Escape; Windows' cursors.
class DropSource final : public IDropSource {
public:
	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
	{
		if (object == nullptr)
			return E_POINTER;
		if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_IDropSource)) {
			*object = static_cast<IDropSource*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++fReferences;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG left = --fReferences;
		if (left == 0)
			delete this;
		return left;
	}

	HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override
	{
		if (escape)
			return DRAGDROP_S_CANCEL;
		if ((keys & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0)
			return DRAGDROP_S_DROP;
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override
	{
		return DRAGDROP_S_USEDEFAULTCURSORS;
	}

private:
	std::atomic<ULONG> fReferences = 1;
};


// The bytes of \a format in \a data, if it has them in an HGLOBAL.
std::optional<std::vector<std::byte>>
Read(IDataObject* data, CLIPFORMAT format)
{
	FORMATETC wanted{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
	STGMEDIUM medium{};
	if (data->GetData(&wanted, &medium) != S_OK)
		return std::nullopt;
	std::optional<std::vector<std::byte>> bytes;
	if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal != nullptr) {
		if (const void* locked = GlobalLock(medium.hGlobal)) {
			bytes = Bytes(locked, GlobalSize(medium.hGlobal));
			GlobalUnlock(medium.hGlobal);
		}
	}
	ReleaseStgMedium(&medium);
	return bytes;
}


// \a bytes up to the first terminating zero of \a unit bytes.
std::vector<std::byte>
UpToZero(std::vector<std::byte> bytes, std::size_t unit)
{
	for (std::size_t i = 0; i + unit <= bytes.size(); i += unit) {
		bool zero = true;
		for (std::size_t j = 0; j < unit; j++)
			zero = zero && bytes[i + j] == std::byte{0};
		if (zero) {
			bytes.resize(i);
			break;
		}
	}
	return bytes;
}


// The name of a registered format, empty for a standard one.
std::string
NameOf(CLIPFORMAT format)
{
	wchar_t name[256];
	const int length = GetClipboardFormatNameW(format, name, 256);
	return length > 0 ? Utf8({name, static_cast<std::size_t>(length)}) : std::string();
}


// The IDropTarget of a window: kDropBegin, kDropPosition, then at the drop
// kDropData with every item, and kDropComplete (also when the drag leaves).
class DropTarget final : public IDropTarget {
public:
	DropTarget(BMessenger target, ScreenToContent toContent)
		:
		fTarget(std::move(target)),
		fToContent(std::move(toContent))
	{
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
	{
		if (object == nullptr)
			return E_POINTER;
		if (IsEqualIID(id, IID_IUnknown) || IsEqualIID(id, IID_IDropTarget)) {
			*object = static_cast<IDropTarget*>(this);
			AddRef();
			return S_OK;
		}
		*object = nullptr;
		return E_NOINTERFACE;
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++fReferences;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		const ULONG left = --fReferences;
		if (left == 0)
			delete this;
		return left;
	}

	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL point,
		DWORD* effect) override
	{
		_Send(BMessage(kDropBegin), point);
		_Send(BMessage(kDropPosition), point);
		_Effect(effect);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL point, DWORD* effect) override
	{
		_Send(BMessage(kDropPosition), point);
		_Effect(effect);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE DragLeave() override
	{
		_Send(BMessage(kDropComplete), fLast);
		return S_OK;
	}

	HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL point,
		DWORD* effect) override
	{
		_Send(BMessage(kDropPosition), point);
		BMessage dropped(kDropData);
		for (const DragItem& item : ItemsOf(data))
			(void)dropped.AddData(item.type, BTypeCode::Mime, item.data);
		_Send(std::move(dropped), point);
		_Send(BMessage(kDropComplete), point);
		_Effect(effect);
		return S_OK;
	}

private:
	void _Send(BMessage message, POINTL point)
	{
		fLast = point;
		(void)message.AddPoint("where", fToContent(point.x, point.y));
		PlatformEventMark::Set(message);
		(void)fTarget.SendMessage(message);
	}

	// A copy where the source allows it.
	static void _Effect(DWORD* effect)
	{
		if (effect == nullptr)
			return;
		*effect = (*effect & DROPEFFECT_COPY) != 0 ? DROPEFFECT_COPY
			: (*effect & DROPEFFECT_MOVE) != 0 ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
	}

	std::atomic<ULONG> fReferences = 1;
	BMessenger fTarget;
	ScreenToContent fToContent;
	POINTL fLast{0, 0};
};


} // namespace


void*
RegisterDropTarget(void* hwnd, BMessenger target, std::function<float()> density)
{
	auto* window = static_cast<HWND>(hwnd);
	ScreenToContent toContent = [window, density = std::move(density)](long x, long y) {
		POINT point{x, y};
		(void)ScreenToClient(window, &point);
		const float scale = density();
		const float divisor = scale > 0 ? scale : 1;
		return gfx::Point{static_cast<float>(point.x) / divisor,
			static_cast<float>(point.y) / divisor};
	};
	// SDL's target goes; SDL revokes again when the window is destroyed,
	// which is harmless then
	(void)RevokeDragDrop(window);
	auto* drop = new DropTarget(std::move(target), std::move(toContent));
	if (RegisterDragDrop(window, drop) != S_OK) {
		drop->Release();
		return nullptr;
	}
	return drop;
}


void
RevokeDropTarget(void* hwnd, void* handle)
{
	if (handle == nullptr)
		return;
	(void)RevokeDragDrop(static_cast<HWND>(hwnd));
	static_cast<IDropTarget*>(handle)->Release();
}


void
RunSystemDrag(const std::vector<DragItem>& items)
{
	auto* data = static_cast<IDataObject*>(MakeDataObject(items));
	auto* source = new DropSource();
	DWORD effect = DROPEFFECT_NONE;
	(void)DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK,
		&effect);
	source->Release();
	data->Release();
}


void*
MakeDataObject(const std::vector<DragItem>& items)
{
	return static_cast<IDataObject*>(new DataObject(FormatsOf(items)));
}


std::vector<DragItem>
ItemsOf(void* dataObject)
{
	auto* data = static_cast<IDataObject*>(dataObject);
	std::vector<DragItem> items;
	const auto add = [&items](std::string type, std::vector<std::byte> bytes) {
		const bool known = std::ranges::any_of(items, [&type](const DragItem& item) {
			return item.type == type;
		});
		if (!known)
			items.push_back({std::move(type), std::move(bytes)});
	};
	// the formats the source offers, in its order
	IEnumFORMATETC* formats = nullptr;
	if (data->EnumFormatEtc(DATADIR_GET, &formats) != S_OK || formats == nullptr)
		return items;
	FORMATETC format{};
	while (formats->Next(1, &format, nullptr) == S_OK) {
		if (format.ptd != nullptr)
			CoTaskMemFree(format.ptd);
		if (format.cfFormat == CF_UNICODETEXT) {
			if (auto bytes = Read(data, CF_UNICODETEXT)) {
				const std::vector<std::byte> text = UpToZero(std::move(*bytes),
					sizeof(wchar_t));
				const std::string utf8 = Utf8({reinterpret_cast<const wchar_t*>(text.data()),
					text.size() / sizeof(wchar_t)});
				add("text/plain", Bytes(utf8.data(), utf8.size()));
			}
			continue;
		}
		if (format.cfFormat == CF_HDROP) {
			FORMATETC wanted{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
			STGMEDIUM medium{};
			if (data->GetData(&wanted, &medium) != S_OK)
				continue;
			auto* drop = static_cast<HDROP>(medium.hGlobal);
			std::string uris;
			const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
			for (UINT i = 0; i < count; i++) {
				std::wstring path(DragQueryFileW(drop, i, nullptr, 0), L'\0');
				(void)DragQueryFileW(drop, i, path.data(), static_cast<UINT>(path.size() + 1));
				uris += detail::FileUri(Utf8(path)) + "\r\n";
			}
			ReleaseStgMedium(&medium);
			if (!uris.empty())
				add("text/uri-list", Bytes(uris.data(), uris.size()));
			continue;
		}
		std::string name = NameOf(format.cfFormat);
		if (name == "PNG")
			name = "image/png";
		else if (name == "text/plain;charset=utf-8")
			name = "text/plain";
		else if (name.find('/') == std::string::npos)
			continue;
		if (auto bytes = Read(data, format.cfFormat)) {
			if (name.starts_with("text/"))
				*bytes = UpToZero(std::move(*bytes), 1);
			add(std::move(name), std::move(*bytes));
		}
	}
	formats->Release();
	return items;
}


void*
MakeDropTarget(BMessenger target, ScreenToContent toContent)
{
	return static_cast<IDropTarget*>(new DropTarget(std::move(target), std::move(toContent)));
}


} // namespace ngui::platform::sdl3
