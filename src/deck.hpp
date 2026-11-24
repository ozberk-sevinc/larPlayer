namespace deck {
using namespace winrt::Windows::Data::Json;
#include "deck-wire.hpp"
#include "deck-hotkeys.hpp"

bool attach(Receiver& receiver,const fs::path& exe,const fs::path& data) {
    DWORD pid=0; std::ifstream(data/L"soloist.pid")>>pid;
    if(!pid || !fs::is_regular_file(data/L"ws.port")) return false;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);
    if(!process) return false;
    wchar_t filename[32768]; DWORD length=32768; std::error_code ec;
    if(!QueryFullProcessImageNameW(process,0,filename,&length) || !fs::equivalent(fs::path(filename),exe,ec) ||
       WaitForSingleObject(process,0)!=WAIT_TIMEOUT) { CloseHandle(process); return false; }
    receiver.process=process; receiver.pid=pid; return true;
}

JsonObject object(const JsonObject& o,const wchar_t* key) {
    if(!o || !o.HasKey(key))return nullptr;
    auto value=o.Lookup(key);
    return value.ValueType()==JsonValueType::Object?value.GetObject():nullptr;
}
wstring str(const JsonObject& o,const wchar_t* key) {return o?printable(wstring(o.GetNamedString(key,L""))):L"";}
wstring entity_name(const JsonObject& o) {return str(object(object(o,L"decorations"),L"identity"),L"name");}
Item entity(const JsonObject& o) {
    Item item{};
    if(!o) return item;
    auto d=object(o,L"decorations"), playback=object(d,L"playback");
    item.uri=str(o,L"uri"); item.kind=str(o,L"entity_type"); item.title=entity_name(o);
    if(item.title.empty() && !item.uri.empty()) item.title=item.uri;
    if(playback) item.duration=static_cast<int>(playback.GetNamedNumber(L"duration_ms",0));
    auto creators=d?d.GetNamedArray(L"creators",nullptr):nullptr;
    if(creators) for(auto c:creators) {
        auto name=entity_name(object(c.GetObject(),L"entity"));
        if(!name.empty()) { if(!item.artist.empty()) item.artist+=L", "; item.artist+=name; }
    }
    item.name=item.title+L" "+item.artist; return item;
}
long long wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
enum class View {Library,Find,Queue,Playlists};
struct State {
    Item track{};
    wstring status=L"idle",album,notice,query;
    bool logged=false,active=false,online=false,editing=false,help=false,demo=false,hotkeySettings=false;
    int hotkeyCapture=-1;
    std::array<wstring,3> hotkeyLabels{};
    int volume=-1,selection=0,offset=0,ramMiB=-1;
    long long anchor=0; double position=0,speed=0;
    View view=View::Library;
    std::vector<Item> queue;
    std::vector<size_t> filtered;
    ULONGLONG noticeUntil=0;
    void message(const wstring& s){notice=s;noticeUntil=GetTickCount64()+5000;}
    long long elapsed() const {
        double p=position;
        if(anchor && status==L"playing") p+=std::max(0LL,wall_ms()-anchor)*speed;
        return static_cast<long long>(std::clamp(p,0.0,track.duration?static_cast<double>(track.duration):1.0e10));
    }
    void filter(const std::vector<Item>& lib){
        const auto term=lower(query); filtered.clear();
        for(size_t i=0;i<lib.size();++i)
            if((view!=View::Playlists || lib[i].kind==L"playlist") &&
               lower(lib[i].name).find(term)!=wstring::npos) filtered.push_back(i);
    }
    void switch_to(View v){
        if(v!=view && (v==View::Playlists || view==View::Playlists))query.clear();
        view=v; selection=offset=0;editing=v==View::Find;
    }
    void change_track(const JsonObject& item) {
        track=entity(item); album=entity_name(object(object(object(item,L"decorations"),L"parent"),L"entity"));
    }
    void sync(const JsonObject& o) {
        if(!o) return;
        anchor=static_cast<long long>(o.GetNamedNumber(L"timestamp_ms",static_cast<double>(wall_ms())));
        position=o.GetNamedNumber(L"position_ms",0); speed=o.GetNamedNumber(L"speed",0);
    }
    wstring event(const std::string& bytes) {
        auto o=JsonObject::Parse(winrt::to_hstring(bytes)); auto type=str(o,L"type");
        if(type==L"auth_state"){
            logged=o.GetNamedBoolean(L"logged_in",false); active=o.GetNamedBoolean(L"is_active",false);
            if(!logged){track={};queue.clear();status=L"idle";speed=0;position=0;album.clear();}
        } else if(type==L"playback_state"){
            status=str(o,L"status"); change_track(object(o,L"item")); sync(object(o,L"position"));
            volume=static_cast<int>(o.GetNamedNumber(L"volume",volume)); active=o.GetNamedBoolean(L"is_active",active);
        } else if(type==L"playback_changed"){
            position=static_cast<double>(elapsed()); anchor=wall_ms(); status=str(o,L"status"); speed=status==L"playing"?1:0;
        } else if(type==L"track_changed"){
            change_track(object(o,L"item")); position=0;anchor=wall_ms();
        } else if(type==L"position_sync") sync(object(o,L"position"));
        else if(type==L"volume_changed") volume=static_cast<int>(o.GetNamedNumber(L"volume",volume));
        else if(type==L"device_changed") active=o.GetNamedBoolean(L"is_active",active);
        else if(type==L"queue_changed"){
            queue.clear();auto rows=o.GetNamedArray(L"upcoming",nullptr);
            if(rows) for(auto row:rows) queue.push_back(entity(object(row.GetObject(),L"item")));
        } else if(type==L"error") message(str(o,L"message"));
        return type;
    }
};

