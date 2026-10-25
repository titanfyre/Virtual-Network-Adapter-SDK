#include "../SDK/NetworkAdapter.h"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <mutex>
#include <memory>
#include <sstream>
#include <iomanip>

namespace {
constexpr COLORREF Background=RGB(15,21,32), Panel=RGB(25,35,51), Blue=RGB(40,110,245), White=RGB(235,242,255);
HWND Window{},Key{},Authorize{},Disconnect{},Status{},Stats{},Hex{},Send{},Log{};
HFONT Font{},Title{};HBRUSH Brush{};std::thread Worker;std::mutex Gate;NetworkAdapter Adapter;bool Busy=false,Preview=false,WasConnected=false;
std::wstring Wide(const std::string& text) { return std::wstring(text.begin(),text.end()); }
void Message(const std::wstring& text) { SetWindowTextW(Status,text.c_str()); }
HWND Child(const wchar_t* kind,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id) {
    auto child=CreateWindowExW(0,kind,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,Window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(Font),TRUE);return child;
}
void Start() {
    if(Busy)return;if(Worker.joinable())Worker.join();
    char raw[256]{};GetWindowTextA(Key,raw,256);std::string key=raw;SecureZeroMemory(raw,sizeof(raw));SetWindowTextW(Key,L"");
    Busy=true;EnableWindow(Authorize,FALSE);Message(L"Requesting a signed session permission over HTTPS...");
    try { Worker=std::thread([key=std::move(key)]() mutable {
        auto result=std::make_unique<std::string>();
        try { std::lock_guard<std::mutex> lock(Gate);*result=Adapter.Authorize(key);if(result->empty())if(auto error=Adapter.Connect())*result=NetworkAdapter::Error(error); }
        catch(...) { *result="Authorization could not complete due to a local resource error."; }
        SecureZeroMemory(key.data(),key.size());
        if(PostMessageW(Window,WM_APP+1,0,reinterpret_cast<LPARAM>(result.get())))result.release();
    }); } catch(...) { SecureZeroMemory(key.data(),key.size());Busy=false;EnableWindow(Authorize,TRUE);Message(L"Could not start authorization worker."); }
}
void Refresh() {
    if(Preview)return;
    std::unique_lock<std::mutex> lock(Gate,std::try_to_lock);if(!lock.owns_lock())return;
    VN_STATE state{};auto error=Adapter.Query(state);
    if(error) { if(WasConnected){Message(L"The driver endpoint became unavailable. Reauthorize and reconnect when it is ready.");WasConnected=false;}SetWindowTextW(Stats,(L"Driver unavailable: "+Wide(NetworkAdapter::Error(error))+L"\r\nInstall the signed evaluation package and reopen as administrator.").c_str());return; }
    if(WasConnected&&!state.connected)Message(L"Link disconnected. Authorize and connect again when ready.");WasConnected=state.connected!=0;
    std::wstring text=std::wstring(state.connected?L"CONNECTED":L"DISCONNECTED")+L"    |    "+(state.running?L"NDIS running":L"NDIS paused")+L"\r\n\r\nWindows -> SDK: "+std::to_wstring(state.transmitted)+L" frames       SDK -> Windows: "+std::to_wstring(state.received)+L" frames\r\nQueue: "+std::to_wstring(state.queued)+L" / 64       Dropped: "+std::to_wstring(state.dropped);
    SetWindowTextW(Stats,text.c_str());
    VN_FRAME frame{};if(Adapter.Receive(frame)==ERROR_SUCCESS) {
        std::wostringstream out;out<<L"Windows -> SDK, "<<frame.length<<L" bytes\r\n"<<std::hex<<std::setfill(L'0');
        for(ULONG i=0;i<frame.length&&i<64;++i)out<<std::setw(2)<<unsigned(frame.data[i])<<L' ';
        if(frame.length>64)out<<L"...";SetWindowTextW(Log,out.str().c_str());
    }
}
LRESULT CALLBACK Proc(HWND h,UINT m,WPARAM w,LPARAM l) {
    switch(m) {
    case WM_CREATE:
        Window=h;Font=CreateFontW(18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        Title=CreateFontW(28,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");Brush=CreateSolidBrush(Panel);
        {auto title=Child(L"STATIC",L"Virtual Network Adapter SDK",0,28,24,700,40,0);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(Title),TRUE);}
        Child(L"STATIC",L"An explicit Ethernet endpoint for your application. No physical adapter capture.",0,28,72,760,26,0);
        Child(L"STATIC",L"Application access key",0,28,98,460,20,0);
        Key=Child(L"EDIT",L"",WS_BORDER|ES_PASSWORD|ES_AUTOHSCROLL|WS_TABSTOP,28,118,460,36,10);
        SendMessageW(Key,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Application access key"));
        SendMessageW(Key,EM_SETLIMITTEXT,128,0);
        Authorize=Child(L"BUTTON",L"Authorize & connect",BS_OWNERDRAW|WS_TABSTOP,504,118,208,36,1);
        Disconnect=Child(L"BUTTON",L"Disconnect",BS_OWNERDRAW|WS_TABSTOP,724,118,126,36,2);
        Status=Child(L"STATIC",L"The link starts disconnected. Authorization lasts up to one hour; renew explicitly.",0,28,174,822,48,0);
        Stats=Child(L"STATIC",L"Reading adapter state...",0,28,240,822,104,0);
        Child(L"STATIC",L"Inject Ethernet bytes into this virtual adapter only (14-1514 bytes, hex):",0,28,370,822,26,0);
        Hex=Child(L"EDIT",L"02 54 49 54 41 01 02 54 49 54 41 02 88 B5",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,28,410,660,36,11);
        SendMessageW(Hex,EM_SETLIMITTEXT,VN_MAX_FRAME*3,0);
        Send=Child(L"BUTTON",L"Send frame",BS_OWNERDRAW|WS_TABSTOP,706,410,144,36,3);
        Log=Child(L"STATIC",L"Frame preview appears here after Windows transmits on the virtual link.",0,28,476,822,90,0);
        Child(L"STATIC",L"OEM integration, deployment and support: Discord titancsa",0,28,590,822,26,0);
        if(Preview){SetWindowTextW(Status,L"Offline UI preview - no driver loaded and no network access.");SetWindowTextW(Stats,L"DISCONNECTED    |    SDK / application preview\r\n\r\nWindows -> SDK: --       SDK -> Windows: --\r\n64-frame bounded queue   |   1500-byte MTU   |   Explicit ownership");EnableWindow(Authorize,FALSE);EnableWindow(Disconnect,FALSE);EnableWindow(Send,FALSE);}else{Adapter.Open();SetTimer(h,1,200,nullptr);}return 0;
    case WM_TIMER:Refresh();return 0;
    case WM_COMMAND:if(HIWORD(w)==BN_CLICKED) {
        if(LOWORD(w)==1)Start();
        else if(LOWORD(w)==2&&!Busy){std::lock_guard<std::mutex> lock(Gate);auto e=Adapter.Disconnect();if(!e)WasConnected=false;Message(e?Wide(NetworkAdapter::Error(e)):L"Disconnected. Windows network configuration was left unchanged.");}
        else if(LOWORD(w)==3&&!Busy){char text[VN_MAX_FRAME*3+1]{};GetWindowTextA(Hex,text,sizeof(text));std::vector<unsigned char> frame;if(!ParseEthernetFrame(text,frame)){Message(L"Enter 14-1514 complete hexadecimal bytes.");return 0;}std::lock_guard<std::mutex> lock(Gate);auto e=Adapter.Send(frame);Message(e?Wide(NetworkAdapter::Error(e)):L"Frame accepted by the virtual adapter.");}
        }return 0;
    case WM_APP+1:{std::unique_ptr<std::string> result(reinterpret_cast<std::string*>(l));if(Worker.joinable())Worker.join();Busy=false;WasConnected=result->empty();EnableWindow(Authorize,TRUE);Message(result->empty()?L"Authorized and connected. Closing the application disconnects the link.":Wide(*result));return 0;}
    case WM_CTLCOLORSTATIC:case WM_CTLCOLOREDIT:SetTextColor(reinterpret_cast<HDC>(w),White);SetBkColor(reinterpret_cast<HDC>(w),Panel);return reinterpret_cast<LRESULT>(Brush);
    case WM_DRAWITEM:{auto d=reinterpret_cast<DRAWITEMSTRUCT*>(l);HBRUSH b=CreateSolidBrush((d->itemState&ODS_DISABLED)?Panel:Blue);FillRect(d->hDC,&d->rcItem,b);DeleteObject(b);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,White);SelectObject(d->hDC,Font);wchar_t text[128]{};GetWindowTextW(d->hwndItem,text,128);DrawTextW(d->hDC,text,-1,&d->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);if(d->itemState&ODS_FOCUS)DrawFocusRect(d->hDC,&d->rcItem);return TRUE;}
    case WM_SETCURSOR:if(reinterpret_cast<HWND>(w)==Authorize||reinterpret_cast<HWND>(w)==Disconnect||reinterpret_cast<HWND>(w)==Send){SetCursor(LoadCursorW(nullptr,IDC_HAND));return TRUE;}break;
    case WM_CLOSE:if(Busy){Message(L"Wait for the authorization request to finish before closing.");return 0;}DestroyWindow(h);return 0;
    case WM_DESTROY:KillTimer(h,1);if(Worker.joinable())Worker.join();Adapter.Close();DeleteObject(Font);DeleteObject(Title);DeleteObject(Brush);PostQuitMessage(0);return 0;
    }return DefWindowProcW(h,m,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR command,int show) {
    Preview=command&&wcsstr(command,L"--preview")!=nullptr;
#ifdef TITAN_OFFLINE_PREVIEW
    Preview=true;
#endif
    SetProcessDPIAware();WNDCLASSW c{};c.lpfnWndProc=Proc;c.hInstance=instance;c.lpszClassName=L"TitanVirtualNetworkSDK";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=CreateSolidBrush(Background);RegisterClassW(&c);
    auto h=CreateWindowW(c.lpszClassName,L"Virtual Network Adapter SDK",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,900,680,nullptr,nullptr,instance,nullptr);if(!h)return 1;ShowWindow(h,show);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(h,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}DeleteObject(c.hbrBackground);return 0;
}
