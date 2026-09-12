#include "../../soh/soh/Network/Direct/Transport.h"
#include "../../soh/soh/Network/Direct/ProgressRules.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace Shipwright::Direct;
using namespace std::chrono_literals;
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(std::string(__func__) + ": " #x); } while (false)
namespace {
using Clock = std::chrono::steady_clock;
using Events = std::vector<Event>;
bool Wait(Transport& transport, Events& events, const std::function<bool(const Events&)>& done,
          std::chrono::milliseconds timeout = 4000ms) {
    auto end = Clock::now() + timeout;
    while (Clock::now() < end) {
        auto more = transport.Poll();
        for (auto& e : more) events.push_back(std::move(e));
        if (done(events)) return true;
        std::this_thread::sleep_for(2ms);
    }
    return false;
}
bool Has(const Events& events, Event::Type type) {
    return std::any_of(events.begin(), events.end(), [=](auto& e) { return e.type == type; });
}
size_t Count(const Events& events, Event::Type type) {
    return std::count_if(events.begin(), events.end(), [=](auto& e) { return e.type == type; });
}
uint16_t StartHost(Transport& server) {
    std::mt19937 random(std::random_device{}());
    for (int attempt = 0; attempt < 50; ++attempt) {
        uint16_t port = static_cast<uint16_t>(20000 + random() % 30000);
        REQUIRE(server.Host(port));
        Events events;
        REQUIRE(Wait(server, events, [](auto& e) { return Has(e, Event::Type::Listening) || Has(e, Event::Type::Error); }));
        if (Has(events, Event::Type::Listening)) return port;
        server.Stop();
    }
    throw std::runtime_error("No test port available");
}
uint32_t Connect(Transport& server, Transport& client, uint16_t port) {
    REQUIRE(client.Join("127.0.0.1", port));
    Events local, remote;
    REQUIRE(Wait(client, local, [](auto& e) { return Has(e, Event::Type::Connected); }));
    REQUIRE(Wait(server, remote, [](auto& e) { return Has(e, Event::Type::Accepted); }));
    for (auto& e : remote) if (e.type == Event::Type::Accepted) return e.peer;
    throw std::runtime_error("No assigned peer ID");
}
void IPv4Validation() {
    for (const char* address : {"127.0.0.1", "192.168.1.20", "10.0.0.2", "001.002.003.004"}) REQUIRE(IsIPv4(address));
    for (const char* address : {"", "localhost", "::1", "127.0.0.1:43384", "1.2.3", "1.2.3.4.5", "256.0.0.1",
                               "1..3.4", "-1.2.3.4", "1.2.3.4 ", "0001.2.3.4", "0.0.0.0", "000.000.000.000", "255.255.255.255"}) REQUIRE(!IsIPv4(address));
}
void FragmentedFrames() {
    std::string payload("a\0b\xffz", 5), encoded = EncodeFrame(payload);
    REQUIRE(encoded.size() == 9); REQUIRE(static_cast<unsigned char>(encoded[3]) == 5);
    FrameDecoder decoder; std::vector<std::string> frames;
    for (char c : encoded) REQUIRE(decoder.Feed(&c, 1, frames));
    REQUIRE(frames.size() == 1); REQUIRE(frames.front() == payload);
}
void CoalescedFrames() {
    FrameDecoder decoder; std::vector<std::string> frames;
    std::string bytes = EncodeFrame("one") + EncodeFrame("two") + EncodeFrame("three");
    REQUIRE(decoder.Feed(bytes.data(), bytes.size(), frames));
    REQUIRE(frames == std::vector<std::string>({"one", "two", "three"}));
}
void DecoderBoundaryAndReset() {
    FrameDecoder decoder; std::vector<std::string> frames;
    std::string payload(MaxFrameBytes, 'x'), bytes = EncodeFrame(payload);
    for (size_t i = 0; i < bytes.size(); i += 8191) REQUIRE(decoder.Feed(bytes.data() + i, std::min<size_t>(8191, bytes.size() - i), frames));
    REQUIRE(frames.size() == 1 && frames[0] == payload);
    const char bad[] = {0, 0, 0, 0}; REQUIRE(!decoder.Feed(bad, 4, frames));
    decoder.Clear(); frames.clear();
    bytes = EncodeFrame("ok"); REQUIRE(decoder.Feed(bytes.data(), bytes.size(), frames)); REQUIRE(frames.at(0) == "ok");
    decoder.Clear(); const char huge[] = {0, 8, 0, 1}; REQUIRE(!decoder.Feed(huge, 4, frames));
    bool rejected = false; try { (void)EncodeFrame(""); } catch (const std::length_error&) { rejected = true; } REQUIRE(rejected);
    rejected = false; try { (void)EncodeFrame(std::string(MaxFrameBytes + 1, 'x')); } catch (const std::length_error&) { rejected = true; } REQUIRE(rejected);
}
void RandomFragmentation() {
    std::mt19937 rng(8128);
    std::vector<std::string> expected, frames; std::string wire;
    for (int i = 0; i < 600; ++i) {
        std::string p(1 + rng() % 1024, ' '); for (char& c : p) c = static_cast<char>(rng());
        wire += EncodeFrame(p); expected.push_back(std::move(p));
    }
    FrameDecoder decoder;
    for (size_t i = 0; i < wire.size();) {
        size_t n = std::min<size_t>(1 + rng() % 337, wire.size() - i);
        REQUIRE(decoder.Feed(wire.data() + i, n, frames)); i += n;
    }
    REQUIRE(frames == expected);
}
void UpgradeTiers() {
    constexpr std::array<unsigned, 8> shifts{0, 3, 6, 9, 12, 14, 17, 20};
    constexpr std::array<unsigned, 8> widths{7, 7, 7, 7, 3, 7, 7, 7};
    for (size_t slot = 0; slot < shifts.size(); ++slot)
        for (unsigned a = 0; a <= widths[slot]; ++a)
            for (unsigned b = 0; b <= widths[slot]; ++b) {
                unsigned own = a << shifts[slot], other = b << shifts[slot];
                REQUIRE(MergeUpgrades(own, other) == (std::max(a,b) << shifts[slot]));
                REQUIRE(MergeUpgrades(own, own) == own);
            }
    REQUIRE(MergeUpgrades(1,2) == 2); REQUIRE(MergeUpgrades(0x80000000u, 0) == 0x80000000u);
}
void HeartProgress() {
    REQUIRE(MergeQuestItems(0x10000001,0x20000002,0x50,0x50) == 0x20000003);
    REQUIRE(MergeQuestItems(0x00000001,0x30000002,0xA0,0x30) == 0x00000003);
    REQUIRE(MergeQuestItems(0x30000001,0x00000002,0x30,0xA0) == 0x00000003);
    for(uint16_t a=0x10;a<=0x140;a+=0x10) for(uint16_t b=0x10;b<=0x140;b+=0x10)
        for(uint32_t x=0;x<4;++x) for(uint32_t y=0;y<4;++y) {
            auto merged=MergeQuestItems(x<<28,y<<28,a,b);
            REQUIRE(merged==MergeQuestItems(y<<28,x<<28,b,a));
            auto maximum=std::max(a*4+x*16,b*4+y*16);
            REQUIRE(std::max(a,b)*4+(merged>>28)*16==maximum);
        }
}
void InventoryValidation() {
    REQUIRE(ValidUpgradeLevels(0)); REQUIRE(ValidUpgradeLevels(3)); REQUIRE(!ValidUpgradeLevels(4));
    REQUIRE(!ValidUpgradeLevels(3<<9)); REQUIRE(!ValidUpgradeLevels(0x80000000));
    REQUIRE(ValidInventoryItem(7,8)); REQUIRE(!ValidInventoryItem(7,11));
    REQUIRE(ValidInventoryItem(18,0x14)); REQUIRE(!ValidInventoryItem(18,3));
    REQUIRE(ValidInventoryItem(22,0x37)); REQUIRE(!ValidInventoryItem(23,0x37));
    REQUIRE(!ValidInventoryItem(24,255));
}
void PersonalItemSlots() {
    REQUIRE(MergePermanentItem(0, 255, 0) == 0);
    REQUIRE(MergePermanentItem(7, 7, 8) == 8); REQUIRE(MergePermanentItem(7, 8, 7) == 8);
    REQUIRE(MergePermanentItem(9, 10, 11) == 11);
    for (size_t slot = 18; slot < 24; ++slot)
        for (unsigned own = 0; own < 256; ++own) REQUIRE(MergePermanentItem(slot, static_cast<uint8_t>(own), 255) == own);
    for (size_t slot = 0; slot < 18; ++slot) REQUIRE(MergePermanentItem(slot, 4, 255) == 4);
}
void Bidirectional() {
    Transport host, client; uint16_t port = StartHost(host); uint32_t id = Connect(host, client, port);
    REQUIRE(id == 2); REQUIRE(host.Send(id, "host->client")); REQUIRE(client.Send(1, "client->host"));
    Events a,b;
    REQUIRE(Wait(host, a, [](auto& e) { return Has(e, Event::Type::Message); }));
    REQUIRE(Wait(client, b, [](auto& e) { return Has(e, Event::Type::Message); }));
    REQUIRE(a.back().text == "client->host" && a.back().peer == id);
    REQUIRE(b.back().text == "host->client" && b.back().peer == 1);
}
void MultiplePeers() {
    Transport host; uint16_t port = StartHost(host);
    std::array<Transport,3> clients; std::array<uint32_t,3> ids;
    for (size_t i=0; i<clients.size(); ++i) ids[i] = Connect(host, clients[i], port);
    REQUIRE(ids[0] != ids[1] && ids[1] != ids[2]);
    for (size_t i=0; i<clients.size(); ++i) REQUIRE(host.Send(ids[i], "private-" + std::to_string(i)));
    for (size_t i=0; i<clients.size(); ++i) {
        Events events; REQUIRE(Wait(clients[i], events, [](auto& e) { return Has(e, Event::Type::Message); }));
        REQUIRE(Count(events, Event::Type::Message)==1); REQUIRE(events.back().text == "private-" + std::to_string(i));
    }
    for (size_t i=0; i<clients.size(); ++i) REQUIRE(clients[i].Send(1, std::to_string(i)));
    Events messages; REQUIRE(Wait(host, messages, [](auto& e) { return Count(e, Event::Type::Message)==3; }));
    for (const auto& e: messages) if(e.type == Event::Type::Message) REQUIRE(e.peer == ids[static_cast<size_t>(std::stoi(e.text))]);
}
void MaximumPayload() {
    Transport host, client; auto port=StartHost(host); auto id=Connect(host,client,port);
    std::string p(MaxFrameBytes,'x'); for(size_t i=0;i<p.size();++i) p[i]=static_cast<char>(i%256);
    REQUIRE(host.Send(id,p)); Events e; REQUIRE(Wait(client,e,[](auto& v){return Has(v,Event::Type::Message);})); REQUIRE(e.back().text==p);
    REQUIRE(!client.Send(1,"")); REQUIRE(!host.Send(id,std::string(MaxFrameBytes+1,'x')));
}
void BurstOrdering() {
    Transport host,client; auto port=StartHost(host); auto id=Connect(host,client,port);
    for(int i=0;i<500;++i) REQUIRE(host.Send(id,std::to_string(i)));
    Events e; REQUIRE(Wait(client,e,[](auto& v){return Count(v,Event::Type::Message)==500;}));
    int i=0; for(auto& event:e) if(event.type==Event::Type::Message) REQUIRE(event.text==std::to_string(i++));
}
void RejectionDrainsBeforeClose() {
    Transport host,client; auto port=StartHost(host); auto id=Connect(host,client,port);
    REQUIRE(host.Send(id,"Rejected: matching seed required")); host.Drop(id);
    Events e; REQUIRE(Wait(client,e,[](auto& v){return Has(v,Event::Type::Disconnected);}));
    REQUIRE(Count(e,Event::Type::Message)==1); REQUIRE(e.front().text=="Rejected: matching seed required");
}
void DisconnectAndReconnect() {
    Transport host,client; auto port=StartHost(host); auto id=Connect(host,client,port);
    client.Stop(); Events e; REQUIRE(Wait(host,e,[](auto& v){return Has(v,Event::Type::Disconnected);}));
    auto newId=Connect(host,client,port); REQUIRE(newId != id);
    host.Stop(); e.clear(); REQUIRE(Wait(client,e,[](auto& v){return Has(v,Event::Type::Disconnected);}));
}
void DuplicatePortAndRefusedConnection() {
    Transport host,duplicate,client; auto port=StartHost(host);
    REQUIRE(duplicate.Host(port)); Events e; REQUIRE(Wait(duplicate,e,[](auto& v){return Has(v,Event::Type::Error);}));
    host.Stop(); REQUIRE(client.Join("127.0.0.1",port)); e.clear();
    REQUIRE(Wait(client,e,[](auto& v){return Has(v,Event::Type::Error)||Has(v,Event::Type::Disconnected);}));
}
void CapacityLimit() {
    Transport host; auto port=StartHost(host); std::array<Transport,MaxPlayers> clients;
    for(size_t i=0;i<MaxPlayers-1;++i) (void)Connect(host,clients[i],port);
    REQUIRE(clients.back().Join("127.0.0.1",port)); Events e;
    REQUIRE(Wait(clients.back(),e,[](auto& v){return Has(v,Event::Type::Disconnected);}));
    Events server=host.Poll(); REQUIRE(!Has(server,Event::Type::Accepted));
}
void CancelAndReuse() {
    Transport transport; REQUIRE(!transport.Join("localhost",DefaultPort)); REQUIRE(!transport.Host(0));
    REQUIRE(transport.Join("192.0.2.1",DefaultPort));
    auto begin=Clock::now(); transport.Stop(); REQUIRE(Clock::now()-begin<1000ms); REQUIRE(!transport.Running());
    for(int i=0;i<12;++i) { (void)StartHost(transport); REQUIRE(!transport.Host(DefaultPort)); transport.Stop(); }
    REQUIRE(!transport.Send(1,"offline"));
}
#if defined(_WIN32)
using RawSocket=SOCKET; constexpr RawSocket InvalidSocket=INVALID_SOCKET;
void CloseRaw(RawSocket s){closesocket(s);}
#else
using RawSocket=int; constexpr RawSocket InvalidSocket=-1;
void CloseRaw(RawSocket s){close(s);}
#endif
struct RawPeer {
    RawSocket socket=InvalidSocket;
    explicit RawPeer(uint16_t port) {
        socket=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP); REQUIRE(socket!=InvalidSocket);
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(port); address.sin_addr.s_addr=htonl(0x7F000001);
        REQUIRE(::connect(socket,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
    }
    ~RawPeer(){if(socket!=InvalidSocket) CloseRaw(socket);}
    void Write(const std::string& bytes) {
        size_t offset=0;
        while(offset<bytes.size()) { int n=static_cast<int>(::send(socket,bytes.data()+offset,static_cast<int>(bytes.size()-offset),0)); REQUIRE(n>0); offset+=static_cast<size_t>(n); }
    }
};
void RejectBadWire() {
    for(const auto& bytes : {std::string("\0\0\0\0",4),std::string("\0\x08\0\x01",4)}) {
        Transport host; auto port=StartHost(host); RawPeer peer(port);
        Events e; REQUIRE(Wait(host,e,[](auto& v){return Has(v,Event::Type::Accepted);}));
        peer.Write(bytes); e.clear(); REQUIRE(Wait(host,e,[](auto& v){return Has(v,Event::Type::Disconnected);}));
        REQUIRE(!Has(e,Event::Type::Message)); REQUIRE(host.Running());
    }
}
void RawFragmentedWire() {
    Transport host; auto port=StartHost(host); RawPeer peer(port);
    Events e; REQUIRE(Wait(host,e,[](auto& v){return Has(v,Event::Type::Accepted);}));
    auto frame=EncodeFrame("fragmented")+EncodeFrame("joined");
    for(size_t i=0;i<frame.size();++i) { peer.Write(frame.substr(i,1)); std::this_thread::sleep_for(1ms); }
    e.clear(); REQUIRE(Wait(host,e,[](auto& v){return Count(v,Event::Type::Message)==2;}));
    REQUIRE(e[0].text=="fragmented"&&e[1].text=="joined");
}
void StopWithStalledReceiver() {
    Transport host; auto port=StartHost(host); RawPeer peer(port);
    Events e; REQUIRE(Wait(host,e,[](auto& v){return Has(v,Event::Type::Accepted);}));
    uint32_t id=e.back().peer; std::string payload(MaxFrameBytes,'x');
    bool bounded=false;
    for(int i=0;i<100;++i) if(!host.Send(id,payload)){bounded=true;break;}
    if (!bounded) { e.clear(); bounded = Wait(host,e,[](auto& v){return Has(v,Event::Type::Disconnected);},2000ms); }
    REQUIRE(bounded); auto begin=Clock::now(); host.Stop(); REQUIRE(Clock::now()-begin<1000ms);
}
} // namespace
int main(){
    const std::vector<std::pair<const char*,std::function<void()>>> tests{
        {"IPv4 validation",IPv4Validation},{"byte-by-byte frame decoding",FragmentedFrames},
        {"coalesced frames",CoalescedFrames},{"frame limits and decoder reset",DecoderBoundaryAndReset},
        {"600 randomized payloads / fragmentation",RandomFragmentation},{"packed upgrade-tier merge",UpgradeTiers},
        {"heart totals without manufactured progress",HeartProgress},{"inventory validation",InventoryValidation},
        {"permanent vs personal item slots",PersonalItemSlots},{"host/client bidirectional",Bidirectional},
        {"three peers / isolated addressing",MultiplePeers},{"512 KiB binary payload",MaximumPayload},
        {"500-message ordering",BurstOrdering},{"rejection flush before close",RejectionDrainsBeforeClose},
        {"disconnect / reconnect / host loss",DisconnectAndReconnect},{"occupied port / refused connection",DuplicatePortAndRefusedConnection},
        {"eight-player capacity",CapacityLimit},{"cancel / stop / repeated reuse",CancelAndReuse},
        {"invalid raw wire rejection",RejectBadWire},{"fragmented live TCP wire",RawFragmentedWire},
        {"bounded queues / shutdown with stalled receiver",StopWithStalledReceiver}
    };
    size_t failures=0;
    for(auto& [name,run]:tests) {
        try {run();std::cout<<"PASS "<<name<<std::endl;}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<std::endl;}
    }
    std::cout<<tests.size()-failures<<" / "<<tests.size()<<" cases passed"<<std::endl;
    return failures?1:0;
}
