#include "pch.h"
#include "resource.h"
#include "ColorConversions.h"

#define MAX_LOADSTRING 100

void VerifyHR(HRESULT hr)
{
	assert(SUCCEEDED(hr));
}

// Global Variables:
HINSTANCE hInst;						// current instance
HWND hMainWnd;							// current window
TCHAR szTitle[MAX_LOADSTRING];			// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];	// The window class name

ComPtr<IDirectDraw> g_lpDD;
ComPtr<IDirectDrawSurface> g_lpPrimary; 
ComPtr<IDirectDrawSurface> g_lpBack;
ComPtr<IDirectDrawClipper> g_lpClipper;

ComPtr<IWICImagingFactory> g_wicImagingFactory;

bool graphicsLoaded{};
bool isRunning = true;

// Constants
const float c_waterAlpha = 0.6f;
Color3F c_waterColor = { 0.0f, 0.176f, 0.357f };
const float c_dampeningFactor = 0.995f;

struct LoadedImage
{
	UINT Width, Height;
	std::vector<UINT> ImageData;
} g_foregroundImage, g_underwaterMask;

std::vector<float> g_heightMaps[2]{};
UINT g_currentHeightmapIndex = 0;

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
BOOL				ExitInstance();
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
void				DrawImpl(LPDIRECTDRAWSURFACE lpSurface);

void EnsureWicImagingFactory()
{
	if (g_wicImagingFactory)
		return;

	VerifyHR(CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID*)&g_wicImagingFactory));
}

LoadedImage LoadImageFile(std::wstring fileName)
{
	EnsureWicImagingFactory();

	LoadedImage l{};

	ComPtr<IWICBitmapDecoder> spDecoder;
	VerifyHR(g_wicImagingFactory->CreateDecoderFromFilename(
		fileName.c_str(),
		NULL,
		GENERIC_READ,
		WICDecodeMetadataCacheOnLoad, &spDecoder));

	ComPtr<IWICBitmapFrameDecode> spSource;
	VerifyHR(spDecoder->GetFrame(0, &spSource));

	// Convert the image format to 32bppPBGRA, equiv to DXGI_FORMAT_B8G8R8A8_UNORM
	ComPtr<IWICFormatConverter> spConverter;
	VerifyHR(g_wicImagingFactory->CreateFormatConverter(&spConverter));

	VerifyHR(spConverter->Initialize(
		spSource.Get(),
		GUID_WICPixelFormat32bppPBGRA,
		WICBitmapDitherTypeNone,
		NULL,
		0.f,
		WICBitmapPaletteTypeMedianCut));

	VerifyHR(spConverter->GetSize(&l.Width, &l.Height));

	l.ImageData.resize(l.Width * l.Height);
	VerifyHR(spConverter->CopyPixels(
		NULL,
		l.Width * sizeof(UINT),
		static_cast<UINT>(l.ImageData.size()) * sizeof(UINT),
		(BYTE*)&l.ImageData[0]));

	return l;
}

void DirectDrawInit()
{
	HRESULT hr = 0;

	// create the DirectDraw object
	VerifyHR(DirectDrawCreate(NULL, &g_lpDD, NULL));

	// set the cooperative level to windowed mode
	VerifyHR(g_lpDD->SetCooperativeLevel(hMainWnd, DDSCL_NORMAL));

	// Primary
	{
		DDSURFACEDESC ddsd{};
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS;
		ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;

		// create the primary buffer
		VerifyHR(g_lpDD->CreateSurface(&ddsd, &g_lpPrimary, NULL));
	}

	// Back buffer
	{
		DDSURFACEDESC ddsd{};
		ddsd.dwSize = sizeof(ddsd);
		ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
		ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
		ddsd.dwWidth = 640;
		ddsd.dwHeight = 448;

		// create the back buffer
		VerifyHR(g_lpDD->CreateSurface(&ddsd, &g_lpBack, NULL));
	}

	VerifyHR(g_lpDD->CreateClipper(0, &g_lpClipper, NULL));

	VerifyHR(g_lpClipper->SetHWnd(0, hMainWnd));

	VerifyHR(g_lpPrimary->SetClipper(g_lpClipper.Get()));

	g_foregroundImage = LoadImageFile(L"fg.png");
	g_underwaterMask = LoadImageFile(L"underwater.png");
	g_wicImagingFactory.Reset();

	for (int i = 0; i < 2; ++i)
	{
		g_heightMaps[i].resize(640 * 448);
	}
}

