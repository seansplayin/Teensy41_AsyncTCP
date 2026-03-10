/****************************************************************************************************************************
  Teensy41_AsyncTCP_Impl.h

  Teensy41_AsyncTCP is a library for Teensy4.1 using LwIP-based QNEthernet

  Based on and modified from :

  1) ESPAsyncTCP
  2) AsyncTCP

  Built by Khoi Hoang https://github.com/khoih-prog/Teensy41_AsyncTCP

  This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  Version: 1.1.0

  Version Modified By   Date      Comments
  ------- -----------  ---------- -----------
  1.0.0    K Hoang     17/03/2022 Initial coding to support only Teensy4.1 using QNEthernet
  1.1.0    K Hoang     26/09/2022 Fix issue with slow browsers or network. Clean up. Remove hard-code if possible
 *****************************************************************************************************************************/

#pragma once

#ifndef _TEENSY41_ASYNC_TCP_IMPL_H_
#define _TEENSY41_ASYNC_TCP_IMPL_H_

#include "Teensy41_AsyncTCP.hpp"

extern "C"
{
  #include "lwip/ip_addr.h"
  #include "lwip/opt.h"
  #include "lwip/tcp.h"
  #include "lwip/inet.h"
  #include "lwip/dns.h"
  #include "lwip/init.h"
}

inline ACErrorTracker::ACErrorTracker(AsyncClient *c)
  : _client(c)
  , _close_error(ERR_OK)
  , _errored(EE_OK)
  , _connectionId(0)
{
#ifdef DEBUG_MORE
  _error_event_cb = NULL;
  _error_event_cb_arg = NULL;
#endif
}

inline ACErrorTracker::~ACErrorTracker() {}

#ifdef DEBUG_MORE
inline void ACErrorTracker::onErrorEvent(AsNotifyHandler cb, void *arg) {
  _error_event_cb = cb;
  _error_event_cb_arg = arg;
}
#endif

inline void ACErrorTracker::setConnectionId(size_t cid) {
#ifdef DEBUG_MORE
  _connectionId = cid;
#endif
}

inline size_t ACErrorTracker::getConnectionId() const {
#ifdef DEBUG_MORE
  return _connectionId;
#else
  return 0;
#endif
}

inline void ACErrorTracker::setCloseError(err_t e) {
  if (e != ERR_OK) {
    ATCP_LOGINFO3("setCloseError() to:", _client->errorToString(e), "=>", e);
  }
  if (_errored == EE_OK) _close_error = e;
}

inline void ACErrorTracker::setErrored(size_t errorEvent) {
  if (EE_OK == _errored) _errored = errorEvent;
#ifdef DEBUG_MORE
  if (_error_event_cb) _error_event_cb(_error_event_cb_arg, errorEvent);
#endif
}

inline err_t ACErrorTracker::getCallbackCloseError() {
  if (EE_OK != _errored) return ERR_OK;
  if (ERR_ABRT == _close_error) setErrored(EE_ABORTED);
  return _close_error;
}

inline err_t ACErrorTracker::getCloseError() const {
  return _close_error;
}

inline bool ACErrorTracker::hasClient() const {
  return _client != NULL;
}

inline void ACErrorTracker::clearClient() {
  _client = NULL;
}

#if DEBUG_T41_ASYNC_TCP
extern size_t AsyncClient::_connectionCount;
#endif

inline AsyncClient::AsyncClient(tcp_pcb* pcb) :
  _connect_cb(0), _connect_cb_arg(0), _discard_cb(0), _discard_cb_arg(0), _sent_cb(0), _sent_cb_arg(0), _error_cb(0), _error_cb_arg(0), _recv_cb(0), _recv_cb_arg(0), _pb_cb(0), _pb_cb_arg(0), _timeout_cb(0), _timeout_cb_arg(0), _poll_cb(0), _poll_cb_arg(0), _pcb_busy(false),
