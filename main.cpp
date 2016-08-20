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

    void OnMessage(websocketpp::connection_hdl hdl, message_ptr msg)
    {
        std::cout << "on_message called with hdl: " << hdl.lock().get() << " and message: " << msg->get_payload()
                  << std::endl;

        m_connections.emplace_back(std::move(hdl));
    }

    void Run()
    {
        mThread->join();
    }

    void Send()
    {
        if (m_connections.empty())
            return;
        auto& hdl = m_connections.back();
        m_connections.pop_back();

        mServer.send(hdl, "{\"fx_underlyings\": [], \"otc_underlyings\": []}", websocketpp::frame::opcode::text);
        mServer.close(hdl, 0, "bye");
    }

    void Listen(int port)
    {
        using websocketpp::lib::placeholders::_1;
        using websocketpp::lib::placeholders::_2;
        using websocketpp::lib::bind;

        mThread.reset(new std::thread([&]()
                                      {
                                          mServer.init_asio();
                                          mServer.set_message_handler(bind(&WebSocketServer::OnMessage, this, _1, _2));
                                          mServer.listen(websocketpp::lib::asio::ip::tcp::v4(), port);
                                          mServer.start_accept();
                                          mServer.poll_one();
                                      }));
    }

private:
    server mServer;
    std::unique_ptr<std::thread> mThread;
};

int main(int argc, char** argv)
{
    if (argc != 2)
        throw std::runtime_error("usage: " + std::string(argv[0]) + " <port>");

    int port = std::atoi(argv[1]);

    WebSocketServer server;
    server.Listen(port);

    while (1)
    {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        server.Send();
    }

    server.Run();
    return 0;
}