void refresh_ram(State& s,DWORD receiverPid){
    PROCESS_MEMORY_COUNTERS counters{};
    if(!GetProcessMemoryInfo(GetCurrentProcess(),&counters,sizeof(counters))){
        s.ramMiB=-1;return;
    }
    SIZE_T bytes=counters.WorkingSetSize;
    if(receiverPid){
        HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,receiverPid);
        PROCESS_MEMORY_COUNTERS player{};
        bool measured=process && GetProcessMemoryInfo(process,&player,sizeof(player));
        if(process)CloseHandle(process);
        if(!measured){s.ramMiB=-1;return;}
        bytes+=player.WorkingSetSize;
    }
    s.ramMiB=static_cast<int>((bytes+1048575)/1048576);
}

const wchar_t* paper=L"\x1b[38;2;238;242;224m";
const wchar_t* lime=L"\x1b[1;38;2;194;250;132m";
const wchar_t* copper=L"\x1b[38;2;250;169;115m";
const wchar_t* moss=L"\x1b[38;2;136;163;145m";
const wchar_t* ruleInk=L"\x1b[38;2;65;92;77m";
const wchar_t* highlight=L"\x1b[1;38;2;202;255;161;48;2;39;62;47m";
const wchar_t* background=L"\x1b[0;48;2;16;24;21m";
int glyph_width(unsigned cp) {
    if((cp>=0x300&&cp<=0x36f)||cp==0xfe0f||cp==0x200d) return 0;
    return ((cp>=0x1100&&cp<=0x115f)||(cp>=0x2e80&&cp<=0xa4cf)||(cp>=0xac00&&cp<=0xd7a3)||
        (cp>=0xf900&&cp<=0xfaff)||(cp>=0xff01&&cp<=0xff60)||(cp>=0x1f300&&cp<=0x1faff)||
        (cp>=0x20000&&cp<=0x3fffd))?2:1;
}
wstring fit(const wstring& s,int limit,bool pad=false){
    wstring out;int used=0;
    for(size_t i=0;i<s.size() && used<limit;) {
        unsigned cp=s[i];size_t n=1;
        if(cp>=0xd800 && cp<=0xdbff && i+1<s.size() && s[i+1]>=0xdc00 && s[i+1]<=0xdfff){cp=0x10000+(cp-0xd800)*1024+(s[i+1]-0xdc00);n=2;}
        int width=glyph_width(cp); if(used+width>limit) break;
        out.append(s,i,n);i+=n;used+=width;
    }
    if(pad && used<limit) out.append(limit-used,L' ');return out;
}
wstring clock_text(long long ms) {
    int sec=static_cast<int>(std::max(0LL,ms)/1000);
    return std::to_wstring(sec/60)+L":"+(sec%60<10?L"0":L"")+std::to_wstring(sec%60);
}
struct Canvas {
    int width; bool color;
    std::vector<wstring> lines;
    wstring ink(const wstring& s,const wchar_t* fg)const {return color?wstring(fg)+s+background:s;}
    void line(wstring s={}) {lines.push_back((color?wstring(background):L"")+s);}
};

