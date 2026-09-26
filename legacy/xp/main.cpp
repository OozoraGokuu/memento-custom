#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <vlc/vlc.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "core.h"

namespace {
enum { OpenMedia=101, OpenSubtitles, CopySubtitle, Library, Audio, PlayPause, Seek, LibraryList };
HWND mainWindow, videoWindow, subtitleWindow, statusWindow, audioBox, seekBar;
HWND libraryWindow, libraryList;
HFONT font;
HBRUSH background;
libvlc_instance_t *engine;
libvlc_media_player_t *player;
std::vector<xp::Cue> cues;
std::vector<std::wstring> library;
std::vector<int> audioIds;
std::wstring configPath, libraryPath, currentPath;
std::string currentText;
int preferredAudio=0;
bool audioPreferenceApplied=false;

std::wstring wide(const std::string &s) {
    if (s.empty()) return {};
    const int size=MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!size) return {};
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), &result[0], size);
    return result;
}
std::string utf8(const std::wstring &s) {
    if (s.empty()) return {};
    const int size=WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &result[0], size, nullptr, nullptr);
    return result;
}
std::string readFile(const std::wstring &path) {
    FILE *file=_wfopen(path.c_str(), L"rb");
    if (!file) return {};
    std::string result; char buffer[8192]; size_t count;
    while ((count=std::fread(buffer,1,sizeof(buffer),file))) {
        result.append(buffer,count);
        if (result.size()>16*1024*1024) { result.clear(); break; }
    }
    std::fclose(file); return result;
}
bool writeFile(const std::wstring &path, const std::string &text) {
    FILE *file=_wfopen(path.c_str(),L"wb");
    if (!file) return false;
    const bool written=std::fwrite(text.data(),1,text.size(),file)==text.size();
    return std::fclose(file)==0 && written;
}
bool clipboard(const std::wstring &text, HWND owner) {
    if (text.empty() || !OpenClipboard(owner)) return false;
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
    if (!memory) { CloseClipboard(); return false; }
    void *target=GlobalLock(memory);
    if (!target) { GlobalFree(memory); CloseClipboard(); return false; }
    std::memcpy(target,text.c_str(),(text.size()+1)*sizeof(wchar_t));
    GlobalUnlock(memory);
    const bool ok=EmptyClipboard() && SetClipboardData(CF_UNICODETEXT,memory);
    if (!ok) GlobalFree(memory);
    CloseClipboard(); return ok;
}
void status(const wchar_t *text) { SetWindowTextW(statusWindow,text); }
std::wstring baseName(const std::wstring &path) {
    const auto pos=path.find_last_of(L"/\\");
    return pos==std::wstring::npos ? path : path.substr(pos+1);
}
void loadSettings() {
    wchar_t path[MAX_PATH]={};
    if (FAILED(SHGetFolderPathW(nullptr,CSIDL_APPDATA|CSIDL_FLAG_CREATE,nullptr,0,path)))
        GetTempPathW(MAX_PATH,path);
    std::wstring folder=std::wstring(path)+L"\\MementoXPPrototype";
    CreateDirectoryW(folder.c_str(),nullptr);
    configPath=folder+L"\\settings.ini"; libraryPath=folder+L"\\library.txt";
    preferredAudio=static_cast<int>(GetPrivateProfileIntW(L"Library",L"AudioOrdinal",0,configPath.c_str()));
    std::istringstream input(readFile(libraryPath)); std::string line;
    while (std::getline(input,line)) {
        auto pathValue=wide(xp::trim(line));
        if (!pathValue.empty()) library.push_back(pathValue);
    }
}
void saveAudio(int ordinal) {
    preferredAudio=ordinal;
    if (!WritePrivateProfileStringW(L"Library",L"AudioOrdinal",std::to_wstring(ordinal).c_str(),configPath.c_str()))
        status(L"Could not save the audio preference.");
}
void refreshLibrary() {
    if (!libraryList) return;
    SendMessageW(libraryList,LB_RESETCONTENT,0,0);
    for (const auto &path:library) SendMessageW(libraryList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(path.c_str()));
}
bool loadSubtitles(const std::wstring &path, bool explicitChoice) {
    auto parsed=xp::parseSrt(readFile(path));
    if (parsed.empty()) {
        if (explicitChoice) MessageBoxW(mainWindow,L"No valid UTF-8 SRT cues were found.",L"Subtitles",MB_OK|MB_ICONINFORMATION);
        return false;
    }
    cues=std::move(parsed); currentText.clear();
    SetWindowTextW(subtitleWindow,L"");
    status(L"SRT loaded. Ctrl+C copies the entire active subtitle.");
    return true;
}
void openMedia(std::wstring path) {
    auto media=libvlc_media_new_path(engine,utf8(path).c_str());
    if (!media) { status(L"Unable to open this media path."); return; }
    libvlc_media_player_stop(player);
    libvlc_media_player_set_media(player,media);
    libvlc_media_release(media);
    currentPath=path; cues.clear(); currentText.clear(); audioIds.clear(); audioPreferenceApplied=false;
    SendMessageW(audioBox,CB_RESETCONTENT,0,0);
    SendMessageW(audioBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Audio: loading…"));
    SendMessageW(audioBox,CB_SETCURSEL,0,0);
    SetWindowTextW(subtitleWindow,L"");
    std::wstring title=L"Memento XP prototype — "+baseName(path);
    SetWindowTextW(mainWindow,title.c_str());
    auto dot=path.find_last_of(L'.');
    if (dot!=std::wstring::npos) loadSubtitles(path.substr(0,dot)+L".srt",false);
    if (libvlc_media_player_play(player)!=0) { status(L"Playback could not start."); return; }
    library.erase(std::remove(library.begin(),library.end(),path),library.end());
    library.insert(library.begin(),path); if(library.size()>200) library.resize(200);
    std::string saved;
    for (const auto &entry:library) saved+=utf8(entry)+"\n";
    if (!writeFile(libraryPath,saved)) status(L"Playback started; could not save library history.");
    else status(L"Experimental XP build • Ctrl+O media • Ctrl+L library • Ctrl+C subtitle");
    refreshLibrary();
}
void chooseFile(bool subtitles) {
    wchar_t file[32768]={};
    OPENFILENAMEW dialog={}; dialog.lStructSize=sizeof(dialog); dialog.hwndOwner=mainWindow;
    dialog.lpstrFile=file; dialog.nMaxFile=32768;
    dialog.lpstrFilter=subtitles ? L"UTF-8 SRT subtitles\0*.srt\0\0" : L"Media files\0*.mkv;*.mp4;*.avi;*.webm;*.mp3;*.wav;*.m4v;*.mov\0All files\0*.*\0\0";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog)) { if(subtitles) loadSubtitles(file,true); else openMedia(file); }
}
LRESULT CALLBACK LibraryProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_CREATE) {
        libraryList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,
            12,12,600,400,hwnd,reinterpret_cast<HMENU>(LibraryList),nullptr,nullptr);
        SendMessageW(libraryList,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        refreshLibrary(); return 0;
    }
    if(message==WM_SIZE) { MoveWindow(libraryList,12,12,LOWORD(l)-24,HIWORD(l)-24,TRUE); return 0; }
    if(message==WM_COMMAND && HIWORD(w)==LBN_DBLCLK) {
        auto index=SendMessageW(libraryList,LB_GETCURSEL,0,0);
        if(index>=0 && static_cast<size_t>(index)<library.size()) openMedia(library[index]);
        return 0;
    }
    if(message==WM_CLOSE) { ShowWindow(hwnd,SW_HIDE); return 0; }
    return DefWindowProcW(hwnd,message,w,l);
}
void showLibrary() {
    if(!libraryWindow) libraryWindow=CreateWindowExW(0,L"MementoXPLibrary",L"Memento XP — media library (double-click to play)",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,760,460,mainWindow,nullptr,GetModuleHandleW(nullptr),nullptr);
    ShowWindow(libraryWindow,SW_SHOW); SetForegroundWindow(libraryWindow);
}
void updateAudio() {
    auto *tracks=libvlc_audio_get_track_description(player);
    std::vector<int> ids; std::vector<std::wstring> labels;
    for(auto *track=tracks;track;track=track->p_next) if(track->i_id>=0) {
        ids.push_back(track->i_id);
        labels.push_back(L"Audio "+std::to_wstring(ids.size())+L": "+wide(track->psz_name?track->psz_name:""));
    }
    libvlc_track_description_list_release(tracks);
    if(ids.empty()) return;
    if(ids!=audioIds) {
        audioIds=ids; SendMessageW(audioBox,CB_RESETCONTENT,0,0);
        for(const auto &label:labels) SendMessageW(audioBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    }
    if(!audioPreferenceApplied) {
        const int target=xp::trackAtOrdinal(audioIds,preferredAudio);
        if(target>=0) audioPreferenceApplied=libvlc_audio_set_track(player,target)==0;
        else if(preferredAudio==0) audioPreferenceApplied=true;
    }
    const int active=libvlc_audio_get_track(player);
    for(size_t i=0;i<audioIds.size();++i) if(audioIds[i]==active) SendMessageW(audioBox,CB_SETCURSEL,i,0);
}
HWND child(const wchar_t *type,const wchar_t *text,DWORD style,int id) {
    HWND window=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,0,0,0,0,mainWindow,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
    SendMessageW(window,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return window;
}
LRESULT CALLBACK WindowProc(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_CREATE:
        mainWindow=hwnd;
        child(L"BUTTON",L"Open media",BS_PUSHBUTTON,OpenMedia);
        child(L"BUTTON",L"Load SRT",BS_PUSHBUTTON,OpenSubtitles);
        child(L"BUTTON",L"Library",BS_PUSHBUTTON,Library);
        child(L"BUTTON",L"Play / pause",BS_PUSHBUTTON,PlayPause);
        audioBox=child(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL,Audio);
        videoWindow=child(L"STATIC",L"",SS_BLACKRECT,0);
        subtitleWindow=child(L"STATIC",L"Open media and a matching UTF-8 SRT file.",SS_CENTER,0);
        statusWindow=child(L"STATIC",L"Experimental port — dictionary and online services are not available yet.",SS_LEFT,0);
        seekBar=child(TRACKBAR_CLASSW,L"",TBS_HORZ|TBS_NOTICKS,Seek);
        SendMessageW(seekBar,TBM_SETRANGE,TRUE,MAKELPARAM(0,1000));
        libvlc_media_player_set_hwnd(player,videoWindow); SetTimer(hwnd,1,100,nullptr); return 0;
    case WM_SIZE: {
        int width=LOWORD(l),height=HIWORD(l);
        MoveWindow(GetDlgItem(hwnd,OpenMedia),12,10,98,28,TRUE);
        MoveWindow(GetDlgItem(hwnd,OpenSubtitles),118,10,90,28,TRUE);
        MoveWindow(GetDlgItem(hwnd,Library),216,10,76,28,TRUE);
        MoveWindow(GetDlgItem(hwnd,PlayPause),300,10,100,28,TRUE);
        MoveWindow(audioBox,412,10,std::max(120,width-424),200,TRUE);
        MoveWindow(videoWindow,12,48,std::max(1,width-24),std::max(1,height-182),TRUE);
        MoveWindow(seekBar,12,std::max(50,height-130),width-24,26,TRUE);
        MoveWindow(subtitleWindow,18,std::max(80,height-98),width-36,60,TRUE);
        MoveWindow(statusWindow,12,std::max(100,height-28),width-24,24,TRUE); return 0;
    }
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize={700,400}; return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor(reinterpret_cast<HDC>(w),RGB(235,235,240));
        SetBkColor(reinterpret_cast<HDC>(w),RGB(25,25,31)); return reinterpret_cast<LRESULT>(background);
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(l)==seekBar && (LOWORD(w)==TB_ENDTRACK || LOWORD(w)==TB_THUMBPOSITION))
            libvlc_media_player_set_position(player,SendMessageW(seekBar,TBM_GETPOS,0,0)/1000.0f);
        return 0;
    case WM_TIMER: {
        const auto text=xp::activeText(cues,libvlc_media_player_get_time(player));
        if(text!=currentText) { currentText=text; SetWindowTextW(subtitleWindow,wide(text).c_str()); }
        if(!(GetKeyState(VK_LBUTTON)&0x8000)) SendMessageW(seekBar,TBM_SETPOS,TRUE,static_cast<LPARAM>(libvlc_media_player_get_position(player)*1000));
        updateAudio();
        if(libvlc_media_player_get_state(player)==libvlc_Error) status(L"Playback error. Try another media file.");
        return 0;
    }
    case WM_COMMAND:
        switch(LOWORD(w)) {
        case OpenMedia: chooseFile(false); break;
        case OpenSubtitles: chooseFile(true); break;
        case Library: showLibrary(); break;
        case PlayPause: libvlc_media_player_pause(player); break;
        case CopySubtitle:
            status(clipboard(wide(currentText),hwnd)?L"Full subtitle copied.":L"No active subtitle to copy, or clipboard is busy."); break;
        case Audio:
            if(HIWORD(w)==CBN_SELCHANGE) {
                int index=static_cast<int>(SendMessageW(audioBox,CB_GETCURSEL,0,0));
                if(index>=0 && static_cast<size_t>(index)<audioIds.size() && libvlc_audio_set_track(player,audioIds[index])==0) {
                    saveAudio(index+1); audioPreferenceApplied=true; status(L"Audio track preference saved for the library.");
                }
            }
            break;
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1); if(libraryWindow) DestroyWindow(libraryWindow);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
int selfTest(const std::wstring &fixture) {
    auto failures=xp::coreTests(); int total=xp::coreTestCount;
    std::string report="Memento XP PROTOTYPE checks (not the modern app test suite)\r\n";
    auto check=[&](bool passed,const char *name) {
        ++total; report+=(passed?"PASS ":"FAIL ")+std::string(name)+"\r\n";
        if(!passed) failures.emplace_back(name);
    };
    const std::wstring sample=L"日本語の文。\nمرحبا";
    check(wide(utf8(sample))==sample,"Japanese/Arabic UTF-8 conversion");
    HWND owner=CreateWindowW(L"STATIC",L"Test clipboard owner",0,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    bool copied=clipboard(sample,owner); std::wstring readBack;
    if(copied && OpenClipboard(owner)) {
        auto handle=GetClipboardData(CF_UNICODETEXT);
        if(handle) { auto text=static_cast<const wchar_t*>(GlobalLock(handle)); if(text) { readBack=text; GlobalUnlock(handle); } }
        CloseClipboard();
    }
    check(copied && readBack==sample,"complete Unicode clipboard round trip"); DestroyWindow(owner);
    wchar_t temporary[MAX_PATH]={},testFile[MAX_PATH]={}; GetTempPathW(MAX_PATH,temporary);
    GetTempFileNameW(temporary,L"mxp",0,testFile);
    bool settings=WritePrivateProfileStringW(L"Library",L"AudioOrdinal",L"2",testFile)!=0;
    check(settings && GetPrivateProfileIntW(L"Library",L"AudioOrdinal",0,testFile)==2,"audio ordinal survives settings reload"); DeleteFileW(testFile);
    check(engine && player,"libVLC runtime loads on this OS");
    if(!fixture.empty()) {
        auto media=libvlc_media_new_path(engine,utf8(fixture).c_str());
        bool started=media!=nullptr;
        if(media) { libvlc_media_player_set_media(player,media); started=libvlc_media_player_play(player)==0; }
        const DWORD begin=GetTickCount(); bool advanced=false; int selected=-1;
        while(started && GetTickCount()-begin<12000) {
            libvlc_media_stats_t stats={};
            if(libvlc_media_player_get_time(player)>400 && media && libvlc_media_get_stats(media,&stats) && stats.i_decoded_video>0) { advanced=true;
                auto tracks=libvlc_audio_get_track_description(player); std::vector<int> ids;
                for(auto p=tracks;p;p=p->p_next) ids.push_back(p->i_id);
                const int target=xp::trackAtOrdinal(ids,2); libvlc_track_description_list_release(tracks);
                if(target>=0 && libvlc_audio_set_track(player,target)==0) { Sleep(100); selected=libvlc_audio_get_track(player)==target?target:-1; }
                break;
            }
            if(libvlc_media_player_get_state(player)==libvlc_Error) break;
            MSG message;
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            Sleep(100);
        }
        check(advanced,"fixture decoded and playback clock advanced");
        check(selected>=0,"second audio track selectable by ordinal");
        report+="Playback state="+std::to_string(static_cast<int>(libvlc_media_player_get_state(player)))+" time="+std::to_string(libvlc_media_player_get_time(player))+"ms\r\n";
        libvlc_media_player_stop(player);
        if(media) libvlc_media_release(media);
    } else check(false,"media fixture supplied");
    for(const auto &name:failures) report+="FAILED: "+name+"\r\n";
    report+="RESULT "+std::to_string(total-failures.size())+"/"+std::to_string(total)+" passed\r\n";
    report+="Original 12 Qt application suites: NOT RUN\r\n";
    if(!writeFile(std::wstring(temporary)+L"memento-xp-tests.txt",report)) return 2;
    return failures.empty()?0:1;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX);
    int argc=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    const bool testing=argc>1 && std::wstring(argv[1])==L"--self-test";
    const char *options[]={"--no-video-title-show","--no-osd","--no-media-library","--no-sub-autodetect-file","--avcodec-hw=none",testing?"--vout=dummy":"--vout=wingdi",testing?"--aout=dummy":"--aout=waveout"};
    engine=libvlc_new(static_cast<int>(sizeof(options)/sizeof(options[0])),options);
    if(engine) player=libvlc_media_player_new(engine);
    if(!engine || !player) { MessageBoxW(nullptr,L"The VLC playback runtime could not initialize.",L"Memento XP prototype",MB_OK|MB_ICONERROR); return 2; }
    if(testing) {
        const int result=selfTest(argc>2?argv[2]:L"");
        LocalFree(argv); libvlc_media_player_release(player); libvlc_release(engine); return result;
    }
    loadSettings();
    INITCOMMONCONTROLSEX controls={sizeof(controls),ICC_BAR_CLASSES}; InitCommonControlsEx(&controls);
    background=CreateSolidBrush(RGB(25,25,31));
    font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Tahoma");
    WNDCLASSW klass={}; klass.hInstance=instance; klass.hCursor=LoadCursor(nullptr,IDC_ARROW); klass.hbrBackground=background;
    klass.lpfnWndProc=WindowProc; klass.lpszClassName=L"MementoXPPrototype"; RegisterClassW(&klass);
    klass.lpfnWndProc=LibraryProc; klass.lpszClassName=L"MementoXPLibrary"; RegisterClassW(&klass);
    mainWindow=CreateWindowExW(0,L"MementoXPPrototype",L"Memento XP — experimental prototype",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,960,640,nullptr,nullptr,instance,nullptr);
    if(!mainWindow) { LocalFree(argv); libvlc_media_player_release(player); libvlc_release(engine); return 2; }
    ShowWindow(mainWindow,show); UpdateWindow(mainWindow);
    if(argc>1) openMedia(argv[1]);
    LocalFree(argv);
    ACCEL entries[]={{FCONTROL|FSHIFT|FVIRTKEY,'O',OpenSubtitles},{FCONTROL|FVIRTKEY,'O',OpenMedia},
        {FCONTROL|FVIRTKEY,'C',CopySubtitle},{FCONTROL|FVIRTKEY,'L',Library},{FVIRTKEY,VK_SPACE,PlayPause}};
    HACCEL accelerators=CreateAcceleratorTableW(entries,sizeof(entries)/sizeof(entries[0]));
    MSG message; while(GetMessageW(&message,nullptr,0,0)>0) {
        if(!TranslateAcceleratorW(mainWindow,accelerators,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    DestroyAcceleratorTable(accelerators); libvlc_media_player_stop(player); libvlc_media_player_release(player); libvlc_release(engine);
    DeleteObject(font); DeleteObject(background); return 0;
}
