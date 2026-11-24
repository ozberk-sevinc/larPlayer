// Windows posts registered global shortcuts to the deck thread even when a game has focus.
#pragma comment(lib,"user32.lib")
struct Binding { UINT vk, modifiers; };

class Hotkeys {
    static constexpr std::array<Binding,3> defaults{{{VK_F1,0},{VK_F2,0},{VK_F3,0}}};
    static constexpr std::array<UINT,3> media{{VK_MEDIA_PLAY_PAUSE,VK_MEDIA_PREV_TRACK,VK_MEDIA_NEXT_TRACK}};
    static constexpr int firstId=0x200;
    std::array<Binding,3> bindings=defaults;
    std::array<bool,6> registered{};
    fs::path file=user_profile()/L"hotkeys.txt";
    bool suspended=false;
    static bool valid(const Binding& binding) {
        return binding.vk>=VK_F1 && binding.vk<=VK_F11 &&
               !(binding.modifiers&~(MOD_ALT|MOD_CONTROL|MOD_SHIFT|MOD_WIN));
    }
    void load() {
        std::ifstream input(file);
        std::array<Binding,3> saved{};
        for(auto& binding:saved) if(!(input>>binding.vk>>binding.modifiers) || !valid(binding))return;
        for(size_t i=0;i<saved.size();++i)
            for(size_t j=0;j<i;++j)
                if(saved[i].vk==saved[j].vk && saved[i].modifiers==saved[j].modifiers)return;
        bindings=saved;
    }
    bool save() const {
        std::error_code ec;fs::create_directories(file.parent_path(),ec);
        if(ec)return false;
        auto temp=file;temp+=L".tmp";
        std::ofstream output(temp,std::ios::trunc);
        if(!output)return false;
        for(auto binding:bindings)output<<binding.vk<<' '<<binding.modifiers<<'\n';
        output.flush();
        bool good=output.good();output.close();
        if(good && MoveFileExW(temp.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return true;
        fs::remove(temp,ec);return false;
    }
public:
    Hotkeys(){load();resume();}
    ~Hotkeys(){suspend();}
    Hotkeys(const Hotkeys&)=delete;
    Hotkeys& operator=(const Hotkeys&)=delete;
    void suspend() {
        suspended=true;
        for(size_t i=0;i<registered.size();++i){
            if(registered[i])UnregisterHotKey(nullptr,firstId+static_cast<int>(i));
            registered[i]=false;
        }
        MSG msg{};
        while(PeekMessageW(&msg,nullptr,WM_HOTKEY,WM_HOTKEY,PM_REMOVE)){}
    }
    void resume() {
        if(!suspended)suspend();
        suspended=false;
        for(size_t i=0;i<bindings.size();++i)
            registered[i]=RegisterHotKey(nullptr,firstId+static_cast<int>(i),
                                          bindings[i].modifiers|MOD_NOREPEAT,bindings[i].vk)!=FALSE;
        for(size_t i=0;i<media.size();++i)
            registered[3+i]=RegisterHotKey(nullptr,firstId+3+static_cast<int>(i),
                                            MOD_NOREPEAT,media[i])!=FALSE;
    }
    bool ready() const {return registered[0] && registered[1] && registered[2];}
    std::array<wstring,3> labels()const{
        std::array<wstring,3> result;
        for(size_t i=0;i<bindings.size();++i){
            const auto b=bindings[i];
            if(b.modifiers&MOD_CONTROL)result[i]+=L"Ctrl+";
            if(b.modifiers&MOD_ALT)result[i]+=L"Alt+";
            if(b.modifiers&MOD_SHIFT)result[i]+=L"Shift+";
            if(b.modifiers&MOD_WIN)result[i]+=L"Win+";
            result[i]+=L"F"+std::to_wstring(b.vk-VK_F1+1);
            if(!registered[i] && !suspended)result[i]+=L"  unavailable";
        }
        return result;
    }
    bool assign(int index,Binding next) {
        if(index<0 || index>=3 || !valid(next))return false;
        for(int i=0;i<3;++i)
            if(i!=index && next.vk==bindings[i].vk && next.modifiers==bindings[i].modifiers)return false;
        auto old=bindings[index];
        suspend(); bindings[index]=next; resume();
        if(!registered[index] || !save()){suspend();bindings[index]=old;resume();return false;}
        return true;
    }
    bool reset() {
        auto old=bindings;
        suspend();bindings=defaults;resume();
        if(!ready() || !save()){suspend();bindings=old;resume();return false;}
        return true;
    }
    int posted(WPARAM id)const{
        if(id<firstId || id>=firstId+6)return -1;
        int index=static_cast<int>(id)-firstId;
        return registered[index]?index%3:-1;
    }
    int local(WORD vk,DWORD controlState)const{
        for(size_t i=0;i<media.size();++i)if(vk==media[i])return static_cast<int>(i);
        UINT held=0;
        if(controlState&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))held|=MOD_CONTROL;
        if(controlState&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))held|=MOD_ALT;
        if(controlState&SHIFT_PRESSED)held|=MOD_SHIFT;
        if(GetAsyncKeyState(VK_LWIN)&0x8000 || GetAsyncKeyState(VK_RWIN)&0x8000)held|=MOD_WIN;
        for(size_t i=0;i<bindings.size();++i)
            if(vk==bindings[i].vk && held==bindings[i].modifiers)return static_cast<int>(i);
        return -1;
    }
    static Binding pressed(WORD vk,DWORD controlState){
        UINT mods=0;
        if(controlState&(LEFT_CTRL_PRESSED|RIGHT_CTRL_PRESSED))mods|=MOD_CONTROL;
        if(controlState&(LEFT_ALT_PRESSED|RIGHT_ALT_PRESSED))mods|=MOD_ALT;
        if(controlState&SHIFT_PRESSED)mods|=MOD_SHIFT;
        if(GetAsyncKeyState(VK_LWIN)&0x8000 || GetAsyncKeyState(VK_RWIN)&0x8000)mods|=MOD_WIN;
        return {vk,mods};
    }
};
