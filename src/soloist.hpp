#pragma once
#include <wincred.h>
#include <wrl/client.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <cwctype>
#include <optional>
#include <set>
#include <mutex>
#include <thread>
#include <deque>
#include <atomic>
#include <chrono>
#include <winhttp.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#pragma comment(lib,"winhttp.lib")
#pragma comment(lib,"wintrust.lib")
#pragma comment(lib,"crypt32.lib")
#pragma comment(lib,"advapi32.lib")
#pragma comment(lib,"windowsapp.lib")
#pragma comment(lib,"ole32.lib")

namespace solo {
using Microsoft::WRL::ComPtr;
constexpr auto keyPage=L"https://developer.spotify.com/dashboard/soloist";
constexpr auto downloadPage=L"https://developer.spotify.com/documentation/soloist/reference/downloads-and-updates";
constexpr auto downloadURL=L"https://soloist-builds.spotifycdn.com/soloist_release_windows_x86_64.zip";
constexpr auto credName=L"LARPlayer/SpotifySoloist";
constexpr auto legacyCredName=L"GrooveDial/SpotifySoloist";

wstring read_line(const wchar_t* prompt) {
    output(prompt);
    std::vector<wchar_t> buf(4096);
    DWORD n=0;
    if (!ReadConsoleW(GetStdHandle(STD_INPUT_HANDLE),buf.data(),static_cast<DWORD>(buf.size()-1),&n,nullptr))
        throw std::runtime_error("Open this command in an interactive Windows terminal.");
    wstring s(buf.data(),n);
    auto first=s.find_first_not_of(L" \r\n\t");
    return first==wstring::npos?L"":s.substr(first,s.find_last_not_of(L" \r\n\t")-first+1);
}

wstring private_key(bool replace) {
    PCREDENTIALW stored=nullptr;
    if (!replace) {
        for (const auto* target:{credName,legacyCredName}) {
            if (!CredReadW(target,CRED_TYPE_GENERIC,0,&stored)) continue;
            wstring result(reinterpret_cast<wchar_t*>(stored->CredentialBlob),stored->CredentialBlobSize/sizeof(wchar_t));
            CredFree(stored);
            return result;
        }
    }
    output(L"\r\nGenerate your own Spotify Soloist key at\r\n"+wstring(keyPage)+L"\r\n");
    HANDLE in=GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode=0;
    if (!GetConsoleMode(in,&mode) || !SetConsoleMode(in,mode & ~ENABLE_ECHO_INPUT))
        throw std::runtime_error("A console is required to enter the key privately.");
    wstring value;
    try { value=read_line(L"Paste your key here (hidden), then press Enter: "); }
    catch (...) { SetConsoleMode(in,mode); throw; }
    SetConsoleMode(in,mode); output(L"\r\n");
    if (value.empty() || value.size()>1200 || value.find_first_of(L"\"\\ \t\r\n")!=wstring::npos)
        throw std::runtime_error("Enter the key copied from Spotify's Soloist dashboard.");
    CREDENTIALW c{}; c.Type=CRED_TYPE_GENERIC; c.TargetName=const_cast<wchar_t*>(credName);
    c.CredentialBlob=reinterpret_cast<BYTE*>(value.data());
    c.CredentialBlobSize=static_cast<DWORD>(value.size()*sizeof(wchar_t));
    c.Persist=CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&c,0)) throw std::runtime_error("Windows Credential Manager could not save the key.");
    return value;
}

fs::path root() { return user_profile().parent_path()/L"soloist"; }
fs::path locate(const fs::path& folder) {
    if (fs::is_directory(folder)) for (const auto& entry:fs::recursive_directory_iterator(folder)) {
        if (entry.is_regular_file() && !_wcsicmp(entry.path().filename().c_str(),L"soloist.exe")) return entry.path();
    }
    return {};
}
fs::path installed() {
    wchar_t overridePath[32768];
    DWORD n=GetEnvironmentVariableW(L"LARPLAYER_SOLOIST_EXE",overridePath,32768);
    if (!n) n=GetEnvironmentVariableW(L"GROOVE_SOLOIST_EXE",overridePath,32768);
    if (n && n<32768 && fs::is_regular_file(overridePath)) return fs::absolute(overridePath);
    return locate(root()/L"current");
}

