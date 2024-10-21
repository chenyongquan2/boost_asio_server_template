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
    //Todo:技巧 使用 shared_from_this() 来确保 Session 对象在异步操作完成之前不会被销毁

    socket_.async_read_some(buf,[this,self](std::error_code ec,std::size_t length){
        if(ec)
        {
            SPDLOG_ERROR("read error: {} ({})", ec.message(), ec.value());
            handle_error(ec);
            return;
        }
        else
        {
            std::string received_data(data_,length);
            SPDLOG_INFO("Received data: {},bytes:{}", received_data, length);
            process_data(received_data);
        }
    });
}

void Session::do_write(const std::string& data)
{
    auto self(shared_from_this());
    //在异步写操作注意发送数据的生命周期，确保 data 在异步操作(因为可能会多次底层写操作，尤其当data很大的情况下)完成之前不会被销毁
    //方法1: session的成员变量保持要发送的数据,适合数据量大，分段发送，错误重试等情况
    //outbound_data_ = std::move(data);
    //auto buf = asio::buffer(outbound_data_.data(),outbound_data_.size());
    //auto buf = asio::buffer(outbound_data_);

    //方法2: 使用lambda值捕获data(lambda里面有每个data的副本)，适合数据量小，一次性发送的情况
    asio::async_write(socket_,asio::buffer(data),[this,self,data](std::error_code ec,std::size_t length){
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

void Session::process_data(const std::string& data)
{
    do_write(data);
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
    SPDLOG_INFO("Session:{} Error occured, Close socket", session_id_);
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
            //Todo:实现一个机制来从客户端的初始消息中提取或生成 session_id
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
        // 用于实现"服务端支持客户端重连";
        SPDLOG_INFO("Reconnected session: {}",session_id);
        //Todo:找到现有会话，可能需要更新一些状态
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

