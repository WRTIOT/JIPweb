/****************************************************************************
 *
 * MODULE:             JIP Web Apps
 *
 * COMPONENT:          Smart devices cgi program
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

#include <libxml/encoding.h>
#include <libxml/xmlreader.h>

#include <Zeroconf.h> 
#include <JIP.h>

#include "CGI.h"

#define CACHE_DEFINITIONS_FILE_NAME "/tmp/jip_cache_definitions.xml"
#define CACHE_NETWORK_FILE_NAME "/tmp/jip_cache_network.xml"

#define CONFIG_FILE_NAME "/etc/SmartDevicesCgiConfig.xml"
#define CONFIG_FILE_VERSION 1


/* Configuration file example format:
 * 
<?xml version="1.0" encoding="ISO-8859-1"?>
<SmartDevicesCgiConfig Version="1">
  <Device ID="0x80821cab" Name="Lamp">
    <!--<Image src="/lamp.gif" />-->
    <StateControl MiB="BulbControl" Var="Mode"/>
    <LevelControl MiB="BulbControl" Var="LumTarget" Max="255"/>
  </Device>
  <Device ID="0x00000002" Name="Plug">
    <StateControl MiB="DeviceControl" Var="Mode"/>
    <LevelFeedback MiB="PlugStatus" Var="P" Label="Power(W)"/>
  </Device>
  <Device ID="0x80ae000a" Name="Relay">
    <StateControl MiB="Relay" Var="Relay One"/>
  </Device>
  
  <Device BaseType="0xE1" Name="DimmableBulb">
    <Image src="/lamp.gif" />
    <StateControl MiB="BulbControl" Var="Mode"/>
    <LevelControl MiB="BulbControl" Var="LumTarget" Max="255"/>
  </Device>

  <Groups>
    <StateControl MiB="BulbControl" Var="Mode"/>
    <LevelControl MiB="BulbControl" Var="LumTarget" Max="255"/>
    <Global Name="Control all" Address="ff15::f00f"/>
    <Group Name="Hall" Address="ff15::a00a"/>
    <Group Name="Lounge" Address="ff15::b00b"/>
  </Groups>

  <Scenes>
    <SceneControl MiB="BulbControl" Var="SceneId" />
    <Scene Name="Home" Address="ff15::f00f" Value="1" />
    <Scene Name="Away" Address="ff15::f00f" Value="2" />
    <Scene Name="Watch TV" Image="/tv.png" Address="ff15::f00f" Value="3" />
  </Scenes>
</SmartDevicesCgiConfig>
*/

#ifndef VERSION
#error Version is not defined!
#else
const char *Version = "0.7 (r" VERSION ")";
#endif

//#define BUILD_SENSOR

static tsJIP_Context sJIP_Context;

static tsCGI sCGI;

static char *pcConnect_address = NULL;

/* Individual device control */

typedef enum {
    E_LOOKUP_NONE,
    E_LOOKUP_DEVICEID,
    E_LOOKUP_BASETYPE,
} teDeviceLookup;

typedef struct
{
    teDeviceLookup eDeviceLookup;
                
    uint32_t    u32DeviceId;
    uint8_t     u8BaseType;
    
    char        *pcName;
    
    char        *pcImage;
    
    char        *pcStateControlMib;
    char        *pcStateControlVar;
    
    char        *pcLevelControlMib;
    char        *pcLevelControlVar;
    char        *pcLevelControlMax;
    
    char        *pcLevelFeedbackMib;
    char        *pcLevelFeedbackVar;
    char        *pcLevelFeedbackLabel;
} tsDevice;

static uint32_t u32NumDevices = 0;
static tsDevice *psDevices = NULL;


/* Group control */

static char *pcGroupStateControlMib = NULL;
static char *pcGroupStateControlVar = NULL;

static char *pcGroupLevelControlMib = NULL;
static char *pcGroupLevelControlVar = NULL;
static char *pcGroupLevelControlMax = NULL;

typedef struct
{
    char *pcName;
    char *pcAddress;
} tsGroup;

