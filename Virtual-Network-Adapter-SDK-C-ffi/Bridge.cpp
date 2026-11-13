#include "SDK/NetworkAdapter.h"
#include "WireTypes.h"
#include <map>
struct ProductSession{
NetworkAdapter client;
Json Invoke(const std::string& op,const Json& a){

if(op=="check" || op=="parse_frame"){std::vector<unsigned char> frame;bool valid=ParseEthernetFrame(a.at("hex").get<std::string>(),frame);return {{"valid",valid},{"data",frame}};}
if(op=="open"){CheckCode(client.Open());return Json::object();}
if(op=="close"){client.Close();return Json::object();}
if(op=="authorize"){CheckMessage(client.Authorize(a.at("key").get<std::string>()));return Json::object();}
if(op=="connect"){CheckCode(client.Connect());return Json::object();}
if(op=="disconnect"){CheckCode(client.Disconnect());return Json::object();}
if(op=="status"){VN_STATE value{};CheckCode(client.Query(value));return EncodeWire(value);}
if(op=="receive"){VN_FRAME frame{};CheckCode(client.Receive(frame));Require(frame.length<=VN_MAX_FRAME,"Invalid native frame length");return {{"data",std::vector<unsigned char>(frame.data,frame.data+frame.length)}};}
if(op=="send"){CheckCode(client.Send(Bytes(a.at("data"),VN_MAX_FRAME)));return Json::object();}

throw SdkError(50,"Unknown operation");}
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned virtual_network_adapter_sdk_json_open(std::uint64_t* id)noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned virtual_network_adapter_sdk_json_close(std::uint64_t id)noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* virtual_network_adapter_sdk_json_call(std::uint64_t id,const char* request,std::uint32_t length)noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void virtual_network_adapter_sdk_json_free(char* response)noexcept{delete[] response;}
