#pragma once

#include "asio/io_context.hpp"
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

private:
    void do_read();
    void do_write(std::size_t length);

private:
    asio::ip::tcp::socket socket_;
    char data_[max_length];

    std::string session_id_;
    asio::io_context& io_context_;
};


class Server
{
public:
    Server(asio::io_context& io_context, short port);

private:
    void do_accept();

    std::shared_ptr<Session> find_or_create_session(const std::string& session_id);

private:
    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::unordered_map<std::string,std::shared_ptr<Session>> sessions_;
};