void Draw()
{
	if (!graphicsLoaded)
		return;

	// clear the back buffer before drawing
	{
		DDBLTFX ddbltfx{};
		ddbltfx.dwSize = sizeof(ddbltfx);
		ddbltfx.dwFillColor = RGB(127, 0, 0);
		g_lpBack->Blt(NULL, NULL, NULL, DDBLT_COLORFILL, &ddbltfx);
	}

	// Draw to back buffer
	{
		DrawImpl(g_lpBack.Get());
	}

	// Copy from back buffer to primary
	{
		RECT rect{};
		GetClientRect(hMainWnd, &rect);

		// copy the rect's data into two points
		POINT p1;
		POINT p2;

		p1.x = rect.left;
		p1.y = rect.top;
		p2.x = rect.right;
		p2.y = rect.bottom;

		// convert it to screen coordinates (like DirectDraw uses)
		ClientToScreen(hMainWnd, &p1);
		ClientToScreen(hMainWnd, &p2);

		// copy the two points' data back into the rect
		rect.left = p1.x;
		rect.top = p1.y;
		rect.right = p2.x;
		rect.bottom = p2.y;

		// blit the back buffer to our window's position
		HRESULT hr = g_lpPrimary->Blt(&rect, g_lpBack.Get(), NULL, DDBLT_WAIT, NULL);
		assert(SUCCEEDED(hr));
	}
}

float SanitizedLoadFromHeightmap(std::vector<float> const& heightMap, int x, int y)
{
	if (x < 0) return 0;
	if (x >= 640) return 0;
	if (y < 0) return 0;
	if (y >= 448) return 0;

	// Don't load heightmap from where it doesn't apply
	if (g_underwaterMask.ImageData[y * 640 + x] == 0) return 0;

	return heightMap[y * 640 + x];
}

UINT SanitizedLoadFromRgbUINT(std::vector<UINT> const& rgb, int x, int y)
{
	if (x < 0) x = 0;
	if (x >= 640) x = 640 - 1;
	if (y < 0) y = 0;
	if (y >= 448) y = 448 - 1;

	return rgb[y * 640 + x];
}

float ComputeHeightmapValue(int x, int y)
{
	std::vector<float>& currentHeightmap = g_heightMaps[g_currentHeightmapIndex];
	std::vector<float>& otherHeightmap = g_heightMaps[1 - g_currentHeightmapIndex];

	float neighbor0 = SanitizedLoadFromHeightmap(otherHeightmap, x - 1, y);
	float neighbor1 = SanitizedLoadFromHeightmap(otherHeightmap, x + 1, y);
	float neighbor2 = SanitizedLoadFromHeightmap(otherHeightmap, x, y - 1);
	float neighbor3 = SanitizedLoadFromHeightmap(otherHeightmap, x, y + 1);
	float neighborSum = neighbor0 + neighbor1 + neighbor2 + neighbor3;

	float& thisPixelInHeightmap = currentHeightmap[640 * y + x];
	float previousVal = thisPixelInHeightmap;

	float newVal = (neighborSum / 2.0f - previousVal) * c_dampeningFactor;
	thisPixelInHeightmap = newVal;
	return thisPixelInHeightmap;
}

void DrawImpl(LPDIRECTDRAWSURFACE lpSurface)
{
	HRESULT hr = 0;

	// Get the bit-depth of the surface, so we draw in different
	// bit-depths correctly
	DDPIXELFORMAT ddpf{};
	ddpf.dwSize = sizeof( ddpf );

	VerifyHR(lpSurface->GetPixelFormat(&ddpf));

	// Get the surface's description and lock it.
	DDSURFACEDESC ddsd{};
	ddsd.dwSize = sizeof( ddsd );

	VerifyHR(lpSurface->Lock(NULL, &ddsd, DDLOCK_SURFACEMEMORYPTR | DDLOCK_WAIT, NULL));
	{
		int x = 0;
		int y = 0;

		const int nBytesPerPixel = ddpf.dwRGBBitCount / 8;
		unsigned char* pVideoMemory = (unsigned char*) ddsd.lpSurface;


		for (int x = 0; x < 640; ++x)
		{
			for (int y = 0; y < 448; ++y)
			{
				// Copy from foreground image
				UINT fgImageRgb = g_foregroundImage.ImageData[y * 640 + x];
				UINT underwaterRgb = g_underwaterMask.ImageData[y * 640 + x];

				unsigned char* pPixelMemory = pVideoMemory + (x * nBytesPerPixel) + (y * ddsd.lPitch);
				UINT* pPixel = reinterpret_cast<UINT*>(pPixelMemory);

				if (fgImageRgb && !underwaterRgb)
				{
					// Just foreground
					*pPixel = fgImageRgb;
				}
				else if (fgImageRgb && underwaterRgb)
				{
					// Foreground and water

					// Update and load from heightmap
					float disp = ComputeHeightmapValue(x, y);
					disp /= 2.0f;
					disp = max(disp, -4);
					disp = min(disp, 4);

					// Resample with heightmap applied
					float adjustedX = x;
					float adjustedY = y + disp;
					UINT resample = SanitizedLoadFromRgbUINT(g_foregroundImage.ImageData, adjustedX, adjustedY);

					// Blend resampled fg with a blue color
					Color3U fgImageComponents = RgbUINTToColor3U(resample);
					Color3F fgImageFloat = Color3UToColor3F(fgImageComponents);
					fgImageFloat.Blend(c_waterColor, c_waterAlpha);

					fgImageComponents = Color3FToColor3U(fgImageFloat);
					UINT resultRGBA = Color3UToRgbUINT(fgImageComponents);

					*pPixel = resultRGBA;
				}
				else if (underwaterRgb)
				{
					// Just water

					// Update and load from heightmap
					float heightmap = ComputeHeightmapValue(x, y);
					float tint = heightmap;

					// tint goes from [-10, 10]
					tint = max(tint, -10);
					tint = min(tint, 10);

					// tint goes from [0, 20]
					tint += 10.0f;

					// tint goes from [0, 1]
					tint /= 20.0f;

					// If positive heightmap, add white. If negative, add black.
					Color3F tintColor = { tint, tint, tint };

					// Load the blue color
					Color3F water = c_waterColor;
					water.Blend(tintColor, 0.05f);

					Color3U components = Color3FToColor3U(water);
					UINT resultRGBA = Color3UToRgbUINT(components);
					*pPixel = resultRGBA;

				}
			}
		}

		g_currentHeightmapIndex = 1 - g_currentHeightmapIndex;
	}
	VerifyHR(lpSurface->Unlock(NULL));
}

