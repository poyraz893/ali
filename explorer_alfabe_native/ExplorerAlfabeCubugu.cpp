#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uiautomation.h>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uiautomationcore.lib")

namespace
{
	constexpr wchar_t kOverlayClass[] = L"ExplorerAlfabeOverlay";
	constexpr wchar_t kPopupClass[] = L"ExplorerAlfabePopup";
	constexpr UINT_PTR kRefreshTimer = 1;
	constexpr int kOverlayWidth = 48;
	constexpr int kOverlayHeight = 40;
	constexpr wchar_t kLetters[] = L"ABCÇDEFGĞHIİJKLMNOÖPQRSŞTUÜVWXYZ";

	struct OverlayState
	{
		HWND explorer{};
		HWND overlay{};
		HWND popup{};
		bool overlayHover{};
		int hoverLetter{-1};
		bool compact{};
	};

	HINSTANCE g_instance{};
	IUIAutomation* g_automation{};
	std::map<HWND, OverlayState> g_states;
	HFONT g_buttonFont{};
	HFONT g_letterFont{};

	bool IsExplorerWindow(HWND hwnd)
	{
		wchar_t cls[64]{};
		GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
		return wcscmp(cls, L"CabinetWClass") == 0 && IsWindowVisible(hwnd) && !IsIconic(hwnd);
	}

	bool FindViewButton(HWND explorer, RECT& result)
	{
		if (!g_automation)
			return false;

		IUIAutomationElement* root{};
		if (FAILED(g_automation->ElementFromHandle(explorer, &root)) || !root)
			return false;

		VARIANT value{};
		value.vt = VT_I4;
		value.lVal = UIA_ButtonControlTypeId;
		IUIAutomationCondition* condition{};
		IUIAutomationElementArray* buttons{};
		bool found = false;
		if (SUCCEEDED(g_automation->CreatePropertyCondition(UIA_ControlTypePropertyId, value, &condition)) && condition &&
			SUCCEEDED(root->FindAll(TreeScope_Descendants, condition, &buttons)) && buttons)
		{
			int count{};
			buttons->get_Length(&count);
			for (int i = 0; i < count && !found; ++i)
			{
				IUIAutomationElement* element{};
				if (FAILED(buttons->GetElement(i, &element)) || !element)
					continue;

				BSTR name{};
				if (SUCCEEDED(element->get_CurrentName(&name)) && name)
				{
					std::wstring text(name, SysStringLen(name));
					if (text == L"Görünüm" || text == L"View" || text.rfind(L"Görünüm", 0) == 0 || text.rfind(L"View", 0) == 0)
					{
						tagRECT rect{};
						if (SUCCEEDED(element->get_CurrentBoundingRectangle(&rect)) && rect.right > rect.left)
						{
							result = { rect.left, rect.top, rect.right, rect.bottom };
							found = true;
						}
					}
					SysFreeString(name);
				}
				element->Release();
			}
		}

		if (buttons) buttons->Release();
		if (condition) condition->Release();
		root->Release();
		return found;
	}

