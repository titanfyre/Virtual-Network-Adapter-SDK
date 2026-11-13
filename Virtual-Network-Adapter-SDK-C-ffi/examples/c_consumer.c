#include "VirtualNetworkAdapterSDKJsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(virtual_network_adapter_sdk_json_open(&session))return 1;char *reply=virtual_network_adapter_sdk_json_call(session,"{",1);if(!reply){virtual_network_adapter_sdk_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;virtual_network_adapter_sdk_json_free(reply);if(virtual_network_adapter_sdk_json_close(session))return 3;return failed;}