// Spotify does not publish a stable digest for its expiring Windows builds.
// Require a valid Windows code-signing chain AND the expected publisher.
bool spotify_signed(const fs::path& exe) {
    WINTRUST_FILE_INFO file{}; file.cbStruct=sizeof(file); file.pcwszFilePath=exe.c_str();
    WINTRUST_DATA trust{}; trust.cbStruct=sizeof(trust); trust.dwUIChoice=WTD_UI_NONE;
    trust.fdwRevocationChecks=WTD_REVOKE_WHOLECHAIN; trust.dwUnionChoice=WTD_CHOICE_FILE;
    trust.pFile=&file; trust.dwStateAction=WTD_STATEACTION_VERIFY;
    GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status=WinVerifyTrust(nullptr,&policy,&trust);
    bool publisher=false;
    if(status==ERROR_SUCCESS) {
        auto provider=WTHelperProvDataFromStateData(trust.hWVTStateData);
        auto signer=provider?WTHelperGetProvSignerFromChain(provider,0,FALSE,0):nullptr;
        auto cert=signer?WTHelperGetProvCertFromChain(signer,0):nullptr;
        if(cert && cert->pCert) {
            wchar_t name[256]{};
            if(CertGetNameStringW(cert->pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,name,256))
                publisher=wcscmp(name,L"Spotify AB")==0;
        }
    }
    trust.dwStateAction=WTD_STATEACTION_CLOSE;
    if(trust.hWVTStateData) WinVerifyTrust(nullptr,&policy,&trust);
    return status==ERROR_SUCCESS && publisher;
}

