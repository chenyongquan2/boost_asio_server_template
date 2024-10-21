#include "server.h"
#include "asio/buffer.hpp"
#include "asio/error.hpp"
#include "asio/write.hpp"
#include "spdlog/spdlog.h"
#include <system_error>


Session::Session(asio::io_context& io_context, const std::string& session_id)
    :session_id_(session_id)
    ,io_context_(io_context)
    ,socket_(io_context)//socket没有默认构造函数
{

}

void Session::start(asio::ip::tcp::socket socket)
{
    //socket is not copyable instead of movable
    socket_ = std::move(socket);
    do_read();
}

void Session::do_read()
{
    auto self(shared_from_this());
    auto buf = asio::buffer(data_,max_length);
    //Todo:这里捕获this是为了在lambda内部方便使用this里的成员变量
    //Todo:技巧 这里捕获智能指针，是为了延长生命周期

    socket_.async_read_some(buf,[this,self](std::error_code ec,std::size_t length){
        if(ec)
        {
            SPDLOG_ERROR("read error: {} ({})", ec.message(), ec.value());
            handle_error(ec);
            return;
        }
        else
        {
            SPDLOG_INFO("Received {} bytes", length);
            // 可以在这里打印接收到的数据
            std::string received_data(data_,length);
            SPDLOG_INFO("Received data: {}", received_data);
            do_write(length);
        }
    });
}

void Session::do_write(std::size_t length)
{
    auto self(shared_from_this());
    auto buf = asio::buffer(data_,length);
    asio::async_write(socket_,buf,[this,self](std::error_code ec,std::size_t length){
        if(ec)
        {
            SPDLOG_ERROR("write error: {} ({})", ec.message(), ec.value());
            handle_error(ec);
            return;
        }
        else
        {
            SPDLOG_INFO("Send {} bytes", length);
            do_read();
        }

    });
}

void Session::handle_error(const std::error_code& ec) 
{
    if(ec == asio::error::eof || ec == asio::error::connection_reset)
    {
        SPDLOG_INFO("Client closed the connection, error: {}", ec.message());
    } 
    else 
    {
        SPDLOG_ERROR("error: {} ({})", ec.message(), ec.value());
    }

    // 关闭socket
    asio::error_code ignored_ec;
    socket_.close(ignored_ec);
    SPDLOG_INFO("Error occured, Close socket");
}

Server::Server(asio::io_context& io_context, short port)
    //boost::asio::ip::tcp::v4() 是一个用于指定 IPv4 TCP 协议的便捷函数，它在创建 socket、endpoint 或进行网络操作时非常有用
    :acceptor_(io_context,asio::ip::tcp::endpoint(asio::ip::tcp::v4(),port)),
    io_context_(io_context)
{
    do_accept();
}

void Server::do_accept()
{
    static size_t sessionId = 0;
    acceptor_.async_accept([this](std::error_code ec,asio::ip::tcp::socket socket){
        if(ec)
        {
            SPDLOG_ERROR("accept error:{}",ec.message());
        }
        else
        {
            sessionId++;
            auto strSessionId = std::to_string(sessionId);
            
            auto newSession = find_or_create_session(strSessionId);
            //Todo:这里必须得调用std::move(socket),因为socket是不支持拷贝构造的，只能移动构造
            newSession->start(std::move(socket));

            //技巧，链式调用
            do_accept();
        }
    });

}

std::shared_ptr<Session> Server::find_or_create_session(const std::string& session_id) {
    auto it = sessions_.find(session_id);
    if(it != sessions_.end())
    {
        // 找到现有会话，可能需要更新一些状态
        SPDLOG_INFO("Reconnected session: {}",session_id);
        return it->second;
    }
    else
    {
        // 创建新会话
        auto new_session = std::make_shared<Session>(io_context_, session_id);
        sessions_[session_id] = new_session;
        SPDLOG_INFO("New session created: {}",session_id);
        return new_session;
    }
}

