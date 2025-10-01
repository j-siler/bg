#include <iostream>
#include <string>
#include <grpcpp/grpcpp.h>
#include "auth.hpp"
#include "match.hpp"
#include "logger.hpp"
#include "rpc_auth_match.hpp"

int main(int argc, char** argv) {
  std::string addr = "0.0.0.0:50051";
  if (argc >= 2) addr = argv[1];

  BG::Logger logger{"logs/admin-server.log"};
  BG::AuthManager auth;
  BG::MatchRegistry registry(logger);

  BG::AuthServiceImpl  auth_service(auth, logger);
  BG::MatchServiceImpl match_service(registry, logger);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&auth_service);
  builder.RegisterService(&match_service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  std::cout << "bg_admin listening on " << addr << "\n";
  server->Wait();
  return 0;
}
