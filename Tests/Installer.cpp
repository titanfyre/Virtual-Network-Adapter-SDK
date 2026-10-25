#include "../Installer/Setup.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
const char* valid=R"inf([Version]
Signature="$WINDOWS NT$"
Class=Net
ClassGuid={4d36e972-e325-11ce-bfc1-08002be10318}
Provider=%Provider%
DriverVer=10/25/2026,0.1.0.0
CatalogFile=TitanVirtualNetwork.cat
PnpLockdown=1

[Manufacturer]
%Provider%=Models,NTamd64.10.0...19041
[Models.NTamd64.10.0...19041]
%Device%=Install,ROOT\TitanVirtualNetwork

[Install.NT]
CopyFiles=CopyDriver
Characteristics=0x1
*IfType=6
*MediaType=0
*PhysicalMediaType=0
BusType=15
AddReg=NetworkSettings

[NetworkSettings]
HKR,Ndi,Service,0,"TitanVirtualNetwork"
HKR,Ndi\Interfaces,UpperRange,0,"ndis5"
HKR,Ndi\Interfaces,LowerRange,0,"ethernet"

[Install.NT.Services]
AddService=TitanVirtualNetwork,2,Service
[Service]
DisplayName=%Device%
ServiceType=1
StartType=3
ErrorControl=1
ServiceBinary=%13%\TitanVirtualNetwork.sys
LoadOrderGroup=NDIS

[DestinationDirs]
CopyDriver=13
[CopyDriver]
TitanVirtualNetwork.sys
[SourceDisksNames]
1=%Disk%,,,
[SourceDisksFiles]
TitanVirtualNetwork.sys=1
[Strings]
Provider="Titanfyre"
Device="Titan Virtual Ethernet Adapter"
Disk="Titan Virtual Network Adapter SDK"
)inf";
void Require(bool value){if(!value)throw std::runtime_error("Installer identity assertion failed.");}
std::string Replace(std::string text,const std::string& before,const std::string& after){auto pos=text.find(before);Require(pos!=std::string::npos);text.replace(pos,before.size(),after);return text;}
}
int wmain(){wchar_t folder[MAX_PATH]{},path[MAX_PATH]{};if(!GetTempPathW(MAX_PATH,folder)||!GetTempFileNameW(folder,L"vnt",0,path))return 1;
try{
 unsigned checks=0;auto check=[&](const std::string& text,bool expected){++checks;std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;file.close();if(Setup::PackageMatches(path)!=expected)throw std::runtime_error("Installer identity assertion failed at case "+std::to_string(checks));};
 check(valid,true);
 check(Replace(valid,"TitanVirtualNetwork.cat","Other.cat"),false);
 check(Replace(valid,"Titanfyre","OtherName"),false);
 check(Replace(valid,"ROOT\\TitanVirtualNetwork","ROOT\\OtherAdapter"),false);
 check(Replace(valid,"Class=Net","Class=System"),false);
 check(Replace(valid,"%Device%=Install,","%Device%=OtherInstall,"),false);
 check(Replace(valid,"AddService=TitanVirtualNetwork,","AddService=OtherService,"),false);
 check(Replace(valid,"CopyFiles=CopyDriver","CopyFiles=OtherFiles"),false);
 check(Replace(valid,"CopyDriver=13","CopyDriver=12"),false);
 check(Replace(valid,"[CopyDriver]\nTitanVirtualNetwork.sys","[CopyDriver]\nOther.sys"),false);
 check(Replace(valid,"%Device%=Install,ROOT\\TitanVirtualNetwork","%Device%=Install,ROOT\\TitanVirtualNetwork\n%Device%=Install,ROOT\\Other"),false);
 Require(!Setup::CatalogTrusted(path));++checks;
 DeleteFileW(path);std::cout<<checks<<" installer identity/signature refusal checks passed. No installation or registry change occurred.\n";return 0;
}catch(const std::exception& error){DeleteFileW(path);std::cerr<<error.what()<<'\n';return 1;}}
