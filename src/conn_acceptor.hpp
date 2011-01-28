#ifndef __CONN_ACCEPTOR_HPP__
#define __CONN_ACCEPTOR_HPP__

#include "arch/arch.hpp"
#include "utils.hpp"
#include "perfmon.hpp"
#include <boost/scoped_ptr.hpp>
#include "concurrency/rwi_lock.hpp"
#include "replication/failover.hpp"

/* The conn_acceptor_t is responsible for accepting incoming network connections, creating
objects to deal with them, and shutting down the network connections when the server
shuts down. It uses tcp_listener_t to actually accept the connections. Each conn_acceptor_t
lasts for the entire lifetime of the server. */

class conn_acceptor_t :
    public tcp_listener_callback_t,
    public home_thread_mixin_t,
    public failover_callback_t //TODO this prevents the conn_acceptor from being more general change that (jdoliner)
{
public:

    /* Whenever a connection is received, the conn_acceptor_t calls handler_t::handle() in a
    new coroutine. When handle() returns the connection is destroyed. handle() will be called on an
    arbitrary thread, and the connection should not be accessed from any thread other than the one
    that handle() was called on.
    
    If the conn_acceptor_t's destructor is called while handle() is running, the conn_acceptor_t
    will close the read end of the socket and then continue waiting for handle() to return. This
    behavior may not be flexible enough in the future. */
    struct handler_t {
        virtual void handle(tcp_conn_t *) = 0;
    };

    /* The constructor can throw this exception */
    struct address_in_use_exc_t :
        public std::exception
    {
        const char *what() throw () {
            return "The requested port is already in use.";
        }
    };

    conn_acceptor_t(int port, handler_t *handler, bool);

    /* Will make sure all connections are closed before it returns. May block. */
    ~conn_acceptor_t();

private:
    void make_listener();

private:
    /* The conn acceptor will be used as a failover_callback_t when we run
     * behind ELB */
    friend class failover_t;
    void on_failure();
    void on_resume();

private:
    handler_t *handler;

    int port;
    boost::scoped_ptr<tcp_listener_t> listener;
    void on_tcp_listener_accept(tcp_conn_t *conn);

    int next_thread;

    /* We maintain a list of active connections (actually, one list per thread) so that we can
    find an shut down active connections when our destructor is called. */
    struct conn_agent_t :
        public intrusive_list_node_t<conn_agent_t>
    {
        conn_acceptor_t *parent;
        tcp_conn_t *conn;
        conn_agent_t(conn_acceptor_t *, tcp_conn_t *);
        void run();
    };
    intrusive_list_t<conn_agent_t> conn_agents[MAX_THREADS];
    rwi_lock_t shutdown_locks[MAX_THREADS];
    void close_connections(int thread);
};

#endif /* __CONN_ACCEPTOR_HPP__ */
