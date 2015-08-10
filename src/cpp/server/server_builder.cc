/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <grpc++/server_builder.h>

#include <grpc/support/cpu.h>
#include <grpc/support/log.h>
#include <grpc++/impl/service_type.h>
#include <grpc++/server.h>
#include <grpc++/thread_pool_interface.h>

namespace grpc {

ServerBuilder::ServerBuilder()
    : max_message_size_(-1), generic_service_(nullptr), thread_pool_(nullptr) {
      grpc_compression_options_init(&compression_options_);
}

std::unique_ptr<ServerCompletionQueue> ServerBuilder::AddCompletionQueue() {
  ServerCompletionQueue* cq = new ServerCompletionQueue();
  cqs_.push_back(cq);
  return std::unique_ptr<ServerCompletionQueue>(cq);
}

ServerBuilder& ServerBuilder::RegisterService(SynchronousService* service) {
  services_.emplace_back(new NamedService<RpcService>(service->service()));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterAsyncService(
    AsynchronousService* service) {
  async_services_.emplace_back(new NamedService<AsynchronousService>(service));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterService(
    const grpc::string& addr, SynchronousService* service) {
  services_.emplace_back(new NamedService<RpcService>(addr, service->service()));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterAsyncService(
    const grpc::string& addr, AsynchronousService* service) {
  async_services_.emplace_back(
      new NamedService<AsynchronousService>(addr, service));
  return *this;
}

ServerBuilder& ServerBuilder::RegisterAsyncGenericService(
    AsyncGenericService* service) {
  if (generic_service_) {
    gpr_log(GPR_ERROR,
            "Adding multiple AsyncGenericService is unsupported for now. "
            "Dropping the service %p",
            service);
  } else {
    generic_service_ = service;
  }
  return *this;
}

ServerBuilder& ServerBuilder::SetMaxMessageSize(int max_message_size) {
  max_message_size_ = max_message_size;
  return *this;
}

ServerBuilder& ServerBuilder::AddListeningPort(const grpc::string& addr,
                                     std::shared_ptr<ServerCredentials> creds,
                                     int* selected_port) {
  Port port = {addr, creds, selected_port};
  ports_.push_back(port);
  return *this;
}

ServerBuilder& ServerBuilder::SetThreadPool(ThreadPoolInterface* thread_pool) {
  thread_pool_ = thread_pool;
  return *this;
}

ServerBuilder& ServerBuilder::SetCompressionOptions(
    const grpc_compression_options& options) {
  compression_options_ = options;
  return *this;
}

std::unique_ptr<Server> ServerBuilder::BuildAndStart() {
  bool thread_pool_owned = false;
  if (!async_services_.empty() && !services_.empty()) {
    gpr_log(GPR_ERROR, "Mixing async and sync services is unsupported for now");
    return nullptr;
  }
  if (!thread_pool_ && !services_.empty()) {
    thread_pool_ = CreateDefaultThreadPool();
    thread_pool_owned = true;
  }
  std::unique_ptr<Server> server(new Server(thread_pool_, thread_pool_owned,
                                            max_message_size_,
                                            compression_options_));
  for (auto cq = cqs_.begin(); cq != cqs_.end(); ++cq) {
    grpc_server_register_completion_queue(server->server_, (*cq)->cq());
  }
  for (auto service = services_.begin(); service != services_.end();
       service++) {
    if (!server->RegisterService((*service)->host.get(), (*service)->service)) {
      return nullptr;
    }
  }
  for (auto service = async_services_.begin();
       service != async_services_.end(); service++) {
    if (!server->RegisterAsyncService((*service)->host.get(),
                                      (*service)->service)) {
      return nullptr;
    }
  }
  if (generic_service_) {
    server->RegisterAsyncGenericService(generic_service_);
  }
  for (auto port = ports_.begin(); port != ports_.end(); port++) {
    int r = server->AddListeningPort(port->addr, port->creds.get());
    if (!r) return nullptr;
    if (port->selected_port != nullptr) {
      *port->selected_port = r;
    }
  }
  if (!server->Start()) {
    return nullptr;
  }
  return server;
}

}  // namespace grpc