#if ASYNC_TCP_SSL_ENABLED
  _pcb_secure(false), _handshake_done(true),
#endif
  _pcb_sent_at(0), _close_pcb(false), _ack_pcb(true), _tx_unacked_len(0), _tx_acked_len(0), _tx_unsent_len(0), _rx_ack_len(0), _rx_last_packet(0), _rx_since_timeout(0), _ack_timeout(ASYNC_MAX_ACK_TIME), _connect_port(0), _recv_pbuf_flags(0), _noDelay(false), _refcnt(0), prev(NULL), next(NULL) {
  _pcb = pcb;
  if (_pcb) {
    _rx_last_packet = millis();
    tcp_setprio(_pcb, TCP_PRIO_NORMAL);
    tcp_arg(_pcb, this);
    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_err(_pcb, &_s_error);
    tcp_poll(_pcb, &_s_poll, 1);
  }
  _errorTracker = std::make_shared<ACErrorTracker>(this);
#if DEBUG_T41_ASYNC_TCP
  _errorTracker->setConnectionId(++_connectionCount);
#endif
}

inline AsyncClient::~AsyncClient() {
  if (_pcb) _close();
  _errorTracker->clearClient();
}

inline AsyncClient & AsyncClient::operator=(const AsyncClient &other) {
  if (_pcb) _close();
  _errorTracker = other._errorTracker;
  _pcb = other._pcb;
  if (_pcb) {
    _rx_last_packet = millis();
    tcp_setprio(_pcb, TCP_PRIO_NORMAL);
    tcp_arg(_pcb, this);
    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_err(_pcb, &_s_error);
    tcp_poll(_pcb, &_s_poll, 1);
  }
  return *this;
}

inline bool AsyncClient::operator==(const AsyncClient &other) {
  return (_pcb != NULL && other._pcb != NULL && (_pcb->remote_ip.addr == other._pcb->remote_ip.addr) && (_pcb->remote_port == other._pcb->remote_port));
}

inline bool AsyncClient::operator!=(const AsyncClient &other) {
  return !(*this == other);
}

inline bool AsyncClient::connect(IPAddress ip, uint16_t port) {
  if (_pcb) return false;
  ip_addr_t addr;
  addr.addr = ip;
  tcp_pcb* pcb = tcp_new();
  if (!pcb) return false;
  tcp_setprio(pcb, TCP_PRIO_NORMAL);
  tcp_arg(pcb, this);
  tcp_err(pcb, &_s_error);
  err_t err = tcp_connect(pcb, &addr, port, reinterpret_cast<tcp_connected_fn>(&_s_connected));
  return (ERR_OK == err);
}

inline bool AsyncClient::connect(const char* host, uint16_t port) {
  ip_addr_t addr;
  err_t err = dns_gethostbyname(host, &addr, reinterpret_cast<dns_found_callback>(&_s_dns_found), this);
  if (err == ERR_OK) return connect(IPAddress(addr.addr), port);
  if (err == ERR_INPROGRESS) {
    _connect_port = port;
    return true;
  }
  return false;
}

inline void AsyncClient::close(bool now) {
  if (_pcb) {
    if (now) tcp_abort(_pcb);
    else _close_pcb = true;
  }
  setCloseError(ERR_OK);
}

inline void AsyncClient::stop() {
  close(false);
}

inline bool AsyncClient::free() {
  if (!_pcb) return true;
  if (tcp_close(_pcb) == ERR_OK) {
    _pcb = NULL;
    return true;
  }
  return false;
}

inline size_t AsyncClient::write(const char* data) {
  if (!_pcb) return 0;
  return write(data, strlen(data), TCP_WRITE_FLAG_COPY);
}

inline size_t AsyncClient::write(const char* data, size_t size, uint8_t apiflags) {
  if (!_pcb) return 0;
  size_t will_send = add(data, size, apiflags);
  if (!will_send) return 0;
  size_t sent = send();
  return (sent == will_send) ? will_send : 0;
}

