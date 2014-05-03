/****************************************************************************
 *
 * MODULE:             JIP Web Apps
 *
 * COMPONENT:          JIP Generic Browser cgi program
 *
 * REVISION:           $Revision: 54601 $
 *
 * DATED:              $Date: 2013-06-10 14:38:04 +0100 (Mon, 10 Jun 2013) $
 *
 * AUTHOR:             Matt Redfearn
 *
 ****************************************************************************
 *
 * This software is owned by NXP B.V. and/or its supplier and is protected
 * under applicable copyright laws. All rights are reserved. We grant You,
 * and any third parties, a license to use this software solely and
 * exclusively on NXP products [NXP Microcontrollers such as JN5148, JN5142, JN5139]. 
 * You, and any third parties must reproduce the copyright and warranty notice
 * and any other legend of ownership on each copy or partial copy of the 
 * software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.

 * Copyright NXP B.V. 2012. All rights reserved
 *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include <Zeroconf.h> 
#include <JIP.h>

#include "CGI.h"

#define DISPLAY_JENNET_MIB

//#define TIME_ANALYSIS

#define CACHE_DEFINITIONS_FILE_NAME "/tmp/jip_cache_definitions.xml"
#define CACHE_NETWORK_FILE_NAME "/tmp/jip_cache_network.xml"

static int verbosity = 0;

#ifndef VERSION
#error Version is not defined!
#else
const char *Version = "0.5 (r" VERSION ")";
#endif

static tsJIP_Context sJIP_Context;

#ifdef TIME_ANALYSIS
struct timeval time_now;
#define TIME_NOW(a) gettimeofday(&time_now, NULL); printf("%s: %u.%u\n", a, (unsigned int)time_now.tv_sec, (unsigned int)time_now.tv_usec);fflush(stdout); 
#else
#define TIME_NOW(a)
#endif /* TIME_ANALYSIS */

static tsCGI sCGI;

static char *pcConnect_address = NULL;

static const int read_config(void)
{
    int iNumAddresses;
    struct in6_addr *asAddresses;
    if (ZC_Get_Module_Addresses(&asAddresses, &iNumAddresses) != 0)
    {
        printf("Could not get coordinator address\n");
    }
    else
    {
        if (iNumAddresses != 1)
        {
            printf("Discovered an unhandled number of coordinators (%d)\n", iNumAddresses);
        }
        else
        {
            char buffer[INET6_ADDRSTRLEN] = "Could not determine address\n";
            /* Just use the first one for now */
            inet_ntop(AF_INET6, asAddresses, buffer, INET6_ADDRSTRLEN);
            
            //printf("Got address %s\n", buffer);
            
            pcConnect_address = strdup(buffer);
        }
        free(asAddresses);
    }

    return 0;
}