fs::path install(bool update) {
    fs::path current=installed();
    if (!update && !current.empty()) return current;
    output(L"Downloading the Windows engine directly from Spotify.\r\n"+wstring(downloadPage)+L"\r\n");
    fs::create_directories(root());
    fs::path stage=root()/(L"setup-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    if (!fs::create_directory(stage)) throw std::runtime_error("Could not create a staging folder.");
    struct Cleanup {fs::path p; ~Cleanup(){ std::error_code ec; fs::remove_all(p,ec); }} cleanup{stage};
    const fs::path bundle=stage/L"spotify.zip", unpack=stage/L"unpacked";
    wchar_t systemDir[MAX_PATH]; if (!GetSystemDirectoryW(systemDir,MAX_PATH)) throw std::runtime_error("Windows system folder unavailable.");
    auto system=fs::path(systemDir);
    int code=run_engine(system/L"curl.exe",{L"--fail",L"--location",L"--proto",L"=https",L"--proto-redir",L"=https",L"--retry",L"2",L"--connect-timeout",L"15",L"--max-time",L"240",L"-o",bundle.wstring(),downloadURL});
    if (code) throw std::runtime_error("Spotify's download failed. Retry setup, or extract the official Windows archive to engine/soloist next to LARPlayer.exe.");
    fs::create_directory(unpack);
    const fs::path extractor=install_dir()/L"scripts"/L"extract-soloist.ps1";
    if (!fs::is_regular_file(extractor) ||
        run_engine(system/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe",
            {L"-NoProfile",L"-NonInteractive",L"-File",extractor.wstring(),L"-Archive",bundle.wstring(),L"-Destination",unpack.wstring()}))
        throw std::runtime_error("Spotify's archive did not pass safe extraction checks.");
    fs::path exe=locate(unpack);
    if (exe.empty() || !spotify_signed(exe))
        throw std::runtime_error("Spotify's download has no valid Spotify AB code signature. Setup stopped before executing it.");
    if (run_engine(exe,{L"--version"})) throw std::runtime_error("The downloaded Spotify executable failed its version check.");
    const fs::path dest=root()/L"current", backup=stage/L"old";
    if (fs::exists(dest)) fs::rename(dest,backup);
    try { fs::rename(unpack,dest); }
    catch (...) { if(fs::exists(backup) && !fs::exists(dest)) fs::rename(backup,dest); throw; }
    return locate(dest);
}

fs::path engine(bool update=false) {
    fs::path local=locate(install_dir()/L"engine"/L"soloist");
    fs::path receiverExe=(!update && installed().empty() && !local.empty())?local:install(update);
    if (!spotify_signed(receiverExe))
        throw std::runtime_error("Soloist must have a valid Spotify AB code signature. Download the current Windows build from Spotify.");
    return receiverExe;
}

void diagnose() {
    auto exe=installed();
    if (exe.empty()) exe=locate(install_dir()/L"engine"/L"soloist");
    if (exe.empty()) output(L"Spotify Soloist: awaiting first-run setup.\r\n");
    else run_engine(exe,{L"--version"});
}

struct Receiver {
    HANDLE job{}, process{};
    DWORD pid{};
    ~Receiver() {
        if (job) CloseHandle(job);
        if (process) { if(job) WaitForSingleObject(process,3000); CloseHandle(process); }
    }
    Receiver()=default;
    Receiver(const Receiver&)=delete;
    Receiver& operator=(const Receiver&)=delete;
    static void store_key(const fs::path& exe,wstring& key,const fs::path& data) {
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES),nullptr,TRUE};
        HANDLE input=nullptr, writer=nullptr;
        if(!CreatePipe(&input,&writer,&attributes,0)) throw std::runtime_error("Could not create the private key pipe.");
        SetHandleInformation(writer,HANDLE_FLAG_INHERIT,0);
        HANDLE nul=CreateFileW(L"NUL",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&attributes,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(nul==INVALID_HANDLE_VALUE) { CloseHandle(input); CloseHandle(writer); throw std::runtime_error("Could not open private setup output."); }
        wstring cmd=L"\""+exe.wstring()+L"\" setup-api-key --data-dir \""+data.wstring()+L"\"";
        STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESTDHANDLES;
        si.hStdInput=input; si.hStdOutput=nul; si.hStdError=nul;
        PROCESS_INFORMATION pi{};
        BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi);
        CloseHandle(input); CloseHandle(nul);
        SecureZeroMemory(cmd.data(),cmd.size()*sizeof(wchar_t));
        if(!ok) { CloseHandle(writer); throw std::runtime_error("Could not start Soloist's private key setup."); }
        int length=WideCharToMultiByte(CP_UTF8,0,key.data(),static_cast<int>(key.size()),nullptr,0,nullptr,nullptr);
        std::string bytes(length+1,'\n');
        if(length) WideCharToMultiByte(CP_UTF8,0,key.data(),static_cast<int>(key.size()),bytes.data(),length,nullptr,nullptr);
        DWORD written=0;
        BOOL sent=WriteFile(writer,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr);
        SecureZeroMemory(bytes.data(),bytes.size());
        CloseHandle(writer); CloseHandle(pi.hThread);
        DWORD wait=WaitForSingleObject(pi.hProcess,15000), code=1;
        if(wait==WAIT_TIMEOUT) TerminateProcess(pi.hProcess,1);
        if(wait==WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess,&code);
        CloseHandle(pi.hProcess);
        if(!sent || written!=static_cast<DWORD>(length+1) || wait!=WAIT_OBJECT_0 || code!=0)
            throw std::runtime_error("Soloist could not store the key through stdin. Update to a Spotify-signed build that supports setup-api-key. The key was not passed on the command line.");
    }
    void start(const fs::path& exe,wstring& key,const fs::path& data,const fs::path& cache) {
        try { store_key(exe,key,data); }
        catch (...) { SecureZeroMemory(key.data(),key.size()*sizeof(wchar_t)); throw; }
        SecureZeroMemory(key.data(),key.size()*sizeof(wchar_t));
        job=CreateJobObjectW(nullptr,nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
            throw std::runtime_error("Could not create the receiver process group.");
        wstring cmd=L"\""+exe.wstring()+L"\" -n \"LARPlayer\" --ws 127.0.0.1:0 -D \""+
            data.wstring()+L"\" -C \""+cache.wstring()+L"\" --cache-size 100";
        STARTUPINFOW si{}; si.cb=sizeof(si); PROCESS_INFORMATION pi{};
        BOOL ok=CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,exe.parent_path().c_str(),&si,&pi);
        SecureZeroMemory(cmd.data(),cmd.size()*sizeof(wchar_t));
        if (!ok) throw std::runtime_error("Windows could not launch Spotify Soloist.");
        process=pi.hProcess; pid=pi.dwProcessId;
        if (!AssignProcessToJobObject(job,process) || ResumeThread(pi.hThread)==static_cast<DWORD>(-1)) {
            TerminateProcess(process,1); CloseHandle(pi.hThread); throw std::runtime_error("Could not start the receiver.");
        }
        CloseHandle(pi.hThread);
    }
    void check() const {
        if (WaitForSingleObject(process,0)!=WAIT_OBJECT_0) return;
        DWORD exit=1; GetExitCodeProcess(process,&exit);
        if (exit==10) throw std::runtime_error("This Soloist build expired. Run LARPlayer.exe update.");
        throw std::runtime_error("Spotify Soloist exited (code "+std::to_string(exit)+"). Close other LARPlayer sessions. Use LARPlayer.exe key to replace an invalid key.");
    }
};