	void ApplyRoundedCorners(HWND hwnd)
	{
		const DWM_WINDOW_CORNER_PREFERENCE preference = DWMWCP_ROUND;
		DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));
	}

	void PositionOverlay(OverlayState& state)
	{
		RECT explorerRect{};
		GetWindowRect(state.explorer, &explorerRect);
		RECT viewRect{};
		int x{};
		int y{};
		if (FindViewButton(state.explorer, viewRect))
		{
			x = viewRect.right + 6;
			y = viewRect.top + ((viewRect.bottom - viewRect.top) - kOverlayHeight) / 2;
		}
		else
		{
			x = explorerRect.right - 235;
			y = explorerRect.top + 116;
		}

		state.compact = (explorerRect.right - explorerRect.left) < 920;
		SetWindowPos(state.overlay, HWND_TOP, x, y, kOverlayWidth, kOverlayHeight,
			SWP_NOACTIVATE | SWP_SHOWWINDOW);
		if (IsWindowVisible(state.popup))
		{
			const int width = state.compact ? 390 : 560;
			const int rows = state.compact ? 3 : 2;
			SetWindowPos(state.popup, HWND_TOP, x - width + kOverlayWidth, y + kOverlayHeight + 6,
				width, rows * 38 + 16, SWP_NOACTIVATE | SWP_SHOWWINDOW);
			InvalidateRect(state.popup, nullptr, TRUE);
		}
	}

	void SendLetter(HWND explorer, wchar_t letter)
	{
		auto found = g_states.find(explorer);
		if (found == g_states.end())
			return;
		ShowWindow(found->second.popup, SW_HIDE);
		SetForegroundWindow(explorer);
		INPUT inputs[2]{};
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wScan = letter;
		inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
		inputs[1] = inputs[0];
		inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
		SendInput(2, inputs, sizeof(INPUT));
	}

	OverlayState* StateFromWindow(HWND hwnd)
	{
		return reinterpret_cast<OverlayState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
	}

	LRESULT CALLBACK OverlayProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* state = StateFromWindow(hwnd);
		switch (message)
		{
		case WM_NCCREATE:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
			return TRUE;
		case WM_MOUSEMOVE:
			if (state && !state->overlayHover)
			{
				state->overlayHover = true;
				TRACKMOUSEEVENT tracking{ sizeof(tracking), TME_LEAVE, hwnd, 0 };
				TrackMouseEvent(&tracking);
				InvalidateRect(hwnd, nullptr, TRUE);
			}
			return 0;
		case WM_MOUSELEAVE:
			if (state) state->overlayHover = false;
			InvalidateRect(hwnd, nullptr, TRUE);
			return 0;
		case WM_LBUTTONUP:
			if (state)
			{
				if (IsWindowVisible(state->popup))
					ShowWindow(state->popup, SW_HIDE);
				else
				{
					ShowWindow(state->popup, SW_SHOWNOACTIVATE);
					PositionOverlay(*state);
				}
			}
			return 0;
		case WM_PAINT:
		{
			PAINTSTRUCT ps{};
			HDC dc = BeginPaint(hwnd, &ps);
			RECT rect{};
			GetClientRect(hwnd, &rect);
			HBRUSH brush = CreateSolidBrush(state && state->overlayHover ? RGB(226, 226, 226) : RGB(243, 243, 243));
			FillRect(dc, &rect, brush);
			DeleteObject(brush);
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, RGB(32, 32, 32));
			SelectObject(dc, g_buttonFont);
			DrawTextW(dc, L"A-Z", -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	LRESULT CALLBACK PopupProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* state = StateFromWindow(hwnd);
		switch (message)
		{
		case WM_NCCREATE:
			SetWindowLongPtrW(hwnd, GWLP_USERDATA,
				reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
			return TRUE;
		case WM_MOUSEMOVE:
			if (state)
			{
				RECT rect{};
				GetClientRect(hwnd, &rect);
				const int count = static_cast<int>(wcslen(kLetters));
				const int columns = state->compact ? 11 : 16;
				const int cellWidth = (rect.right - 16) / columns;
				const int x = GET_X_LPARAM(lParam) - 8;
				const int y = GET_Y_LPARAM(lParam) - 8;
				const int index = x < 0 || y < 0 ? -1 : (y / 38) * columns + (x / cellWidth);
				const int newHover = index >= 0 && index < count ? index : -1;
				if (newHover != state->hoverLetter)
				{
					state->hoverLetter = newHover;
					InvalidateRect(hwnd, nullptr, TRUE);
				}
			}
			return 0;
		case WM_LBUTTONUP:
			if (state && state->hoverLetter >= 0)
				SendLetter(state->explorer, kLetters[state->hoverLetter]);
			return 0;
		case WM_PAINT:
		{
			PAINTSTRUCT ps{};
			HDC dc = BeginPaint(hwnd, &ps);
			RECT client{};
			GetClientRect(hwnd, &client);
			HBRUSH background = CreateSolidBrush(RGB(249, 249, 249));
			FillRect(dc, &client, background);
			DeleteObject(background);
			const int columns = state && state->compact ? 11 : 16;
			const int cellWidth = (client.right - 16) / columns;
			const int count = static_cast<int>(wcslen(kLetters));
			SetBkMode(dc, TRANSPARENT);
			SelectObject(dc, g_letterFont);
			for (int i = 0; i < count; ++i)
			{
				const int row = i / columns;
				const int column = i % columns;
				RECT cell{ 8 + column * cellWidth, 8 + row * 38, 8 + (column + 1) * cellWidth - 2, 8 + (row + 1) * 38 - 2 };
				if (state && state->hoverLetter == i)
				{
					HBRUSH hover = CreateSolidBrush(RGB(229, 241, 251));
					FillRect(dc, &cell, hover);
					DeleteObject(hover);
				}
				SetTextColor(dc, RGB(30, 30, 30));
				wchar_t text[2]{ kLetters[i], 0 };
				DrawTextW(dc, text, 1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
			}
			EndPaint(hwnd, &ps);
			return 0;
		}
		case WM_ERASEBKGND:
			return 1;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}

	void CreateOverlay(HWND explorer)
	{
		auto [it, inserted] = g_states.try_emplace(explorer);
		if (!inserted)
			return;

		auto& state = it->second;
		state.explorer = explorer;
		state.overlay = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			kOverlayClass, nullptr, WS_POPUP, 0, 0, kOverlayWidth, kOverlayHeight,
			explorer, nullptr, g_instance, &state);
		state.popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
			kPopupClass, nullptr, WS_POPUP, 0, 0, 560, 92,
			explorer, nullptr, g_instance, &state);
		SetLayeredWindowAttributes(state.popup, 0, 250, LWA_ALPHA);
		ApplyRoundedCorners(state.overlay);
		ApplyRoundedCorners(state.popup);
		PositionOverlay(state);
	}

	BOOL CALLBACK EnumExplorerProc(HWND hwnd, LPARAM)
	{
		if (IsExplorerWindow(hwnd))
			CreateOverlay(hwnd);
		return TRUE;
	}

	void RefreshOverlays()
	{
		for (auto it = g_states.begin(); it != g_states.end();)
		{
			if (!IsWindow(it->first) || !IsExplorerWindow(it->first))
			{
				DestroyWindow(it->second.popup);
				DestroyWindow(it->second.overlay);
				it = g_states.erase(it);
			}
			else
			{
				PositionOverlay(it->second);
				++it;
			}
		}
		EnumWindows(EnumExplorerProc, 0);
	}

	LRESULT CALLBACK ControllerProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_TIMER && wParam == kRefreshTimer)
		{
			RefreshOverlays();
			return 0;
		}
		if (message == WM_DESTROY)
		{
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(hwnd, message, wParam, lParam);
	}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	g_instance = instance;
	HANDLE mutex = CreateMutexW(nullptr, TRUE, L"ExplorerAlfabeCubugu.SingleInstance");
	if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS)
		return 0;

	CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_automation));
	g_buttonFont = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");
	g_letterFont = CreateFontW(-16, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI Variable Text");

	WNDCLASSEXW overlayClass{ sizeof(overlayClass) };
	overlayClass.hInstance = instance;
	overlayClass.lpfnWndProc = OverlayProc;
	overlayClass.hCursor = LoadCursor(nullptr, IDC_HAND);
	overlayClass.lpszClassName = kOverlayClass;
	RegisterClassExW(&overlayClass);

	WNDCLASSEXW popupClass{ sizeof(popupClass) };
	popupClass.hInstance = instance;
	popupClass.lpfnWndProc = PopupProc;
	popupClass.hCursor = LoadCursor(nullptr, IDC_HAND);
	popupClass.lpszClassName = kPopupClass;
	RegisterClassExW(&popupClass);

	WNDCLASSEXW controllerClass{ sizeof(controllerClass) };
	controllerClass.hInstance = instance;
	controllerClass.lpfnWndProc = ControllerProc;
	controllerClass.lpszClassName = L"ExplorerAlfabeController";
	RegisterClassExW(&controllerClass);
	HWND controller = CreateWindowExW(0, controllerClass.lpszClassName, nullptr, 0,
		0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
	SetTimer(controller, kRefreshTimer, 500, nullptr);
	RefreshOverlays();

	MSG message{};
	while (GetMessageW(&message, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&message);
		DispatchMessageW(&message);
	}

	for (auto& [_, state] : g_states)
	{
		DestroyWindow(state.popup);
		DestroyWindow(state.overlay);
	}
	if (g_automation) g_automation->Release();
	DeleteObject(g_buttonFont);
	DeleteObject(g_letterFont);
	CoUninitialize();
	ReleaseMutex(mutex);
	CloseHandle(mutex);
	return 0;
}