//
//   FUNCTION: WinMain()
//
//   PURPOSE: The virtual entry point for the app.
//
//   COMMENTS:
//
//        Handles startup, running, and shutdown.
//
int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR     lpCmdLine,
                     int       nCmdShow)
{
 	// TODO: Place code here.
	HACCEL hAccelTable;


	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_DDWATER, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance(hInstance, nCmdShow))
	{
		assert(false);
	}

	hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_DDWATER);

	MSG msg{};

	LARGE_INTEGER qpf;
	QueryPerformanceFrequency(&qpf);
	unsigned long int ticksPerFrame = qpf.QuadPart / 120;
	unsigned long int ticksLastFrame = 0;
	unsigned long int target = ticksLastFrame + ticksPerFrame;

	// Main message loop:
	while (isRunning)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		LARGE_INTEGER qpc{};
		QueryPerformanceCounter(&qpc);
		if (qpc.QuadPart >= target)
		{
			Draw();
			target = qpc.QuadPart + ticksPerFrame;
		}
	}

	// Perform application cleanup
	g_lpBack.Reset();

	if (g_lpPrimary)
	{
		// release the clipper (indirectly)
		g_lpPrimary->SetClipper(NULL);
		g_lpClipper.Reset();
		g_lpPrimary.Reset();
	}
	g_lpDD.Reset();

	return msg.wParam;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage is only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX); 

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= (WNDPROC)WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_DDWATER);
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH) GetStockObject(BLACK_BRUSH);
	wcex.lpszMenuName	= (LPCWSTR)IDC_DDWATER;
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL);

	return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HANDLE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   RECT windowRect{};
   windowRect.right = 640;
   windowRect.bottom = 448;
   AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

   hMainWnd = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, NULL, NULL, hInstance, NULL);

   if (!hMainWnd)
   {
	   return FALSE;
   }

   ShowWindow(hMainWnd, nCmdShow);
   UpdateWindow(hMainWnd);

   // Create DirectDraw and its objects
   DirectDrawInit();
   graphicsLoaded = true;

   return TRUE;
}

//
//   FUNCTION: ExitInstance()
//
//   PURPOSE: Cleans up the app.
//
//   COMMENTS:
//
//        This function destroys DirectDraw and its objects.
//
BOOL ExitInstance()
{
	// Destroy DirectDraw and its objects

	return TRUE;
}

//
//  FUNCTION: WndProc(HWND, unsigned, WORD, LONG)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;
	PAINTSTRUCT ps;
	HDC hdc;

	switch (message) 
	{
		case WM_COMMAND:
			wmId    = LOWORD(wParam); 
			wmEvent = HIWORD(wParam); 
			// Parse the menu selections:
			switch (wmId)
			{
				case IDM_ABOUT:
				   DialogBox(hInst, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
				   break;
				default:
				   return DefWindowProc(hWnd, message, wParam, lParam);
			}
			break;
		case WM_KEYUP:
			switch( wParam )
			{
			case VK_ESCAPE: // end the program
				PostQuitMessage(0);
				isRunning = false;
				break;
			}
			break;
		case WM_PAINT:
			hdc = BeginPaint(hWnd, &ps);
			// TODO: Add any drawing code here...
			EndPaint(hWnd, &ps);
			break;
		case WM_LBUTTONDOWN:
		{
			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			if (pt.x < 0) break;
			if (pt.x >= 640) break;
			if (pt.y < 0) break;
			if (pt.y >= 448) break;

			g_heightMaps[g_currentHeightmapIndex][pt.y * 640 + pt.x] = 1000;

			break;
		}
		case WM_ACTIVATE:
			if( LOWORD( wParam ) == WA_INACTIVE )
			{
				// the user is now working with another app
				isRunning = false;
			}
			else
			{
				// the user has now switched back to our app
				isRunning = true;
			}
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
			isRunning = false;
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

// Mesage handler for about box.
LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_INITDIALOG:
				return TRUE;

		case WM_COMMAND:
			if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
			{
				EndDialog(hDlg, LOWORD(wParam));
				return TRUE;
			}
			break;
	}
    return FALSE;
}
