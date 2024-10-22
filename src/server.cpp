#include "server.h"
#include "asio/buffer.hpp"
#include "asio/error.hpp"
#include "asio/error_code.hpp"
#include "asio/write.hpp"
#include "spdlog/spdlog.h"
#include <chrono>
#include <system_error>


Session::Session(asio::io_context& io_context, const std::string& session_id)
    :session_id_(session_id)
    ,io_context_(io_context)
    ,socket_(io_context)//socket没有默认构造函数
    ,inactivity_timer_(io_context)
{

}

void Session::start(asio::ip::tcp::socket socket)
{
    //socket is not copyable instead of movable
    socket_ = std::move(socket);

    start_inactivity_timer();// 启动不活动计时器

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
            //当客户端断开连接时，服务器能够检测到并相应地更新会话状态
            //在 do_read 和 do_write 中，如果遇到错误（可能是连接断开），会调用 handle_disconnect
            SPDLOG_ERROR("read error: {} ({})", ec.message(), ec.value());
            handle_error(ec);
            return;
        }
        else
        {
            std::string received_data(data_,length);
            SPDLOG_INFO("Received data: {},bytes:{}", received_data, length);
            process_data(received_data);

            start_inactivity_timer(); // 重置不活动计时器
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
            //当客户端断开连接时，服务器能够检测到并相应地更新会话状态
            //在 do_read 和 do_write 中，如果遇到错误（可能是连接断开），会调用 handle_disconnect
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

bool Session::is_connected() const 
{
    return socket_.is_open();
}

void Session::handle_disconnect() 
{
    // 关闭socket,以此释放资源
    socket_.close();
    inactivity_timer_.cancel();//取消定时器
    //Todo: 这里可以添加一些清理工作，但保留会话状态以便重连
    SPDLOG_INFO("Session:{} Close socket", session_id_);
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
   
    handle_disconnect();
}

void Session::start_inactivity_timer() 
{
    auto self(shared_from_this());

    // 取消任何现有的定时器操作
    inactivity_timer_.cancel();

    //Todo:加上一个开关来控制,且时间可配置
    constexpr size_t sec = 30;
    // 设置新的过期时间（30秒后）
    inactivity_timer_.expires_after(std::chrono::seconds(sec));
    //正确写法
    inactivity_timer_.async_wait([this,self](const std::error_code& ec){
        //async_wait里面的可调用函数签名一定得是void (asio::error_code) 
        //ok的写法 (const std::error_code& ec) (std::error_code ec)
        //错误写法(asio::error_code& ec)
        if(!ec)
        {
            SPDLOG_INFO("Session:{} imed out due to inactivity, then close it",session_id_);
            handle_disconnect();
        }
    });
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
            if(newSession->is_connected())
            {
                //当客户端重新连接时，服务器可以找到现有的会话，断开旧的连接（如果还存在），并使用新的 socket 重新启动会话
                SPDLOG_INFO( "Session:{} is reconnecting", strSessionId);
                newSession->handle_disconnect();//断开旧连接
            }

            //Todo:这里必须得调用std::move(socket),因为socket是不支持拷贝构造的，只能移动构造
            newSession->start(std::move(socket));//启动新连接

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