inline size_t AsyncClient::add(const char* data, size_t size, uint8_t apiflags) {
  if (!_pcb) return 0;
  err_t err = tcp_write(_pcb, data, size, apiflags);
  if (err != ERR_OK) return 0;
  _tx_unsent_len += size;
  return size;
}

inline bool AsyncClient::send() {
  err_t err = tcp_output(_pcb);
  if (err != ERR_OK) return false;
  _tx_unsent_len = 0;
  return true;
}

inline size_t AsyncClient::space() {
#if ASYNC_TCP_SSL_ENABLED
  if (_pcb_secure && _handshake_done) {
#ifdef AXTLS_2_0_0_SNDBUF
    return tcp_sndbuf(_pcb);
#else
    return tcp_ssl_sndbuf(_pcb);
#endif
  }
#endif
  if (_pcb) return tcp_sndbuf(_pcb);
  return 0;
}

inline size_t AsyncClient::ack(size_t len) {
  if (len > _rx_ack_len) len = _rx_ack_len;
  if (len) tcp_recved(_pcb, len);
  _rx_ack_len -= len;
  return len;
}

inline void AsyncClient::ackLater() {
  _ack_pcb = false;
}

inline bool AsyncClient::canSend() {
  return _pcb && space() > 0;
}

inline bool AsyncClient::connected() {
  if (_pcb) return (_pcb->state == ESTABLISHED);
  return false;
}

inline bool AsyncClient::connecting() {
  if (_pcb) return (_pcb->state == SYN_SENT || _pcb->state == SYN_RCVD);
  return false;
}

inline bool AsyncClient::disconnecting() {
  if (_pcb) return (_pcb->state == FIN_WAIT_1 || _pcb->state == FIN_WAIT_2 || _pcb->state == TIME_WAIT);
  return false;
}

inline bool AsyncClient::disconnected() {
  if (_pcb) return (_pcb->state == CLOSED);
  return true;
}

inline bool AsyncClient::freeable() {
  return disconnected() || disconnecting();
}

inline uint16_t AsyncClient::getMss() {
  if (_pcb) return tcp_mss(_pcb);
  return 0;
}

inline uint32_t AsyncClient::getRxTimeout() {
  return _rx_since_timeout;
}

inline void AsyncClient::setRxTimeout(uint32_t timeout) {
  _rx_since_timeout = timeout;
}

inline uint32_t AsyncClient::getAckTimeout() {
  return _ack_timeout;
}

inline void AsyncClient::setAckTimeout(uint32_t timeout) {
  _ack_timeout = timeout;
}

inline void AsyncClient::setNoDelay(bool nodelay) {
  if (_pcb) {
    if (nodelay) tcp_nagle_disable(_pcb);
    else tcp_nagle_enable(_pcb);
  } else _noDelay = nodelay;
}

inline bool AsyncClient::getNoDelay() {
  if (_pcb) return tcp_nagle_disabled(_pcb);
  return _noDelay;
}

inline uint32_t AsyncClient::getRemoteAddress() {
  if (_pcb) return _pcb->remote_ip.addr;
  return 0;
}

inline uint16_t AsyncClient::getRemotePort() {
  if (_pcb) return _pcb->remote_port;
  return 0;
}

inline uint32_t AsyncClient::getLocalAddress() {
  if (_pcb) return _pcb->local_ip.addr;
  return 0;
}

inline uint16_t AsyncClient::getLocalPort() {
  if (_pcb) return _pcb->local_port;
  return 0;
}

inline IPAddress AsyncClient::remoteIP() {
  return IPAddress(getRemoteAddress());
}

inline uint16_t AsyncClient::remotePort() {
  return getRemotePort();
}

inline IPAddress AsyncClient::localIP() {
  return IPAddress(getLocalAddress());
}

inline uint16_t AsyncClient::localPort() {
  return getLocalPort();
}

