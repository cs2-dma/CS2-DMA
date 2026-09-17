

#ifdef HAVE_WIN32_AGENTS 

#include <stdlib.h>  



#define WIN32_OPENSSH_AGENT_SOCK "\\\\.\\pipe\\openssh-ssh-agent"

static int
agent_connect_openssh(LIBSSH2_AGENT *agent)
{
    int ret = LIBSSH2_ERROR_NONE;
    const char *path;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE event = NULL;

    path = agent->identity_agent_path;
    if(!path) {
        path = getenv("SSH_AUTH_SOCK");
        if(!path)
            path = WIN32_OPENSSH_AGENT_SOCK;
    }

    for(;;) {
        pipe = CreateFileA(
            path,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            
            
            SECURITY_SQOS_PRESENT |
            SECURITY_IDENTIFICATION,
            NULL
        );

        if(pipe != INVALID_HANDLE_VALUE)
            break;
        if(GetLastError() != ERROR_PIPE_BUSY)
            break;

        
        if(!WaitNamedPipeA(path, 1000))
            break;
    }

    if(pipe == INVALID_HANDLE_VALUE) {
        ret = _libssh2_error(agent->session, LIBSSH2_ERROR_AGENT_PROTOCOL,
                             "unable to connect to agent pipe");
        goto cleanup;
    }

    if(SetHandleInformation(pipe, HANDLE_FLAG_INHERIT, 0) == FALSE) {
        ret = _libssh2_error(agent->session, LIBSSH2_ERROR_AGENT_PROTOCOL,
                             "unable to set handle information of agent pipe");
        goto cleanup;
    }

    event = CreateEventA(NULL, TRUE, FALSE, NULL);
    if(!event) {
        ret = _libssh2_error(agent->session, LIBSSH2_ERROR_AGENT_PROTOCOL,
                             "unable to create async I/O event");
        goto cleanup;
    }

    agent->pipe = pipe;
    pipe = INVALID_HANDLE_VALUE;
    agent->overlapped.hEvent = event;
    event = NULL;
    agent->fd = 0; 

cleanup:
    if(event)
        CloseHandle(event);
    if(pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
    return ret;
}

#define RECV_SEND_ALL(func, agent, buffer, length, total)              \
    DWORD bytes_transfered;                                            \
    BOOL ret;                                                          \
    DWORD err;                                                         \
    int rc;                                                            \
                                                                       \
    while(*total < length) {                                           \
        if(!agent->pending_io)                                         \
            ret = func(agent->pipe, (char *)buffer + *total,           \
                       (DWORD)(length - *total), &bytes_transfered,    \
                       &agent->overlapped);                            \
        else                                                           \
            ret = GetOverlappedResult(agent->pipe, &agent->overlapped, \
                                      &bytes_transfered, FALSE);       \
                                                                       \
        *total += bytes_transfered;                                    \
        if(!ret) {                                                     \
            err = GetLastError();                                      \
            if((!agent->pending_io && ERROR_IO_PENDING == err)         \
               || (agent->pending_io && ERROR_IO_INCOMPLETE == err)) { \
                agent->pending_io = TRUE;                              \
                return LIBSSH2_ERROR_EAGAIN;                           \
            }                                                          \
                                                                       \
            return LIBSSH2_ERROR_SOCKET_NONE;                          \
        }                                                              \
        agent->pending_io = FALSE;                                     \
    }                                                                  \
                                                                       \
    rc = (int)*total;                                                  \
    *total = 0;                                                        \
    return rc;

static int
win32_openssh_send_all(LIBSSH2_AGENT *agent, void *buffer, size_t length,
                       size_t *send_recv_total)
{
    RECV_SEND_ALL(WriteFile, agent, buffer, length, send_recv_total)
}

static int
win32_openssh_recv_all(LIBSSH2_AGENT *agent, void *buffer, size_t length,
                       size_t *send_recv_total)
{
    RECV_SEND_ALL(ReadFile, agent, buffer, length, send_recv_total)
}

#undef RECV_SEND_ALL

static int
agent_transact_openssh(LIBSSH2_AGENT *agent, agent_transaction_ctx_t transctx)
{
    unsigned char buf[4];
    int rc;

    
    if(transctx->state == agent_NB_state_request_created) {
        _libssh2_htonu32(buf, (uint32_t)transctx->request_len);
        rc = win32_openssh_send_all(agent, buf, sizeof(buf),
                                    &transctx->send_recv_total);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return LIBSSH2_ERROR_EAGAIN;
        else if(rc < 0)
            return _libssh2_error(agent->session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "agent send failed");
        transctx->state = agent_NB_state_request_length_sent;
    }

    
    if(transctx->state == agent_NB_state_request_length_sent) {
        rc = win32_openssh_send_all(agent, transctx->request,
                                    transctx->request_len,
                                    &transctx->send_recv_total);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return LIBSSH2_ERROR_EAGAIN;
        else if(rc < 0)
            return _libssh2_error(agent->session, LIBSSH2_ERROR_SOCKET_SEND,
                                  "agent send failed");
        transctx->state = agent_NB_state_request_sent;
    }

    
    if(transctx->state == agent_NB_state_request_sent) {
        rc = win32_openssh_recv_all(agent, buf, sizeof(buf),
                                    &transctx->send_recv_total);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return LIBSSH2_ERROR_EAGAIN;
        else if(rc < 0)
            return _libssh2_error(agent->session, LIBSSH2_ERROR_SOCKET_RECV,
                                  "agent recv failed");

        transctx->response_len = _libssh2_ntohu32(buf);
        transctx->response = LIBSSH2_ALLOC(agent->session,
                                           transctx->response_len);
        if(!transctx->response)
            return LIBSSH2_ERROR_ALLOC;

        transctx->state = agent_NB_state_response_length_received;
    }

    
    if(transctx->state == agent_NB_state_response_length_received) {
        rc = win32_openssh_recv_all(agent, transctx->response,
                                    transctx->response_len,
                                    &transctx->send_recv_total);
        if(rc == LIBSSH2_ERROR_EAGAIN)
            return LIBSSH2_ERROR_EAGAIN;
        else if(rc < 0)
            return _libssh2_error(agent->session, LIBSSH2_ERROR_SOCKET_RECV,
                                  "agent recv failed");
        transctx->state = agent_NB_state_response_received;
    }

    return LIBSSH2_ERROR_NONE;
}

static int
agent_disconnect_openssh(LIBSSH2_AGENT *agent)
{
    if(!CancelIo(agent->pipe))
        return _libssh2_error(agent->session, LIBSSH2_ERROR_SOCKET_DISCONNECT,
                              "failed to cancel pending IO of agent pipe");
    if(!CloseHandle(agent->overlapped.hEvent))
        return _libssh2_error(agent->session, LIBSSH2_ERROR_SOCKET_DISCONNECT,
                              "failed to close handle to async I/O event");
    agent->overlapped.hEvent = NULL;
    
    SleepEx(0, TRUE);
    if(!CloseHandle(agent->pipe))
        return _libssh2_error(agent->session, LIBSSH2_ERROR_SOCKET_DISCONNECT,
                              "failed to close handle to agent pipe");

    agent->pipe = INVALID_HANDLE_VALUE;
    agent->fd = LIBSSH2_INVALID_SOCKET;

    return LIBSSH2_ERROR_NONE;
}

static struct agent_ops agent_ops_openssh = {
    agent_connect_openssh,
    agent_transact_openssh,
    agent_disconnect_openssh
};

#endif 