static uint32_t u32NumGroups = 0;
static tsGroup *psGroups = NULL;

/* Global Group */
static tsGroup *psGlobalGroup = NULL;


/* Scene control */

static char *pcSceneControlMib = NULL;
static char *pcSceneControlVar = NULL;

typedef struct
{
    char *pcName;
    char *pcAddress;
    char *pcImage;
    char *pcValue;
} tsScene;

static uint32_t u32NumScenes = 0;
static tsScene *psScenes = NULL;

static void
processNode(xmlTextReaderPtr reader)
{
    char *NodeName;
    static int iValidConfigFile = 0;
    static enum 
    {
        E_NONE, E_DEVICE, E_GROUPS, E_SCENES
    } eState = E_NONE;
    
    NodeName = (char *)xmlTextReaderName(reader);
    
    if (strcmp(NodeName, "SmartDevicesCgiConfig") == 0)
    {
        if (xmlTextReaderAttributeCount(reader) > 0)
        {
            char *pcVersion;
            long int u32Version;
            
            pcVersion = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Version");
            if (pcVersion == NULL)
            {
                u32Version = 0;
            }
            else
            {
                errno = 0;
                u32Version = strtoll(pcVersion, NULL, 16);
                if (errno)
                {
                    u32Version = 0;
                    perror("strtol");
                }
                free(pcVersion);
            }
            
            if (u32Version == CONFIG_FILE_VERSION)
            {
                iValidConfigFile = 1;
                //printf("Valid config file\n");
            }
        }
    }
    
    if (!iValidConfigFile)
    {
        /* Not a valid config file */
        return;
    }
    
    if (strcmp(NodeName, "Device") == 0)
    {
        if (xmlTextReaderAttributeCount(reader) == 2)
        {
            teDeviceLookup eDeviceLookup = E_LOOKUP_NONE;
            char *pcDeviceId = NULL;
            uint32_t u32DeviceId = 0;
            char *pcBaseType = NULL;
            uint8_t u8BaseType = 0;
            char *pcName     = NULL;
            
            pcDeviceId = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"ID");
            if (pcDeviceId != NULL)
            {
                errno = 0;
                u32DeviceId = strtoll(pcDeviceId, NULL, 16);
                if (errno)
                {
                    perror("strtol");
                    return;
                }
                eDeviceLookup = E_LOOKUP_DEVICEID;
                free(pcDeviceId);
            }
            
            pcBaseType = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"BaseType");
            if (pcBaseType != NULL)
            {
                errno = 0;
                u8BaseType = (uint8_t)strtoll(pcBaseType, NULL, 16);
                if (errno)
                {
                    perror("strtol");
                    return;
                }
                eDeviceLookup = E_LOOKUP_BASETYPE;
                free(pcBaseType);
            }
            
            if (eDeviceLookup == E_LOOKUP_NONE)
            {
                /* Need either device ID or base type to be specified */
                return;
            }
            
            pcName = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Name");
            if (pcName == NULL)
            {
                return;
            }
            
            u32NumDevices++;
            {
                tsDevice *psNewDevices = realloc(psDevices, sizeof(tsDevice) * (u32NumDevices));
                
                if (!psNewDevices)
                {
                    return;
                }
                else
                {
                    psDevices = psNewDevices;
                }
            }

            memset(&psDevices[u32NumDevices-1], 0, sizeof(tsDevice));
            psDevices[u32NumDevices-1].eDeviceLookup = eDeviceLookup;
            psDevices[u32NumDevices-1].u32DeviceId = u32DeviceId;
            psDevices[u32NumDevices-1].u8BaseType = u8BaseType;
            psDevices[u32NumDevices-1].pcName = pcName;
            
            eState = E_DEVICE;

            //printf("Got Device, ID: 0x%08x, Name: %s\n", u32DeviceId, pcName);
            return;
        }
    }
    else if (strcmp(NodeName, "Groups") == 0)
    {
        if (xmlTextReaderAttributeCount(reader) == 0)
        {
            eState = E_GROUPS;
            return;
        }
    }
    else if (strcmp(NodeName, "Scenes") == 0)
    {
        if (xmlTextReaderAttributeCount(reader) == 0)
        {
            eState = E_SCENES;
            return;
        }
    }
    
    
    switch (eState)
    {
        case E_DEVICE:
        {
            if (strcmp(NodeName, "Image") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 1)
                {
                    psDevices[u32NumDevices-1].pcImage = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"src");
                    return;
                }
            }
            else if (strcmp(NodeName, "StateControl") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 2)
                {
                    psDevices[u32NumDevices-1].pcStateControlMib = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"MiB");
                    psDevices[u32NumDevices-1].pcStateControlVar = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Var");
                    return;
                }
            }
            else if (strcmp(NodeName, "LevelControl") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 3)
                {
                    psDevices[u32NumDevices-1].pcLevelControlMib = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"MiB");
                    psDevices[u32NumDevices-1].pcLevelControlVar = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Var");
                    psDevices[u32NumDevices-1].pcLevelControlMax = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Max");
                    return;
                }
            }
            else if (strcmp(NodeName, "LevelFeedback") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 3)
                {
                    char *pcLocalUpdate;
                    psDevices[u32NumDevices-1].pcLevelFeedbackMib   = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"MiB");
                    psDevices[u32NumDevices-1].pcLevelFeedbackVar   = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Var");
                    psDevices[u32NumDevices-1].pcLevelFeedbackLabel = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Label");
                    return;
                }
            }
            
            break;
        }
        
        case E_GROUPS:
        {
            if (strcmp(NodeName, "StateControl") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 2)
                {
                    pcGroupStateControlMib      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"MiB");
                    pcGroupStateControlVar      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Var");
                    return;
                }
            }
            else if (strcmp(NodeName, "LevelControl") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 3)
                {
                    pcGroupLevelControlMib      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"MiB");
                    pcGroupLevelControlVar      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Var");
                    pcGroupLevelControlMax      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Max");
                    return;
                }
            }
            else if (strcmp(NodeName, "Global") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 2)
                {
                    psGlobalGroup = malloc(sizeof(tsGroup));
                    if (!psGlobalGroup) return;
                    psGlobalGroup->pcName       = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Name");
                    psGlobalGroup->pcAddress    = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Address");
                    
                    if (!psGlobalGroup->pcName || !psGlobalGroup->pcAddress)
                    {
                        free(psGlobalGroup);
                        psGlobalGroup = NULL;
                    }
                    else
                    {
                        //printf("Loaded global group\n");
                    }
                    return;
                }
            }
            else if (strcmp(NodeName, "Group") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 2)
                {
                    char *pcName = NULL;
                    char *pcAddress = NULL;
                    
                    pcName      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Name");
                    pcAddress   = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Address");
                    
                    if (!(pcName && pcAddress))
                    {
                        return;
                    }
                    
                    u32NumGroups++;
                    {
                        tsGroup *psNewGroups = realloc(psGroups, sizeof(tsGroup) * (u32NumGroups));
                        
                        if (!psNewGroups)
                        {
                            return;
                        }
                        else
                        {
                            psGroups = psNewGroups;
                        }
                    }

                    psGroups[u32NumGroups-1].pcName     = pcName;
                    psGroups[u32NumGroups-1].pcAddress  = pcAddress;
                    
                    //printf("Loaded group name %s, address %s\n", pcName, pcAddress);
                    return;
                }
            }
            
            break;
        }
            
        case E_SCENES:
        {
            if (strcmp(NodeName, "SceneControl") == 0)
            {
                if (xmlTextReaderAttributeCount(reader) == 2)
                {
                    pcSceneControlMib      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"MiB");
                    pcSceneControlVar      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Var");
                    //printf("Scene control MiB is %s, var is %s\n", pcSceneControlMib, pcSceneControlVar);
                    return;
                }
            }
            else if (strcmp(NodeName, "Scene") == 0)
            {
                int attributes = xmlTextReaderAttributeCount(reader);
                if (attributes >= 3 && attributes <= 4)
                {
                    char *pcName = NULL;
                    char *pcAddress = NULL;
                    char *pcValue = NULL;
                    char *pcImage = NULL;
                    
                    pcName      = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Name");
                    pcAddress   = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Address");
                    pcValue     = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Value");
                    pcImage     = (char *)xmlTextReaderGetAttribute(reader, (unsigned char *)"Image");
                    
                    if (!(pcName && pcAddress && pcValue))
                    {
                        return;
                    }
                    
                    u32NumScenes++;
                    {
                        tsScene *psNewScenes = realloc(psScenes, sizeof(tsScene) * (u32NumScenes));
                        
                        if (!psNewScenes)
                        {
                            return;
                        }
                        else
                        {
                            psScenes = psNewScenes;
                        }
                    }

                    psScenes[u32NumScenes-1].pcName     = pcName;
                    psScenes[u32NumScenes-1].pcAddress  = pcAddress;
                    psScenes[u32NumScenes-1].pcValue    = pcValue;
                    psScenes[u32NumScenes-1].pcImage    = pcImage;
                    
                    //printf("Loaded Scene name %s, address %s, value %s, image %s\n", pcName, pcAddress, pcValue, pcImage);
                }
            }
        }
    }
    
    return;
}