inline uint8_t AsyncClient::state() {
  if (_pcb) return _pcb->state;
  return CLOSED;
}

inline const char* AsyncClient::errorToString(err_t error) {
  switch (error) {
    case ERR_OK: return "OK";
    case ERR_MEM: return "Out of memory error";
    case ERR_BUF: return "Buffer error";
    case ERR_TIMEOUT: return "Timeout";
    case ERR_RTE: return "Routing problem";
    case ERR_INPROGRESS: return "Operation in progress";
    case ERR_VAL: return "Illegal value";
    case ERR_WOULDBLOCK: return "Operation would block";
    case ERR_USE: return "Address in use";
    case ERR_ALREADY: return "Already connecting";
    case ERR_ISCONN: return "Conn already established";
    case ERR_CONN: return "Not connected";
    case ERR_IF: return "Low level netif error";
    case ERR_ABRT: return "Connection aborted";
    case ERR_RST: return "Connection reset";
    case ERR_CLSD: return "Connection closed";
    case ERR_ARG: return "Illegal argument";
    default: return "UNKNOWN";
  }
}

inline const char* AsyncClient::stateToString() {
  switch (state()) {
    case CLOSED: return "Closed";
    case LISTEN: return "Listen";
    case SYN_SENT: return "SYN Sent";
    case SYN_RCVD: return "SYN Received";
    case ESTABLISHED: return "Established";
    case FIN_WAIT_1: return "FIN Wait 1";
    case FIN_WAIT_2: return "FIN Wait 2";
    case CLOSE_WAIT: return "Close Wait";
    case CLOSING: return "Closing";
    case LAST_ACK: return "Last ACK";
    case TIME_WAIT: return "Time Wait";
    default: return "UNKNOWN";
  }
}

inline err_t AsyncClient::abort() {
  if (_pcb) {
    tcp_abort(_pcb);
    _pcb = NULL;
    _errorTracker->setCloseError(ERR_ABRT);
  }
  return ERR_ABRT;
}

inline void AsyncClient::ref() {
  ++_refcnt;
}

inline void AsyncClient::unref() {
  if (--_refcnt == 0) delete this;
}

inline void AsyncClient::setCloseError(err_t e) {
  _errorTracker->setCloseError(e);
}

inline err_t AsyncClient::getCloseError() const {
  return _errorTracker->getCloseError();
}

inline size_t AsyncClient::getConnectionId() const {
  return _errorTracker->getConnectionId();
}

inline std::shared_ptr<ACErrorTracker> AsyncClient::getACErrorTracker() const {
  return _errorTracker;
}

