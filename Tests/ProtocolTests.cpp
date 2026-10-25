#include "../SDK/NetworkAdapter.h"
#include <iostream>
#include <random>
#include <stdexcept>
int main() {
    unsigned checks=0;auto require=[&](bool ok){++checks;if(!ok)throw std::runtime_error("Frame boundary regression");};
    std::vector<unsigned char> frame;
    require(!ParseEthernetFrame("",frame));require(!ParseEthernetFrame("0 1",frame));
    require(!ParseEthernetFrame("00 01 02 03 04 05 06 07 08 09 0a 0b 0c",frame));
    require(ParseEthernetFrame("02:54:49:54:41:01:02:54:49:54:41:02:88:B5",frame));require(frame.size()==14&&frame[13]==0xb5);
    require(!ParseEthernetFrame("02 54 49 54 41 01 02 54 49 54 41 02 88 ZZ",frame));require(frame.empty());
    require(ParseEthernetFrame(std::string(VN_MAX_FRAME*2,'a'),frame));require(frame.size()==VN_MAX_FRAME);
    require(!ParseEthernetFrame(std::string((VN_MAX_FRAME+1)*2,'a'),frame));require(frame.empty());
    require(!ParseEthernetFrame(std::string(VN_MAX_FRAME*3+1,' '),frame));
    NetworkAdapter adapter;require(adapter.Send({})==ERROR_BAD_LENGTH);require(adapter.Send(std::vector<unsigned char>(VN_MAX_FRAME+1))==ERROR_BAD_LENGTH);
    std::mt19937 rng(0x17A54);constexpr char digits[]="0123456789abcdef";
    for(unsigned test=0;test<10000;++test){size_t length=14+rng()%(VN_MAX_FRAME-13);std::vector<unsigned char> expected;std::string hex;for(size_t i=0;i<length;++i){auto byte=static_cast<unsigned char>(rng());expected.push_back(byte);hex+=digits[byte>>4];hex+=digits[byte&15];}require(ParseEthernetFrame(hex,frame));require(frame==expected);hex+='f';require(!ParseEthernetFrame(hex,frame));require(frame.empty());}
    std::cout<<checks<<" frame parsing, ABI and bounded-input assertions passed. No driver installation or network changes.\n";
}
