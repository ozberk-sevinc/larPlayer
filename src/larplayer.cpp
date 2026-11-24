#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <io.h>
#include <conio.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using std::wstring;
constexpr const wchar_t* views[] = {L"library", L"search", L"queue"};
constexpr const wchar_t* labels[] = {L"Your library", L"Find music", L"Play queue", L"Classic client"};
constexpr const wchar_t* hints[] = {L"Saved music on this PC", L"Search saved music or paste a link", L"See what's playing next", L"Open the original ncspot client"};
constexpr const wchar_t* keys[] = {L"L", L"S", L"Q", L"C"};
constexpr int menu_count=4;
constexpr auto mint = L"\x1b[38;2;185;245;130m";
constexpr auto amber = L"\x1b[38;2;255;179;103m";
constexpr auto muted = L"\x1b[38;2;145;165;153m";
constexpr auto white = L"\x1b[1;38;2;247;242;216m";
constexpr auto selected = L"\x1b[1;38;2;18;28;19;48;2;185;245;130m";
constexpr auto reset = L"\x1b[0m";

void output(const wstring& s, HANDLE dest = GetStdHandle(STD_OUTPUT_HANDLE)) {
    DWORD mode, count;
    if (GetConsoleMode(dest, &mode)) {
        WriteConsoleW(dest, s.data(), static_cast<DWORD>(s.size()), &count, nullptr);
    } else {
        int size = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
        std::string bytes(size, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), bytes.data(), size, nullptr, nullptr);
        WriteFile(dest, bytes.data(), size, &count, nullptr);
    }
}

// A vinyl record drawn at terminal resolution, using a 2 by 4 dot cell.
std::array<wstring, 9> record() {
    constexpr int dots[4][2] = {{1,8},{2,16},{4,32},{64,128}};
    std::array<wstring,9> rows;
    for (int row=0; row<9; ++row) {
        for (int col=0; col<20; ++col) {
            int mask = 0;
            for (int y=0; y<4; ++y) for (int x=0; x<2; ++x) {
                double dx=(col*2+x-19.5)/19, dy=(row*4+y-17.5)/17;
                double radius=std::hypot(dx,dy);
                bool gloss=std::abs(dx+1.6*dy)<0.20 && radius>0.34;
                bool groove=std::fmod(radius,0.16)<0.048;
                if ((radius<1 && (groove || gloss || radius>0.95)) ||
                    (radius<0.29 && radius>0.065)) mask |= dots[y][x];
            }
            rows[row] += mask ? static_cast<wchar_t>(0x2800+mask) : L' ';
        }
    }
    return rows;
}

wstring frame(int active, int columns, bool color, bool compact) {
    auto paint = [&](const wstring& s, const wchar_t* ink) -> wstring {
        return color ? wstring(ink)+s+reset : s;
    };
    int width=std::clamp(columns-2, 1, 68);
    wstring result;
    auto line = [&](const wstring& s) { result += L" " + s + L"\r\n"; };
    auto rule = [&] { line(paint(wstring(width,L'─'),muted)); };
    if (width>=55 && !compact) {
        const auto art=record();
        const wchar_t* legend[]={L"",L"S T E R E O",L"",L"L A R P L A Y E R",L"Spotify terminal player",L"",L"●  33⅓ RPM",L"",L""};
        for (int i=0;i<9;++i)
            line(L"  "+paint(art[i],i==4?amber:mint)+L"     "+paint(legend[i],i==3?white:(i==6?amber:muted)));
    } else {
        line(paint(wstring(L"LARPLAYER").substr(0,width),mint));
    }
    rule();
    for (int i=0;i<menu_count;++i) {
        wstring item = (i==active ? L" ▸ " : L"   ") + wstring(keys[i]) + L"   " + labels[i];
        if (width>=55) { item.resize(26,L' '); item+=hints[i]; }
        item.resize(width,L' ');
        line(paint(item,i==active?selected:white));
    }
    rule();
    line(paint(wstring(L" ↑↓ choose    Enter open    Esc quit").substr(0,width),muted));
    return result;
}