int main(int argc, char *argv[])
{
    char *pcMode = NULL;
    char *pcNodeAddress = NULL;
    char *pcMulticastAddress = NULL;
    char *pcUpdateAddress = NULL;
    char *pcUpdateMib = NULL;
    char *pcUpdateVar = NULL;
    char *pcUpdateValue = NULL;
    char *pcMiB = NULL;

    if (eCGIReadVariables(&sCGI) != E_CGI_OK)
    {
        printf("Error initialising CGI\n\r");
        return -1;
    }

    pcMode = pcCGIGetValue(&sCGI, "Mode");
    if (!pcMode)
    {
        pcMode = "Network";
    }

    pcNodeAddress       = pcCGIGetValue(&sCGI, "nodeaddress");
    pcMiB               = pcCGIGetValue(&sCGI, "mib");
    
    pcMulticastAddress  = pcCGIGetValue(&sCGI, "mcastaddress");
    pcUpdateMib         = pcCGIGetValue(&sCGI, "mib");
    pcUpdateVar         = pcCGIGetValue(&sCGI, "var");
    pcUpdateValue       = pcCGIGetValue(&sCGI, "value");

    
    if (pcMulticastAddress)
    {
        pcUpdateAddress = pcMulticastAddress;
    }
    else
    {
        pcUpdateAddress = pcNodeAddress;
    }
    
    printf("Content-type: text/html\r\n\r\n");
    
    TIME_NOW("Read config");

    read_config();
    
    if (pcConnect_address == NULL)
    {
        printf("Failed to find gateway address\n");
        return -1;
    }

    if (eJIP_Init(&sJIP_Context, E_JIP_CONTEXT_CLIENT) != E_JIP_OK)
    {
        printf("JIP startup failed\n");
    }

    if (eJIP_Connect(&sJIP_Context, pcConnect_address, JIP_DEFAULT_PORT) != E_JIP_OK)
    {
        printf("JIP connect failed\n");
    }
    
    TIME_NOW("JIP Connected");
    
    /* Load the cached device id's and any network contents if possible */
    if (eJIPService_PersistXMLLoadDefinitions(&sJIP_Context, CACHE_DEFINITIONS_FILE_NAME) != E_JIP_OK)
    {
        // Couldn't load the definitions file, fall back to discovery.
        if (eJIPService_DiscoverNetwork(&sJIP_Context) != E_JIP_OK)
        {
            printf("JIP discover network failed\n");
        }
    }
    
    TIME_NOW("Network loaded");
    
    if (((pcUpdateAddress) && (pcUpdateMib) && (pcUpdateVar) && (pcUpdateValue)))
    {
        tsNode *psNode;
        tsMib *psMib;
        tsVar *psVar;
        
        /* Load the cached network if possible */
        if (eJIPService_PersistXMLLoadNetwork(&sJIP_Context, CACHE_NETWORK_FILE_NAME) != E_JIP_OK)
        {
            // Couldn't load the network file, fall back to discovery.
            if (eJIPService_DiscoverNetwork(&sJIP_Context) != E_JIP_OK)
            {
                printf("JIP discover network failed\n");
            }
        }

        printf("Update node %s, mib %s, var %s to value %s ... \n", pcUpdateAddress, pcUpdateMib, pcUpdateVar, pcUpdateValue);
        
        eJIP_Lock(&sJIP_Context);

        psNode = sJIP_Context.sNetwork.psNodes;
        while (psNode)
        {
            char buffer[INET6_ADDRSTRLEN] = "Could not determine address\n";
            inet_ntop(AF_INET6, &psNode->sNode_Address.sin6_addr, buffer, INET6_ADDRSTRLEN);
            
            if (pcMulticastAddress || (strcmp(pcUpdateAddress, buffer) == 0))
            {
                //printf("Found node to update\n");
                psMib = psJIP_LookupMib(psNode, NULL, pcUpdateMib);
                
                if (psMib)
                {
                    //printf("Found Mib to update\n");
                    psVar = psJIP_LookupVar(psMib, NULL, pcUpdateVar);
                    if (psVar)
                    {
                        char *buf = NULL;
                        int supported = 1, freeable = 1;
                        uint32_t u32Size = 0;
                        
                        //printf("Found variable to update\n");
                        
                        switch (psVar->eVarType)
                        {
                            case (E_JIP_VAR_TYPE_INT8):
                            case (E_JIP_VAR_TYPE_UINT8):
                                buf = malloc(sizeof(uint8_t));
                                errno = 0;
                                buf[0] = strtoul(pcUpdateValue, NULL, 0);
                                if (errno)
                                {
                                    printf("Invalid value: '%s'\n\r", pcUpdateValue);
                                    return 0;
                                }
                                break;
                            
                            case (E_JIP_VAR_TYPE_INT16):
                            case (E_JIP_VAR_TYPE_UINT16):
                            {
                                uint16_t u16Var;
                                errno = 0;
                                u16Var = strtoul(pcUpdateValue, NULL, 0);
                                if (errno)
                                {
                                    printf("Invalid value: '%s'\n\r", pcUpdateValue);
                                    return 0;
                                }
                                buf = malloc(sizeof(uint16_t));
                                memcpy(buf, &u16Var, sizeof(uint16_t));
                                break;
                            }
                                
                            case (E_JIP_VAR_TYPE_INT32):
                            case (E_JIP_VAR_TYPE_UINT32):
                            {
                                uint32_t u32Var;
                                errno = 0;
                                u32Var = strtoul(pcUpdateValue, NULL, 0);
                                if (errno)
                                {
                                    printf("Invalid value: '%s'\n\r", pcUpdateValue);
                                    return 0;
                                }
                                buf = malloc(sizeof(uint32_t));
                                memcpy(buf, &u32Var, sizeof(uint32_t));
                                break;
                            }
                            
                            case (E_JIP_VAR_TYPE_INT64):
                            case (E_JIP_VAR_TYPE_UINT64):
                            {
                                uint64_t u64Var;
                                errno = 0;
                                u64Var = strtoull(pcUpdateValue, NULL, 0);
                                if (errno)
                                {
                                    printf("Invalid value: '%s'\n\r", pcUpdateValue);
                                    return 0;
                                }
                                buf = malloc(sizeof(uint64_t));
                                memcpy(buf, &u64Var, sizeof(uint64_t));
                                break;
                            }
                            
                            case (E_JIP_VAR_TYPE_FLT):
                            {
                                float f32Var = strtof(pcUpdateValue, NULL);
                                buf = malloc(sizeof(uint32_t));
                                memcpy(buf, &f32Var, sizeof(uint32_t));
                                break;
                            }
                            
                            case (E_JIP_VAR_TYPE_DBL):
                            {
                                double d64Var = strtod(pcUpdateValue, NULL);
                                buf = malloc(sizeof(uint64_t));
                                memcpy(buf, &d64Var, sizeof(uint64_t));
                                break;
                            }
                                
                            case(E_JIP_VAR_TYPE_STR):
                            {
                                buf = pcUpdateValue;
                                //printf("Update to \"%s\"\n", buf);
                                u32Size = strlen(pcUpdateValue);
                                freeable = 0;
                                break;
                            }
                                
                            case(E_JIP_VAR_TYPE_BLOB):
                            {
                                int i, j;
                                buf = malloc(strlen(pcUpdateValue));
                                memset(buf, 0, strlen(pcUpdateValue));
                                if (strncmp(pcUpdateValue, "0x", 2) == 0)
                                {
                                    pcUpdateValue += 2;
                                }
                                u32Size = 0;
                                for (i = 0, j = 0; (i < strlen(pcUpdateValue)) && supported; i++)
                                {
                                    uint8_t u8Nibble = 0;
                                    if ((pcUpdateValue[i] >= '0') && (pcUpdateValue[i] <= '9'))
                                    {
                                        u8Nibble = pcUpdateValue[i]-'0';
                                        //printf("Got 0-9 0x%02x\n", );
                                    }
                                    else if ((pcUpdateValue[i] >= 'a') && (pcUpdateValue[i] <= 'f'))
                                    {
                                        u8Nibble = pcUpdateValue[i]-'a' + 0x0A;
                                    }
                                    else if ((pcUpdateValue[i] >= 'A') && (pcUpdateValue[i] <= 'F'))
                                    {
                                        u8Nibble = pcUpdateValue[i]-'A' + 0x0A;
                                    }
                                    else
                                    {
                                        printf("String contains non-hex character\n");
                                        supported = 0;
                                        break;
                                    }
                                        
                                    if ((u32Size & 0x01) == 0)
                                    {
                                        // Even numbered byte
                                        buf[j] = u8Nibble << 4;
                                    }
                                    else
                                    {
                                        buf[j] |= u8Nibble & 0x0F;
                                        j++;
                                    }
                                    u32Size++;
                                }
                                if (u32Size & 0x01)
                                {
                                    // Odd length string
                                    u32Size = (u32Size >> 1) + 1;
                                }
                                else
                                {
                                    u32Size = u32Size >> 1;
                                }
                                
                                break;
                            }
                            
                            default:
                                supported = 0;
                        }
                        
                        if (supported)
                        {
                            //printf("Attempting to set variable\n");
                            if (pcMulticastAddress)
                            {
                                tsJIPAddress MCastAddress;
                                int s;
                                
                                memset (&MCastAddress, 0, sizeof(struct sockaddr_in6));
                                MCastAddress.sin6_family  = AF_INET6;
                                MCastAddress.sin6_port    = htons(1873);
                                
                                s = inet_pton(AF_INET6, pcUpdateAddress, &MCastAddress.sin6_addr);
                                if (s <= 0)
                                {
                                    if (s == 0)
                                    {
                                        fprintf(stderr, "Unknown host: %s\n", pcUpdateAddress);
                                        return 0;
                                    }
                                    else if (s < 0)
                                    {
                                        perror("inet_pton failed");
                                        return 0;
                                    }
                                }
                                
                                if (eJIP_MulticastSetVar(&sJIP_Context, psVar, buf, u32Size, &MCastAddress, 2) != E_JIP_OK)
                                {
                                    printf("Error setting new value\n");
                                }
                                else
                                {
                                    printf("Success\n");
                                }
                            }
                            else
                            {
                                if (eJIP_SetVar(&sJIP_Context, psVar, buf, u32Size) != E_JIP_OK)
                                {
                                    printf("Error setting new value\n");
                                }
                                else
                                {
                                    printf("Success\n");
                                }
                            }
                        }
                        else
                        {
                            printf("Variable type not supported\n");
                        }
                        
                        if (freeable)
                        {
                            free(buf);
                        }
                        goto updated;
                    }
                }
            }
            psNode = psNode->psNext;
        }
updated:
        eJIP_Unlock(&sJIP_Context);
        TIME_NOW("Variable updated");
    }
    else
    {
        printf("<HTML><HEAD><TITLE>\n");
        printf("JenNet-IP Browser\n");
        printf("</TITLE>\n");
        printf("<link rel=\"stylesheet\" href=\"/style.css\" type=\"text/css\" media=\"screen\" />\n");
        
        printf("<script type=\"text/javascript\"> \n\
                function UpdateVariable(address, mib, variable, mcastaddress, value) \n\
                { \n\
                    var xmlhttp; \n\
                    if (window.XMLHttpRequest) \n\
                    {// code for IE7+, Firefox, Chrome, Opera, Safari \n\
                        xmlhttp=new XMLHttpRequest(); \n\
                    } \n\
                    else \n\
                    {// code for IE6, IE5 \n\
                        xmlhttp=new ActiveXObject(\"Microsoft.XMLHTTP\"); \n\
                    } \n\
                    var request; \n\
                    var path = \"Browser.cgi\"; \n\
                    if (mcastaddress) {\n\
                        request = \"mcastaddress=\" + mcastaddress \n\
                    }\n\
                    else {\n\
                        request = \"nodeaddress=\" + address \n\
                    }\n\
                    request = request + \"&mib=\" + mib \n\
                    request = request + \"&var=\" + variable \n\
                    request = request + \"&value=\" + value \n\
                    \n\
                    xmlhttp.onreadystatechange=function() \n\
                    { \n\
                        if (xmlhttp.readyState==4 && xmlhttp.status==200) \n\
                        { \n\
                            document.getElementById(\"result\").innerHTML=xmlhttp.responseText; \n\
                        } \n\
                    } \n\
                    xmlhttp.open(\"POST\",path,true); \n\
                    xmlhttp.setRequestHeader(\"Content-type\",\"application/x-www-form-urlencoded\"); \n\
                    xmlhttp.send(request); \n\
                } \n\
                </script>");
        
        printf("<script type=\"text/javascript\"> \n\
                function getWindowHeight() { \n\
                var windowHeight=0; \n\
                if (typeof(window.innerHeight)=='number') { \n\
                windowHeight=window.innerHeight; \n\
                } \n\
                else { \n\
                if (document.documentElement&& \n\
                document.documentElement.clientHeight) { \n\
                windowHeight= \n\
                document.documentElement.clientHeight; \n\
                } \n\
                else { \n\
                if (document.body&&document.body.clientHeight) { \n\
                windowHeight=document.body.clientHeight; \n\
                } \n\
                } \n\
                } \n\
                return windowHeight; \n\
                } \n\n\
                function setFooter() { \n\
                if (document.getElementById) { \n\
                var windowHeight=getWindowHeight(); \n\
                if (windowHeight>0) { \n\
                var contentHeight= \n\
                document.getElementById('content').offsetHeight + \n\
                document.getElementById('header').offsetHeight + \n\
                document.getElementById('navigation').offsetHeight; \n\
                var footerElement= \n\
                document.getElementById('footer'); \n\
                var footerHeight=footerElement.offsetHeight; \n\
                if (windowHeight-(contentHeight+footerHeight)>=0) { \n\
                footerElement.style.position='relative'; \n\
                footerElement.style.top=(windowHeight- \n\
                (contentHeight+footerHeight))+'px'; \n\
                } \n\
                else { \n\
                footerElement.style.position='static'; \n\
                } \n\
                } \n\
                } \n\
                } \n\
                window.onload = function() { \n\
                    setFooter(); \n\
                    var forms = document.getElementsByTagName('form'); \n\
                    for (var i = 0; i < forms.length; i++) { \n\
                        forms[i].reset(); \n\
                    } \n\
                } \n\
                window.onresize = function() { \n\
                setFooter(); \n\
                } \n\
                </script>\n");

        printf("</HEAD><body><div id=\"container\" >\n");
        
        printf("<div id=\"header\"><div style=\"float: left;\"><H1>NXP JenNet-IP Browser</H1></div>");
        printf("<div style=\"float: right;\"><img src=\"/img/NXP_logo.gif\"></div>");
   
        printf("</div>\n"); 

        printf("<div id=\"navigation\"><ul>\n");
    
        printf("  <li %s><a href=\"/cgi-bin/Browser.cgi?Mode=Network\">Network</a></li>\n", strcmp(pcMode, "Network") ? "": "class=\"selected\"");
        
        if (pcNodeAddress)
        {
            printf("  <li %s><a href=\"/cgi-bin/Browser.cgi?Mode=Node&nodeaddress=%s\">Node: %s</a></li>\n", strcmp(pcMode, "Node") ? "": "class=\"selected\"", pcNodeAddress, pcNodeAddress);
            
            if (pcMiB)
            {
                printf("  <li %s><a href=\"/cgi-bin/Browser.cgi?Mode=MiB&nodeaddress=%s&mib=%s\">MiB: %s</a></li>\n", strcmp(pcMode, "MiB") ? "": "class=\"selected\"", pcNodeAddress, pcMiB, pcMiB);
            }
        }
        printf("</ul></div>\n\n");
        printf("<div id=\"content\">\n");
        
        if (eJIPService_DiscoverNetwork(&sJIP_Context) != E_JIP_OK)
        {
            printf("JIP discover network failed\n");
        }

        if ((!pcNodeAddress))
        {
            // Show all available nodes
            tsNode *psNode;
            tsMib *psMib;
            tsVar *psVar;
            printf("<div>\n");
            printf("<P style=\"margin-left: 10px; \"><H2>Network Contents</H2></P>\n");
            
            eJIP_Lock(&sJIP_Context);
            
            psNode = sJIP_Context.sNetwork.psNodes;
            while (psNode)
            {
                char acName[255] = "Unknown";
                char *pcIPv6URL;
                char tempbuffer[INET6_ADDRSTRLEN] = "Could not determine address\n";
                inet_ntop(AF_INET6, &psNode->sNode_Address.sin6_addr, tempbuffer, INET6_ADDRSTRLEN);
                
                if (eCGIURLEncode(&pcIPv6URL, tempbuffer) != E_CGI_OK)
                {
                    pcIPv6URL = strdup("Unknown Address");
                }
                
                psMib = psJIP_LookupMib(psNode, NULL, "Node");

                if (psMib)
                {
                    psVar = psJIP_LookupVar(psMib, NULL, "DescriptiveName");
                    
                    if (psVar)
                    {
                        if (eJIP_GetVar(&sJIP_Context, psVar) == E_JIP_OK)
                        {
                            if ((psVar->pvData) && (psVar->eVarType == E_JIP_VAR_TYPE_STR))
                            {
                                sprintf(acName, "%s", ((uint8_t*)psVar->pvData));
                                printf("<div><H3><a href=\"/cgi-bin/Browser.cgi?Mode=Node&nodeaddress=%s\">%s</a></H3></div>\n", pcIPv6URL, acName);
                            }
                        }
                        else
                        {
                            printf("<div><H3><a href=\"/cgi-bin/Browser.cgi?Mode=Node&nodeaddress=%s\">Unknown name (%s)</a></H3></div>\n", pcIPv6URL, tempbuffer);
                        }
                    }
                }
                else
                {
                    printf("No Node Mib for address %s<BR>\n", tempbuffer);
                }
                free(pcIPv6URL);
                psNode = psNode->psNext;
            }
            eJIP_Unlock(&sJIP_Context);
            printf("</div>\n");
        }
        else
        {
            if (!pcMiB)
            {
                // Viewing a specific Node
                tsNode *psNode;
                tsMib *psMib;
                tsVar *psVar;

                printf("<div>\n");
                printf("<P style=\"margin-left: 10px; \"><H2>Node \"%s\" MiBs:</H2></P><HR>\n", pcNodeAddress);

                eJIP_Lock(&sJIP_Context);
                
                psNode = sJIP_Context.sNetwork.psNodes;
                while (psNode)
                {
                    char buffer[INET6_ADDRSTRLEN] = "Could not determine address\n";
                    inet_ntop(AF_INET6, &psNode->sNode_Address.sin6_addr, buffer, INET6_ADDRSTRLEN);
                    
                    if (strcmp(pcNodeAddress, buffer))
                    {
                        /* Not the node - next */
                        psNode = psNode->psNext;
                        continue;
                    }

                    psMib = psNode->psMibs;
                    while (psMib)
                    {
#ifndef DISPLAY_JENNET_MIB
                        if (strcmp("JenNet", psMib->pcName) == 0)
                        {
                            /* Don't care much about this */
                            psMib = psMib->psNext;
                            continue;
                        }
#endif /* DISPLAY_JENNET_MIB */

                        if (!pcUpdateMib)
                        {
                            printf("  <div class=\"MiB\"><span>MiB ID 0x%08x</span><a href=\"/cgi-bin/Browser.cgi?Mode=MiB&nodeaddress=%s&mib=%s\"><H2>%s</H2></a></div><HR>\n", 
                                   psMib->u32MibId, buffer, psMib->pcName, psMib->pcName);
                        }
                        psMib = psMib->psNext;
                    }
                    psNode = psNode->psNext;
                }
                eJIP_Unlock(&sJIP_Context);
                printf("</div>\n");
            }
            else
            {
                // Viewing a specific MiB on a specific Node
                tsNode *psNode;
                tsMib *psMib;
                tsVar *psVar;
                int VarID = 0;
                
                printf("<div>\n");
                printf("<P style=\"margin-left: 10px; \"><H2>MiB \"%s\" on Node \"%s\" variables:</H2></P><HR>\n", pcMiB, pcNodeAddress);

                eJIP_Lock(&sJIP_Context);
                
                psNode = sJIP_Context.sNetwork.psNodes;
                while (psNode)
                {
                    char buffer[INET6_ADDRSTRLEN] = "Could not determine address\n";
                    inet_ntop(AF_INET6, &psNode->sNode_Address.sin6_addr, buffer, INET6_ADDRSTRLEN);
                    
                    if (strcmp(pcNodeAddress, buffer))
                    {
                        /* Not the node - next */
                        psNode = psNode->psNext;
                        continue;
                    }

                    psMib = psNode->psMibs;
                    while (psMib)
                    {
                        if (strncmp(pcMiB, psMib->pcName, 255) == 0)
                        {
                            psVar = psMib->psVars;
                            while (psVar)
                            {
                                char *acCurrentValue = malloc(255);
                                uint32_t u32CurrentValueLength = 255;
                                
                                if (eJIP_GetVar(&sJIP_Context, psVar) == E_JIP_OK)
                                {
                                    if (psVar->pvData)
                                    {
                                        switch (psVar->eVarType)
                                        {
#define TEST(a, b, c) case  (a): sprintf(acCurrentValue, b, *((c*)psVar->pvData)); break
                                            TEST(E_JIP_VAR_TYPE_INT8,  "%d\n",  uint8_t);
                                            TEST(E_JIP_VAR_TYPE_UINT8,  "%u\n",  uint8_t);
                                            TEST(E_JIP_VAR_TYPE_INT16,  "%d\n",  uint16_t);
                                            TEST(E_JIP_VAR_TYPE_UINT16,  "%u\n",  uint16_t);
                                            TEST(E_JIP_VAR_TYPE_INT32, "%d\n",  uint32_t);
                                            TEST(E_JIP_VAR_TYPE_UINT32, "%u\n",  uint32_t);
                                            TEST(E_JIP_VAR_TYPE_INT64, "%llx\n", uint64_t);
                                            TEST(E_JIP_VAR_TYPE_UINT64, "%llx\n", uint64_t);
                                            TEST(E_JIP_VAR_TYPE_FLT,   "%f\n",  float);
                                            TEST(E_JIP_VAR_TYPE_DBL,   "%f\n",  double);
                                            case  (E_JIP_VAR_TYPE_STR): 
                                                sprintf(acCurrentValue, "%s\n", ((uint8_t*)psVar->pvData)); 
                                                break;
                                            case (E_JIP_VAR_TYPE_BLOB):
                                            {
                                                uint32_t i, u32Position = 0;
                                                u32Position += sprintf(acCurrentValue, "0x");
                                                for (i = 0; i < psVar->u8Size; i++)
                                                {
                                                    u32Position += sprintf(&acCurrentValue[u32Position], "%02x", ((uint8_t*)psVar->pvData)[i]);
                                                }
                                                break;
                                            }
                                            case (E_JIP_VAR_TYPE_TABLE_BLOB):
                                            {
                                                tsTable *psTable;
                                                tsTableRow *psTableRow;
                                                psTable = (tsTable *)psVar->pvData;
                                                uint32_t u32CurrentValuePos = 0;
                                                int i;
                                                
                                                if (psTable->u32NumRows > 0)
                                                {

                                                    for (i = 0; i < psTable->u32NumRows; i++)
                                                    {
                                                        uint8_t *pcNewCurrentValue;
                                                        psTableRow = &psTable->psRows[i];
                                                        
                                                        if (psTableRow->pvData)
                                                        {
                                                            uint32_t j;

                                                            u32CurrentValuePos += sprintf(&acCurrentValue[u32CurrentValuePos], "<P style=\"margin-left: 50px; \"> %03d { 0x", i);
                                                            for (j = 0; j < psTableRow->u32Length; j++)
                                                            {
                                                                u32CurrentValuePos += sprintf(&acCurrentValue[u32CurrentValuePos], "%02x", ((uint8_t*)psTableRow->pvData)[j]);
                                                            }
                                                            u32CurrentValuePos += sprintf(&acCurrentValue[u32CurrentValuePos], " }</P>\n");
                                                        }
                                                        else
                                                        {
                                                            u32CurrentValuePos += sprintf(&acCurrentValue[u32CurrentValuePos], "<P style=\"margin-left: 50px; \"> %03d { Empty Row }</P>\n", i);
                                                        }
                                                        u32CurrentValueLength = u32CurrentValuePos + 255;

                                                        pcNewCurrentValue = realloc(acCurrentValue, u32CurrentValueLength);
                                                        
                                                        if (!pcNewCurrentValue)
                                                        {
                                                            printf("Failed to print Table\n");
                                                            break;
                                                        }
                                                        acCurrentValue = pcNewCurrentValue;
                                                    }
                                                }
                                                else
                                                {
                                                    sprintf(acCurrentValue, "Empty table\n");
                                                }
                                                break;
                                            }
                                            default: sprintf(acCurrentValue, "Unknown Type\n");
#undef TEST
                                        }
                                    }
                                    else
                                    {
                                        sprintf(acCurrentValue, "?\n");
                                    }
                                }
                                else
                                {
                                    sprintf(acCurrentValue, "Error reading variable\n");
                                }                  

                                if (psVar->eAccessType == E_JIP_ACCESS_TYPE_READ_WRITE)
                                {
                                    printf("<div class=\"Var\">");
                                    printf("<span>Variable Index %d</span>\n", psVar->u8Index);
                                    printf("<H2>%s</H2>", psVar->pcName);
                                    printf("<form name=\"Var%dEditForm\">\n", VarID);
                                    
                                    printf("<script language=\"javascript\"> \n\
                                        var VarEdit%dChange = function() { \
                                        UpdateVariable('%s', '%s', '%s', document.Var%dEditForm.mcastaddress.value, document.Var%dEditForm.value.value) \n} \n</script>"
                                        , VarID, buffer, psMib->pcName, psVar->pcName, VarID, VarID);
                            
                                    printf("<div align=\"left\" style=\"position:relative; float:left;\" ><input type=\"text\" name=\"value\" value=\"%s\" /></div>\n", acCurrentValue);

                                    printf("<div align=\"right\" style=\"position:relative; float:right\">\n");
                                    
                                    printf("Set via Multicast Address: <input type=\"text\", name=\"mcastaddress\", value=\"%s\">\n", pcMulticastAddress ? pcMulticastAddress : "");
                                    
                                    printf("<input type=\"button\" value=\"Update\" onclick=\"VarEdit%dChange();\"/>\n", VarID);
                                    printf("</div>\n");
                                    
                                    printf("</form>\n");
                                    VarID++;
                                    
                                    printf("</div>");
                                }
                                else
                                {
                                    printf("<div class=\"Var\"><span>Variable Index %d</span><H2>%s</H2><BR>%s</div>", psVar->u8Index, psVar->pcName, acCurrentValue);
                                }
                                printf("<HR>\n");
                                psVar = psVar->psNext;
                            }
                        }
                        psMib = psMib->psNext;
                    }
                    psNode = psNode->psNext;
                }
                eJIP_Unlock(&sJIP_Context);
                printf("</div>\n");
            }
        }
        
        printf("<div id=\"result\"></div>\n");
        printf("</div>"); //content
        printf("<div id=\"footer\">JIP Browser cgi version %s using libJIP version %s</div>\n", Version, JIP_Version);
        printf("</div>"); //Container
        printf("</body></HTML>\n");
        
        TIME_NOW("Content generated");
    }

    /* Save the device id's and network contents */
    (void)eJIPService_PersistXMLSaveDefinitions(&sJIP_Context, CACHE_DEFINITIONS_FILE_NAME);
    (void)eJIPService_PersistXMLSaveNetwork(&sJIP_Context, CACHE_NETWORK_FILE_NAME);

    TIME_NOW("Network saved");
    
    eJIP_Destroy(&sJIP_Context);

}
