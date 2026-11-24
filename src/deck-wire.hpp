// One loopback connection carries live state and controls. The reader never touches the console.
struct HttpHandle {
    HINTERNET h{};
    HttpHandle()=default;
    explicit HttpHandle(HINTERNET value):h(value){}
    ~HttpHandle(){ if(h) WinHttpCloseHandle(h); }
    HttpHandle(const HttpHandle&)=delete;
    HttpHandle& operator=(const HttpHandle&)=delete;
};
class Wire {
    HttpHandle session, connection;
    HINTERNET socket{};
    std::thread listener;
    std::mutex guard;
    std::deque<std::string> messages;
    std::atomic_bool ended{false};
    void receive(HINTERNET ws) {
        std::string msg;
        char buf[8192];
        for (;;) {
            DWORD n=0; WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            if(WinHttpWebSocketReceive(ws,buf,sizeof(buf),&n,&type) || type==WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
            if(type!=WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE && type!=WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) break;
            if(msg.size()+n>4*1024*1024) break;
            msg.append(buf,n);
            if(type==WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) {
                std::lock_guard lock(guard);
                if(messages.size()>=512) break;
                messages.push_back(std::move(msg)); msg.clear();
            }
        }
        ended=true;
    }
public:
    Wire()=default;
    ~Wire(){ disconnect(); }
    void disconnect() {
        if(socket) {
            if(!ended.load()) WinHttpWebSocketShutdown(socket,WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS,nullptr,0);
            WinHttpCloseHandle(socket); socket=nullptr;
        }
        if(listener.joinable()) listener.join();
        if(connection.h){ WinHttpCloseHandle(connection.h); connection.h=nullptr; }
        if(session.h){ WinHttpCloseHandle(session.h); session.h=nullptr; }
        std::lock_guard lock(guard); messages.clear();
    }
    bool live()const {return socket && !ended.load();}
    bool connect(const fs::path& data) {
        disconnect(); ended=false;
        unsigned port=0; std::ifstream(data/L"ws.port")>>port;
        if(!port || port>65535) return false;
        session.h=WinHttpOpen(L"GrooveDial",WINHTTP_ACCESS_TYPE_NO_PROXY,nullptr,nullptr,0);
        if(!session.h) return false;
        WinHttpSetTimeouts(session.h,800,800,1200,2000);
        connection.h=WinHttpConnect(session.h,L"127.0.0.1",static_cast<INTERNET_PORT>(port),0);
        if(!connection.h) return false;
        HttpHandle request(WinHttpOpenRequest(connection.h,L"GET",L"/",nullptr,nullptr,nullptr,0));
        if(!request.h || !WinHttpSetOption(request.h,WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,nullptr,0) ||
            !WinHttpSendRequest(request.h,nullptr,0,nullptr,0,0,0) || !WinHttpReceiveResponse(request.h,nullptr)) return false;
        socket=WinHttpWebSocketCompleteUpgrade(request.h,0);
        if(!socket) return false;
        // A paused player may stay silent indefinitely.
        DWORD noTimeout=0; WinHttpSetOption(socket,WINHTTP_OPTION_RECEIVE_TIMEOUT,&noTimeout,sizeof(noTimeout));
        listener=std::thread([this,ws=socket] {receive(ws);});
        return true;
    }
    std::deque<std::string> take() {
        std::lock_guard lock(guard);
        std::deque<std::string> result; messages.swap(result); return result;
    }
    bool send(const wstring& cmd,const wstring& uri={},const wchar_t* field=nullptr,int value=0) {
        if(!live()) return false;
        JsonObject obj;
        obj.SetNamedValue(L"type",JsonValue::CreateStringValue(L"command"));
        obj.SetNamedValue(L"command",JsonValue::CreateStringValue(cmd));
        if(!uri.empty()) obj.SetNamedValue(L"uri",JsonValue::CreateStringValue(uri));
        if(field) obj.SetNamedValue(field,JsonValue::CreateNumberValue(value));
        auto json=winrt::to_string(obj.Stringify());
        if(!WinHttpWebSocketSend(socket,WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,json.data(),static_cast<DWORD>(json.size()))) return true;
        ended=true; return false;
    }
};