int menu(bool preview) {
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE), in=GetStdHandle(STD_INPUT_HANDLE);
    DWORD previous=0, inputMode=0;
    bool console=GetConsoleMode(out,&previous);
    if (!preview && (!console || !GetConsoleMode(in,&inputMode))) {
        throw std::runtime_error("Open LARPlayer.exe in a Windows terminal, or run LARPlayer.exe preview.");
    }
    bool vt=console && SetConsoleMode(out,previous|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    bool color=vt && !std::getenv("NO_COLOR");
    int active=0;
    if (vt && !preview) output(L"\x1b[?1049h\x1b[?25l");
    for (;;) {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        bool sized=GetConsoleScreenBufferInfo(out,&info);
        int width=sized?info.srWindow.Right-info.srWindow.Left+1:72;
        if (vt && !preview) output(L"\x1b[H\x1b[J");
        else if (console && !preview) SetConsoleCursorPosition(out,{0,0});
        output(frame(active,width,color,sized && info.srWindow.Bottom-info.srWindow.Top<16));
        if (preview) break;
        int k=_getwch();
        if (k==0 || k==224) {
            int arrow=_getwch();
            if (arrow==72) active=(active+menu_count-1)%menu_count;
            if (arrow==80) active=(active+1)%menu_count;
        } else if (k==27 || k==3 || k=='x') { active=-1; break; }
        else if (k=='\r') break;
        else if (k=='l' || k=='L') { active=0; break; }
        else if (k=='s' || k=='S') { active=1; break; }
        else if (k=='q' || k=='Q') { active=2; break; }
        else if (k=='c' || k=='C') { active=3; break; }
        else if (k=='j') active=(active+1)%menu_count;
        else if (k=='k') active=(active+menu_count-1)%menu_count;
    }
    if (vt && !preview) output(L"\x1b[0m\x1b[?25h\x1b[?1049l");
    if (vt) SetConsoleMode(out,previous);
    return active;
}

fs::path install_dir() {
    std::vector<wchar_t> buf(32768);
    DWORD length=GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (!length || length==buf.size()) throw std::runtime_error("Could not locate LARPlayer.exe.");
    return fs::path(wstring(buf.data(),length)).parent_path();
}

fs::path user_profile() {
    const wchar_t* overridePath=_wgetenv(L"LARPLAYER_PROFILE");
    if (!overridePath || !*overridePath) overridePath=_wgetenv(L"GROOVE_PROFILE");
    if (overridePath && *overridePath) return fs::absolute(overridePath);
    const wchar_t* local=_wgetenv(L"LOCALAPPDATA");
    if (!local || !*local) throw std::runtime_error("Windows local app data is unavailable.");
    auto current=fs::path(local)/L"LARPlayer"/L"profile";
    auto legacy=fs::path(local)/L"GrooveDial"/L"profile";
    // Existing installations keep their cache and playback session after the rename.
    return fs::exists(current) || !fs::exists(legacy) ? current : legacy;
}

