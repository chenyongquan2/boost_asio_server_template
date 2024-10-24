#pragma once

#include "asio/io_context.hpp"
#include "asio/steady_timer.hpp"
#include <asio.hpp>
#include <memory>
#include <unordered_map>

constexpr int max_length = 1024;

class Session: public std::enable_shared_from_this<Session>
{
public:
    Session(asio::io_context& io_context, const std::string& session_id);
    void start(asio::ip::tcp::socket socket);

    std::string get_session_id() const
    {
        return session_id_;
    }

    bool is_connected() const;
    void handle_disconnect();

private:
    void do_read();
    void do_write(const std::string& data);
    void process_data(const std::string& data);
    void handle_error(const std::error_code& ec);

    void start_inactivity_timer();
    
private:
    asio::ip::tcp::socket socket_;
    asio::steady_timer inactivity_timer_;//不活动计时器功能

    char data_[max_length];
    //std::string outbound_data_;//输出缓冲区

    std::string session_id_;//唯一标识会话
    asio::io_context& io_context_;
};


class Server
{
public:
    Server(asio::io_context& io_context, short port);

private:
    void do_accept();

    //用于服务端支持客户端重连
    std::shared_ptr<Session> find_or_create_session(const std::string& session_id);//用于处理新连接或重连。

private:
    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;

    std::unordered_map<std::string,std::shared_ptr<Session>> sessions_;
};