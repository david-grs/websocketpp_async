#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <vector>
#include <chrono>
#include <iostream>
#include <thread>
#include <memory>
#include <cstdlib>

struct WebSocketServer
{
    using server = websocketpp::server<websocketpp::config::asio>;
    using message_ptr = server::message_ptr;

    std::vector<websocketpp::connection_hdl> m_connections;

    void Listen(int port)
    {
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;
        using websocketpp::lib::bind;

        mServer.init_asio();
        mServer.set_message_handler(bind(&WebSocketServer::OnMessage, this, _1, _2));

        websocketpp::lib::error_code err;
        mServer.listen(websocketpp::lib::asio::ip::tcp::v4(), port, err);

        if (err)
            throw std::runtime_error(err.message());

        mServer.start_accept();
        mServer.poll();
    }

    void Send()
    {
        if (m_connections.empty())
            return;

        auto& hdl = m_connections.back();
        m_connections.pop_back();

        mServer.send(hdl, "{\"fx_underlyings\": [\"FOO\", \"BAR\"], \"otc_underlyings\": []}", websocketpp::frame::opcode::text);
        mServer.close(hdl, 0, "bye");
    }

    void Poll()
    {
        std::cout << "start polling..." << std::endl;
        auto sz = mServer.poll();
        std::cout << "poll done, " << sz << " handlers processed" << std::endl;
    }

    void OnMessage(websocketpp::connection_hdl hdl, message_ptr msg)
    {
        std::cout << "on_message called with hdl: " << hdl.lock().get() << " and message: " << msg->get_payload() << std::endl;
        m_connections.emplace_back(hdl);
    }

private:
    server mServer;
};

int main(int argc, char** argv)
{
    if (argc != 2)
        throw std::runtime_error("usage: " + std::string(argv[0]) + " <port>");

    int port = std::atoi(argv[1]);

    WebSocketServer server;
    server.Listen(port);

    // simulating a busy event loop that polls
    while (1)
    {
        server.Poll();
        server.Send();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