static const int read_config(void)
{
    xmlTextReaderPtr reader;
    int ret;
    
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
    
    /* Load xml config file */
    LIBXML_TEST_VERSION
    
    reader = xmlReaderForFile(CONFIG_FILE_NAME, NULL, 0);
    if (reader != NULL)
    {
        ret = xmlTextReaderRead(reader);
        //printf("Read: %d\n", ret);
        while (ret == 1)
        {
            processNode(reader);
            ret = xmlTextReaderRead(reader);
        }
        xmlFreeTextReader(reader);
        if (ret != 0) {
            return 0;
        }
    } else {
        return 0;
    }
    

    return 0;
}


int Menu(const char *pcName, const char *pcAddress, const char *pcImage,
         const char *pcStateControlMib, const char *pcStateControlVar,
         const char *pcLevelControlMib, const char *pcLevelControlVar, const char *pcLevelControlMax,
         const char *pcLevelFeedbackMib, const char *pcLevelFeedbackVar, const char *pcLevelFeedbackLabel)
{
    static int MenuID = 0;
    char *pcIPv6URL;
    
    if (eCGIURLEncode(&pcIPv6URL, pcAddress) != E_CGI_OK)
    {
        pcIPv6URL = strdup("Unknown Address");
    }

    printf("<div class=\"Lamp\">");
    printf("<span>IPv6 Address: %s</span>\n", pcAddress);
    printf("<h2>%s</h2>\n", pcName);

    printf("<div class=\"Lamp_Image\">");
    if (pcImage)
    {
        printf("  <img src=\"%s\" />\n", pcImage);
    }
    printf("</div>\n\n");
    
    if ((pcStateControlMib) && (pcStateControlVar))
    {
        printf("  <div class=\"button\" onclick=\"UpdateVariable('%s', '%s', '%s', '0')\">Off</div> \n", 
               pcIPv6URL, pcStateControlMib, pcStateControlVar);
        printf("  <div class=\"button\" onclick=\"UpdateVariable('%s', '%s', '%s', '1')\">On</div>\n", 
               pcIPv6URL, pcStateControlMib, pcStateControlVar);
    }
    
    if ((pcLevelControlMib) && (pcLevelControlVar) && (pcLevelControlMax))
    {
        printf("  <div class=\"slider\" id=\"slider%d\" ></div> \n\
            <script language=\"javascript\"> \n\
            var lampslider%d = new SimpleSlider(\"slider%d\", 710, 50); \n\
            lampslider%d.onNewPosition = function() { \
                var pos = parseInt(lampslider%d.position * %s); \n", 
               MenuID, MenuID, MenuID, MenuID, MenuID, pcLevelControlMax);

        printf("                UpdateVariable('%s', '%s', '%s', pos) \n} \n</script>", 
               pcIPv6URL, pcLevelControlMib, pcLevelControlVar);
    }
    
    if ((pcLevelFeedbackMib) && (pcLevelFeedbackVar) && (pcLevelFeedbackLabel))
    {
        
        printf("<div class='feedback' id='feedback%d'> \n \
            <script language=\"javascript\"> \n\
            var Monitor%d = new JIP_Monitor(%d, '%s', '%s', '%s', function(value) { \n\
                if (typeof(value)=='string') \n\
                { \n\
                    var newvalue=parseInt(value);\n\
                    if (newvalue < 0) \n\
                    { \n\
                        newvalue = 0; \n\
                    } \n\
                    value = newvalue.toString(); \n\
                    document.getElementById('feedback%d').innerHTML = '%s ' + value; \n\
                    feedback_graph%d_update(value); \n\
                } \n\
                else \n\
                { \n\
                    alert(\"Non-string!\"); \n\
                } \n\
            })\n\
            </script> \n\
            </div>\n",
            MenuID, MenuID, MenuID, pcIPv6URL, pcLevelFeedbackMib, pcLevelFeedbackVar, MenuID, pcLevelFeedbackLabel, MenuID);
        
        printf("<div> \n\
            <canvas class='feedback_graph' id='feedback_graph%d'></canvas> \n\
            \n\
            </div> \n\
            <script language=\"javascript\"> \n\
            var history_length = 60; \n\
            var history%d = [] ; \n\
             \n\
            /* wait for drawing */ \n\
            window.setTimeout( \n\
            function() { \n\
            /* prefill datasets */ \n\
            for (var i = 0; i < history_length; i++) \n\
            { \n\
                history%d[i] = 0; \n\
            } \n\
            }, 1000)\n\
            \n\
            function feedback_graph%d_update(value) \n\
            {\n\
                var newvalue=parseFloat(value);\n\
                if (isNaN(newvalue))\n\
                {\n\
                return;\n\
                }\n\
                history%d.push(newvalue);\n\
                history%d = history%d.slice(history%d.length - history_length, history%d.length); \n\
                var data_max = 0; \n\
                for (var i = 0; i < history%d.length; i++) \n\
                {\n\
                    data_max = Math.max(data_max, history%d[i]);\n\
                }\n\
                if (data_max < 50) data_max = 50; \n\
                var canvas  = document.getElementById('feedback_graph%d'); \n\
                var ctx     = canvas.getContext(\"2d\"); \n\
                var step   = canvas.width / history_length;\n\
                var data_scale = canvas.height / (data_max * 1.2); \n\
                ctx.clearRect(0,0,canvas.width, canvas.height); \n\
                ctx.strokeStyle='#222222'\n\
                ctx.beginPath(); \n\
                ctx.moveTo(0, Math.floor(canvas.height - 8 - history%d[0] * data_scale)); \n\
                for (var i = 1; i < history%d.length; i++)\n\
                {\n\
                    ctx.lineTo((i * step), Math.floor(canvas.height - 8 - history%d[i] * data_scale));\n\
                }\n\
                ctx.stroke()\n\
            \n\
            }\n\
            \n\
            </script> \n\
            ", MenuID, MenuID, MenuID, MenuID, MenuID, MenuID, MenuID, MenuID, MenuID, MenuID, MenuID, MenuID, MenuID,  MenuID, MenuID);

    }

    free(pcIPv6URL);
    
    printf("</div>\n\n");
    
    MenuID++;
    
    return 0;
}


