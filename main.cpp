#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include <lua5.2/lua.hpp>

#include <vector>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <memory>
#include <cstdlib>

struct Script
{
    Script()
    {
        mState = luaL_newstate();
        luaL_openlibs(mState);
    }

    ~Script()
    {
        Stop();
    }

    void Stop()
    {
        lua_close(mState);
    }

    void Execute(const std::string& filename)
    {
        if (luaL_loadfile(mState, filename.c_str()) != LUA_OK)
            throw std::runtime_error("error while loading lua file: " + GetLastError());

        lua_pushlightuserdata(mState, this);
        lua_pushcclosure(mState, &Script::SubscribeHelper, 1);
        lua_setglobal(mState, "subscribe");

        if (lua_pcall(mState, 0, LUA_MULTRET, 0) != LUA_OK)
            throw std::runtime_error("error while running lua script: " + GetLastError());
    }

    void Callback()
    {
        // lua_getglobal(mState, "hello");

        lua_rawgeti(mState, LUA_REGISTRYINDEX, mCallback);
        lua_pushnumber(mState, 1);

        if (lua_pcall(mState, 1, 1, 0 ) != 0)
            throw std::runtime_error("failed to call callback: " + GetLastError());
    }

private:
    int Subscribe(lua_State* L)
    {
        mCallback = luaL_ref(L, LUA_REGISTRYINDEX);
        std::cout << "subscribe has been called with " << mCallback << std::endl;

        return 0;
    }

    static int SubscribeHelper(lua_State* L)
    {
        Script* script = (Script*)lua_touserdata(L, lua_upvalueindex(1));
        return script->Subscribe(L);
    }

    std::string GetLastError()
    {
        std::string message = lua_tostring(mState, -1);
        lua_pop(mState, 1);
        return message;
    }

    lua_State* mState;
    int mCallback;
};

struct WebSocketServer
{
    using server = websocketpp::server<websocketpp::config::asio>;
    using message_ptr = server::message_ptr;

    void Listen(int port)
    {
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;
        using websocketpp::lib::bind;

        // Disable all logging
        mServer.clear_access_channels(websocketpp::log::alevel::all);
        mServer.clear_error_channels(websocketpp::log::alevel::all);

        mServer.init_asio();
        mServer.set_message_handler(bind(&WebSocketServer::OnMessage, this, _1, _2));
        mServer.set_close_handler(bind(&WebSocketServer::OnClose, this, _1));
        mServer.set_open_handler(bind(&WebSocketServer::OnNewClient, this, _1));

        websocketpp::lib::error_code err;
        mServer.listen(websocketpp::lib::asio::ip::tcp::v4(), port, err);

        if (err)
            throw std::runtime_error(err.message());

        mServer.start_accept();
        mServer.poll();
    }

    void Poll()
    {
        auto sz = mServer.poll();

        if (sz > 0)
            std::cout << sz << " handlers processed" << std::endl;
    }

    void OnNewClient(websocketpp::connection_hdl hdl)
    {
        auto& view = boost::multi_index::get<ByHandler>(mConnections);
        auto it = view.find(hdl);

        assert(it == view.end());

        connection newClient;
        newClient.hdl = hdl;

        static int count = 1;
        newClient.name = "fx_" + std::to_string(count++);

        mConnections.insert(newClient);

        std::cout << "new client " << newClient.name << ", clients = " << mConnections.size() << std::endl;
    }

    void OnMessage(websocketpp::connection_hdl hdl, message_ptr msg)
    {
        auto& view = boost::multi_index::get<ByHandler>(mConnections);
        auto it = view.find(hdl);

        assert(it != view.end());
        const_cast<connection&>(*it).messages.emplace_back(msg->get_payload());

        std::cout << "rcv message from " << it->name << ", sending answer..." << std::endl;

        mServer.send(hdl, "{\"fx_underlyings\": [\"FOO\", \"BAR\"], \"otc_underlyings\": []}", websocketpp::frame::opcode::text);
    }

    void OnClose(websocketpp::connection_hdl hdl)
    {
        auto& view = boost::multi_index::get<ByHandler>(mConnections);
        auto it = view.find(hdl);

        assert(it != view.end());

        std::cout << "[OnClose] connection with client " << it->name << " closed." << std::endl;
        mConnections.erase(it);
        std::cout << "[OnClose] remaining clients = " << mConnections.size() << std::endl;
    }

    void Close(const std::string& name)
    {
        auto& view = boost::multi_index::get<ByName>(mConnections);
        auto it = view.lower_bound(name);
        auto itEnd = view.upper_bound(name);

        if (it == view.end())
        {
            std::cout << "cant find any connection with name " << name << std::endl;
            return;
        }

        for (; it != itEnd; ++it)
        {
            std::cout << "[Close] closing connection of client " << it->name << std::endl;

            auto con = mServer.get_con_from_hdl(it->hdl);

            if (con->get_state() == websocketpp::session::state::open)
            {
                mServer.close(it->hdl, 0, "bye");
            }
            else
            {
                std::cout << "[Close] cant close ; state = " << con->get_state() << std::endl;
            }
        }
    }

private:
    server mServer;

    struct connection
    {
        websocketpp::connection_hdl hdl;
        std::string name;

        std::vector<std::string> messages;
    };

    using connections = boost::multi_index_container<
        connection,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<BOOST_MULTI_INDEX_MEMBER(connection, websocketpp::connection_hdl, hdl), std::owner_less<websocketpp::connection_hdl>>,
            boost::multi_index::ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(connection, std::string, name)>
        >>;

    enum
    {
        ByHandler = 0,
        ByName = 1,
    };

    connections mConnections;
};

int main(int argc, char** argv)
{
    if (argc != 3)
        throw std::runtime_error("usage: " + std::string(argv[0]) + " <port> <lua_script>");

    int port = std::atoi(argv[1]);

    Script script;
    script.Execute(argv[2]);
    script.Callback();
    script.Callback();
    script.Callback();

    std::cout << "running " << argv[2] << "..." << std::endl;

    WebSocketServer server;
    server.Listen(port);

    auto start = std::chrono::system_clock::now();

    // simulate an event loop that polls each ms
    while (1)
    {
        server.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (std::chrono::system_clock::now() > start + std::chrono::seconds(10))
            server.Close("fx_2");
    }

    script.Stop();

    return 0;
}