struct Item { wstring name,uri,title,artist,kind; int duration=0; };
wstring normalize(wstring s) {
    constexpr std::wstring_view web=L"https://open.spotify.com/";
    if (s.rfind(web,0)==0) {
        s.erase(0,web.size());
        if (s.rfind(L"intl-",0)==0 && s.find(L'/')!=wstring::npos) s.erase(0,s.find(L'/')+1);
        s=s.substr(0,s.find_first_of(L"?#"));
        std::replace(s.begin(),s.end(),L'/',L':'); s=L"spotify:"+s;
    }
    size_t pos=s.rfind(L':');
    if (pos==wstring::npos || s.size()-pos-1!=22) return {};
    auto prefix=s.substr(0,pos);
    if (prefix!=L"spotify:track" && prefix!=L"spotify:album" && prefix!=L"spotify:playlist" && prefix!=L"spotify:episode") return {};
    for (size_t i=pos+1;i<s.size();i++) if (!(s[i]>=L'0' && s[i]<=L'9') && !(s[i]>=L'A' && s[i]<=L'Z') && !(s[i]>=L'a' && s[i]<=L'z')) return {};
    return s;
}
wstring lower(wstring s) { for(auto& c:s) c=static_cast<wchar_t>(towlower(c)); return s; }
wstring printable(wstring s) {
    for(auto& c:s) if(c<32 || (c>=127 && c<160)) c=L' ';
    return s;
}
std::vector<Item> library(const fs::path& folder) {
    using namespace winrt::Windows::Data::Json;
    std::vector<Item> items;
    for (const auto& kind:{wstring(L"track"),wstring(L"album"),wstring(L"playlist")}) {
        fs::path file=folder/(kind+L"s.db");
        if (!fs::is_regular_file(file)) continue;
        std::ifstream stream(file,std::ios::binary);
        std::string json((std::istreambuf_iterator<char>(stream)),{});
        try {
            auto entries=JsonArray::Parse(winrt::to_hstring(json));
            for (const auto& entry:entries) {
                auto item=entry.GetObject();
                wstring uri=normalize(L"spotify:"+kind+L":"+item.GetNamedString(L"id",L"").c_str());
                if (uri.empty()) continue;
                wstring title=printable(item.GetNamedString(kind==L"playlist"?L"name":L"title",L"").c_str());
                wstring name=title, artist;
                auto artists=item.GetNamedArray(L"artists",nullptr);
                if (artists && artists.Size() && artists.GetAt(0).ValueType()==JsonValueType::String)
                    artist=printable(artists.GetStringAt(0).c_str());
                if(!artist.empty()) name+=L"  /  "+artist;
                items.push_back({name+L"  ["+kind+L"]",uri,title,artist,kind,static_cast<int>(item.GetNamedNumber(L"duration",0))});
            }
        } catch(const winrt::hresult_error&) { output(L"Could not read saved "+kind+L"s.\r\n"); }
    }
    return items;
}