inline void AsyncClient::onConnect(AcConnectHandler cb, void* arg) {
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

inline void AsyncClient::onDisconnect(AcConnectHandler cb, void* arg) {
  _discard_cb = cb;
  _discard_cb_arg = arg;
}

inline void AsyncClient::onAck(AcAckHandler cb, void* arg) {
  _sent_cb = cb;
  _sent_cb_arg = arg;
}

inline void AsyncClient::onError(AcErrorHandler cb, void* arg) {
  _error_cb = cb;
  _error_cb_arg = arg;
}

inline void AsyncClient::onData(AcDataHandler cb, void* arg) {
  _recv_cb = cb;
  _recv_cb_arg = arg;
}

inline void AsyncClient::onPacket(AcPacketHandler cb, void* arg) {
  _pb_cb = cb;
  _pb_cb_arg = arg;
}

inline void AsyncClient::onTimeout(AcTimeoutHandler cb, void* arg) {
  _timeout_cb = cb;
  _timeout_cb_arg = arg;
}

inline void AsyncClient::onPoll(AcConnectHandler cb, void* arg) {
  _poll_cb = cb;
  _poll_cb_arg = arg;
}

inline void AsyncClient::ackPacket(struct pbuf * pb) {
  if (!pb) return;
  _rx_ack_len += pb->len;
  pbuf_free(pb);
}

inline void AsyncClient::_dns_found(const ip_addr_t *ipaddr) {
  if (ipaddr) {
    connect(IPAddress(ipaddr->addr), _connect_port);
  } else {
    if (_error_cb) _error_cb(_error_cb_arg, this, -55);
    if (_discard_cb) _discard_cb(_discard_cb_arg, this);
  }
}

inline void AsyncClient::_close() {
  if (_pcb) {
#if ASYNC_TCP_SSL_ENABLED
    if (_pcb_secure) {
      tcp_ssl_free(_pcb);
    }
#endif
    tcp_arg(_pcb, NULL);
    tcp_sent(_pcb, NULL);
    tcp_recv(_pcb, NULL);
    tcp_err(_pcb, NULL);
    tcp_poll(_pcb, NULL, 0);
    if (tcp_close(_pcb) != ERR_OK) {
      tcp_abort(_pcb);
    }
    _pcb = NULL;
  }
  if (_discard_cb) _discard_cb(_discard_cb_arg, this);
}

inline void AsyncClient::_error(err_t err) {
  _errorTracker->setErrored(EE_ERROR_CB);
  _errorTracker->setCloseError(err);
  if (_error_cb) _error_cb(_error_cb_arg, this, err);
  _close();
}

#if ASYNC_TCP_SSL_ENABLED
inline void AsyncClient::_ssl_error(int8_t err) {
  _errorTracker->setErrored(EE_ERROR_CB);
  _errorTracker->setCloseError(ERR_ABRT);
  if (_error_cb) _error_cb(_error_cb_arg, this, err);
  _close();
}
#endif

inline void AsyncClient::_connected(std::shared_ptr<ACErrorTracker>& closeAbort, struct tcp_pcb* pcb, err_t err) {
  _pcb = pcb;
  if (_pcb) {
    _rx_last_packet = millis();
    tcp_setprio(_pcb, TCP_PRIO_NORMAL);
    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_poll(_pcb, &_s_poll, 1);
  }
  if (_connect_cb) _connect_cb(_connect_cb_arg, this);
  (void) closeAbort;
  (void) err;
}

inline void AsyncClient::_sent(std::shared_ptr<ACErrorTracker>& closeAbort, struct tcp_pcb* pcb, uint16_t len) {
  _rx_last_packet = millis();
  _tx_acked_len += len;
  _tx_unacked_len -= len;
  if (_sent_cb) _sent_cb(_sent_cb_arg, this, len, millis() - _pcb_sent_at);
  if (_close_pcb) _close();
  (void) closeAbort;
  (void) pcb;
}

inline void AsyncClient::_recv(std::shared_ptr<ACErrorTracker>& closeAbort, struct tcp_pcb* pcb, struct pbuf *pb, err_t err) {
  if (!pb) {
    _close();
    return;
  }
  _rx_last_packet = millis();
  _rx_ack_len += pb->tot_len;
  if (_pb_cb) {
    _pb_cb(_pb_cb_arg, this, pb);
  } else if (_recv_cb) {
    _recv_cb(_recv_cb_arg, this, (char*)pb->payload, pb->tot_len);
    if (_ack_pcb) ack(pb->tot_len);
    pbuf_free(pb);
  } else {
    pbuf_free(pb);
  }
  (void) closeAbort;
  (void) pcb;
  (void) err;
}

inline void AsyncClient::_poll(std::shared_ptr<ACErrorTracker>& closeAbort, struct tcp_pcb* pcb) {
  if (_poll_cb) _poll_cb(_poll_cb_arg, this);
  (void) closeAbort;
  (void) pcb;
}

inline err_t AsyncClient::_s_poll(void *arg, struct tcp_pcb *tpcb) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_poll(c->_errorTracker, tpcb);
  return ERR_OK;
}