// Two-by-four Braille cells give the platter fine grooves. Only the label and reflection turn.
std::array<wstring,9> platter(const Canvas& c,int phase) {
    std::array<wstring,9> rows;
    constexpr int dot[4][2]={{1,8},{2,16},{4,32},{64,128}};
    double angle=phase*3.141592653589793/12;
    for(int y=0;y<9;++y){
        wstring line; const wchar_t* last=nullptr;
        for(int x=0;x<24;++x){
            int mask=0;const double radius=std::hypot((x-11.5)/11.3,(y-4.0)/4.5);
            for(int yy=0;yy<4;++yy) for(int xx=0;xx<2;++xx) {
                double dx=(2*x+xx-23.5)/22.8,dy=(4*y+yy-17.5)/17.2,r=std::hypot(dx,dy);
                double sweep=std::abs(dx*std::sin(angle+0.55)+dy*std::cos(angle+0.55));
                bool groove=std::fmod(r,0.153)<0.041, glint=sweep<0.09 && r>0.35;
                bool label=r<0.31 && r>0.055 && !(dx*std::cos(angle)+dy*std::sin(angle)>0.09 && sweep<0.065);
                if(r<1 && (r>0.95 || groove || glint || label)) mask|=dot[yy][xx];
            }
            auto fg=radius<0.34?copper:(radius>0.92?moss:lime);
            if(c.color && fg!=last){line+=fg;last=fg;}
            line+=mask?static_cast<wchar_t>(0x2800+mask):L' ';
        }
        rows[y]=line+(c.color?background:L"");
    }
    return rows;
}
int count(const State& s,const std::vector<Item>&) {
    return static_cast<int>(s.view==View::Queue?s.queue.size():s.filtered.size());
}
const Item& at(const State& s,const std::vector<Item>& lib,int i) {
    return s.view==View::Queue?s.queue.at(i):lib.at(s.filtered.at(i));
}
std::vector<wstring> render(State& s,const std::vector<Item>& lib,int cols,int rows,bool color) {
    const int w=std::clamp(cols-2,12,154);
    Canvas c{w,color,{}};
    const bool large=w>=55 && rows>=24;
    const bool mid=w>=40 && rows>=17;
    auto label=[&](const wstring& name,const wchar_t* shade){return c.ink(name,shade);};
    auto hr=[&]{c.line(L" "+label(wstring(w,L'─'),ruleInk));};
    wstring badge=s.demo?L"DEMO":!s.online?L"CONNECTING":!s.logged?L"PAIR DEVICE":s.active?L"●  CONNECTED":L"STANDBY";
    wstring memory=s.ramMiB<0?L"RAM --":w>=40?L"RAM "+std::to_wstring(s.ramMiB)+L" MiB":
        std::to_wstring(s.ramMiB)+L"M";
    wstring gap=w>=40?L"   ":L" ";
    wstring brand=w>=38?L"◉   G R O O V E   D I A L":L"◉  GROOVE DIAL";
    int brandRoom=std::max(1,w-static_cast<int>(badge.size()+gap.size()+memory.size())-1);
    c.line(L" "+label(fit(brand,brandRoom,true),lime)+L" "+label(badge,moss)+gap+label(memory,copper));
    hr();
    const bool playing=s.status==L"playing";
    wstring status=playing?L"▶  PLAYING":s.status==L"paused"?L"Ⅱ  PAUSED":s.status==L"buffering"?L"◌  BUFFERING":L"○  READY";
    auto title=s.track.title.empty()?(s.logged?L"Choose a track":L"Connect your Spotify"):s.track.title;
    wstring artist=s.track.artist.empty()?(s.logged?L"":L"Select Groove Dial in Spotify’s device picker."):s.track.artist;
    auto elapsed=s.elapsed(); wstring times=clock_text(elapsed)+L" / "+(s.track.duration?clock_text(s.track.duration):L"–:––");
    auto progress=[&](int n) {
        int used=s.track.duration?static_cast<int>(std::clamp(elapsed/double(s.track.duration),0.,1.)*n):0;
        return label(wstring(used,L'━'),lime)+label(wstring(n-used,L'─'),ruleInk);
    };
    if(large){
        static const auto records=[&]{
            std::array<std::array<wstring,9>,24> frames;
            for(int i=0;i<24;i++) frames[i]=platter(c,i);
            return frames;
        }();
        const auto& art=records[(elapsed/500)%24];
        int room=w-29; int n=std::min(room,46);
        wstring info[9]={L"",label(status,copper),L"",label(fit(title,room),white),
            label(fit(artist,room),paper),label(fit(s.album,room),moss),L"",progress(n),label(fit(times+(s.volume>=0?L"    VOL "+std::to_wstring(s.volume)+L"%":L""),room),moss)};
        for(int i=0;i<9;++i)c.line(L"  "+art[i]+L"   "+info[i]);
    }else {
        c.line(L" "+label(fit(status+L"   "+title,w),white));
        if(mid)c.line(L" "+label(fit(artist,w),moss));
        if(rows>=14)c.line(L" "+progress(std::max(3,w-static_cast<int>(times.size())-3))+L"  "+label(times,moss));
    }
    hr();
    if(s.hotkeySettings){
        c.line(L" "+label(L"Global playback shortcuts",lime));
        c.line(L" "+label(fit(L" 1  Pause / resume       "+s.hotkeyLabels[0],w),paper));
        c.line(L" "+label(fit(L" 2  Previous track       "+s.hotkeyLabels[1],w),paper));
        c.line(L" "+label(fit(L" 3  Next track           "+s.hotkeyLabels[2],w),paper));
        c.line();
        c.line(L" "+label(fit(s.hotkeyCapture<0?L"Choose 1–3, then press F1–F11 with optional Ctrl / Alt / Shift.":L"Press an F1–F11 key for this action. Esc cancels.",w),copper));
        c.line(L" "+label(fit(L"4  Reset defaults     H or Esc  Back to music",w),moss));
    } else if(s.help){
        c.line(L" "+label(L"Player keys",lime));
        for(auto text:{L"1 Library  2 Find  3 Queue  4 Playlists",L"/ or Ctrl+F  Search the current list",L"↑ ↓ / PgUp PgDn   Move selection",L"Enter   Play selection or pasted Spotify link",L"Space   Pause / resume     N / P   Next / previous",L"− / +   Volume             V   Audio check",L"Esc   Finish search         X   Quit player",L"?   Close help"}) c.line(L" "+label(fit(text,w),paper));
    } else {
        wstring tabs;
        if(w>=55){
            for(auto [v,name]:{std::pair{View::Library,L"1 Library"},std::pair{View::Find,L"2 Find"},std::pair{View::Queue,L"3 Queue"},std::pair{View::Playlists,L"4 Playlists"}})
                tabs+=label(L" "+wstring(name)+L" ",s.view==v?highlight:moss);
        }else {
            for(auto [v,name]:{std::pair{View::Library,L"1Lib"},std::pair{View::Find,L"2Find"},std::pair{View::Queue,L"3Queue"},std::pair{View::Playlists,L"4Lists"}})
                tabs+=label(wstring(name)+L" ",s.view==v?highlight:moss);
        }
        c.line(L" "+tabs);
        if(s.view!=View::Queue) {
            wstring hint=s.view==View::Playlists?L"Search playlists  /  name":s.view==View::Library?L"Search library  /  title or artist":L"Find saved music or paste a Spotify link";
            wstring search=L"⌕  "+(s.query.empty()&&!s.editing?hint:s.query)+(s.editing?L"▏":L"");
            wstring matches=s.query.empty()?L"":L"  "+std::to_wstring(s.filtered.size())+
                (s.filtered.size()==1?L" match":L" matches");
            int room=std::max(1,w-static_cast<int>(matches.size()));
            c.line(L" "+label(fit(search,room,true),copper)+label(matches,moss));
        }
        int total=count(s,lib);
        int visible=std::max(1,rows-static_cast<int>(c.lines.size())-5);
        s.selection=std::clamp(s.selection,0,std::max(0,total-1));
        s.offset=std::clamp(s.offset,std::max(0,s.selection-visible+1),s.selection);
        bool columns=w>=61 && s.view!=View::Playlists;
        int durationWidth=columns?7:0, artistW=columns?std::max(14,w/3):0, titleW=w-4-artistW-durationWidth;
        if(rows>=18)c.line(L" "+label(L"    "+fit(s.view==View::Queue?L"UP NEXT":s.view==View::Playlists?L"YOUR PLAYLISTS":L"TITLE",titleW,true)+(columns?fit(L"ARTIST",artistW,true)+L" LENGTH":L""),moss));
        for(int i=s.offset;i<total && i<s.offset+visible;++i) {
            auto& item=at(s,lib,i);bool focus=i==s.selection, current=!item.uri.empty()&&item.uri==s.track.uri;
            wstring lead=focus?L" ▸  ":current?L" ♪  ":L"    ";
            wstring right=item.duration?clock_text(item.duration):item.kind;
            wstring row=lead+fit(item.title,titleW,true)+(columns?fit(item.artist,artistW,true)+fit(right,7,true):L"");
            c.line(L" "+label(row,focus?highlight:current?lime:paper));
        }
        if(!total){
            wstring empty=s.view==View::Queue?L"The queue is empty.":s.view==View::Playlists?
                (s.query.empty()?L"No saved playlists in the local cache.":L"No matching playlists. Search another name."):
                !s.query.empty()?L"No saved matches. Search another title or artist.":L"Your saved music will appear here. Press / to paste a link.";
            c.line(L" "+label(fit(empty,w),moss));
        }
    }
    while(static_cast<int>(c.lines.size())<rows-3)c.line();
    hr();
    wstring foot=s.editing?L"Type to filter  ·  Enter play  ·  Esc finish":L"Space pause/play   N/P skip   / search   H hotkeys   X quit";
    if(s.noticeUntil>GetTickCount64())foot=s.notice;
    c.line(L" "+label(fit(foot,w),s.noticeUntil>GetTickCount64()?copper:moss));
    if(static_cast<int>(c.lines.size())>rows)c.lines.resize(rows);
    return c.lines;
}