int DeviceMenu(tsDevice *psDevice, const char *pcName, const char *pcAddress)
{
    return Menu(pcName, pcAddress, psDevice->pcImage,
                psDevice->pcStateControlMib, psDevice->pcStateControlVar,
                psDevice->pcLevelControlMib, psDevice->pcLevelControlVar, psDevice->pcLevelControlMax,
                psDevice->pcLevelFeedbackMib, psDevice->pcLevelFeedbackVar, psDevice->pcLevelFeedbackLabel);
}


int GroupMenu(tsGroup *psGroup)
{
    return Menu(psGroup->pcName, psGroup->pcAddress, NULL,
                pcGroupStateControlMib, pcGroupStateControlVar,
                pcGroupLevelControlMib, pcGroupLevelControlVar, pcGroupLevelControlMax,
                NULL, NULL, NULL);
}


int SceneMenu(tsScene *psScene)
{
    char *pcIPv6URL;
    
    if (eCGIURLEncode(&pcIPv6URL, psScene->pcAddress) != E_CGI_OK)
    {
        pcIPv6URL = strdup("Unknown Address");
    }
    
    printf("<div class=\"Scene\" onclick=\"UpdateVariable('%s', '%s', '%s', '%s')\">%s", 
           pcIPv6URL, pcSceneControlMib, pcSceneControlVar, psScene->pcValue, psScene->pcName);

    if (psScene->pcImage)
    {
        printf("<img src=\"%s\" />\n", psScene->pcImage);
    }
    printf("</div>");
    free(pcIPv6URL);
    return 0;
}