int run_engine(const fs::path& engine, const std::vector<wstring>& args) {
    // Paths come from the filesystem and all other arguments are app constants.
    wstring command=L"\""+engine.wstring()+L"\"";
    for (const auto& arg:args) {
        command+=L" \"";
        size_t trailing=0;
        for (auto ch:arg) {
            if (ch==L'"') throw std::runtime_error("Invalid double quote in a path.");
            trailing=ch==L'\\' ? trailing+1 : 0;
            command+=ch;
        }
        command.append(trailing,L'\\');
        command+=L'"';
    }
    STARTUPINFOW startup{};
    startup.cb=sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(engine.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("Windows could not start the required executable.");
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD code=1;
    GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
}

#include "soloist.hpp"

int memory() {
    struct Group { const wchar_t* image; const wchar_t* label; int matched=0, read=0; double mib=0; };
    Group groups[]={{L"LARPlayer.exe",L"LARPlayer launcher"},{L"soloist.exe",L"Spotify Soloist"},{L"ncspot.exe",L"Classic engine"},{L"Spotify.exe",L"Spotify Desktop"}};
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    if (snap==INVALID_HANDLE_VALUE) throw std::runtime_error("Windows could not list processes.");
    PROCESSENTRY32W entry{}; entry.dwSize=sizeof(entry);
    if (Process32FirstW(snap,&entry)) do {
        if (entry.th32ProcessID==GetCurrentProcessId()) continue;
        for (auto& group:groups) {
            if (_wcsicmp(entry.szExeFile,group.image)) continue;
            ++group.matched;
            HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,entry.th32ProcessID);
            PROCESS_MEMORY_COUNTERS counters{};
            if (p && GetProcessMemoryInfo(p,&counters,sizeof(counters))) {
                ++group.read;
                group.mib+=counters.WorkingSetSize/1048576.0;
            }
            if (p) CloseHandle(p);
        }
    } while (Process32NextW(snap,&entry));
    CloseHandle(snap);
    output(L"\r\n  LARPLAYER   /   MEMORY\r\n\r\n");
    for (const auto& g:groups) {
        wchar_t row[180];
        if (g.matched) swprintf(row,180,L"  %-22ls %7.1f MiB   (%d/%d processes measured)\r\n",g.label,g.mib,g.read,g.matched);
        else swprintf(row,180,L"  %-22ls      closed\r\n",g.label);
        output(row);
    }
    output(L"\r\n  Windows working set. Shared pages can count twice.\r\n  Terminal and this measurement command are excluded.\r\n");
    return 0;
}

int wmain(int argc,wchar_t** argv) {
    try {
        wstring action=argc>1?argv[1]:L"";
        if (argc>2) throw std::runtime_error("Expected one command. Run LARPlayer.exe help.");
        if (action==L"help" || action==L"--help" || action==L"-h") {
            output(L"LARPlayer.exe  [library | playlists | search | queue | demo | commands | menu | classic | setup | key | update | preview | ram | doctor]\r\n");
            return 0;
        }
        if (action==L"ram") return memory();
        if (action==L"preview") { menu(true); return 0; }
        bool fromMenu=action.empty() || action==L"menu";
        if (action.empty()) {
            if (!_isatty(_fileno(stdin)) || !_isatty(_fileno(stdout))) throw std::runtime_error("Open LARPlayer.exe in a Windows terminal.");
            action=L"library";
        } else if (action==L"menu") {
            int view=menu(false); if (view<0) return 0; action=view==3?L"classic":views[view];
        }
        if (action==L"demo") {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            return solo::deck::run(L"library",solo::deck::demo_library(),nullptr,{});
        }
        if (action==L"setup" || action==L"update" || action==L"key" || action==L"commands" || action==L"playlists" ||
            std::find(std::begin(views),std::end(views),action)!=std::end(views)) {
            try { return solo::run(action); }
            catch (...) {
                if (fromMenu && _isatty(_fileno(stdin))) {
                    try { throw; } catch (const std::exception& e) { std::fprintf(stderr,"LARPlayer: %s\n",e.what()); }
                    output(L"Press any key to return to Windows.\r\n"); _getwch(); return 1;
                }
                throw;
            }
        }
        bool doctor=action==L"doctor";
        if (!doctor && action!=L"classic")
            throw std::runtime_error("Unknown command. Run LARPlayer.exe help.");
        const fs::path root=install_dir(), engine=root/L"engine"/L"ncspot.exe", profile=user_profile();
        fs::create_directories(profile/L".config");
        for (auto view:views) {
            wstring name=L"larplayer-"+wstring(view)+L".toml";
            fs::copy_file(root/L"settings"/name,profile/L".config"/name,fs::copy_options::skip_existing);
        }
        if (doctor) {
            solo::diagnose();
            int rc=run_engine(engine,{L"--version"});
            return rc ? rc : run_engine(engine,{L"-b",profile.wstring(),L"info"});
        }
        int rc=run_engine(engine,{L"-b",profile.wstring(),L"-c",L"larplayer-library.toml"});
        if (rc && argc==1 && _isatty(_fileno(stdin))) {
            output(L"\r\nThe playback engine exited with an error. Press any key to close.\r\n");
            _getwch();
        }
        return rc;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"LARPlayer: %s\n",error.what());
        return 1;
    }
}