struct Terminal {
    HANDLE out=GetStdHandle(STD_OUTPUT_HANDLE),in=GetStdHandle(STD_INPUT_HANDLE);
    DWORD inMode{},outMode{};
    bool color=false;
    std::vector<wstring> previous;
    Terminal(){
        if(!GetConsoleMode(in,&inMode)||!GetConsoleMode(out,&outMode))
            throw std::runtime_error("Open GrooveDial.exe in a Windows terminal.");
        if(!SetConsoleMode(out,outMode|ENABLE_VIRTUAL_TERMINAL_PROCESSING))
            throw std::runtime_error("The deck needs Windows Terminal or a console with VT support.");
        DWORD mode=(inMode|ENABLE_WINDOW_INPUT|ENABLE_EXTENDED_FLAGS)&~(ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_INPUT|ENABLE_QUICK_EDIT_MODE|ENABLE_VIRTUAL_TERMINAL_INPUT);
        if(!SetConsoleMode(in,mode)){SetConsoleMode(out,outMode);throw std::runtime_error("Could not read terminal keys.");}
        color=!std::getenv("NO_COLOR");output(L"\x1b[?1049h\x1b[?25l\x1b[2J");
    }
    ~Terminal(){output(L"\x1b[0m\x1b[?25h\x1b[?1049l");SetConsoleMode(out,outMode);SetConsoleMode(in,inMode);}
    std::pair<int,int> size()const {
        CONSOLE_SCREEN_BUFFER_INFO v{}; if(!GetConsoleScreenBufferInfo(out,&v)) return {80,24};
        return {v.srWindow.Right-v.srWindow.Left+1,v.srWindow.Bottom-v.srWindow.Top+1};
    }
    void paint(const std::vector<wstring>& rows){
        wstring batch;
        for(size_t i=0;i<rows.size();++i) if(i>=previous.size()||rows[i]!=previous[i])
            batch+=L"\x1b["+std::to_wstring(i+1)+L";1H"+rows[i]+L"\x1b[K";
        if(rows.size()<previous.size())batch+=L"\x1b["+std::to_wstring(rows.size()+1)+L";1H\x1b[J";
        if(!batch.empty())output(batch); previous=rows;
    }
};
std::vector<Item> demo_library(){
    return {
        {L"Night Drive Vela Park",L"spotify:track:0000000000000000000001",L"Night Drive",L"Vela Park",L"track",224000},
        {L"Low Light The Afterhours",L"spotify:track:0000000000000000000002",L"Low Light",L"The Afterhours",L"track",201000},
        {L"Silverline North Channel",L"spotify:track:0000000000000000000003",L"Silverline",L"North Channel",L"track",257000},
        {L"Radio Silence Mira Vale",L"spotify:track:0000000000000000000004",L"Radio Silence",L"Mira Vale",L"track",192000},
        {L"Last Train Cinder FM",L"spotify:track:0000000000000000000005",L"Last Train",L"Cinder FM",L"track",232000},
        {L"After Midnight",L"spotify:playlist:0000000000000000000006",L"After Midnight",L"",L"playlist",0},
        {L"Night Routes",L"spotify:playlist:0000000000000000000007",L"Night Routes",L"",L"playlist",0}
    };
}
void seed(State& s,const std::vector<Item>& lib) {
    s.demo=s.logged=s.active=s.online=true;s.status=L"playing";s.track=lib.front();s.album=L"After Midnight";
    s.position=72000;s.anchor=wall_ms();s.speed=1;s.volume=65;
    for(const auto& item:lib)if(item.kind==L"track" && item.uri!=s.track.uri)s.queue.push_back(item);
}