// The per-session meter belongs to the Soloist PID. Sound from a browser or another process cannot pass.
bool soundcheck(DWORD pid,bool verbose=true) {
    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IMMDeviceCollection> outputs;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&devices))) ||
        FAILED(devices->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&outputs)))
        throw std::runtime_error("Windows audio endpoints are unavailable.");
    UINT count=0; outputs->GetCount(&count);
    std::vector<ComPtr<IAudioMeterInformation>> meters;
    for(UINT i=0;i<count;++i) {
        ComPtr<IMMDevice> dev; ComPtr<IAudioSessionManager2> mgr; ComPtr<IAudioSessionEnumerator> sessions;
        if(FAILED(outputs->Item(i,&dev)) || FAILED(dev->Activate(__uuidof(IAudioSessionManager2),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(mgr.GetAddressOf()))) ||
           FAILED(mgr->GetSessionEnumerator(&sessions))) continue;
        int n=0; sessions->GetCount(&n);
        for(int j=0;j<n;++j) {
            ComPtr<IAudioSessionControl> session; ComPtr<IAudioSessionControl2> detail; ComPtr<IAudioMeterInformation> meter;
            DWORD current=0;
            if(SUCCEEDED(sessions->GetSession(j,&session)) && SUCCEEDED(session.As(&detail)) &&
               SUCCEEDED(detail->GetProcessId(&current)) && current==pid && SUCCEEDED(session.As(&meter))) meters.push_back(meter);
        }
    }
    float peak=0;
    if(!meters.empty()) for (int i=0;i<60;++i) {
        for(const auto& meter:meters) { float value=0; if(SUCCEEDED(meter->GetPeakValue(&value))) peak=std::max(peak,value); }
        Sleep(50);
    }
    const wchar_t* message=peak>0.00001f?L"Audio signal detected from Spotify Soloist.":(meters.empty()?L"No audio session found for Spotify Soloist.":L"The Spotify Soloist audio session was silent during the sample.");
    if(verbose) output(wstring(message)+L"\r\n");
    std::ofstream report(root()/L"soundcheck.txt",std::ios::trunc);
    report<<"pid="<<pid<<"\npeak="<<peak<<"\nsessions="<<meters.size()<<"\n";
    return peak>0.00001f;
}
#include "deck.hpp"
inline int run(const wstring& action) {
    if (action==L"key") { auto k=private_key(true); SecureZeroMemory(k.data(),k.size()*sizeof(wchar_t)); return 0; }
    auto exe=engine(action==L"update");
    if(action==L"setup" || action==L"update") return run_engine(exe,{L"--version"});
    auto data=root()/L"data", cache=root()/L"cache";
    fs::create_directories(data); fs::create_directories(cache);
    Receiver receiver;
    bool attached=deck::attach(receiver,exe,data);
    if(!attached) {
        auto key=private_key(false);
        receiver.start(exe,key,data,cache);
    }
    for(int i=0;i<100;++i) {
        receiver.check();
        DWORD owner=0; std::ifstream(data/L"soloist.pid")>>owner;
        if(owner==receiver.pid && fs::exists(data/L"ws.port")) break;
        Sleep(100);
    }
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    const auto catalog=library(user_profile()/L".cache");
    if(action!=L"commands") return deck::run(action,catalog,&receiver,data);
    output(L"\r\n  LARPLAYER  /  SPOTIFY SOLOIST\r\n\r\n  On your phone, open Spotify's device picker and select LARPlayer.\r\n  Start a song there, or paste its Spotify link below after pairing.\r\n");
    auto ctl=[&](std::vector<wstring> args) {
        std::vector<wstring> cmd={L"ctl",L"--data-dir",data.wstring()};
        cmd.insert(cmd.end(),args.begin(),args.end()); return run_engine(exe,cmd);
    };
    std::vector<wstring> matches;
    const auto show=[&](const wstring& term) {
        matches.clear();
        for(const auto& item:catalog) {
            if(lower(item.name).find(lower(term))==wstring::npos) continue;
            matches.push_back(item.uri); output(L"  "+std::to_wstring(matches.size())+L". "+item.name+L"\r\n");
            if(matches.size()==20) break;
        }
        if(matches.empty()) output(L"  No saved matches. Paste a Spotify link or choose music on your phone.\r\n");
    };
    output(L"\r\n  Enter a Spotify link, search words, or a numbered result.\r\n  Commands: play / pause / next / prev / now / queue / status / check / quit\r\n");
    for(;;) {
        receiver.check();
        wstring line=read_line(L"\r\n  larplayer > ");
        if(line==L"quit" || line==L"q") return 0;
        if(line==L"ui") return deck::run(L"library",catalog,&receiver,data);
        if(line.empty()) continue;
        if(line==L"check") { soundcheck(receiver.pid); continue; }
        if(line==L"library") { show(L""); continue; }
        if(line==L"status" || line==L"play" || line==L"pause" || line==L"next" || line==L"prev" || line==L"now" || line==L"queue") { ctl({line}); continue; }
        if(line.find_first_not_of(L"0123456789")==wstring::npos && line.size()<=3) {
            auto n=std::stoul(line);
            if(n && n<=matches.size()) ctl({L"play",matches[n-1]});
            else output(L"  Choose a number shown above.\r\n");
            continue;
        }
        wstring uri=normalize(line);
        if(!uri.empty()) ctl({L"play",uri});
        else show(line);
    }
}
} // namespace solo
