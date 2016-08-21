#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>


#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/member.hpp>

#include <vector>
#include <chrono>
#include <iostream>
#include <algorithm>
#include <memory>
#include <cstdlib>

struct WebSocketServer
{
    using server = websocketpp::server<websocketpp::config::asio>;
    using message_ptr = server::message_ptr;

    void Listen(int port)
    {
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;
        using websocketpp::lib::bind;

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

        assert(it == mConnections.end());

        connection newClient;
        newClient.hdl = hdl;

        static int count = 1;
        newClient.name = "fx_" + std::to_string(count++);

        std::cout << "new client: hdl: " << hdl.lock().get() << ", " << newClient.name << std::endl;

        mConnections.insert(newClient);
    }

    void OnMessage(websocketpp::connection_hdl hdl, message_ptr msg)
    {
        std::cout << "on_message called with hdl: " << hdl.lock().get() << " and message: " << msg->get_payload() << std::endl;

        auto& view = boost::multi_index::get<ByHandler>(mConnections);
        auto it = view.find(hdl);

        assert(it != mConnections.end());
        const_cast<connection&>(*it).messages.emplace_back(msg->get_payload());

        std::cout << "send message back to " << it->name << std::endl;

        mServer.send(hdl, "{\"fx_underlyings\": [\"FOO\", \"BAR\"], \"otc_underlyings\": []}", websocketpp::frame::opcode::text);
        //mServer.close(hdl, 0, "bye");
    }

    void OnClose(websocketpp::connection_hdl hdl)
    {
        std::cout << "on_close called with hdl: " << hdl.lock().get() << std::endl;

        auto& view = boost::multi_index::get<ByHandler>(mConnections);
        auto it = view.find(hdl);

        assert(it != mConnections.end());
        mConnections.erase(it);
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
            boost::multi_index::ordered_non_unique<BOOST_MULTI_INDEX_MEMBER(connection, websocketpp::connection_hdl, hdl), std::owner_less<websocketpp::connection_hdl>>,
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
    if (argc != 2)
        throw std::runtime_error("usage: " + std::string(argv[0]) + " <port>");

    int port = std::atoi(argv[1]);

    WebSocketServer server;
    server.Listen(port);

    // simulate an event loop that polls each ms
    while (1)
    {
        server.Poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