int run(const wstring& startView,const std::vector<Item>& lib,Receiver* receiver,const fs::path& data){
    State s;Wire wire;Terminal terminal;Hotkeys hotkeys;
    s.hotkeyLabels=hotkeys.labels();
    if(!hotkeys.ready())s.message(L"A global shortcut is unavailable. Press H to change it.");
    if(!receiver)seed(s,lib);
    s.switch_to(startView==L"search"?View::Find:startView==L"queue"?View::Queue:
                startView==L"playlists"?View::Playlists:View::Library);s.filter(lib);
    ULONGLONG retry=0,nextRam=0;int lastW=0,lastH=0;
    bool initialActivation=false,initialResume=false,snapshotSeen=false;
    std::optional<wstring> pendingPlay;
    fs::path recent=user_profile()/L"last-track.txt";
    std::string remembered;
    if(receiver){
        std::ifstream(recent)>>remembered;
        auto value=wstring(winrt::to_hstring(remembered).c_str());
        if(normalize(value).empty())remembered.clear();
    }
    auto resumeUri=[&]()->wstring{
        if(!remembered.empty())return wstring(winrt::to_hstring(remembered).c_str());
        auto it=std::find_if(lib.begin(),lib.end(),[](const Item& item){return item.kind==L"track";});
        return it==lib.end()?L"":it->uri;
    };
    auto send=[&](const wstring& verb,const wstring& uri=L"",int vol=-1) {
        if(s.demo){
            if(verb==L"play" && !uri.empty()){
                auto it=std::find_if(lib.begin(),lib.end(),[&](const Item& item){return item.uri==uri;});
                if(it==lib.end()){s.message(L"Spotify links play in the connected deck.");return;}
                s.track=*it;s.position=0;s.anchor=wall_ms();
            }
            if(verb==L"skip_next"||verb==L"skip_prev"){
                auto it=std::find_if(lib.begin(),lib.end(),[&](const Item& i){return i.uri==s.track.uri;});
                auto n=static_cast<int>(lib.size());auto index=static_cast<int>(it-lib.begin());
                s.track=lib[(index+n+(verb==L"skip_next"?1:-1))%n];s.position=0;s.anchor=wall_ms();
            }else if(verb==L"play"||verb==L"pause"){
                s.position=static_cast<double>(s.elapsed());s.anchor=wall_ms();s.speed=verb==L"play"?1:0;s.status=verb==L"play"?L"playing":L"paused";
            }else if(verb==L"set_volume")s.volume=vol;
        }else if(!s.logged)s.message(L"Waiting for Spotify pairing. Select Groove Dial in the device picker.");
        else {
            if(verb==L"play" && !s.active){
                pendingPlay=uri;
                if(!wire.send(L"activate"))s.message(L"Reconnecting to Soloist…");
                else s.message(L"Activating Groove Dial for playback…");
                return;
            }
            if(!wire.send(verb,uri,vol<0?nullptr:L"volume",vol))s.message(L"Reconnecting to Soloist…");
        }
    };
    auto change=[&](View view) {
        s.switch_to(view);
        s.filter(lib);
        if(view==View::Queue && wire.live() && s.logged) wire.send(L"get_queue",L"",L"limit",80);
    };
    int previousShortcut=-1;ULONGLONG shortcutTime=0;
    auto trigger=[&](int slot){
        if(slot<0)return;
        ULONGLONG now=GetTickCount64();
        if(slot==previousShortcut && now-shortcutTime<180)return;
        previousShortcut=slot;shortcutTime=now;
        if(slot==0)send(s.status==L"playing"?L"pause":L"play");
        else send(slot==1?L"skip_prev":L"skip_next");
    };
    for(;;){
        if(GetTickCount64()>=nextRam){
            refresh_ram(s,receiver?receiver->pid:0);
            nextRam=GetTickCount64()+2000;
        }
        if(receiver){
            receiver->check();
            if(!wire.live()){
                if(s.online){s.position=static_cast<double>(s.elapsed());s.status=L"idle";s.speed=0;}
                s.online=s.logged=s.active=false;
                initialActivation=initialResume=snapshotSeen=false;
                pendingPlay.reset();
                if(GetTickCount64()>=retry){
                    s.online=wire.connect(data);retry=GetTickCount64()+2000;
                }
            }
            for(auto& json:wire.take()){
                try{
                    bool logged=s.logged;auto eventType=s.event(json);
                    if(eventType==L"playback_state")snapshotSeen=true;
                    if(!s.track.uri.empty() && s.track.uri!=wstring(winrt::to_hstring(remembered).c_str()) &&
                       !normalize(s.track.uri).empty()){
                        remembered=winrt::to_string(s.track.uri);
                        std::error_code ec;fs::create_directories(recent.parent_path(),ec);
                        std::ofstream(recent,std::ios::trunc)<<remembered;
                    }
                    if(s.logged && s.active && pendingPlay){
                        wire.send(L"play",*pendingPlay);
                        pendingPlay.reset();
                        initialResume=true;
                    }
                    if(!logged && s.logged){
                        wire.send(L"get_queue",L"",L"limit",80);
                        if(!initialActivation) {
                            initialActivation=true;
                            if(!s.active)wire.send(L"activate");
                        }
                    }
                    if(s.logged && s.active && snapshotSeen && !initialResume){
                        initialResume=true;
                        if(s.status==L"paused")wire.send(L"play");
                        else if(s.status==L"idle" && s.track.uri.empty()){
                            auto uri=resumeUri();
                            if(!uri.empty())wire.send(L"play",uri);
                        }
                    }
                }catch(const winrt::hresult_error&){s.message(L"Soloist sent unreadable playback data.");}
            }
        }
        MSG shortcut{};
        while(PeekMessageW(&shortcut,nullptr,WM_HOTKEY,WM_HOTKEY,PM_REMOVE))
            if(s.hotkeyCapture<0)trigger(hotkeys.posted(shortcut.wParam));
        auto [cols,rows]=terminal.size();
        if(cols!=lastW||rows!=lastH){terminal.previous.clear();lastW=cols;lastH=rows;}
        if(cols<31||rows<11)terminal.paint({fit(L"Enlarge terminal • X quits",std::max(1,cols-1))});
        else terminal.paint(render(s,lib,cols,rows,terminal.color));
        if(WaitForSingleObject(terminal.in,s.status==L"playing"?350:550)!=WAIT_OBJECT_0)continue;
        INPUT_RECORD input[96];DWORD n=0;
        if(!ReadConsoleInputW(terminal.in,input,96,&n))throw std::runtime_error("Could not read terminal input.");
        for(DWORD k=0;k<n;k++){
            if(input[k].EventType!=KEY_EVENT||!input[k].Event.KeyEvent.bKeyDown)continue;
            auto key=input[k].Event.KeyEvent;WORD vk=key.wVirtualKeyCode;wchar_t ch=key.uChar.UnicodeChar;
            bool ctrl=(key.dwControlKeyState&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))!=0;
            if(ctrl && vk=='C')return 0;
            if(s.hotkeySettings){
                if(s.hotkeyCapture>=0){
                    if(vk==VK_ESCAPE){
                        hotkeys.resume();s.hotkeyCapture=-1;s.hotkeyLabels=hotkeys.labels();
                    }else if(vk>=VK_F1 && vk<=VK_F11){
                        bool changed=hotkeys.assign(s.hotkeyCapture,Hotkeys::pressed(vk,key.dwControlKeyState));
                        if(!changed)hotkeys.resume();
                        s.hotkeyCapture=-1;s.hotkeyLabels=hotkeys.labels();
                        s.message(changed?L"Global shortcut saved.":L"Shortcut unavailable or already assigned. Choose another.");
                    }
                }else if(vk==VK_ESCAPE || ch==L'h' || ch==L'H')s.hotkeySettings=false;
                else if(ch>=L'1' && ch<=L'3'){
                    s.hotkeyCapture=ch-L'1';hotkeys.suspend();s.hotkeyLabels=hotkeys.labels();
                }else if(ch==L'4'){
                    bool restored=hotkeys.reset();s.hotkeyLabels=hotkeys.labels();
                    s.message(restored?L"Default F1 / F2 / F3 shortcuts restored.":L"A default key is in use. Keep your current shortcuts.");
                }
                continue;
            }
            if(s.help){if(vk==VK_ESCAPE||ch==L'?')s.help=false;continue;}
            int slot=hotkeys.local(vk,key.dwControlKeyState);
            if(slot>=0){trigger(slot);continue;}
            int total=count(s,lib),page=std::max(1,static_cast<int>(terminal.previous.size())-(rows>=24&&cols>=57?17:10));
            const int repeat=std::max(1,static_cast<int>(key.wRepeatCount));
            if(vk==VK_UP||vk==VK_DOWN||vk==VK_PRIOR||vk==VK_NEXT){
                int step=vk==VK_UP?-1:vk==VK_DOWN?1:vk==VK_PRIOR?-page:page;
                s.selection=std::clamp(s.selection+step*repeat,0,std::max(0,total-1));continue;
            }
            if(vk==VK_TAB){change(static_cast<View>((static_cast<int>(s.view)+((key.dwControlKeyState&SHIFT_PRESSED)?3:1))%4));continue;}
            if(vk==VK_RETURN){
                wstring uri=s.view!=View::Queue?normalize(s.query):L"";
                if(uri.empty() && s.selection<total)uri=at(s,lib,s.selection).uri;
                if(!uri.empty()){send(L"play",uri);s.editing=false;}continue;
            }
            if(s.editing){
                if(vk==VK_ESCAPE){s.editing=false;continue;}
                if(vk==VK_BACK && !s.query.empty()){
                    wchar_t tail=s.query.back();s.query.pop_back();
                    if(tail>=0xdc00 && tail<=0xdfff && !s.query.empty() && s.query.back()>=0xd800 && s.query.back()<=0xdbff)s.query.pop_back();
                }else if(ctrl && vk=='U')s.query.clear();
                else if(ch>=32 && ch!=127 && s.query.size()<2048)s.query.append(std::min<size_t>(repeat,2048-s.query.size()),ch);
                else continue;
                s.selection=s.offset=0;s.filter(lib);continue;
            }
            if(ch==L'x'||ch==L'X'||ch==L'q'||vk==VK_ESCAPE)return 0;
            if(ch==L'h'||ch==L'H'){s.hotkeySettings=true;continue;}
            if(ch==L'1')change(View::Library);
            else if(ch==L'/' || (ctrl && vk=='F')){
                if(s.view==View::Queue)change(View::Library);
                s.editing=true;
            }
            else if(ch==L'2')change(View::Find);
            else if(ch==L'3')change(View::Queue);
            else if(ch==L'4')change(View::Playlists);
            else if(ch==L' ')send(s.status==L"playing"?L"pause":L"play");
            else if(ch==L'n'||ch==L'N')send(L"skip_next");
            else if(ch==L'p'||ch==L'P')send(L"skip_prev");
            else if((ch==L'-'||ch==L'+'||ch==L'=') && s.volume>=0)send(L"set_volume",L"",std::clamp(s.volume+(ch==L'-'?-5:5),0,100));
            else if(ch==L'?')s.help=true;
            else if(ch==L'v'||ch==L'V'){
                if(s.demo){s.message(L"Audio check is available during real playback.");continue;}
                s.message(L"Checking the Soloist audio session…");terminal.paint(render(s,lib,cols,rows,terminal.color));
                try{s.message(soundcheck(receiver->pid,false)?L"✓  Audio signal detected from Spotify Soloist.":L"No signal detected. Try during a playing track.");}
                catch(const std::exception&){s.message(L"Windows audio endpoints are unavailable.");}
            }
        }
    }
}
} // namespace deck