int main(int argc, char *argv[])
{
    char *pcUpdateAddress;
    char *pcUpdateMib;
    char *pcUpdateVar;
    char *pcUpdateValue;
    
    char *pcViewAddress;
    char *pcMode;
    
    if (eCGIReadVariables(&sCGI) != E_CGI_OK)
    {
        printf("Error initialising CGI\n\r");
        return -1;
    }
    
    pcMode = pcCGIGetValue(&sCGI, "Mode");
    if (!pcMode)
    {
        pcMode = "Global";
    }
    
    pcUpdateAddress     = pcCGIGetValue(&sCGI, "address");
    pcUpdateMib         = pcCGIGetValue(&sCGI, "mib");
    pcUpdateVar         = pcCGIGetValue(&sCGI, "var");
    pcUpdateValue       = pcCGIGetValue(&sCGI, "value");
    pcUpdateValue       = pcCGIGetValue(&sCGI, "value");
    pcViewAddress       = pcCGIGetValue(&sCGI, "address");

    printf("Content-type: text/html\r\n\r\n");

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
    
    /* Load the cached device id's and any network contents if possible */
    if (eJIPService_PersistXMLLoadDefinitions(&sJIP_Context, CACHE_DEFINITIONS_FILE_NAME) != E_JIP_OK)
    {
        // Couldn't load the definitions file, fall back to discovery.
        if (eJIPService_DiscoverNetwork(&sJIP_Context) != E_JIP_OK)
        {
            printf("JIP discover network failed\n");
        }
    }
    
    if ((pcUpdateAddress) && (pcUpdateMib) && (pcUpdateVar) && (pcUpdateValue))
    {
        /* Load the cached network if possible */
        if (eJIPService_PersistXMLLoadNetwork(&sJIP_Context, CACHE_NETWORK_FILE_NAME) != E_JIP_OK)
        {
            // Couldn't load the network file, fall back to discovery.
            if (eJIPService_DiscoverNetwork(&sJIP_Context) != E_JIP_OK)
            {
                printf("JIP discover network failed\n");
            }
        }
        
        printf("<div>Update address %s, mib %s, var %s to value %s\n", pcUpdateAddress, pcUpdateMib, pcUpdateVar, pcUpdateValue);
        
        int multicast = 0;
        tsNode *psNode;
        tsMib *psMib;
        tsVar *psVar;
        
        if ((strncmp("FF", pcUpdateAddress, 2) == 0) || (strncmp("ff", pcUpdateAddress, 2) == 0))
        {
            multicast = 1;
        }
        
        eJIP_Lock(&sJIP_Context);

        psNode = sJIP_Context.sNetwork.psNodes;
        while (psNode)
        {
            char buffer[INET6_ADDRSTRLEN] = "Could not determine address\n";
            inet_ntop(AF_INET6, &psNode->sNode_Address.sin6_addr, buffer, INET6_ADDRSTRLEN);
            
            if (multicast || (strcmp(pcUpdateAddress, buffer) == 0))
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
                                buf[0] = strtol(pcUpdateValue, NULL, 0);
                                //printf("..%d..", buf[0] & 0xFF);
                                break;
                            
                            case (E_JIP_VAR_TYPE_INT16):
                            case (E_JIP_VAR_TYPE_UINT16):
                            {
                                uint16_t u16Var = strtol(pcUpdateValue, NULL, 0);
                                buf = malloc(sizeof(uint16_t));
                                memcpy(buf, &u16Var, sizeof(uint16_t));
                                //printf("..%d..", *(uint16_t *)buf & 0xFFFF);
                                break;
                            }
                                
                            case (E_JIP_VAR_TYPE_INT32):
                            case (E_JIP_VAR_TYPE_UINT32):
                            {
                                uint32_t u32Var = strtol(pcUpdateValue, NULL, 0);
                                buf = malloc(sizeof(uint32_t));
                                memcpy(buf, &u32Var, sizeof(uint32_t));
                                //printf("..%d..", *(uint32_t *)buf);
                                break;
                            }
                            
                            case (E_JIP_VAR_TYPE_INT64):
                            case (E_JIP_VAR_TYPE_UINT64):
                            {
                                uint64_t u64Var = strtoll(pcUpdateValue, NULL, 0);
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
                            
                            default:
                                supported = 0;
                        }
                        
                        if (supported)
                        {
                            if (multicast)
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
                                        printf("Unknown host: %s\n", pcUpdateAddress);
                                        return 0;
                                    }
                                    else if (s < 0)
                                    {
                                        printf("inet_pton failed (%s)", strerror(errno));
                                        return 0;
                                    }
                                }
                                
                                printf("...\n");
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
                                printf("...\n");
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
        eJIP_Unlock(&sJIP_Context);
updated:
        printf("</div>");
    }
    else
    {
        printf("<HTML><HEAD><TITLE>\n");
        printf("NXP Smart Devices Demo\n");
        printf("</TITLE>\n");
        printf("<link rel=\"stylesheet\" href=\"/style.css\" type=\"text/css\" media=\"screen\" />\n");
        printf("<script type=\"text/javascript\" src=\"/js/SimpleSlider.js\"></script>\n");

        printf("<script type=\"text/javascript\"> \n\
                function UpdateVariable(address, mib, variable, value) \n\
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
                    var path = \"SmartDevices.cgi\"; \n\
                    request = \"address=\" + address \n\
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
                JIP_MonitorsRun(); \n\
                } \n\
                window.onresize = function() { \n\
                setFooter(); \n\
                } \n\
                </script>\n");

        printf("</HEAD><body><div id=\"container\" >\n");
        
        printf("<div id=\"header\"><div style=\"float: left;\"><H1>NXP Smart Devices Demo</H1></div>");
        printf("<div style=\"float: right;\"><img src=\"/img/NXP_logo.gif\"></div>"); 
        printf("</div>\n"); 

        printf("<div id=\"navigation\"><ul>");
    
        if (psGlobalGroup)
        {
            printf("<li %s><a href=\"/cgi-bin/SmartDevices.cgi?Mode=Global\">Global Control</a></li>\n", strcmp(pcMode, "Global") ? "": "class=\"selected\"");
        }
        
        if (u32NumGroups > 0)
        {
            printf("<li %s><a href=\"/cgi-bin/SmartDevices.cgi?Mode=Group\">Group Control</a></li>\n", strcmp(pcMode, "Group") ? "": "class=\"selected\"");
        }
        
        printf("<li %s><a href=\"/cgi-bin/SmartDevices.cgi?Mode=Individual\">Individual Control</a></li>\n", strcmp(pcMode, "Individual") ? "": "class=\"selected\"");

        if (u32NumScenes > 0)
        {
            printf("<li %s><a href=\"/cgi-bin/SmartDevices.cgi?Mode=Scene\">Scene Control</a></li>\n", strcmp(pcMode, "Scene") ? "": "class=\"selected\"");
        }
        
        printf("</ul></div>");
        
        printf("<div id=\"content\">\n\n");
        
        if (eJIPService_DiscoverNetwork(&sJIP_Context) != E_JIP_OK)
        {
            printf("JIP discover network failed\n");
        }
        
        if ((strcmp("Global", pcMode) == 0) && psGlobalGroup)
        {
            GroupMenu(psGlobalGroup);
        }
        else if (strcmp("Group", pcMode) == 0)
        {
            int i;
            
            for (i = 0; i < u32NumGroups; i++)
            {
                GroupMenu(&psGroups[i]);
            }
        }
        else if (strcmp("Individual", pcMode) == 0)
        {
            tsNode *psNode;
            tsMib *psMib;
            tsVar *psVar;
            
            eJIP_Lock(&sJIP_Context);

            psNode = sJIP_Context.sNetwork.psNodes;
            while (psNode)
            {
                char buffer[INET6_ADDRSTRLEN] = "Could not determine address\n";
                inet_ntop(AF_INET6, &psNode->sNode_Address.sin6_addr, buffer, INET6_ADDRSTRLEN);
                int i;
                tsDevice *psDevice = NULL;
                
                for (i = 0; i < u32NumDevices; i++)
                {
                    switch (psDevices[i].eDeviceLookup)
                    {
                        case(E_LOOKUP_DEVICEID):
                            if (psDevices[i].u32DeviceId == psNode->u32DeviceId)
                            {
                                psDevice = &psDevices[i];
                                break;
                            }
                            break;
                        case (E_LOOKUP_BASETYPE):
                            if (psDevices[i].u8BaseType == (uint8_t)(psNode->u32DeviceId & 0x000000FF))
                            {
                                psDevice = &psDevices[i];
                                break;
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                if (!psDevice)
                {
                    /* Not handling this device ID */
                    psNode = psNode->psNext;
                    continue;
                }
                
                psMib = psJIP_LookupMib(psNode, NULL, "Node");
                if (psMib)
                {
                    psVar = psJIP_LookupVar(psMib, NULL, "DescriptiveName");
                    if (psVar)
                    {
                        char acCurrentValue[255];
                        
                        (void)eJIP_GetVar(&sJIP_Context, psVar);

                        if (psVar->pvData)
                        {
                            switch (psVar->eVarType)
                            {
                                case  (E_JIP_VAR_TYPE_STR): 
                                    sprintf(acCurrentValue, "%s\n", ((uint8_t*)psVar->pvData)); 
                                    break;
                                default: sprintf(acCurrentValue, "Unknown Type\n");
                            }
                        }
                        else
                        {
                            sprintf(acCurrentValue, "?\n");
                        }                    
                        
                        DeviceMenu(psDevice, acCurrentValue, buffer);
                    }
                }
                psNode = psNode->psNext;
            }
            eJIP_Unlock(&sJIP_Context);
        }
        else if (strcmp("Scene", pcMode) == 0)
        {
            int i;
            
            printf("<div id=\"Scenes\">");
            for (i = 0; i < u32NumScenes; i++)
            {
                SceneMenu(&psScenes[i]);
            }
            printf("</div>\n");
        }
        
        printf("<div id=\"result\"></div>\n");
        printf("</div>"); //content
        printf("<div id=\"footer\">Smart Devices cgi version %s using libJIP version %s</div>\n", Version, JIP_Version);
        printf("</div>"); //Container
        printf("</body></HTML>\n");
    }
 
    /* Save the device id's and network contents */
    (void)eJIPService_PersistXMLSaveDefinitions(&sJIP_Context, CACHE_DEFINITIONS_FILE_NAME);
    (void)eJIPService_PersistXMLSaveNetwork(&sJIP_Context, CACHE_NETWORK_FILE_NAME);
    
    eJIP_Destroy(&sJIP_Context);
    return 0;
}