inline err_t AsyncClient::_s_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_recv(c->_errorTracker, tpcb, pb, err);
  return ERR_OK;
}

inline void AsyncClient::_s_error(void *arg, err_t err) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_error(err);
}

inline err_t AsyncClient::_s_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_sent(c->_errorTracker, tpcb, len);
  return ERR_OK;
}

inline err_t AsyncClient::_s_connected(void* arg, struct tcp_pcb* tpcb, err_t err) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_connected(c->_errorTracker, tpcb, err);
  return ERR_OK;
}

inline void AsyncClient::_s_dns_found(const char *name, const ip_addr_t *ipaddr, void *arg) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_dns_found(ipaddr);
}

#if ASYNC_TCP_SSL_ENABLED
inline void AsyncClient::_s_data(void *arg, struct tcp_pcb *tcp, uint8_t * data, size_t len) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  if (c->_recv_cb) c->_recv_cb(c->_recv_cb_arg, c, data, len);
}

inline void AsyncClient::_s_handshake(void *arg, struct tcp_pcb *tcp, SSL *ssl) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_handshake_done = true;
}

inline void AsyncClient::_s_ssl_error(void *arg, struct tcp_pcb *tcp, int8_t err) {
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_ssl_error(err);
}
#endif

inline AsyncServer::AsyncServer(IPAddress addr, uint16_t port) : _port(port), _addr(addr), _noDelay(false), _pcb(0), _connect_cb(0), _connect_cb_arg(0) {}

inline AsyncServer::AsyncServer(uint16_t port) : _port(port), _addr((uint32_t) IPADDR_ANY), _noDelay(false), _pcb(0), _connect_cb(0), _connect_cb_arg(0) {}

inline AsyncServer::~AsyncServer() {
  end();
}

inline void AsyncServer::onClient(AcConnectHandler cb, void* arg) {
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

inline void AsyncServer::begin() {
  if (_pcb) return;
  tcp_pcb* pcb = tcp_new();
  if (!pcb) return;
  tcp_setprio(pcb, TCP_PRIO_NORMAL);
  ip_addr_t local_addr;
  local_addr.addr = (uint32_t) _addr;
  err_t err = tcp_bind(pcb, &local_addr, _port);
  if (err != ERR_OK) {
    tcp_close(pcb);
    return;
  }
  tcp_pcb* listen_pcb = tcp_listen(pcb);
  if (!listen_pcb) {
    tcp_close(pcb);
    return;
  }
  _pcb = listen_pcb;
  tcp_arg(_pcb, (void*) this);
  tcp_accept(_pcb, &_s_accept);
}

inline void AsyncServer::end() {
  if (_pcb) {
    tcp_arg(_pcb, NULL);
    tcp_accept(_pcb, NULL);
    if (tcp_close(_pcb) != ERR_OK) {
      tcp_abort(_pcb);
    }
    _pcb = NULL;
  }
}

inline void AsyncServer::setNoDelay(bool nodelay) {
  _noDelay = nodelay;
}

inline bool AsyncServer::getNoDelay() {
  return _noDelay;
}

inline uint8_t AsyncServer::status() {
  if (_pcb) return _pcb->state;
  return CLOSED;
}

inline err_t AsyncServer::_accept(tcp_pcb* pcb, err_t err) {
  if (err != ERR_OK || pcb == NULL || _connect_cb == NULL) {
    return ERR_VAL;
  }
  tcp_setprio(pcb, TCP_PRIO_NORMAL);
  if (_noDelay) tcp_nagle_disable(pcb);
  AsyncClient *client = new AsyncClient(pcb);
  _connect_cb(_connect_cb_arg, client);
  return ERR_OK;
}

inline err_t AsyncServer::_s_accept(void *arg, tcp_pcb* pcb, err_t err) {
  return reinterpret_cast<AsyncServer*>(arg)->_accept(pcb, err);
}


#endif /* _TEENSY41_ASYNC_TCP_IMPL_H_ */